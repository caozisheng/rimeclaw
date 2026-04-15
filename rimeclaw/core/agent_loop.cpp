// Copyright 2025-2026 RimeClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "rimeclaw/core/agent_loop.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <sstream>
#include <thread>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "rimeclaw/constants.hpp"
#include "rimeclaw/core/content_block.hpp"
#include "rimeclaw/core/default_context_engine.hpp"
#include "rimeclaw/core/memory_manager.hpp"
#include "rimeclaw/core/skill_loader.hpp"
#include "rimeclaw/providers/failover_resolver.hpp"
#include "rimeclaw/providers/provider_error.hpp"
#include "rimeclaw/providers/provider_registry.hpp"
#include "rimeclaw/tools/tool_registry.hpp"

namespace rimeclaw {

static bool has_non_whitespace(const std::string& value) {
  return std::any_of(value.begin(), value.end(),
                     [](unsigned char ch) { return !std::isspace(ch); });
}

static void append_thinking_block(Message& msg, const std::string& text) {
  if (!has_non_whitespace(text)) {
    return;
  }
  ContentBlock block;
  block.type = "thinking";
  block.text = text;
  msg.content.push_back(std::move(block));
}

static std::vector<ToolCall>
filter_valid_tool_calls(const std::vector<ToolCall>& tool_calls,
                        bool* saw_invalid = nullptr) {
  std::vector<ToolCall> valid;
  bool invalid = false;

  for (const auto& tc : tool_calls) {
    if (!has_non_whitespace(tc.name)) {
      invalid = true;
      spdlog::warn("Ignoring invalid tool call with empty name (id='{}')", tc.id);
      continue;
    }
    valid.push_back(tc);
  }

  if (saw_invalid) {
    *saw_invalid = invalid;
  }
  return valid;
}

static constexpr const char* kInvalidToolCallStopText =
    "I couldn't continue because the model emitted an invalid tool call. "
    "Please try again.";

static constexpr const char* kEmptyStreamStopText =
    "I couldn't complete that request because the model returned no usable "
    "response. Please try again.";

// Truncate a tool result if it exceeds the limit (head + tail with ellipsis)
static std::string truncate_tool_result(const std::string& result,
                                        int max_chars, int keep_lines) {
  if (static_cast<int>(result.size()) <= max_chars)
    return result;

  // Split into lines
  std::vector<std::string> lines;
  std::istringstream stream(result);
  std::string line;
  while (std::getline(stream, line))
    lines.push_back(line);

  if (static_cast<int>(lines.size()) <= keep_lines * 2)
    return result;

  std::string truncated;
  for (int i = 0; i < keep_lines; ++i) {
    truncated += lines[i] + "\n";
  }
  int omitted = static_cast<int>(lines.size()) - keep_lines * 2;
  truncated += "\n... [" + std::to_string(omitted) + " lines omitted] ...\n\n";
  for (int i = static_cast<int>(lines.size()) - keep_lines;
       i < static_cast<int>(lines.size()); ++i) {
    truncated += lines[i] + "\n";
  }
  return truncated;
}

// Get context window size for a model name
static int get_context_window(const std::string& model) {
  // Anthropic models
  if (model.find("claude") != std::string::npos)
    return kContextWindow200K;
  // OpenAI models
  if (model.find("gpt-4o") != std::string::npos)
    return kContextWindow128K;
  if (model.find("gpt-4-turbo") != std::string::npos)
    return kContextWindow128K;
  if (model.find("gpt-4") != std::string::npos)
    return kContextWindow8K;
  if (model.find("gpt-3.5") != std::string::npos)
    return kContextWindow16K;
  // Qwen
  if (model.find("qwen") != std::string::npos)
    return kContextWindow128K;
  // DeepSeek
  if (model.find("deepseek") != std::string::npos)
    return kContextWindow128K;
  return kDefaultContextWindow;
}

AgentLoop::AgentLoop(std::shared_ptr<MemoryManager> memory_manager,
                     std::shared_ptr<SkillLoader> skill_loader,
                     std::shared_ptr<ToolRegistry> tool_registry,
                     std::shared_ptr<LLMProvider> llm_provider,
                     const AgentConfig& agent_config)
    : memory_manager_(std::move(memory_manager)),
      skill_loader_(std::move(skill_loader)),
      tool_registry_(std::move(tool_registry)),
      llm_provider_(std::move(llm_provider)),
      agent_config_(agent_config) {
  // Use dynamic max iterations based on context window
  max_iterations_ = agent_config_.DynamicMaxIterations();
  spdlog::info("AgentLoop initialized with model: {}, max_iterations: {}",
                agent_config_.model, max_iterations_);
}

std::shared_ptr<LLMProvider> AgentLoop::resolve_provider() {
  resolved_request_model_ = agent_config_.model;

  // If failover resolver is available, use it for profile rotation + fallback
  if (failover_resolver_) {
    auto resolved =
        failover_resolver_->Resolve(agent_config_.model, session_key_);
    if (resolved) {
      last_provider_id_ = resolved->provider_id;
      last_profile_id_ = resolved->profile_id;
      resolved_request_model_ = resolved->model;
      if (resolved->is_fallback) {
        spdlog::info("Using fallback model: {}/{}", resolved->provider_id, resolved->model);
      }
      return resolved->provider;
    }
    spdlog::error("FailoverResolver exhausted all models/profiles for '{}'",
                   agent_config_.model);
    // Fall through to registry / injected provider
  }

  if (!provider_registry_) {
    return llm_provider_;
  }

  auto ref = provider_registry_->ResolveModel(agent_config_.model);
  auto provider = provider_registry_->GetProviderForModel(ref);
  if (provider) {
    last_provider_id_ = ref.provider;
    last_profile_id_ = "";
    resolved_request_model_ = ref.model;
    return provider;
  }

  spdlog::warn("Failed to resolve provider for model '{}', falling back to injected provider",
                agent_config_.model);
  return llm_provider_;
}

void AgentLoop::SetModel(const std::string& model_ref) {
  agent_config_.model = model_ref;
  spdlog::info("Model set to: {}", model_ref);
}

std::vector<Message> AgentLoop::ProcessMessage(
    const std::string& message, const std::vector<Message>& history,
    const std::string& system_prompt, const std::string& usage_session_key) {
  const std::string& effective_session_key =
      usage_session_key.empty() ? session_key_ : usage_session_key;
  spdlog::info("Processing message (non-streaming)");
  stop_requested_ = false;

  auto provider = resolve_provider();

  std::vector<Message> new_messages;

  // --- Context assembly via pluggable engine ---
  auto engine =
      context_engine_
          ? context_engine_
          : std::make_shared<DefaultContextEngine>(agent_config_);
  int ctx_window = agent_config_.context_window > 0
                       ? agent_config_.context_window
                       : get_context_window(agent_config_.model);
  auto assembled = engine->Assemble(history, system_prompt, message, ctx_window,
                                    agent_config_.max_tokens);

  // Create LLM request
  ChatCompletionRequest request;
  request.messages = assembled.messages;
  request.model = resolved_request_model_;
  request.temperature = agent_config_.temperature;
  request.max_tokens = agent_config_.max_tokens;
  request.thinking = agent_config_.thinking;

  // Add tool schemas
  nlohmann::json tools_json = nlohmann::json::array();
  for (const auto& schema : tool_registry_->GetToolSchemas()) {
    nlohmann::json tool;
    tool["type"] = "function";
    tool["function"]["name"] = schema.name;
    tool["function"]["description"] = schema.description;
    tool["function"]["parameters"] = schema.parameters;
    tools_json.push_back(tool);
  }
  request.tools = tools_json.get<std::vector<nlohmann::json>>();
  request.tool_choice_auto = true;

  int iterations = 0;
  int overflow_retries = 0;

  while (iterations < max_iterations_ && !stop_requested_) {
    try {
      auto response = provider->ChatCompletion(request);

      // --- Usage tracking ---
      if (usage_accumulator_ && !effective_session_key.empty()) {
        usage_accumulator_->Record(effective_session_key,
                                   response.usage.prompt_tokens,
                                   response.usage.completion_tokens);
      }
      spdlog::debug("Token usage: prompt={} completion={}",
                     response.usage.prompt_tokens, response.usage.completion_tokens);

      // Record success for failover tracking
      if (failover_resolver_ && !last_provider_id_.empty()) {
        failover_resolver_->RecordSuccess(last_provider_id_, last_profile_id_,
                                          session_key_);
      }

      bool saw_invalid_tool_call = false;
      auto valid_tool_calls = filter_valid_tool_calls(
          response.tool_calls, &saw_invalid_tool_call);

      if (!valid_tool_calls.empty()) {
        spdlog::info("LLM requested {} tool calls", valid_tool_calls.size());

        std::vector<nlohmann::json> tool_calls_json;
        for (const auto& tc : valid_tool_calls) {
          nlohmann::json tc_json;
          tc_json["id"] = tc.id;
          tc_json["function"]["name"] = tc.name;
          tc_json["function"]["arguments"] = tc.arguments.dump();
          tool_calls_json.push_back(tc_json);
        }
        auto tool_results = handle_tool_calls(tool_calls_json);

        // --- Tool result truncation fallback ---
        for (auto& result : tool_results) {
          result = truncate_tool_result(result, kToolResultMaxChars,
                                        kToolResultKeepLines);
        }

        // Assistant message: text + tool_use blocks
        Message assistant_msg;
        assistant_msg.role = "assistant";
        append_thinking_block(assistant_msg, response.reasoning_content);
        if (!response.content.empty())
          assistant_msg.content.push_back(
              ContentBlock::MakeText(response.content));
        for (const auto& tc : valid_tool_calls)
          assistant_msg.content.push_back(
              ContentBlock::MakeToolUse(tc.id, tc.name, tc.arguments));
        request.messages.push_back(assistant_msg);
        new_messages.push_back(assistant_msg);

        // Tool results: single user message with tool_result blocks
        Message results_msg;
        results_msg.role = "user";
        for (size_t i = 0; i < valid_tool_calls.size(); i++)
          results_msg.content.push_back(ContentBlock::MakeToolResult(
              valid_tool_calls[i].id, tool_results[i]));
        request.messages.push_back(results_msg);
        new_messages.push_back(results_msg);

        iterations++;
        continue;
      }

      // All tool calls were invalid and no text content
      if (saw_invalid_tool_call && !has_non_whitespace(response.content)) {
        spdlog::error("LLM returned only invalid tool calls");
        Message final_msg;
        final_msg.role = "assistant";
        final_msg.content.push_back(
            ContentBlock::MakeText(kInvalidToolCallStopText));
        new_messages.push_back(final_msg);
        return new_messages;
      }

      // If we got a final response without tool calls, we're done
      if (has_non_whitespace(response.content)) {
        spdlog::info("LLM provided final response");

        Message final_msg;
        final_msg.role = "assistant";
        append_thinking_block(final_msg, response.reasoning_content);
        final_msg.content.push_back(ContentBlock::MakeText(response.content));
        new_messages.push_back(final_msg);
        return new_messages;
      }

      spdlog::error("Unexpected LLM response format");
      break;

    } catch (const ProviderError& pe) {
      // --- Overflow compaction retry ---
      if (pe.Kind() == ProviderErrorKind::kContextOverflow &&
          overflow_retries < kOverflowCompactionMaxRetries) {
        overflow_retries++;
        spdlog::warn("Context overflow (attempt {}/{}), compacting and retrying",
                     overflow_retries, kOverflowCompactionMaxRetries);
        request.messages =
            engine->CompactOverflow(request.messages, system_prompt, 0);
        continue;
      }

      // Context overflow with retries exhausted
      if (pe.Kind() == ProviderErrorKind::kContextOverflow) {
        spdlog::error("Context overflow: all {} compaction retries exhausted",
                      kOverflowCompactionMaxRetries);
        throw;
      }

      // 400 Bad Request — permanent client error, never retry
      if (pe.Kind() == ProviderErrorKind::kBadRequest) {
        spdlog::error("Bad request (HTTP 400), not retrying: {}", pe.what());
        throw;
      }

      // Record failure for failover tracking (with Retry-After if provided)
      if (failover_resolver_ && !last_provider_id_.empty()) {
        failover_resolver_->RecordFailure(last_provider_id_, last_profile_id_,
                                          pe.Kind(), pe.RetryAfterSeconds());

        // Try to re-resolve with a different profile or fallback model
        spdlog::warn("Provider error ({}), attempting failover: {}",
                     ProviderErrorKindToString(pe.Kind()), pe.what());

        // Restore original model for re-resolution.
        // Guard: resolve_provider() may throw if fallback provider
        // construction fails (e.g. missing model_path for local provider).
        std::shared_ptr<LLMProvider> new_provider;
        try {
          new_provider = resolve_provider();
        } catch (const std::exception& re) {
          spdlog::error("Failover provider construction failed: {}", re.what());
        }
        if (new_provider && new_provider != provider) {
          provider = new_provider;
          // Update the request model to the newly resolved model
          request.model = resolved_request_model_;
          iterations++;
          continue;
        }
      }

      // No failover available or failover also failed
      spdlog::error("Provider error with no failover available: {}", pe.what());
      if (iterations < kMaxTransientRetries) {
        std::this_thread::sleep_for(
            std::chrono::seconds(1 << std::min(iterations, 4)));
        iterations++;
        continue;
      }
      throw;

    } catch (const std::exception& e) {
      spdlog::error("Error in LLM processing: {}", e.what());
      if (iterations < kMaxTransientRetries) {
        std::this_thread::sleep_for(
            std::chrono::seconds(1 << std::min(iterations, 4)));
        iterations++;
        continue;
      }
      throw;
    }
  }

  if (stop_requested_) {
    Message stop_msg;
    stop_msg.role = "assistant";
    stop_msg.content.push_back(
        ContentBlock::MakeText("[Agent turn stopped by user]"));
    new_messages.push_back(stop_msg);
    return new_messages;
  }

  throw std::runtime_error("Failed to get valid response after " +
                           std::to_string(max_iterations_) + " iterations");
}

std::vector<Message> AgentLoop::ProcessMessageStream(
    const std::string& message, const std::vector<Message>& history,
    const std::string& system_prompt, AgentEventCallback callback,
    const std::string& usage_session_key) {
  const std::string& effective_session_key =
      usage_session_key.empty() ? session_key_ : usage_session_key;
  spdlog::info("Processing message (streaming)");
  stop_requested_ = false;

  auto provider = resolve_provider();

  std::vector<Message> new_messages;

  // --- Context assembly via pluggable engine ---
  auto engine =
      context_engine_
          ? context_engine_
          : std::make_shared<DefaultContextEngine>(agent_config_);
  int ctx_window = agent_config_.context_window > 0
                       ? agent_config_.context_window
                       : get_context_window(agent_config_.model);
  auto assembled = engine->Assemble(history, system_prompt, message, ctx_window,
                                    agent_config_.max_tokens);

  ChatCompletionRequest request;
  request.messages = assembled.messages;
  request.model = resolved_request_model_;
  request.temperature = agent_config_.temperature;
  request.max_tokens = agent_config_.max_tokens;
  request.stream = true;
  request.thinking = agent_config_.thinking;

  nlohmann::json tools_json = nlohmann::json::array();
  for (const auto& schema : tool_registry_->GetToolSchemas()) {
    nlohmann::json tool;
    tool["type"] = "function";
    tool["function"]["name"] = schema.name;
    tool["function"]["description"] = schema.description;
    tool["function"]["parameters"] = schema.parameters;
    tools_json.push_back(tool);
  }
  request.tools = tools_json.get<std::vector<nlohmann::json>>();
  request.tool_choice_auto = true;

  int iterations = 0;
  int overflow_retries_stream = 0;

  while (iterations < max_iterations_ && !stop_requested_) {
    try {
      std::string full_response;
      std::string full_reasoning;
      TokenUsage stream_usage;
      bool handled_tool_calls = false;
      bool saw_invalid_tool_call = false;
      bool saw_stream_end = false;

      provider->ChatCompletionStream(
          request, [&](const ChatCompletionResponse& chunk) {
            // Accumulate usage from stream chunks
            stream_usage.prompt_tokens += chunk.usage.prompt_tokens;
            stream_usage.completion_tokens += chunk.usage.completion_tokens;

            if (chunk.is_stream_end) {
              saw_stream_end = true;
              if (!has_non_whitespace(full_response) &&
                  has_non_whitespace(chunk.content)) {
                full_response = chunk.content;
              }
              if (!has_non_whitespace(full_reasoning) &&
                  has_non_whitespace(chunk.reasoning_content)) {
                full_reasoning = chunk.reasoning_content;
              }
              return;
            }

            if (!chunk.content.empty()) {
              full_response += chunk.content;
              if (callback) {
                callback({"text_delta", {{"text", chunk.content}}});
              }
            }

            if (!chunk.reasoning_content.empty()) {
              full_reasoning += chunk.reasoning_content;
            }

            if (!chunk.tool_calls.empty()) {
              bool chunk_has_invalid = false;
              auto valid_tool_calls = filter_valid_tool_calls(
                  chunk.tool_calls, &chunk_has_invalid);
              saw_invalid_tool_call =
                  saw_invalid_tool_call || chunk_has_invalid;
              if (valid_tool_calls.empty()) {
                return;
              }

              handled_tool_calls = true;
              for (const auto& tc : valid_tool_calls) {
                if (callback) {
                  callback({"tool_use",
                            {{"id", tc.id},
                             {"name", tc.name},
                             {"input", tc.arguments}}});
                }
              }

              // Persist one assistant turn per model round
              Message assistant_msg;
              assistant_msg.role = "assistant";
              append_thinking_block(assistant_msg, full_reasoning);
              if (!full_response.empty()) {
                assistant_msg.content.push_back(
                    ContentBlock::MakeText(full_response));
              }
              for (const auto& tc : valid_tool_calls) {
                assistant_msg.content.push_back(
                    ContentBlock::MakeToolUse(tc.id, tc.name, tc.arguments));
              }
              request.messages.push_back(assistant_msg);
              new_messages.push_back(assistant_msg);
              full_response.clear();
              full_reasoning.clear();

              Message results_msg;
              results_msg.role = "user";
              for (const auto& tc : valid_tool_calls) {
                try {
                  auto result =
                      tool_registry_->ExecuteTool(tc.name, tc.arguments);
                  result = truncate_tool_result(result, kToolResultMaxChars,
                                                kToolResultKeepLines);
                  if (callback) {
                    callback({"tool_result",
                              {{"tool_use_id", tc.id}, {"content", result}}});
                  }
                  results_msg.content.push_back(
                      ContentBlock::MakeToolResult(tc.id, result));
                } catch (const std::exception& e) {
                  std::string error_content = "Error: " + std::string(e.what());
                  if (callback) {
                    callback({"tool_result",
                              {{"tool_use_id", tc.id},
                               {"content", error_content},
                               {"is_error", true}}});
                  }
                  results_msg.content.push_back(
                      ContentBlock::MakeToolResult(tc.id, error_content));
                }
              }
              request.messages.push_back(results_msg);
              new_messages.push_back(results_msg);
              return;
            }
          });

      // --- Usage tracking ---
      if (usage_accumulator_ && !effective_session_key.empty()) {
        usage_accumulator_->Record(effective_session_key,
                                   stream_usage.prompt_tokens,
                                   stream_usage.completion_tokens);
      }
      spdlog::debug("Token usage (stream): prompt={} completion={}",
                     stream_usage.prompt_tokens, stream_usage.completion_tokens);

      // Record success for failover tracking
      if (failover_resolver_ && !last_provider_id_.empty()) {
        failover_resolver_->RecordSuccess(last_provider_id_, last_profile_id_,
                                          session_key_);
      }

      if (handled_tool_calls) {
        iterations++;
        continue;
      }

      // If we got a final response without tool calls, we're done
      if (has_non_whitespace(full_response)) {
        if (callback) {
          callback({"message_end", {{"content", full_response}}});
        }
        Message final_msg;
        final_msg.role = "assistant";
        append_thinking_block(final_msg, full_reasoning);
        final_msg.content.push_back(ContentBlock::MakeText(full_response));
        new_messages.push_back(final_msg);
        return new_messages;
      }

      if (saw_invalid_tool_call) {
        if (callback) {
          callback({"message_end", {{"content", kInvalidToolCallStopText}}});
        }
        Message final_msg;
        final_msg.role = "assistant";
        final_msg.content.push_back(
            ContentBlock::MakeText(kInvalidToolCallStopText));
        new_messages.push_back(final_msg);
        return new_messages;
      }

      if (saw_stream_end) {
        spdlog::error("Streaming response ended without text or valid tool calls");
        if (callback) {
          callback({"message_end", {{"content", kEmptyStreamStopText}}});
        }
        Message final_msg;
        final_msg.role = "assistant";
        final_msg.content.push_back(
            ContentBlock::MakeText(kEmptyStreamStopText));
        new_messages.push_back(final_msg);
        return new_messages;
      }

      iterations++;

    } catch (const ProviderError& pe) {
      // --- Overflow compaction retry ---
      if (pe.Kind() == ProviderErrorKind::kContextOverflow &&
          overflow_retries_stream < kOverflowCompactionMaxRetries) {
        overflow_retries_stream++;
        spdlog::warn("Streaming context overflow (attempt {}/{}), compacting",
                     overflow_retries_stream, kOverflowCompactionMaxRetries);
        request.messages =
            engine->CompactOverflow(request.messages, system_prompt, 0);
        continue;
      }

      // Context overflow with retries exhausted
      if (pe.Kind() == ProviderErrorKind::kContextOverflow) {
        spdlog::error("Streaming context overflow: retries exhausted");
        if (callback) {
          callback({"message_end", {{"error", pe.what()}}});
        }
        return new_messages;
      }

      // 400 Bad Request — permanent client error, never retry
      if (pe.Kind() == ProviderErrorKind::kBadRequest) {
        spdlog::error("Bad request (HTTP 400), not retrying: {}", pe.what());
        if (callback) {
          callback({"message_end", {{"error", pe.what()}}});
        }
        return new_messages;
      }

      // Record failure and attempt failover (with Retry-After if provided)
      if (failover_resolver_ && !last_provider_id_.empty()) {
        failover_resolver_->RecordFailure(last_provider_id_, last_profile_id_,
                                          pe.Kind(), pe.RetryAfterSeconds());

        spdlog::warn("Streaming provider error ({}), attempting failover: {}",
                     ProviderErrorKindToString(pe.Kind()), pe.what());

        // Guard: resolve_provider() may throw if fallback provider
        // construction fails (e.g. missing model_path for local provider).
        std::shared_ptr<LLMProvider> new_provider;
        try {
          new_provider = resolve_provider();
        } catch (const std::exception& re) {
          spdlog::error("Failover provider construction failed: {}", re.what());
        }
        if (new_provider && new_provider != provider) {
          provider = new_provider;
          request.model = resolved_request_model_;
          iterations++;
          continue;
        }
      }

      spdlog::error("Provider error in streaming: {}", pe.what());
      if (iterations < kMaxTransientRetries) {
        std::this_thread::sleep_for(
            std::chrono::seconds(1 << std::min(iterations, 4)));
        iterations++;
        continue;
      }
      if (callback) {
        callback({"message_end", {{"error", pe.what()}}});
      }
      return new_messages;

    } catch (const std::exception& e) {
      spdlog::error("Error in streaming: {}", e.what());
      if (iterations < kMaxTransientRetries) {
        std::this_thread::sleep_for(
            std::chrono::seconds(1 << std::min(iterations, 4)));
        iterations++;
        continue;
      }
      if (callback) {
        callback({"message_end", {{"error", e.what()}}});
      }
      return new_messages;
    }
  }

  std::string stop_text =
      stop_requested_ ? "[Stopped]" : "[Max iterations reached]";
  if (callback) {
    callback({"message_end", {{"content", stop_text}}});
  }
  Message stop_msg;
  stop_msg.role = "assistant";
  stop_msg.content.push_back(ContentBlock::MakeText(stop_text));
  new_messages.push_back(stop_msg);
  return new_messages;
}

std::vector<std::string>
AgentLoop::handle_tool_calls(const std::vector<nlohmann::json>& tool_calls) {
  std::vector<std::string> results;

  for (const auto& tool_call : tool_calls) {
    try {
      std::string tool_name = tool_call["function"]["name"];
      nlohmann::json arguments;
      const auto& args_val = tool_call["function"]["arguments"];
      if (args_val.is_string()) {
        arguments = nlohmann::json::parse(args_val.get<std::string>());
      } else {
        arguments = args_val;
      }

      spdlog::info("Executing tool: {} with arguments: {}", tool_name, arguments.dump());
      std::string result = tool_registry_->ExecuteTool(tool_name, arguments);
      results.push_back(result);
      spdlog::info("Tool execution successful");

    } catch (const std::exception& e) {
      spdlog::error("Tool execution failed: {}", e.what());
      results.push_back("Error executing tool: " + std::string(e.what()));
    }
  }

  return results;
}

}  // namespace rimeclaw
