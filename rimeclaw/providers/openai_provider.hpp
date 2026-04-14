// Copyright 2025-2026 RimeClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string>

#include "rimeclaw/providers/curl_raii.hpp"

#include "llm_provider.hpp"

namespace rimeclaw {

class OpenAIProvider : public LLMProvider {
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

 private:
  std::string MakeApiRequest(const std::string& json_payload) const;
  CurlSlist CreateHeaders() const;

  std::string api_key_;
  std::string base_url_;
  std::string proxy_;
  int timeout_;
};

}  // namespace rimeclaw
