// Copyright 2025-2026 RimeClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "rimeclaw/providers/openai_provider.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

#include <curl/curl.h>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

namespace rimeclaw {

namespace {

constexpr size_t kMaxPendingToolCallIndex = 256;

static bool HasNonWhitespace(const std::string& value) {
  return std::any_of(value.begin(), value.end(),
                     [](unsigned char ch) { return !std::isspace(ch); });
}

} // namespace

static std::string json_nullable_string_or_empty(const nlohmann::json& obj,
                                                 const std::string& key) {
  auto it = obj.find(key);
  if (it == obj.end() || it->is_null()) {
    return "";
  }
  return it->get<std::string>();
}

static std::string message_text_content(const Message& msg) {
  std::string text;
  for (const auto& block : msg.content) {
    if (block.type == "text") {
      text += block.text;
    }
  }
  return text;
}

static std::string message_reasoning_content(const Message& msg) {
  std::string reasoning;
  for (const auto& block : msg.content) {
    if (block.type == "thinking") {
      reasoning += block.text;
    }
  }
  return reasoning;
}

static std::string json_text_content_or_empty(const nlohmann::json& value) {
  if (value.is_null()) {
    return "";
  }
  if (value.is_string()) {
    return value.get<std::string>();
  }
  if (value.is_array()) {
    std::string text;
    for (const auto& item : value) {
      if (item.is_string()) {
        text += item.get<std::string>();
      } else if (item.is_object()) {
        text += json_nullable_string_or_empty(item, "text");
      }
    }
    return text;
  }
  if (value.is_object()) {
    return json_nullable_string_or_empty(value, "text");
  }
  return "";
}

// Serialize Message vector to OpenAI wire format
static nlohmann::json
serialize_messages_to_openai(const std::vector<Message>& messages,
                             bool thinking_enabled) {
  nlohmann::json arr = nlohmann::json::array();

  for (const auto& msg : messages) {
    std::string text_content = message_text_content(msg);
    std::string reasoning_content = message_reasoning_content(msg);

    bool has_tool_use = false;
    bool has_tool_result = false;
    for (const auto& b : msg.content) {
      if (b.type == "tool_use")
        has_tool_use = true;
      if (b.type == "tool_result")
        has_tool_result = true;
    }

    if (has_tool_result) {
      for (const auto& b : msg.content) {
        if (b.type == "tool_result") {
          arr.push_back({{"role", "tool"},
                         {"tool_call_id", b.tool_use_id},
                         {"content", b.content}});
        }
      }
    } else if (has_tool_use) {
      nlohmann::json j;
      j["role"] = "assistant";

      if (!text_content.empty()) {
        j["content"] = text_content;
      }
      if (!reasoning_content.empty() || thinking_enabled) {
        j["reasoning_content"] = reasoning_content;
      }

      nlohmann::json tool_calls = nlohmann::json::array();
      for (const auto& b : msg.content) {
        if (b.type == "tool_use") {
          tool_calls.push_back(
              {{"id", b.id},
               {"type", "function"},
               {"function",
                {{"name", b.name}, {"arguments", b.input.dump()}}}});
        }
      }
      j["tool_calls"] = tool_calls;
      arr.push_back(j);
    } else {
      nlohmann::json j = {{"role", msg.role}, {"content", text_content}};
      if (msg.role == "assistant" && !reasoning_content.empty()) {
        j["reasoning_content"] = reasoning_content;
      }
      arr.push_back(std::move(j));
    }
  }

  return arr;
}

OpenAIProvider::OpenAIProvider(const std::string& api_key,
                               const std::string& base_url, int timeout,
                               const std::string& proxy)
    : HttpProviderBase(api_key,
                       base_url.empty() ? "https://api.openai.com/v1" : base_url,
                       timeout, proxy) {
  spdlog::debug("OpenAIProvider initialized with base_url: {}", base_url_);
}

ChatCompletionResponse
OpenAIProvider::ChatCompletion(const ChatCompletionRequest& request) {
  nlohmann::json payload;
  payload["model"] = request.model;
  payload["temperature"] = request.temperature;
  payload["max_tokens"] = request.max_tokens;

  payload["messages"] =
      serialize_messages_to_openai(request.messages, request.thinking != "off");

  if (!request.tools.empty()) {
    payload["tools"] = request.tools;
    if (request.tool_choice_auto) {
      payload["tool_choice"] = "auto";
    }
  }

  std::string json_payload = payload.dump();
  spdlog::debug("Sending request to OpenAI API: {}", json_payload);

  std::string response = MakeApiRequest(json_payload);
  spdlog::debug("Received response from OpenAI API: {}", response);

  nlohmann::json response_json = nlohmann::json::parse(response);

  ChatCompletionResponse result;
  if (response_json.contains("choices") && !response_json["choices"].empty()) {
    auto choice = response_json["choices"][0];
    if (choice.contains("message")) {
      auto message = choice["message"];
      if (message.contains("content")) {
        result.content = json_text_content_or_empty(message["content"]);
      }
      if (message.contains("reasoning_content")) {
        result.reasoning_content =
            json_text_content_or_empty(message["reasoning_content"]);
      }
      if (message.contains("tool_calls")) {
        for (const auto& tc : message["tool_calls"]) {
          ToolCall tool_call;
          tool_call.id = tc.value("id", "");
          if (tc.contains("function")) {
            tool_call.name = tc["function"].value("name", "");
            if (tc["function"].contains("arguments")) {
              auto args_str = tc["function"]["arguments"].get<std::string>();
              tool_call.arguments = nlohmann::json::parse(args_str);
            }
          }
          result.tool_calls.push_back(tool_call);
        }
      }
    }
    if (choice.contains("finish_reason")) {
      result.finish_reason =
          json_nullable_string_or_empty(choice, "finish_reason");
    }
  }

  return result;
}

std::string OpenAIProvider::GetProviderName() const {
  return "openai";
}

CurlSlist OpenAIProvider::CreateHeaders() const {
  CurlSlist headers;
  headers.append("Content-Type: application/json");

  std::string auth_header = "Authorization: Bearer " + api_key_;
  headers.append(auth_header.c_str());

  return headers;
}

// --- SSE Streaming support ---

struct StreamContext {
  std::function<void(const ChatCompletionResponse&)> callback;
  std::string buffer;

  struct PendingToolCall {
    std::string id;
    std::string name;
    std::string arguments;
  };
  std::vector<PendingToolCall> pending_tool_calls;
};

static std::vector<ToolCall> TakeCompleteToolCalls(StreamContext* ctx,
                                                   bool drop_incomplete) {
  std::vector<ToolCall> complete;
  std::vector<StreamContext::PendingToolCall> remaining;

  for (const auto& pending : ctx->pending_tool_calls) {
    nlohmann::json parsed_arguments = nlohmann::json::object();
    bool invalid_arguments = false;
    if (HasNonWhitespace(pending.arguments)) {
      parsed_arguments =
          nlohmann::json::parse(pending.arguments, nullptr, false);
      invalid_arguments = parsed_arguments.is_discarded();
    }

    if (pending.id.empty() || !HasNonWhitespace(pending.name) ||
        invalid_arguments) {
      if (!drop_incomplete) {
        remaining.push_back(pending);
      } else {
        spdlog::warn("Dropping incomplete streamed tool call: id='{}', name='{}', arguments_len={}",
                     pending.id, pending.name, pending.arguments.size());
      }
      continue;
    }

    ToolCall tc;
    tc.id = pending.id;
    tc.name = pending.name;
    tc.arguments = std::move(parsed_arguments);
    complete.push_back(std::move(tc));
  }

  ctx->pending_tool_calls = std::move(remaining);
  return complete;
}

static size_t StreamWriteCallback(void* contents, size_t size, size_t nmemb,
                                  void* userp) {
  auto* ctx = static_cast<StreamContext*>(userp);
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
    if (line.substr(0, 6) != "data: ")
      continue;

    std::string data = line.substr(6);
    if (data == "[DONE]") {
      if (!ctx->pending_tool_calls.empty()) {
        auto complete = TakeCompleteToolCalls(ctx, true);
        ChatCompletionResponse tc_resp;
        tc_resp.finish_reason = "tool_calls";
        tc_resp.tool_calls = std::move(complete);
        if (!tc_resp.tool_calls.empty()) {
          ctx->callback(tc_resp);
        }
      }

      ChatCompletionResponse end_resp;
      end_resp.is_stream_end = true;
      ctx->callback(end_resp);
      return total;
    }

    auto j = nlohmann::json::parse(data, nullptr, false);
    if (j.is_discarded())
      continue;

    spdlog::debug("SSE chunk: {}", j.dump().substr(0, 500));

    if (!j.contains("choices") || j["choices"].empty())
      continue;
    const auto& choice = j["choices"][0];

    nlohmann::json delta;
    if (choice.contains("delta") && !choice["delta"].is_null()) {
      delta = choice["delta"];
    }
    std::string finish_reason;
    if (choice.contains("finish_reason") && !choice["finish_reason"].is_null()) {
      finish_reason = choice["finish_reason"].get<std::string>();
    }

    if (delta.contains("content") && !delta["content"].is_null()) {
      ChatCompletionResponse resp;
      resp.content = delta["content"].get<std::string>();
      ctx->callback(resp);
    }

    if (delta.contains("reasoning_content") &&
        !delta["reasoning_content"].is_null()) {
      ChatCompletionResponse resp;
      resp.reasoning_content = delta["reasoning_content"].get<std::string>();
      if (!resp.reasoning_content.empty()) {
        ctx->callback(resp);
      }
    }

    if (delta.contains("reasoning") && !delta["reasoning"].is_null()) {
      ChatCompletionResponse resp;
      resp.reasoning_content = delta["reasoning"].get<std::string>();
      if (!resp.reasoning_content.empty()) {
        ctx->callback(resp);
      }
    }

    if (delta.contains("tool_calls") && delta["tool_calls"].is_array()) {
      for (const auto& tc_delta : delta["tool_calls"]) {
        int index = tc_delta.value("index", 0);
        if (index < 0) {
          continue;
        }
        const auto tool_index = static_cast<size_t>(index);
        if (tool_index >= kMaxPendingToolCallIndex) {
          spdlog::warn("Ignoring streamed tool_call with oversized index={}",
                       tool_index);
          continue;
        }

        if (ctx->pending_tool_calls.size() <= tool_index) {
          ctx->pending_tool_calls.resize(tool_index + 1);
        }

        auto& ptc = ctx->pending_tool_calls[tool_index];
        if (tc_delta.contains("id") && !tc_delta["id"].is_null()) {
          ptc.id = tc_delta["id"].get<std::string>();
        }
        if (tc_delta.contains("function")) {
          const auto& fn = tc_delta["function"];
          if (fn.contains("name") && !fn["name"].is_null()) {
            ptc.name = fn["name"].get<std::string>();
          }
          if (fn.contains("arguments") && !fn["arguments"].is_null()) {
            ptc.arguments += fn["arguments"].get<std::string>();
          }
        }
      }
    }

    if (finish_reason == "tool_calls" && !ctx->pending_tool_calls.empty()) {
      auto complete = TakeCompleteToolCalls(ctx, false);
      ChatCompletionResponse tc_resp;
      tc_resp.finish_reason = "tool_calls";
      tc_resp.tool_calls = std::move(complete);
      if (!tc_resp.tool_calls.empty()) {
        ctx->callback(tc_resp);
      }
    }
  }

  return total;
}

void OpenAIProvider::ChatCompletionStream(
    const ChatCompletionRequest& request,
    std::function<void(const ChatCompletionResponse&)> callback) {
  nlohmann::json payload;
  payload["model"] = request.model;
  payload["temperature"] = request.temperature;
  payload["max_tokens"] = request.max_tokens;
  payload["stream"] = true;

  payload["messages"] =
      serialize_messages_to_openai(request.messages, request.thinking != "off");

  if (!request.tools.empty()) {
    payload["tools"] = request.tools;
    if (request.tool_choice_auto) {
      payload["tool_choice"] = "auto";
    }
  }

  std::string json_payload = payload.dump();
  spdlog::debug("Sending streaming request to OpenAI API");

  StreamContext stream_ctx;
  stream_ctx.callback = callback;

  RetryAfterCapture retry_capture;
  CurlHandle curl;
  CurlSlist headers = CreateHeaders();

  // Set stream-specific write callback
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, StreamWriteCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &stream_ctx);

  // Configure shared streaming options
  ConfigureStreamingCurl(curl, headers, json_payload, retry_capture);

  CURLcode res = curl_easy_perform(curl);
  CheckStreamingErrors(curl, res, retry_capture);
}

std::vector<std::string> OpenAIProvider::GetSupportedModels() const {
  return {"gpt-4-turbo", "gpt-4", "gpt-3.5-turbo"};
}

}  // namespace rimeclaw
