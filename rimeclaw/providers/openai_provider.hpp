// Copyright 2025-2026 RimeClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string>

#include "rimeclaw/providers/http_provider_base.hpp"

namespace rimeclaw {

class OpenAIProvider : public HttpProviderBase {
 public:
  OpenAIProvider(const std::string& api_key, const std::string& base_url,
                 int timeout, const std::string& proxy = "");

  ChatCompletionResponse
  ChatCompletion(const ChatCompletionRequest& request) override;
  void ChatCompletionStream(
      const ChatCompletionRequest& request,
      std::function<void(const ChatCompletionResponse&)> callback) override;
  std::string GetProviderName() const override;
  std::vector<std::string> GetSupportedModels() const override;

 protected:
  std::string GetApiEndpoint() const override { return "/chat/completions"; }
  CurlSlist CreateHeaders() const override;
  std::string GetProviderTag() const override { return "openai"; }
};

}  // namespace rimeclaw
