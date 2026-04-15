// Copyright 2025-2026 RimeClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once
#ifdef BUILD_LLAMA_LOCAL_PROVIDER

#include <mutex>
#include <string>
#include <vector>

#include <llama.h>
#include <nlohmann/json.hpp>

#include "rimeclaw/providers/llm_provider.hpp"

namespace rimeclaw {

struct LlamaLocalConfig {
  std::string model_path;
  int n_ctx = 4096;
  int n_gpu_layers = -1;  // -1 = offload all (auto)
  int n_threads = 0;      // 0 = auto-detect
  int n_batch = 512;
  float repeat_penalty = 1.1f;

  static LlamaLocalConfig FromExtra(const nlohmann::json& extra);
};

class LlamaLocalProvider : public LLMProvider {
 public:
  explicit LlamaLocalProvider(const LlamaLocalConfig& config);
  ~LlamaLocalProvider() override;

  LlamaLocalProvider(const LlamaLocalProvider&) = delete;
  LlamaLocalProvider& operator=(const LlamaLocalProvider&) = delete;

  // LLMProvider interface
  ChatCompletionResponse
  ChatCompletion(const ChatCompletionRequest& req) override;
  void ChatCompletionStream(
      const ChatCompletionRequest& req,
      std::function<void(const ChatCompletionResponse&)> cb) override;
  std::string GetProviderName() const override { return "local"; }
  std::vector<std::string> GetSupportedModels() const override;

 private:
  void LoadModel();
  void UnloadModel();

  // Prompt building (Qwen3 chat template with tool support)
  std::string BuildPrompt(const ChatCompletionRequest& req) const;
  std::string FormatToolsForSystemPrompt(
      const std::vector<nlohmann::json>& tools) const;

  // Tokenization
  std::vector<llama_token> Tokenize(const std::string& text,
                                    bool add_special) const;
  std::string TokenToStr(llama_token token) const;

  // Generation core
  struct GenerateResult {
    std::string text;
    int prompt_tokens = 0;
    int completion_tokens = 0;
    std::string finish_reason;  // "stop" | "length"
  };
  GenerateResult Generate(const std::string& prompt, int max_tokens,
                          float temperature,
                          std::function<bool(const std::string&)> token_cb = nullptr);

  // Tool call parsing from model output
  std::vector<ToolCall> ParseToolCalls(const std::string& output) const;

  // Persistent context for KV cache reuse across calls
  void EnsureContext();
  void ClearKVCache();

  LlamaLocalConfig config_;
  llama_model* model_ = nullptr;
  const llama_vocab* vocab_ = nullptr;
  llama_context* ctx_ = nullptr;
  std::vector<llama_token> kv_tokens_;  // tokens currently in KV cache
  std::mutex inference_mutex_;
};

}  // namespace rimeclaw

#endif  // BUILD_LLAMA_LOCAL_PROVIDER
