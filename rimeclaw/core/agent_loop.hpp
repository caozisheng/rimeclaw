// Copyright 2025-2026 RimeClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>
#include "rimeclaw/common/noncopyable.hpp"
#include "rimeclaw/config.hpp"
#include "rimeclaw/core/context_engine.hpp"
#include "rimeclaw/core/usage_accumulator.hpp"
#include "rimeclaw/providers/llm_provider.hpp"

namespace rimeclaw {

class MemoryManager;
class SkillLoader;
class ToolRegistry;
class ProviderRegistry;
class SubagentManager;
class FailoverResolver;

// --- Agent Event (for streaming) ---

struct AgentEvent {
  std::string
      type;  // "text_delta" | "tool_use" | "tool_result" | "message_end"
  nlohmann::json data;
};

using AgentEventCallback = std::function<void(const AgentEvent&)>;

// --- Agent Loop ---

class AgentLoop : public Noncopyable {
 public:
  AgentLoop(std::shared_ptr<MemoryManager> memory_manager,
            std::shared_ptr<SkillLoader> skill_loader,
            std::shared_ptr<ToolRegistry> tool_registry,
            std::shared_ptr<LLMProvider> llm_provider,
            const AgentConfig& agent_config);

  // Process a message with externally-provided history and system prompt.
  // Returns all new messages generated during the turn (assistant +
  // tool_result). usage_session_key: if non-empty, usage is recorded under
  // this key instead of session_key_ — allows per-request tracking without
  // mutating shared state.
  std::vector<Message>
  ProcessMessage(const std::string& message,
                 const std::vector<Message>& history,
                 const std::string& system_prompt,
                 const std::string& usage_session_key = "");

  // Streaming version — returns all new messages generated during the turn.
  // usage_session_key: same semantics as ProcessMessage.
  std::vector<Message>
  ProcessMessageStream(const std::string& message,
                       const std::vector<Message>& history,
                       const std::string& system_prompt,
                       AgentEventCallback event_cb,
                       const std::string& usage_session_key = "");

  // Request early stop (e.g. from signal handler)
  void RequestStop() { stop_requested_ = true; }

  // Inject optional dependencies
  void SetProviderRegistry(ProviderRegistry* reg) {
    provider_registry_ = reg;
  }
  void SetSubagentManager(SubagentManager* mgr) {
    subagent_manager_ = mgr;
  }
  void SetFailoverResolver(FailoverResolver* fr) {
    failover_resolver_ = fr;
  }
  void SetContextEngine(std::shared_ptr<ContextEngine> engine) {
    context_engine_ = std::move(engine);
  }
  void SetSessionKey(const std::string& key) { session_key_ = key; }
  void SetUsageAccumulator(std::shared_ptr<UsageAccumulator> acc) {
    usage_accumulator_ = std::move(acc);
  }

  std::shared_ptr<UsageAccumulator> GetUsageAccumulator() const {
    return usage_accumulator_;
  }

  // Set model dynamically (resolves via ProviderRegistry if available)
  void SetModel(const std::string& model_ref);

 private:
  // Resolve current provider (from registry or fallback to injected provider)
  std::shared_ptr<LLMProvider> resolve_provider();

  std::vector<std::string>
  handle_tool_calls(const std::vector<nlohmann::json>& tool_calls);

  std::shared_ptr<MemoryManager> memory_manager_;
  std::shared_ptr<SkillLoader> skill_loader_;
  std::shared_ptr<ToolRegistry> tool_registry_;
  std::shared_ptr<LLMProvider> llm_provider_;  // Fallback / injected provider
  ProviderRegistry* provider_registry_ = nullptr;        // Non-owning, optional
  SubagentManager* subagent_manager_ = nullptr;          // Non-owning, optional
  FailoverResolver* failover_resolver_ = nullptr;        // Non-owning, optional
  std::shared_ptr<UsageAccumulator> usage_accumulator_;  // Shared ownership
  std::shared_ptr<ContextEngine> context_engine_;        // Pluggable engine
  std::string session_key_;  // For failover session pinning
  AgentConfig agent_config_;
  std::atomic<bool> stop_requested_{false};
  int max_iterations_ = 15;

  // Tracking last resolved provider/profile for failover reporting
  std::string last_provider_id_;
  std::string last_profile_id_;
  std::string resolved_request_model_;  // Model name to use in LLM requests
};

}  // namespace rimeclaw
