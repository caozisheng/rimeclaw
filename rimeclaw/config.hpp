// Copyright 2025-2026 RimeClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string>
#include <optional>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include "rimeclaw/constants.hpp"

namespace rimeclaw {

// ---------------------------------------------------------------------------
// JSON safe-access helper
// ---------------------------------------------------------------------------

template <typename T>
T json_value(const nlohmann::json& node, const std::string& key, const T& def) {
  if (node.contains(key) && !node[key].is_null()) {
    try {
      return node[key].get<T>();
    } catch (...) {
    }
  }
  return def;
}

// --- Agent / LLM ---

struct AgentConfig {
  std::string model = "anthropic/claude-sonnet-4-6";
  int max_iterations = kDefaultMaxIterations;
  double temperature = kDefaultTemperature;
  int max_tokens = kDefaultMaxTokens;
  int context_window = kDefaultContextWindow;  // Model context window (tokens)
  std::string thinking = "off";        // "off" | "low" | "medium" | "high"
  std::vector<std::string> fallbacks;  // Model fallback chain

  // Auto-compaction settings
  bool auto_compact = true;  // Enable automatic compaction
  int compact_max_messages =
      kDefaultCompactMaxMessages;  // Compact when history exceeds this
  int compact_keep_recent =
      kDefaultCompactKeepRecent;  // Keep this many recent messages
  int compact_max_tokens =
      kDefaultCompactMaxTokens;  // Compact when tokens exceed this

  static AgentConfig FromJson(const nlohmann::json& node);

  // Compute dynamic max iterations based on context window.
  // Scales linearly from kMinMaxIterations (32) at 32K
  // to kMaxMaxIterations (160) at 200K.
  int DynamicMaxIterations() const;
};

// --- Model definitions (multi-model format) ---

struct ModelCost {
  double input = 0;
  double output = 0;
  double cache_read = 0;
  double cache_write = 0;
  static ModelCost FromJson(const nlohmann::json& node);
};

struct ModelDefinition {
  std::string id;    // "qwen3-max"
  std::string name;  // "Qwen3 Max"
  bool reasoning = false;
  std::vector<std::string> input = {"text"};  // "text", "image"
  ModelCost cost;
  int context_window = 0;  // 128000
  int max_tokens = 0;      // 8192
  static ModelDefinition FromJson(const nlohmann::json& node);
};

struct ModelEntryConfig {
  std::string alias;      // "max", "plus", "vision"
  nlohmann::json params;  // Provider-specific API params (kept as JSON)
  static ModelEntryConfig FromJson(const nlohmann::json& node);
};

// Auth profile: one API key entry within a provider's "profiles" array.
struct AuthProfileConfig {
  std::string id;           // e.g. "prod", "backup"
  std::string api_key;      // Direct key value
  std::string api_key_env;  // Env var name (resolved at startup)
  int priority = 0;         // Lower = higher priority (0 is highest)

  static AuthProfileConfig FromJson(const nlohmann::json& node);
};

struct ProviderConfig {
  std::string api_key;
  std::string base_url;
  std::string api;  // "openai-completions", "anthropic-messages"
  std::string proxy;  // HTTP proxy URL (e.g. "http://127.0.0.1:7897")
  int timeout = kDefaultProviderTimeoutSec;
  nlohmann::json extra;                       // Provider-specific settings
  std::vector<ModelDefinition> models;        // Per-provider model definitions
  std::vector<AuthProfileConfig> profiles;    // Multi-key rotation

  static ProviderConfig FromJson(const nlohmann::json& node);
};

// --- Tools ---

struct ToolConfig {
  bool enabled = true;
  std::vector<std::string> allowed_paths;
  std::vector<std::string> denied_paths;
  std::vector<std::string> allowed_cmds;
  std::vector<std::string> denied_cmds;
  int timeout = kDefaultToolTimeoutSec;

  static ToolConfig FromJson(const nlohmann::json& node);
};

struct ToolPermissionConfig {
  std::vector<std::string> allow;  // e.g. ["group:fs", "group:runtime"]
  std::vector<std::string> deny;

  static ToolPermissionConfig FromJson(const nlohmann::json& node);
};

// --- MCP ---

struct MCPServerConfig {
  std::string name;
  std::string url;
  int timeout = kDefaultMcpTimeoutSec;

  static MCPServerConfig FromJson(const nlohmann::json& node);
};

struct MCPConfig {
  std::vector<MCPServerConfig> servers;

  static MCPConfig FromJson(const nlohmann::json& node);
};

// --- System ---

struct SystemConfig {
  std::string name = "RimeClaw";
  std::string version = "0.1.0";
  std::string log_level = "info";
  int log_retention_days =
      7;  // Delete log files older than N days (0 = keep forever)
  int log_max_size_mb =
      50;  // Total log storage cap in MiB across all rotated files
  std::optional<std::string> home;  // nullopt = use ~/, "" = use config file dir, non-empty = explicit path

  static SystemConfig FromJson(const nlohmann::json& node);
};

// --- Security ---

struct SecurityConfig {
  std::string permission_level = "auto";  // "auto" | "strict" | "permissive"
  bool allow_local_execute = true;

  static SecurityConfig FromJson(const nlohmann::json& node);
};

// --- Skills ---

struct SkillEntryConfig {
  bool enabled = true;
  static SkillEntryConfig FromJson(const nlohmann::json& node);
};

struct SkillsLoadConfig {
  std::vector<std::string> extra_dirs;
  static SkillsLoadConfig FromJson(const nlohmann::json& node);
};

struct SkillsConfig {
  std::string path;                       // skills.path
  std::vector<std::string> auto_approve;  // skills.auto_approve
  SkillsLoadConfig load;
  std::unordered_map<std::string, SkillEntryConfig> entries;
  nlohmann::json configs;  // skills.configs (kept as JSON for schema compat)

  static SkillsConfig FromJson(const nlohmann::json& node);
};

// --- Top-level config ---

struct RimeClawConfig {
  SystemConfig system;
  AgentConfig agent;
  SecurityConfig security;
  std::unordered_map<std::string, ProviderConfig> providers;
  ToolPermissionConfig tools_permission;
  MCPConfig mcp;
  SkillsConfig skills;

  // Plugins raw config (plugins section)
  nlohmann::json plugins_config;

  // Session maintenance config (raw JSON, consumed by SessionMaintenance)
  nlohmann::json session_maintenance_config;

  // Subagent config (raw JSON, consumed by SubagentManager)
  nlohmann::json subagent_config;

  // Exec approval config (raw JSON, consumed by ExecApprovalManager)
  nlohmann::json exec_approval_config;

  // Models section (models.providers)
  std::unordered_map<std::string, ProviderConfig> model_providers;

  // Per-model aliases and params (agents.defaults.models)
  std::unordered_map<std::string, ModelEntryConfig> model_entries;

  // Legacy compatibility
  std::unordered_map<std::string, ToolConfig> tools;

  static RimeClawConfig FromJson(const nlohmann::json& node);
  static RimeClawConfig LoadFromFile(const std::string& filepath);

  static std::string ExpandHome(const std::string& path);
  static std::string DefaultConfigPath();

  // Set config path override (used by --config/-c command line option)
  static void set_config_path(const std::string& path);

 private:
  // Internal: parse after ${VAR} expansion has already been applied
  static RimeClawConfig FromJsonExpanded(const nlohmann::json& node);

  static std::string config_path_override_;
};

}  // namespace rimeclaw
