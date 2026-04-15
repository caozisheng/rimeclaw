// Copyright 2025-2026 RimeClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#ifdef BUILD_LLAMA_LOCAL_PROVIDER

#include "rimeclaw/providers/llama_local_provider.hpp"

#include <algorithm>
#include <cstring>
#include <sstream>
#include <stdexcept>

#include <spdlog/spdlog.h>

#include "rimeclaw/common/defer.hpp"
#include "rimeclaw/providers/provider_error.hpp"

namespace rimeclaw {

// ---------------------------------------------------------------------------
// LlamaLocalConfig
// ---------------------------------------------------------------------------

LlamaLocalConfig
LlamaLocalConfig::FromExtra(const nlohmann::json& extra) {
  LlamaLocalConfig cfg;
  cfg.model_path = extra.value("model_path", std::string{});
  cfg.n_ctx = extra.value("n_ctx", 4096);
  cfg.n_gpu_layers = extra.value("n_gpu_layers", -1);
  cfg.n_threads = extra.value("n_threads", 0);
  cfg.n_batch = extra.value("n_batch", 512);
  cfg.repeat_penalty = extra.value("repeat_penalty", 1.1f);
  return cfg;
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

LlamaLocalProvider::LlamaLocalProvider(const LlamaLocalConfig& config)
    : config_(config) {
  // Forward llama.cpp warnings too — GGML_ASSERT prints context at WARN
  // level before calling abort(); suppressing it makes crashes invisible.
  llama_log_set(
      [](enum ggml_log_level level, const char* text, void* /*user_data*/) {
        if (level == GGML_LOG_LEVEL_ERROR) {
          spdlog::error("[llama] {}", text);
        } else if (level == GGML_LOG_LEVEL_WARN) {
          spdlog::warn("[llama] {}", text);
        }
      },
      nullptr);
  llama_backend_init();
  LoadModel();
}

LlamaLocalProvider::~LlamaLocalProvider() {
  UnloadModel();
}

void LlamaLocalProvider::LoadModel() {
  if (config_.model_path.empty()) {
    throw ProviderError(ProviderErrorKind::kBadRequest, 0,
                        "model_path is empty", "local");
  }

  llama_model_params mparams = llama_model_default_params();
  mparams.n_gpu_layers = config_.n_gpu_layers;

  model_ = llama_model_load_from_file(config_.model_path.c_str(), mparams);
  if (!model_) {
    throw ProviderError(ProviderErrorKind::kBadRequest, 0,
                        "Failed to load model: " + config_.model_path, "local");
  }

  vocab_ = llama_model_get_vocab(model_);
  spdlog::info("LlamaLocalProvider: loaded model {}", config_.model_path);
}

void LlamaLocalProvider::UnloadModel() {
  if (ctx_) {
    llama_free(ctx_);
    ctx_ = nullptr;
    kv_tokens_.clear();
  }
  if (model_) {
    llama_model_free(model_);
    model_ = nullptr;
    vocab_ = nullptr;
  }
}

void LlamaLocalProvider::EnsureContext() {
  if (ctx_) return;

  llama_context_params cparams = llama_context_default_params();
  cparams.n_ctx = config_.n_ctx;
  cparams.n_batch = config_.n_batch;
  if (config_.n_threads > 0) {
    cparams.n_threads = config_.n_threads;
    cparams.n_threads_batch = config_.n_threads;
  }

  ctx_ = llama_init_from_model(model_, cparams);
  if (!ctx_) {
    throw ProviderError(ProviderErrorKind::kTransient, 0,
                        "Failed to create llama context", "local");
  }
  kv_tokens_.clear();
}

void LlamaLocalProvider::ClearKVCache() {
  if (ctx_) {
    llama_memory_clear(llama_get_memory(ctx_), true);
  }
  kv_tokens_.clear();
}

// ---------------------------------------------------------------------------
// Supported models
// ---------------------------------------------------------------------------

std::vector<std::string> LlamaLocalProvider::GetSupportedModels() const {
  return {"local"};
}

// ---------------------------------------------------------------------------
// Tokenization
// ---------------------------------------------------------------------------

std::vector<llama_token>
LlamaLocalProvider::Tokenize(const std::string& text, bool add_special) const {
  int n_tokens = text.size() + 2 * add_special;
  std::vector<llama_token> tokens(n_tokens);

  n_tokens = llama_tokenize(vocab_, text.c_str(),
                            static_cast<int32_t>(text.size()), tokens.data(),
                            static_cast<int32_t>(tokens.size()), add_special,
                            /*parse_special=*/true);
  if (n_tokens < 0) {
    tokens.resize(-n_tokens);
    n_tokens = llama_tokenize(vocab_, text.c_str(),
                              static_cast<int32_t>(text.size()), tokens.data(),
                              static_cast<int32_t>(tokens.size()), add_special,
                              /*parse_special=*/true);
  }
  tokens.resize(n_tokens);
  return tokens;
}

std::string LlamaLocalProvider::TokenToStr(llama_token token) const {
  char buf[128];
  int n = llama_token_to_piece(vocab_, token, buf, sizeof(buf), 0, true);
  if (n < 0) {
    // Buffer too small — allocate dynamically
    std::vector<char> large(static_cast<size_t>(-n));
    n = llama_token_to_piece(vocab_, token, large.data(),
                             static_cast<int>(large.size()), 0, true);
    return std::string(large.data(), n);
  }
  return std::string(buf, n);
}

// ---------------------------------------------------------------------------
// Prompt building — Qwen3 chat template with tool support
// ---------------------------------------------------------------------------

std::string LlamaLocalProvider::FormatToolsForSystemPrompt(
    const std::vector<nlohmann::json>& tools) const {
  std::string block;
  block += "\n\n# Tools\n\n";
  block +=
      "You may call one or more functions to assist with the user query.\n";
  block += "You are provided with function signatures within <tools></tools> "
           "XML tags:\n<tools>\n";
  for (const auto& tool : tools) {
    block += tool.dump() + "\n";
  }
  block += "</tools>\n\n";
  block += "For each function call, return a json object with function name "
           "and arguments within <tool_call></tool_call> XML tags:\n";
  block += "<tool_call>\n";
  block += "{\"name\": \"function_name\", \"arguments\": {\"arg1\": "
           "\"value1\"}}\n";
  block += "</tool_call>";
  return block;
}

std::string
LlamaLocalProvider::BuildPrompt(const ChatCompletionRequest& req) const {
  // Build llama_chat_message array for llama_chat_apply_template()
  std::vector<llama_chat_message> chat_msgs;
  // Temporary storage for strings (llama_chat_message uses const char*)
  std::vector<std::string> role_storage;
  std::vector<std::string> content_storage;

  for (const auto& msg : req.messages) {
    std::string role = msg.role;
    std::string text;

    if (role == "system") {
      text = msg.text();
      // Inject tool definitions into system prompt
      if (!req.tools.empty()) {
        text += FormatToolsForSystemPrompt(req.tools);
      }
    } else if (role == "user") {
      // Check if this is a tool_result message
      bool has_tool_result = false;
      for (const auto& block : msg.content) {
        if (block.type == "tool_result") {
          has_tool_result = true;
          break;
        }
      }
      if (has_tool_result) {
        role = "tool";
        text = "<tool_response>\n";
        for (const auto& block : msg.content) {
          if (block.type == "tool_result") {
            text += block.content;
          }
        }
        text += "\n</tool_response>";
      } else {
        text = msg.text();
      }
    } else {
      // assistant, tool, etc.
      text = msg.text();
    }

    role_storage.push_back(std::move(role));
    content_storage.push_back(std::move(text));
  }

  // Build llama_chat_message array
  for (size_t i = 0; i < role_storage.size(); ++i) {
    llama_chat_message cm;
    cm.role = role_storage[i].c_str();
    cm.content = content_storage[i].c_str();
    chat_msgs.push_back(cm);
  }

  // Use llama_chat_apply_template to format the prompt
  // First call with nullptr to get required buffer size
  int32_t n = llama_chat_apply_template(
      llama_model_chat_template(model_, /*name=*/nullptr), chat_msgs.data(),
      static_cast<size_t>(chat_msgs.size()), /*add_ass=*/true, nullptr, 0);

  std::vector<char> buf(static_cast<size_t>(n) + 1);
  llama_chat_apply_template(
      llama_model_chat_template(model_, /*name=*/nullptr), chat_msgs.data(),
      static_cast<size_t>(chat_msgs.size()), /*add_ass=*/true, buf.data(),
      static_cast<int32_t>(buf.size()));

  return std::string(buf.data(), n);
}

// ---------------------------------------------------------------------------
// Generation core
// ---------------------------------------------------------------------------

LlamaLocalProvider::GenerateResult
LlamaLocalProvider::Generate(
    const std::string& prompt, int max_tokens, float temperature,
    std::function<bool(const std::string&)> token_cb) {
  std::lock_guard<std::mutex> lock(inference_mutex_);

  GenerateResult result;
  EnsureContext();

  // Tokenize prompt
  std::vector<llama_token> prompt_tokens = Tokenize(prompt, false);
  result.prompt_tokens = static_cast<int>(prompt_tokens.size());

  if (result.prompt_tokens >= config_.n_ctx) {
    ClearKVCache();
    throw ProviderError(ProviderErrorKind::kContextOverflow, 0,
                        "Prompt exceeds context window", "local");
  }

  // --- KV cache prefix matching ---
  // Find the longest common prefix between cached tokens and new prompt.
  size_t prefix_len = 0;
  const size_t max_prefix =
      std::min(kv_tokens_.size(), prompt_tokens.size());
  while (prefix_len < max_prefix &&
         kv_tokens_[prefix_len] == prompt_tokens[prefix_len]) {
    ++prefix_len;
  }

  // Trim divergent tail from KV cache
  if (prefix_len < kv_tokens_.size()) {
    llama_memory_seq_rm(llama_get_memory(ctx_), 0, static_cast<llama_pos>(prefix_len), -1);
    kv_tokens_.resize(prefix_len);
  }

  const int n_past = static_cast<int>(prefix_len);
  const int n_new = result.prompt_tokens - n_past;
  spdlog::info("LlamaLocalProvider: prompt {} tokens, KV reuse {}, new {}",
               result.prompt_tokens, n_past, n_new);

  // Clamp max_tokens so prompt + generation never exceeds the KV cache.
  const int remaining = config_.n_ctx - result.prompt_tokens;
  max_tokens = std::min(max_tokens, remaining);

  // Decode only new prompt tokens in batches
  llama_batch batch = llama_batch_init(config_.n_batch, 0, 1);
  DEFER(llama_batch_free(batch));

  if (n_new > 0) {
    for (int i = 0; i < n_new; i += config_.n_batch) {
      int n_eval = std::min(config_.n_batch, n_new - i);
      batch.n_tokens = n_eval;
      for (int j = 0; j < n_eval; ++j) {
        int idx = n_past + i + j;
        batch.token[j] = prompt_tokens[idx];
        batch.pos[j] = idx;
        batch.n_seq_id[j] = 1;
        batch.seq_id[j][0] = 0;
        batch.logits[j] = (idx == result.prompt_tokens - 1) ? 1 : 0;
      }
      if (llama_decode(ctx_, batch) != 0) {
        ClearKVCache();
        throw ProviderError(ProviderErrorKind::kTransient, 0,
                            "llama_decode failed during prompt processing",
                            "local");
      }
    }
  } else {
    // Entire prompt is already cached.  Re-decode the last token to
    // refresh its logits (the previous generation overwrote them).
    batch.n_tokens = 1;
    batch.token[0] = prompt_tokens.back();
    batch.pos[0] = result.prompt_tokens - 1;
    batch.n_seq_id[0] = 1;
    batch.seq_id[0][0] = 0;
    batch.logits[0] = 1;
    if (llama_decode(ctx_, batch) != 0) {
      ClearKVCache();
      throw ProviderError(ProviderErrorKind::kTransient, 0,
                          "llama_decode failed (logits refresh)", "local");
    }
  }

  // Record prompt tokens in cache (generated tokens will be appended below)
  kv_tokens_.assign(prompt_tokens.begin(), prompt_tokens.end());

  // Build sampler chain
  llama_sampler* smpl = llama_sampler_chain_init(
      llama_sampler_chain_default_params());
  DEFER(llama_sampler_free(smpl));

  if (config_.repeat_penalty != 1.0f) {
    llama_sampler_chain_add(
        smpl, llama_sampler_init_penalties(
                  /*penalty_last_n=*/64, config_.repeat_penalty,
                  /*penalty_freq=*/0.0f, /*penalty_present=*/0.0f));
  }
  if (temperature <= 0.0f) {
    llama_sampler_chain_add(smpl, llama_sampler_init_greedy());
  } else {
    llama_sampler_chain_add(smpl, llama_sampler_init_temp(temperature));
    llama_sampler_chain_add(smpl, llama_sampler_init_top_p(0.9f, 1));
    llama_sampler_chain_add(smpl, llama_sampler_init_min_p(0.1f, 1));
    llama_sampler_chain_add(smpl, llama_sampler_init_dist(LLAMA_DEFAULT_SEED));
  }

  // Token generation loop
  llama_token eos = llama_vocab_eos(vocab_);
  llama_token eot = llama_vocab_eot(vocab_);
  std::string generated_text;

  for (int i = 0; i < max_tokens; ++i) {
    llama_token new_token = llama_sampler_sample(smpl, ctx_, -1);

    // Check for end of sequence
    if (llama_vocab_is_eog(vocab_, new_token) ||
        new_token == eos || new_token == eot) {
      result.finish_reason = "stop";
      break;
    }

    std::string piece = TokenToStr(new_token);
    generated_text += piece;
    result.completion_tokens++;
    kv_tokens_.push_back(new_token);

    // Streaming callback
    if (token_cb) {
      if (!token_cb(piece)) {
        result.finish_reason = "stop";
        break;
      }
    }

    // Prepare next decode
    batch.n_tokens = 1;
    batch.token[0] = new_token;
    batch.pos[0] = static_cast<llama_pos>(kv_tokens_.size() - 1);
    batch.n_seq_id[0] = 1;
    batch.seq_id[0][0] = 0;
    batch.logits[0] = 1;
    if (llama_decode(ctx_, batch) != 0) {
      spdlog::warn("LlamaLocalProvider: decode failed at token {}", i);
      result.finish_reason = "stop";
      break;
    }
  }

  if (result.finish_reason.empty()) {
    result.finish_reason = "length";
  }

  result.text = std::move(generated_text);

  spdlog::debug("LlamaLocalProvider: Generate done, {} completion tokens, "
                "finish_reason={}",
                result.completion_tokens, result.finish_reason);

  return result;
}

// ---------------------------------------------------------------------------
// Tool call parsing
// ---------------------------------------------------------------------------

std::vector<ToolCall>
LlamaLocalProvider::ParseToolCalls(const std::string& output) const {
  std::vector<ToolCall> calls;

  // Manual XML tag search — avoids MSVC std::regex stack overflow on long
  // or malformed model output (the recursive backtracking engine can crash
  // with [\s\S]*? patterns on certain inputs).
  static constexpr const char* kOpen  = "<tool_call>";
  static constexpr const char* kClose = "</tool_call>";
  static constexpr size_t kOpenLen  = 11;  // strlen("<tool_call>")
  static constexpr size_t kCloseLen = 12;  // strlen("</tool_call>")

  int call_idx = 0;
  size_t search_pos = 0;

  while (search_pos < output.size()) {
    size_t open_pos = output.find(kOpen, search_pos);
    if (open_pos == std::string::npos) break;

    size_t body_start = open_pos + kOpenLen;
    size_t close_pos = output.find(kClose, body_start);
    if (close_pos == std::string::npos) break;  // no closing tag — stop

    // Trim leading/trailing whitespace from body
    size_t body_end = close_pos;
    while (body_start < body_end &&
           (output[body_start] == ' ' || output[body_start] == '\t' ||
            output[body_start] == '\n' || output[body_start] == '\r'))
      ++body_start;
    while (body_end > body_start &&
           (output[body_end - 1] == ' ' || output[body_end - 1] == '\t' ||
            output[body_end - 1] == '\n' || output[body_end - 1] == '\r'))
      --body_end;

    std::string json_str = output.substr(body_start, body_end - body_start);

    try {
      auto j = nlohmann::json::parse(json_str);
      ToolCall tc;
      tc.id = "local_call_" + std::to_string(call_idx++);
      tc.name = j.value("name", "");
      tc.arguments = j.value("arguments", nlohmann::json::object());
      if (!tc.name.empty()) {
        calls.push_back(std::move(tc));
      }
    } catch (const nlohmann::json::exception& e) {
      spdlog::warn("LlamaLocalProvider: failed to parse tool call JSON: {}",
                    e.what());
    }

    search_pos = close_pos + kCloseLen;
  }
  return calls;
}

// ---------------------------------------------------------------------------
// Strip tool call XML from text content
// ---------------------------------------------------------------------------

static std::string StripToolCallBlocks(const std::string& text) {
  // Manual removal of <tool_call>...</tool_call> blocks (avoids std::regex).
  static constexpr const char* kOpen  = "<tool_call>";
  static constexpr const char* kClose = "</tool_call>";
  static constexpr size_t kOpenLen  = 11;
  static constexpr size_t kCloseLen = 12;

  std::string result;
  result.reserve(text.size());
  size_t pos = 0;

  while (pos < text.size()) {
    size_t open_pos = text.find(kOpen, pos);
    if (open_pos == std::string::npos) {
      result.append(text, pos);
      break;
    }
    result.append(text, pos, open_pos - pos);
    size_t close_pos = text.find(kClose, open_pos + kOpenLen);
    if (close_pos == std::string::npos) {
      // No closing tag — keep the rest as-is
      result.append(text, open_pos);
      break;
    }
    pos = close_pos + kCloseLen;
  }

  // Trim trailing whitespace
  auto end = result.find_last_not_of(" \t\n\r");
  if (end != std::string::npos) {
    result.erase(end + 1);
  }
  return result;
}

// ---------------------------------------------------------------------------
// ChatCompletion (synchronous)
// ---------------------------------------------------------------------------

ChatCompletionResponse
LlamaLocalProvider::ChatCompletion(const ChatCompletionRequest& req) {
  std::string prompt = BuildPrompt(req);

  float temp = static_cast<float>(req.temperature);
  GenerateResult gen = Generate(prompt, req.max_tokens, temp);

  ChatCompletionResponse resp;
  resp.usage.prompt_tokens = gen.prompt_tokens;
  resp.usage.completion_tokens = gen.completion_tokens;
  resp.usage.total_tokens = gen.prompt_tokens + gen.completion_tokens;

  // Parse tool calls
  resp.tool_calls = ParseToolCalls(gen.text);

  if (!resp.tool_calls.empty()) {
    resp.finish_reason = "tool_calls";
    resp.content = StripToolCallBlocks(gen.text);
  } else {
    resp.finish_reason = gen.finish_reason;
    resp.content = gen.text;
  }

  return resp;
}

// ---------------------------------------------------------------------------
// ChatCompletionStream (streaming)
// ---------------------------------------------------------------------------

void LlamaLocalProvider::ChatCompletionStream(
    const ChatCompletionRequest& req,
    std::function<void(const ChatCompletionResponse&)> cb) {
  std::string prompt = BuildPrompt(req);

  float temp = static_cast<float>(req.temperature);

  // Accumulate full text for final tool call parsing
  std::string full_text;

  auto token_cb = [&](const std::string& piece) -> bool {
    full_text += piece;

    ChatCompletionResponse delta;
    delta.content = piece;
    cb(delta);
    return true;
  };

  GenerateResult gen = Generate(prompt, req.max_tokens, temp, token_cb);

  spdlog::debug("LlamaLocalProvider: stream Generate returned, "
                "full_text size={}", full_text.size());

  // Parse tool calls and emit them in a separate non-end chunk so that the
  // agent loop (which ignores tool_calls on is_stream_end chunks) can see them.
  auto tool_calls = ParseToolCalls(full_text);
  spdlog::debug("LlamaLocalProvider: ParseToolCalls found {} call(s)",
                tool_calls.size());

  if (!tool_calls.empty()) {
    ChatCompletionResponse tool_chunk;
    tool_chunk.tool_calls = std::move(tool_calls);
    cb(tool_chunk);
  }

  // Final response with usage
  ChatCompletionResponse final_resp;
  final_resp.is_stream_end = true;
  final_resp.usage.prompt_tokens = gen.prompt_tokens;
  final_resp.usage.completion_tokens = gen.completion_tokens;
  final_resp.usage.total_tokens = gen.prompt_tokens + gen.completion_tokens;
  final_resp.finish_reason =
      final_resp.tool_calls.empty() ? gen.finish_reason : "tool_calls";

  cb(final_resp);
  spdlog::debug("LlamaLocalProvider: ChatCompletionStream done");
}

}  // namespace rimeclaw

#endif  // BUILD_LLAMA_LOCAL_PROVIDER
