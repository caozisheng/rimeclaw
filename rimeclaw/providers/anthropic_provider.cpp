// Copyright 2025-2026 RimeClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "rimeclaw/providers/anthropic_provider.hpp"

#include <sstream>

#include <curl/curl.h>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

namespace rimeclaw {

// Serialize messages to Anthropic format.
// Returns {system_prompt, messages_json} -- system messages extracted to
// top-level field.
static std::pair<std::string, nlohmann::json>
serialize_messages_to_anthropic(const std::vector<Message>& messages) {
  std::string system_prompt;
  nlohmann::json arr = nlohmann::json::array();

  for (const auto& msg : messages) {
    if (msg.role == "system") {
      if (!system_prompt.empty())
        system_prompt += "\n";
      system_prompt += msg.text();
      continue;
    }

    nlohmann::json content_arr = nlohmann::json::array();

    for (const auto& b : msg.content) {
      if (b.type == "text" || b.type == "thinking") {
        if (!b.text.empty()) {
          content_arr.push_back({{"type", "text"}, {"text", b.text}});
        }
      } else if (b.type == "tool_use") {
        content_arr.push_back({{"type", "tool_use"},
                               {"id", b.id},
                               {"name", b.name},
                               {"input", b.input}});
      } else if (b.type == "tool_result") {
        content_arr.push_back({{"type", "tool_result"},
                               {"tool_use_id", b.tool_use_id},
                               {"content", b.content}});
      }
    }

    if (content_arr.empty())
      continue;

    std::string role = msg.role;
    if (role == "tool")
      role = "user";

    arr.push_back({{"role", role}, {"content", content_arr}});
  }

  return {system_prompt, arr};
}

// Map thinking level string to budget_tokens for Anthropic extended thinking
// API. Returns 0 if thinking is disabled.
static int thinking_budget_tokens(const std::string& level) {
  if (level == "low")
    return 1024;
  if (level == "medium")
    return 4096;
  if (level == "high")
    return 16000;
  return 0;
}

// Apply thinking parameters to an Anthropic API payload.
static void apply_thinking_params(nlohmann::json& payload,
                                  const ChatCompletionRequest& request) {
  int budget = thinking_budget_tokens(request.thinking);
  if (budget > 0) {
    payload["thinking"] = {{"type", "enabled"}, {"budget_tokens", budget}};
    payload["temperature"] = 1;
    if (request.max_tokens <= budget) {
      payload["max_tokens"] = budget + 4096;
    }
  }
}

// Convert tools from OpenAI format to Anthropic format.
static nlohmann::json
convert_tools_to_anthropic(const std::vector<nlohmann::json>& tools) {
  nlohmann::json arr = nlohmann::json::array();
  for (const auto& tool : tools) {
    if (tool.value("type", "") == "function" && tool.contains("function")) {
      const auto& fn = tool["function"];
      nlohmann::json anthropic_tool;
      anthropic_tool["name"] = fn.value("name", "");
      anthropic_tool["description"] = fn.value("description", "");
      if (fn.contains("parameters")) {
        anthropic_tool["input_schema"] = fn["parameters"];
      } else {
        anthropic_tool["input_schema"] = {
            {"type", "object"}, {"properties", nlohmann::json::object()}};
      }
      arr.push_back(anthropic_tool);
    }
  }
  return arr;
}

AnthropicProvider::AnthropicProvider(const std::string& api_key,
                                     const std::string& base_url, int timeout,
                                     const std::string& proxy)
    : HttpProviderBase(api_key,
                       base_url.empty() ? "https://api.anthropic.com" : base_url,
                       timeout, proxy) {
  spdlog::info("AnthropicProvider initialized with base_url: {}", base_url_);
}

ChatCompletionResponse
AnthropicProvider::ChatCompletion(const ChatCompletionRequest& request) {
  auto [system_prompt, messages_json] =
      serialize_messages_to_anthropic(request.messages);

  nlohmann::json payload;
  payload["model"] = request.model;
  payload["temperature"] = request.temperature;
  payload["max_tokens"] = request.max_tokens;
  payload["messages"] = messages_json;

  if (!system_prompt.empty()) {
    payload["system"] = system_prompt;
  }

  if (!request.tools.empty()) {
    payload["tools"] = convert_tools_to_anthropic(request.tools);
    if (request.tool_choice_auto) {
      payload["tool_choice"] = {{"type", "auto"}};
    }
  }

  apply_thinking_params(payload, request);

  std::string json_payload = payload.dump(-1, ' ', false,
                                          nlohmann::json::error_handler_t::replace);
  spdlog::debug("Sending request to Anthropic API: {}", json_payload);

  std::string response = MakeApiRequest(json_payload);
  spdlog::debug("Received response from Anthropic API: {}", response);

  nlohmann::json response_json = nlohmann::json::parse(response);

  ChatCompletionResponse result;
  if (response_json.contains("content") &&
      response_json["content"].is_array()) {
    for (const auto& block : response_json["content"]) {
      std::string block_type = block.value("type", "");
      if (block_type == "text") {
        if (!result.content.empty())
          result.content += "\n";
        result.content += block.value("text", "");
      } else if (block_type == "tool_use") {
        ToolCall tc;
        tc.id = block.value("id", "");
        tc.name = block.value("name", "");
        tc.arguments = block.value("input", nlohmann::json::object());
        result.tool_calls.push_back(tc);
      }
    }
  }

  std::string stop_reason = response_json.value("stop_reason", "");
  if (stop_reason == "end_turn") {
    result.finish_reason = "stop";
  } else if (stop_reason == "tool_use") {
    result.finish_reason = "tool_calls";
  } else if (stop_reason == "max_tokens") {
    result.finish_reason = "length";
  } else {
    result.finish_reason = stop_reason;
  }

  return result;
}

std::string AnthropicProvider::GetProviderName() const {
  return "anthropic";
}

CurlSlist AnthropicProvider::CreateHeaders() const {
  CurlSlist headers;
  headers.append("Content-Type: application/json");

  std::string api_key_header = "x-api-key: " + api_key_;
  headers.append(api_key_header.c_str());
  headers.append("anthropic-version: 2023-06-01");

  return headers;
}

// --- SSE Streaming support ---

struct AnthropicStreamContext {
  std::function<void(const ChatCompletionResponse&)> callback;
  std::string buffer;
  std::string event_type;

  struct PendingToolCall {
    std::string id;
    std::string name;
    std::string arguments;
  };
  std::vector<PendingToolCall> pending_tool_calls;
  int current_block_index = -1;
  std::string current_block_type;
};

static size_t AnthropicStreamWriteCallback(void* contents, size_t size,
                                           size_t nmemb, void* userp) {
  auto* ctx = static_cast<AnthropicStreamContext*>(userp);
  size_t total = size * nmemb;
  std::string chunk(static_cast<char*>(contents), total);
  ctx->buffer += chunk;

  size_t pos;
  while ((pos = ctx->buffer.find('\n')) != std::string::npos) {
    std::string line = ctx->buffer.substr(0, pos);
    ctx->buffer.erase(0, pos + 1);

    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }

    if (line.empty())
      continue;

    if (line.substr(0, 6) == "event:") {
      ctx->event_type = line.substr(6);
      if (!ctx->event_type.empty() && ctx->event_type[0] == ' ') {
        ctx->event_type = ctx->event_type.substr(1);
      }
      continue;
    }

    if (line.substr(0, 5) != "data:")
      continue;
    std::string data = line.substr(5);
    if (!data.empty() && data[0] == ' ') {
      data = data.substr(1);
    }

    auto j = nlohmann::json::parse(data, nullptr, false);
    if (j.is_discarded())
      continue;

    if (ctx->event_type == "content_block_start") {
      ctx->current_block_index++;
      if (j.contains("content_block")) {
        const auto& block = j["content_block"];
        ctx->current_block_type = block.value("type", "");
        if (ctx->current_block_type == "tool_use") {
          AnthropicStreamContext::PendingToolCall ptc;
          ptc.id = block.value("id", "");
          ptc.name = block.value("name", "");
          ctx->pending_tool_calls.push_back(ptc);
        }
      }
    } else if (ctx->event_type == "content_block_delta") {
      if (j.contains("delta")) {
        const auto& delta = j["delta"];
        std::string delta_type = delta.value("type", "");

        if (delta_type == "text_delta") {
          ChatCompletionResponse resp;
          resp.content = delta.value("text", "");
          ctx->callback(resp);
        } else if (delta_type == "input_json_delta") {
          if (!ctx->pending_tool_calls.empty()) {
            ctx->pending_tool_calls.back().arguments +=
                delta.value("partial_json", "");
          }
        }
      }
    } else if (ctx->event_type == "message_stop") {
      if (!ctx->pending_tool_calls.empty()) {
        ChatCompletionResponse tc_resp;
        tc_resp.finish_reason = "tool_calls";
        for (const auto& ptc : ctx->pending_tool_calls) {
          ToolCall tc;
          tc.id = ptc.id;
          tc.name = ptc.name;
          tc.arguments = nlohmann::json::parse(ptc.arguments, nullptr, false);
          if (tc.arguments.is_discarded()) {
            tc.arguments = nlohmann::json::object();
          }
          tc_resp.tool_calls.push_back(tc);
        }
        ctx->pending_tool_calls.clear();
        ctx->callback(tc_resp);
      }

      ChatCompletionResponse end_resp;
      end_resp.is_stream_end = true;
      ctx->callback(end_resp);
      return total;
    }
  }

  return total;
}

void AnthropicProvider::ChatCompletionStream(
    const ChatCompletionRequest& request,
    std::function<void(const ChatCompletionResponse&)> callback) {
  auto [system_prompt, messages_json] =
      serialize_messages_to_anthropic(request.messages);

  nlohmann::json payload;
  payload["model"] = request.model;
  payload["temperature"] = request.temperature;
  payload["max_tokens"] = request.max_tokens;
  payload["stream"] = true;
  payload["messages"] = messages_json;

  if (!system_prompt.empty()) {
    payload["system"] = system_prompt;
  }

  if (!request.tools.empty()) {
    payload["tools"] = convert_tools_to_anthropic(request.tools);
    if (request.tool_choice_auto) {
      payload["tool_choice"] = {{"type", "auto"}};
    }
  }

  apply_thinking_params(payload, request);

  std::string json_payload = payload.dump(-1, ' ', false,
                                          nlohmann::json::error_handler_t::replace);
  spdlog::debug("Sending streaming request to Anthropic API");

  AnthropicStreamContext stream_ctx;
  stream_ctx.callback = callback;

  RetryAfterCapture retry_capture;
  CurlHandle curl;
  CurlSlist headers = CreateHeaders();

  // Set stream-specific write callback
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, AnthropicStreamWriteCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &stream_ctx);

  // Configure shared streaming options
  ConfigureStreamingCurl(curl, headers, json_payload, retry_capture);

  CURLcode res = curl_easy_perform(curl);
  CheckStreamingErrors(curl, res, retry_capture);
}

std::vector<std::string> AnthropicProvider::GetSupportedModels() const {
  return {"claude-sonnet-4-6", "claude-opus-4-6", "claude-haiku-4-5"};
}

}  // namespace rimeclaw
