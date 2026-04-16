// Copyright 2025-2026 RimeClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "rimeclaw/providers/http_provider_base.hpp"

#include <curl/curl.h>
#include <spdlog/spdlog.h>

#include "rimeclaw/providers/provider_error.hpp"

namespace rimeclaw {

// --- Shared curl callbacks ---

static size_t WriteCallback(void* contents, size_t size, size_t nmemb,
                            std::string* userp) {
  userp->append((char*)contents, size * nmemb);
  return size * nmemb;
}

static size_t HeaderCallback(char* buffer, size_t size, size_t nitems,
                             void* userdata) {
  size_t total = size * nitems;
  auto* capture = static_cast<RetryAfterCapture*>(userdata);
  std::string header(buffer, total);

  // Case-insensitive prefix match for "retry-after:"
  if (header.size() > 12) {
    std::string lower = header.substr(0, 12);
    for (auto& c : lower)
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (lower == "retry-after:") {
      std::string value = header.substr(12);
      auto start = value.find_first_not_of(" \t");
      if (start != std::string::npos) {
        value = value.substr(start);
      }
      try {
        capture->retry_after_seconds = std::stoi(value);
      } catch (...) {
      }
    }
  }
  return total;
}

// --- Constructor ---

HttpProviderBase::HttpProviderBase(const std::string& api_key,
                                   const std::string& base_url, int timeout,
                                   const std::string& proxy)
    : api_key_(api_key),
      base_url_(base_url),
      proxy_(proxy),
      timeout_(timeout) {}

// --- Synchronous HTTP POST ---

std::string
HttpProviderBase::MakeApiRequest(const std::string& json_payload) const {
  std::string read_buffer;
  RetryAfterCapture retry_capture;

  CurlHandle curl;
  CurlSlist headers = CreateHeaders();

  std::string url = base_url_ + GetApiEndpoint();
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers.get());
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_payload.c_str());
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &read_buffer);
  curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, HeaderCallback);
  curl_easy_setopt(curl, CURLOPT_HEADERDATA, &retry_capture);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout_);
  if (!proxy_.empty()) {
    curl_easy_setopt(curl, CURLOPT_PROXY, proxy_.c_str());
  }

  CURLcode res = curl_easy_perform(curl);
  if (res != CURLE_OK) {
    ProviderErrorKind kind = ProviderErrorKind::kUnknown;
    if (res == CURLE_OPERATION_TIMEDOUT) {
      kind = ProviderErrorKind::kTimeout;
    } else if (res == CURLE_COULDNT_CONNECT ||
               res == CURLE_COULDNT_RESOLVE_HOST) {
      kind = ProviderErrorKind::kTransient;
    }
    throw ProviderError(
        kind, 0,
        "CURL request failed: " + std::string(curl_easy_strerror(res)),
        GetProviderTag());
  }

  long http_code = 0;
  curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &http_code);

  if (http_code >= 400) {
    auto error_kind =
        ClassifyHttpError(static_cast<int>(http_code), read_buffer);
    if (retry_capture.retry_after_seconds > 0) {
      spdlog::warn("{} API HTTP {}: rate limited, retry-after={}s",
                   GetProviderTag(), http_code,
                   retry_capture.retry_after_seconds);
    } else {
      spdlog::error("{} API HTTP {}: {}", GetProviderTag(), http_code,
                    read_buffer.substr(0, 256));
    }
    ProviderError err(error_kind, static_cast<int>(http_code),
                      GetProviderTag() + " API error (HTTP " +
                          std::to_string(http_code) + "): " + read_buffer,
                      GetProviderTag());
    err.SetRetryAfterSeconds(retry_capture.retry_after_seconds);
    throw err;
  }

  return read_buffer;
}

// --- Streaming curl configuration ---

void HttpProviderBase::ConfigureStreamingCurl(
    CurlHandle& curl, CurlSlist& headers, const std::string& json_payload,
    RetryAfterCapture& retry_capture) const {
  std::string url = base_url_ + GetApiEndpoint();
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers.get());
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_payload.c_str());
  curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, HeaderCallback);
  curl_easy_setopt(curl, CURLOPT_HEADERDATA, &retry_capture);

  // Disable Nagle algorithm to reduce latency
  curl_easy_setopt(curl, CURLOPT_TCP_NODELAY, 1L);

  // Set overall connection timeout for the initial connection phase
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, timeout_);

  // Set a large overall timeout for the streaming session (10 minutes)
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 600L);

  // Disable low speed limit to prevent aborting long-running streams
  curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 0L);
  curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 0L);

  // Enable TCP Keep-Alive to prevent NATs/firewalls from dropping idle
  // connections
  curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);
  curl_easy_setopt(curl, CURLOPT_TCP_KEEPIDLE, 15L);
  curl_easy_setopt(curl, CURLOPT_TCP_KEEPINTVL, 5L);

  if (!proxy_.empty()) {
    curl_easy_setopt(curl, CURLOPT_PROXY, proxy_.c_str());
  }
}

// --- Streaming error checking ---

void HttpProviderBase::CheckStreamingErrors(CurlHandle& curl, CURLcode res,
                                            const RetryAfterCapture& retry) const {
  if (res != CURLE_OK) {
    ProviderErrorKind kind = ProviderErrorKind::kUnknown;
    if (res == CURLE_OPERATION_TIMEDOUT) {
      kind = ProviderErrorKind::kTimeout;
    } else if (res == CURLE_COULDNT_CONNECT ||
               res == CURLE_COULDNT_RESOLVE_HOST) {
      kind = ProviderErrorKind::kTransient;
    }
    throw ProviderError(kind, 0,
                        "CURL streaming request failed: " +
                            std::string(curl_easy_strerror(res)),
                        GetProviderTag());
  }

  long http_code = 0;
  curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &http_code);

  if (http_code >= 400) {
    auto error_kind = ClassifyHttpError(static_cast<int>(http_code), "");
    if (retry.retry_after_seconds > 0) {
      spdlog::warn("{} streaming HTTP {}: rate limited, retry-after={}s",
                   GetProviderTag(), http_code, retry.retry_after_seconds);
    } else {
      spdlog::error("{} streaming HTTP {}", GetProviderTag(), http_code);
    }
    ProviderError err(error_kind, static_cast<int>(http_code),
                      GetProviderTag() + " streaming API error (HTTP " +
                          std::to_string(http_code) + ")",
                      GetProviderTag());
    err.SetRetryAfterSeconds(retry.retry_after_seconds);
    throw err;
  }
}

}  // namespace rimeclaw
