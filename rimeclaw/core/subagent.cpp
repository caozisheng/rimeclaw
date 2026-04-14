// Copyright 2025-2026 RimeClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "rimeclaw/core/subagent.hpp"

#include <algorithm>
#include <random>
#include <sstream>

namespace rimeclaw {

// --- SpawnMode ---

std::string spawn_mode_to_string(SpawnMode m) {
  switch (m) {
    case SpawnMode::kRun:
      return "run";
    case SpawnMode::kSession:
      return "session";
  }
  return "run";
}

SpawnMode spawn_mode_from_string(const std::string& s) {
  if (s == "session")
    return SpawnMode::kSession;
  return SpawnMode::kRun;
}

// --- SubagentConfig ---

SubagentConfig SubagentConfig::FromJson(const nlohmann::json& j) {
  SubagentConfig c;
  c.max_depth = j.value("maxDepth", 5);
  c.max_children = j.value("maxChildren", 5);
  if (j.contains("allowedAgents") && j["allowedAgents"].is_array()) {
    for (const auto& item : j["allowedAgents"]) {
      if (item.is_string())
        c.allowed_agents.push_back(item.get<std::string>());
    }
  }
  c.spawn_mode = spawn_mode_from_string(j.value("spawnMode", "run"));
  return c;
}

nlohmann::json SubagentConfig::ToJson() const {
  nlohmann::json j;
  j["maxDepth"] = max_depth;
  j["maxChildren"] = max_children;
  j["spawnMode"] = spawn_mode_to_string(spawn_mode);
  nlohmann::json agents = nlohmann::json::array();
  for (const auto& a : allowed_agents)
    agents.push_back(a);
  j["allowedAgents"] = agents;
  return j;
}

// --- SubagentManager ---

SubagentManager::SubagentManager(SubagentConfig config, AgentRunFn runner)
    : config_(std::move(config)), agent_runner_(std::move(runner)) {}

std::string SubagentManager::generate_run_id() const {
  static std::mt19937_64 rng{std::random_device{}()};
  std::uniform_int_distribution<uint64_t> dist;
  std::ostringstream oss;
  oss << std::hex << dist(rng);
  return oss.str();
}

std::string SubagentManager::generate_session_key(
    const std::string& agent_id) const {
  return "subagent:" + agent_id + ":" + generate_run_id();
}

bool SubagentManager::IsAllowed(const std::string& agent_id) const {
  if (config_.allowed_agents.empty())
    return true;
  return std::find(config_.allowed_agents.begin(), config_.allowed_agents.end(),
                   agent_id) != config_.allowed_agents.end();
}

int SubagentManager::ChildCount(const std::string& parent_run_id) const {
  std::lock_guard<std::mutex> lk(mu_);
  auto it = parent_children_.find(parent_run_id);
  if (it == parent_children_.end())
    return 0;
  return static_cast<int>(it->second.size());
}

SubagentRun SubagentManager::Spawn(const SpawnParams& params,
                                   const std::string& parent_run_id,
                                   int current_depth) {
  SubagentRun run;
  run.run_id = generate_run_id();
  run.agent_id = params.agent_id.empty() ? "default" : params.agent_id;
  run.label = params.label.empty() ? params.task : params.label;
  run.mode = params.mode;
  run.depth = current_depth + 1;
  run.status = SubagentRunStatus::kPending;
  run.started_at = std::chrono::system_clock::now();

  // Depth check
  if (run.depth > config_.max_depth) {
    run.status = SubagentRunStatus::kFailed;
    run.error = "Max subagent depth (" + std::to_string(config_.max_depth) +
                ") exceeded";
    return run;
  }

  // Allowed agents check
  if (!IsAllowed(run.agent_id)) {
    run.status = SubagentRunStatus::kFailed;
    run.error = "Agent '" + run.agent_id + "' is not in allowedAgents";
    return run;
  }

  // Child count check
  if (!parent_run_id.empty() &&
      ChildCount(parent_run_id) >= config_.max_children) {
    run.status = SubagentRunStatus::kFailed;
    run.error = "Max children (" + std::to_string(config_.max_children) +
                ") exceeded for parent " + parent_run_id;
    return run;
  }

  // Record relationship
  {
    std::lock_guard<std::mutex> lk(mu_);
    runs_[run.run_id] = run;
    if (!parent_run_id.empty())
      parent_children_[parent_run_id].push_back(run.run_id);
  }

  // Execute via runner
  if (agent_runner_) {
    run.status = SubagentRunStatus::kRunning;
    {
      std::lock_guard<std::mutex> lk(mu_);
      runs_[run.run_id].status = SubagentRunStatus::kRunning;
    }

    try {
      std::string session_key =
          (run.mode == SpawnMode::kSession)
              ? generate_session_key(run.agent_id)
              : "";
      run.result = agent_runner_(params, run.run_id, session_key);
      run.status = SubagentRunStatus::kCompleted;
    } catch (const std::exception& e) {
      run.status = SubagentRunStatus::kFailed;
      run.error = e.what();
    }
  } else {
    run.status = SubagentRunStatus::kFailed;
    run.error = "No agent runner configured";
  }

  run.finished_at = std::chrono::system_clock::now();

  // Update stored state
  {
    std::lock_guard<std::mutex> lk(mu_);
    runs_[run.run_id] = run;
  }

  return run;
}

std::optional<SubagentRun> SubagentManager::GetRun(
    const std::string& run_id) const {
  std::lock_guard<std::mutex> lk(mu_);
  auto it = runs_.find(run_id);
  if (it == runs_.end())
    return std::nullopt;
  return it->second;
}

std::vector<SubagentRun> SubagentManager::GetChildren(
    const std::string& parent_run_id) const {
  std::lock_guard<std::mutex> lk(mu_);
  std::vector<SubagentRun> result;
  auto it = parent_children_.find(parent_run_id);
  if (it == parent_children_.end())
    return result;
  for (const auto& id : it->second) {
    auto rit = runs_.find(id);
    if (rit != runs_.end())
      result.push_back(rit->second);
  }
  return result;
}

void SubagentManager::Cancel(const std::string& run_id) {
  std::lock_guard<std::mutex> lk(mu_);
  auto it = runs_.find(run_id);
  if (it != runs_.end() &&
      it->second.status == SubagentRunStatus::kRunning) {
    it->second.status = SubagentRunStatus::kFailed;
    it->second.error = "Cancelled";
    it->second.finished_at = std::chrono::system_clock::now();
  }
}

}  // namespace rimeclaw
