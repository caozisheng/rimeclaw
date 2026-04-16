// Copyright 2025-2026 RimeClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string>

#include <curl/curl.h>

#include "rimeclaw/providers/curl_raii.hpp"
#include "rimeclaw/providers/llm_provider.hpp"

namespace rimeclaw {

// Captures the Retry-After header value from HTTP response headers.
struct RetryAfterCapture {
  int retry_after_seconds = 0;
};

// Base class for HTTP-based LLM providers (Anthropic, OpenAI, etc.).
// Extracts shared curl setup, error handling, and request infrastructure.
class HttpProviderBase : public LLMProvider {
 public:
  HttpProviderBase(const std::string& api_key, const std::string& base_url,
                   int timeout, const std::string& proxy);

 protected:
  // Subclass hooks
  virtual std::string GetApiEndpoint() const = 0;
  virtual CurlSlist CreateHeaders() const = 0;
  virtual std::string GetProviderTag() const = 0;

  // Synchronous HTTP POST, returns response body. Throws ProviderError on failure.
  std::string MakeApiRequest(const std::string& json_payload) const;

  // Configure a curl handle for SSE streaming (TCP_NODELAY, keepalive, timeouts, proxy).
  // Caller still sets WRITEFUNCTION/WRITEDATA before calling this.
  void ConfigureStreamingCurl(CurlHandle& curl, CurlSlist& headers,
                              const std::string& json_payload,
                              RetryAfterCapture& retry_capture) const;

  // Check curl result and HTTP status after a streaming request. Throws ProviderError on failure.
  void CheckStreamingErrors(CurlHandle& curl, CURLcode res,
                            const RetryAfterCapture& retry) const;

  std::string api_key_;
  std::string base_url_;
  std::string proxy_;
  int timeout_;
};

}  // namespace rimeclaw
