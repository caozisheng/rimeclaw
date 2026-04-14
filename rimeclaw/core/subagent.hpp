// Copyright 2025-2026 RimeClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <chrono>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

namespace rimeclaw {

// Spawn mode (compatible with rimeclaw subagent.mode)
enum class SpawnMode {
  kRun,      // Ephemeral: auto-cleanup on completion
  kSession,  // Persistent: stays active for follow-ups
};

std::string spawn_mode_to_string(SpawnMode m);
SpawnMode spawn_mode_from_string(const std::string& s);

// Subagent spawn parameters
struct SpawnParams {
  std::string task;            // Task description for subagent
  std::string label;           // Human-readable label
  std::string agent_id;        // Target agent ID (defaults to current)
  std::string model;           // Model override
  std::string thinking;        // Thinking level: off|low|medium|high
  int timeout_seconds = 300;   // Run timeout
  SpawnMode mode = SpawnMode::kRun;
  bool cleanup = true;         // Auto-delete session on completion
  std::string subagent_role;   // "" | "orchestrator" | "leaf"
};

// Run status
enum class SubagentRunStatus {
  kPending,
  kRunning,
  kCompleted,
  kFailed,
  kCancelled,
};

// Tracked child run info
struct SubagentRun {
  std::string run_id;
  std::string agent_id;
  std::string label;
  SpawnMode mode = SpawnMode::kRun;
  int depth = 0;

  SubagentRunStatus status = SubagentRunStatus::kPending;
  std::string error;
  std::string result;

  std::chrono::system_clock::time_point started_at;
  std::chrono::system_clock::time_point finished_at;
};

// Subagent configuration limits
struct SubagentConfig {
  int max_depth = 5;
  int max_children = 5;
  bool enabled = true;
  std::vector<std::string> allowed_agents;
  SpawnMode spawn_mode = SpawnMode::kRun;

  static SubagentConfig FromJson(const nlohmann::json& j);
  nlohmann::json ToJson() const;
};

// Agent runner function type
using AgentRunFn =
    std::function<std::string(const SpawnParams& params,
                              const std::string& run_id,
                              const std::string& session_key)>;

class SubagentManager {
 public:
  SubagentManager(SubagentConfig config, AgentRunFn runner);

  // Spawn a subagent; returns the run info
  SubagentRun Spawn(const SpawnParams& params,
                    const std::string& parent_run_id,
                    int current_depth = 0);

  // Check if agent is in allowed list
  bool IsAllowed(const std::string& agent_id) const;

  // Count children of a parent run
  int ChildCount(const std::string& parent_run_id) const;

  // Get a specific run
  std::optional<SubagentRun> GetRun(const std::string& run_id) const;

  // Get children of a parent run
  std::vector<SubagentRun>
  GetChildren(const std::string& parent_run_id) const;

  // Cancel a running subagent
  void Cancel(const std::string& run_id);

  // Get current config
  const SubagentConfig& GetConfig() const { return config_; }

 private:
  SubagentConfig config_;
  AgentRunFn agent_runner_;

  mutable std::mutex mu_;
  std::unordered_map<std::string, SubagentRun> runs_;
  std::unordered_map<std::string, std::vector<std::string>> parent_children_;

  std::string generate_run_id() const;
  std::string generate_session_key(const std::string& agent_id) const;
};

}  // namespace rimeclaw
