// Copyright 2025-2026 RimeClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "rimeclaw/config.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <stdexcept>

namespace rimeclaw {

// Static member definition
std::string RimeClawConfig::config_path_override_;

// ---------------------------------------------------------------------------
// ${VAR} environment variable substitution
// ---------------------------------------------------------------------------

static std::string substitute_env_vars(const std::string& input) {
  static const std::regex env_re(R"(\$\{([^}]+)\})");
  std::string result;
  auto begin = std::sregex_iterator(input.begin(), input.end(), env_re);
  auto end = std::sregex_iterator();

  size_t last_pos = 0;
  for (auto it = begin; it != end; ++it) {
    auto& match = *it;
    result.append(input, last_pos, match.position() - last_pos);
    std::string var_name = match[1].str();
    const char* env_val = std::getenv(var_name.c_str());
    if (env_val) {
      result.append(env_val);
    }
    // If env var not set, replace with empty string
    last_pos = match.position() + match.length();
  }
  result.append(input, last_pos, std::string::npos);
  return result;
}

// ---------------------------------------------------------------------------
// Walk a JSON tree and expand ${VAR} in all string values
// ---------------------------------------------------------------------------

static nlohmann::json expand_env_in_json(const nlohmann::json& node) {
  if (node.is_string()) {
    std::string s = node.get<std::string>();
    if (s.find("${") != std::string::npos) {
      return nlohmann::json(substitute_env_vars(s));
    }
    return node;
  }
  if (node.is_object()) {
    nlohmann::json result = nlohmann::json::object();
    for (auto it = node.begin(); it != node.end(); ++it) {
      result[it.key()] = expand_env_in_json(it.value());
    }
    return result;
  }
  if (node.is_array()) {
    nlohmann::json result = nlohmann::json::array();
    for (size_t i = 0; i < node.size(); ++i) {
      result.push_back(expand_env_in_json(node[i]));
    }
    return result;
  }
  return node;  // number, bool, null
}

// ---------------------------------------------------------------------------
// Helper: read a JSON array as vector<string>
// ---------------------------------------------------------------------------

static std::vector<std::string> json_string_list(const nlohmann::json& node,
                                                  const std::string& key,
                                                  const std::vector<std::string>& def = {}) {
  if (node.contains(key) && node[key].is_array()) {
    std::vector<std::string> result;
    for (const auto& item : node[key]) {
      if (item.is_string()) {
        result.push_back(item.get<std::string>());
      }
    }
    return result;
  }
  return def;
}

// ---------------------------------------------------------------------------
// FromJson implementations
// ---------------------------------------------------------------------------

AgentConfig AgentConfig::FromJson(const nlohmann::json& node) {
  AgentConfig config;
  if (node.contains("model") && node["model"].is_string()) {
    config.model = node["model"].get<std::string>();
  }
  config.max_iterations = json_value(node, "max_iterations", kDefaultMaxIterations);
  config.temperature = json_value(node, "temperature", kDefaultTemperature);
  config.max_tokens = json_value(node, "max_tokens", kDefaultMaxTokens);
  config.context_window = json_value(node, "context_window", kDefaultContextWindow);
  config.thinking = json_value<std::string>(node, "thinking", "off");
  config.fallbacks = json_string_list(node, "fallbacks");

  // Compaction settings
  config.auto_compact = json_value(node, "auto_compact", true);
  config.compact_max_messages =
      json_value(node, "compact_max_messages", kDefaultCompactMaxMessages);
  config.compact_keep_recent =
      json_value(node, "compact_keep_recent", kDefaultCompactKeepRecent);
  config.compact_max_tokens =
      json_value(node, "compact_max_tokens", kDefaultCompactMaxTokens);
  return config;
}

ModelCost ModelCost::FromJson(const nlohmann::json& node) {
  ModelCost c;
  c.input = json_value(node, "input", 0.0);
  c.output = json_value(node, "output", 0.0);
  c.cache_read = json_value(node, "cache_read", 0.0);
  c.cache_write = json_value(node, "cache_write", 0.0);
  return c;
}

ModelDefinition ModelDefinition::FromJson(const nlohmann::json& node) {
  ModelDefinition m;
  m.id = json_value<std::string>(node, "id", "");
  m.name = json_value<std::string>(node, "name", "");
  m.reasoning = json_value(node, "reasoning", false);
  m.input = json_string_list(node, "input", {"text"});
  if (node.contains("cost") && node["cost"].is_object()) {
    m.cost = ModelCost::FromJson(node["cost"]);
  }
  m.context_window = json_value(node, "context_window", 0);
  m.max_tokens = json_value(node, "max_tokens", 0);
  return m;
}

ModelEntryConfig ModelEntryConfig::FromJson(const nlohmann::json& node) {
  ModelEntryConfig c;
  c.alias = json_value<std::string>(node, "alias", "");
  if (node.contains("params") && node["params"].is_object()) {
    c.params = node["params"];
  }
  return c;
}

AuthProfileConfig AuthProfileConfig::FromJson(const nlohmann::json& node) {
  AuthProfileConfig p;
  p.id = json_value<std::string>(node, "id", "");
  p.api_key = json_value<std::string>(node, "api_key", "");
  p.api_key_env = json_value<std::string>(node, "api_key_env", "");
  p.priority = json_value(node, "priority", 0);
  // Resolve env var if api_key is empty but env var name is set
  if (p.api_key.empty() && !p.api_key_env.empty()) {
    const char* env_val = std::getenv(p.api_key_env.c_str());
    if (env_val)
      p.api_key = env_val;
  }
  return p;
}

ProviderConfig ProviderConfig::FromJson(const nlohmann::json& node) {
  ProviderConfig config;
  config.api_key = json_value<std::string>(node, "api_key", "");
  config.base_url = json_value<std::string>(node, "base_url", "");
  config.api = json_value<std::string>(node, "api", "");
  config.proxy = json_value<std::string>(node, "proxy", "");
  config.timeout = json_value(node, "timeout", kDefaultProviderTimeoutSec);
  if (node.contains("extra") && node["extra"].is_object()) {
    config.extra = node["extra"];
  }
  if (node.contains("models") && node["models"].is_array()) {
    for (const auto& item : node["models"]) {
      config.models.push_back(ModelDefinition::FromJson(item));
    }
  }
  // Parse profiles array for multi-key rotation
  if (node.contains("profiles") && node["profiles"].is_array()) {
    std::set<std::string> seen_ids;
    int auto_idx = 0;
    for (const auto& item : node["profiles"]) {
      auto profile = AuthProfileConfig::FromJson(item);
      // Assign auto-generated id if empty
      if (profile.id.empty()) {
        profile.id = "profile_" + std::to_string(auto_idx);
      }
      // Skip duplicate ids
      if (seen_ids.count(profile.id)) {
        continue;
      }
      seen_ids.insert(profile.id);
      config.profiles.push_back(std::move(profile));
      ++auto_idx;
    }
  }
  return config;
}

ToolConfig ToolConfig::FromJson(const nlohmann::json& node) {
  ToolConfig config;
  config.enabled = json_value(node, "enabled", true);
  config.allowed_paths = json_string_list(node, "allowed_paths");
  config.denied_paths = json_string_list(node, "denied_paths");
  config.allowed_cmds = json_string_list(node, "allowed_cmds");
  config.denied_cmds = json_string_list(node, "denied_cmds");
  config.timeout = json_value(node, "timeout", kDefaultToolTimeoutSec);
  return config;
}

ToolPermissionConfig ToolPermissionConfig::FromJson(const nlohmann::json& node) {
  ToolPermissionConfig config;
  config.allow =
      json_string_list(node, "allow", {"group:fs", "group:runtime"});
  config.deny = json_string_list(node, "deny");
  return config;
}

MCPServerConfig MCPServerConfig::FromJson(const nlohmann::json& node) {
  MCPServerConfig config;
  config.name = json_value<std::string>(node, "name", "");
  config.url = json_value<std::string>(node, "url", "");
  config.timeout = json_value(node, "timeout", kDefaultMcpTimeoutSec);
  return config;
}

MCPConfig MCPConfig::FromJson(const nlohmann::json& node) {
  MCPConfig config;
  if (node.contains("servers") && node["servers"].is_array()) {
    for (const auto& item : node["servers"]) {
      config.servers.push_back(MCPServerConfig::FromJson(item));
    }
  }
  return config;
}

SystemConfig SystemConfig::FromJson(const nlohmann::json& node) {
  SystemConfig c;
  c.name = json_value<std::string>(node, "name", "RimeClaw");
  c.version = json_value<std::string>(node, "version", "0.1.0");
  c.log_level = json_value<std::string>(node, "log_level", "info");
  c.log_retention_days = json_value(node, "log_retention_days", 7);
  c.log_max_size_mb = json_value(node, "log_max_size_mb", 50);
  // home: nullopt = field absent (use ~/), "" = field present but empty (use config dir),
  //        non-empty = explicit path
  if (node.contains("home") && node["home"].is_string()) {
    c.home = node["home"].get<std::string>();
  }
  // Resolve to absolute path so all derived paths are always absolute.
  if (c.home.has_value() && !c.home->empty()) {
    std::error_code ec;
    auto abs = std::filesystem::weakly_canonical(*c.home, ec);
    if (!ec)
      c.home = abs.string();
  }
  return c;
}

SecurityConfig SecurityConfig::FromJson(const nlohmann::json& node) {
  SecurityConfig c;
  c.permission_level = json_value<std::string>(node, "permission_level", "auto");
  c.allow_local_execute = json_value(node, "allow_local_execute", true);
  return c;
}

SkillEntryConfig SkillEntryConfig::FromJson(const nlohmann::json& node) {
  SkillEntryConfig config;
  config.enabled = json_value(node, "enabled", true);
  return config;
}

SkillsLoadConfig SkillsLoadConfig::FromJson(const nlohmann::json& node) {
  SkillsLoadConfig config;
  config.extra_dirs = json_string_list(node, "extra_dirs");
  return config;
}

SkillsConfig SkillsConfig::FromJson(const nlohmann::json& node) {
  SkillsConfig config;

  config.path = json_value<std::string>(node, "path", "");
  config.auto_approve = json_string_list(node, "auto_approve");
  if (node.contains("configs") && node["configs"].is_object()) {
    config.configs = node["configs"];
  }

  if (node.contains("load") && node["load"].is_object()) {
    config.load = SkillsLoadConfig::FromJson(node["load"]);
  }
  // path -> extra_dirs compatibility
  if (!config.path.empty() && config.load.extra_dirs.empty()) {
    config.load.extra_dirs.push_back(config.path);
  }

  if (node.contains("entries") && node["entries"].is_object()) {
    for (auto it = node["entries"].begin(); it != node["entries"].end(); ++it) {
      config.entries[it.key()] = SkillEntryConfig::FromJson(it.value());
    }
  }
  return config;
}

// ---------------------------------------------------------------------------
// Top-level config
// ---------------------------------------------------------------------------

RimeClawConfig RimeClawConfig::FromJson(const nlohmann::json& node) {
  // Expand ${VAR} references
  nlohmann::json expanded = expand_env_in_json(node);
  return FromJsonExpanded(expanded);
}

RimeClawConfig RimeClawConfig::FromJsonExpanded(const nlohmann::json& node) {
  RimeClawConfig config;

  // ================================================================
  // System section
  // ================================================================
  if (node.contains("system") && node["system"].is_object()) {
    config.system = SystemConfig::FromJson(node["system"]);
  }

  // ================================================================
  // LLM section (flat, single provider shorthand)
  // Format: "llm": { "provider": "openai", "model": ..., "api_key": ..., "base_url": ... }
  // ================================================================
  if (node.contains("llm") && node["llm"].is_object()) {
    const auto& llm = node["llm"];
    std::string provider_name = json_value<std::string>(llm, "provider", "openai");

    config.agent.model =
        json_value<std::string>(llm, "model", "anthropic/claude-sonnet-4-6");
    config.agent.temperature = json_value(llm, "temperature", kDefaultTemperature);
    config.agent.max_tokens = json_value(llm, "max_tokens", kDefaultMaxTokens);

    ProviderConfig prov;
    prov.api_key = json_value<std::string>(llm, "api_key", "");
    prov.base_url = json_value<std::string>(llm, "base_url", "");
    prov.timeout = json_value(llm, "timeout", kDefaultProviderTimeoutSec);
    config.providers[provider_name] = prov;
  }

  // ================================================================
  // Agent section (takes priority over llm if both exist)
  // ================================================================
  if (node.contains("agent") && node["agent"].is_object()) {
    config.agent = AgentConfig::FromJson(node["agent"]);
  } else if (node.contains("agents") && node["agents"].contains("defaults")) {
    config.agent = AgentConfig::FromJson(node["agents"]["defaults"]);
  }

  // ================================================================
  // Providers (multi-provider format, merges with llm-derived provider)
  // ================================================================
  if (node.contains("providers") && node["providers"].is_object()) {
    for (auto it = node["providers"].begin(); it != node["providers"].end();
         ++it) {
      config.providers[it.key()] = ProviderConfig::FromJson(it.value());
    }
  }

  // ================================================================
  // Models providers (models.providers)
  // ================================================================
  if (node.contains("models") && node["models"].is_object() &&
      node["models"].contains("providers") && node["models"]["providers"].is_object()) {
    for (auto it = node["models"]["providers"].begin();
         it != node["models"]["providers"].end(); ++it) {
      config.model_providers[it.key()] = ProviderConfig::FromJson(it.value());
    }
  }

  // ================================================================
  // Model aliases (agents.defaults.models)
  // ================================================================
  if (node.contains("agents") && node["agents"].is_object() &&
      node["agents"].contains("defaults") && node["agents"]["defaults"].is_object() &&
      node["agents"]["defaults"].contains("models") &&
      node["agents"]["defaults"]["models"].is_object()) {
    for (auto it = node["agents"]["defaults"]["models"].begin();
         it != node["agents"]["defaults"]["models"].end(); ++it) {
      config.model_entries[it.key()] = ModelEntryConfig::FromJson(it.value());
    }
  }

  // ================================================================
  // Agent model object form (agents.defaults.model as map with
  // primary/fallbacks)
  // ================================================================
  if (node.contains("agents") && node["agents"].is_object() &&
      node["agents"].contains("defaults") && node["agents"]["defaults"].is_object() &&
      node["agents"]["defaults"].contains("model") &&
      node["agents"]["defaults"]["model"].is_object()) {
    const auto& model_val = node["agents"]["defaults"]["model"];
    config.agent.model =
        json_value<std::string>(model_val, "primary", config.agent.model);
    config.agent.fallbacks = json_string_list(model_val, "fallbacks");
  }

  // ================================================================
  // Security
  // ================================================================
  if (node.contains("security") && node["security"].is_object()) {
    config.security = SecurityConfig::FromJson(node["security"]);
  }

  // ================================================================
  // MCP
  // ================================================================
  if (node.contains("mcp") && node["mcp"].is_object()) {
    config.mcp = MCPConfig::FromJson(node["mcp"]);
  }

  // ================================================================
  // Skills
  // ================================================================
  if (node.contains("skills") && node["skills"].is_object()) {
    config.skills = SkillsConfig::FromJson(node["skills"]);
  }

  // ================================================================
  // Plugins (raw JSON)
  // ================================================================
  if (node.contains("plugins") && node["plugins"].is_object()) {
    config.plugins_config = node["plugins"];
  }

  // ================================================================
  // Session maintenance
  // ================================================================
  if (node.contains("session") && node["session"].is_object()) {
    if (node["session"].contains("maintenance")) {
      config.session_maintenance_config = node["session"]["maintenance"];
    }
  }

  // ================================================================
  // Subagent config
  // ================================================================
  if (node.contains("subagents") && node["subagents"].is_object()) {
    config.subagent_config = node["subagents"];
  } else if (node.contains("agents") && node["agents"].is_object() &&
             node["agents"].contains("defaults") &&
             node["agents"]["defaults"].contains("subagents")) {
    config.subagent_config = node["agents"]["defaults"]["subagents"];
  }

  // ================================================================
  // Exec approval (from tools.exec section)
  // ================================================================
  if (node.contains("tools") && node["tools"].is_object() &&
      node["tools"].contains("exec") && node["tools"]["exec"].is_object()) {
    config.exec_approval_config = node["tools"]["exec"];
  }

  // ================================================================
  // Tools (permission allow/deny or legacy named configs)
  // ================================================================
  if (node.contains("tools") && node["tools"].is_object()) {
    const auto& tools_node = node["tools"];
    if (tools_node.contains("allow") || tools_node.contains("deny")) {
      config.tools_permission = ToolPermissionConfig::FromJson(tools_node);
    } else {
      for (auto it = tools_node.begin(); it != tools_node.end(); ++it) {
        std::string key = it.key();
        if (key == "exec")
          continue;  // Already handled above
        if (it.value().is_object()) {
          config.tools[key] = ToolConfig::FromJson(it.value());
        }
      }
    }
  }

  return config;
}

// ---------------------------------------------------------------------------
// Path utilities
// ---------------------------------------------------------------------------

std::string RimeClawConfig::ExpandHome(const std::string& path) {
  std::string expanded = path;
  if (expanded.size() >= 2 && expanded.substr(0, 2) == "~/") {
    const char* home = std::getenv("HOME");
#ifdef _WIN32
    if (!home)
      home = std::getenv("USERPROFILE");
#endif
    if (home) {
      expanded = std::string(home) + expanded.substr(1);
    }
  }
  return expanded;
}

std::string RimeClawConfig::DefaultConfigPath() {
  if (!config_path_override_.empty()) {
    return config_path_override_;
  }
  return ExpandHome("~/.rimeclaw/rimeclaw.json");
}

void RimeClawConfig::set_config_path(const std::string& path) {
  config_path_override_ = path;
}

RimeClawConfig RimeClawConfig::LoadFromFile(const std::string& filepath) {
  std::string expanded_path = ExpandHome(filepath);

  if (!std::filesystem::exists(expanded_path)) {
    throw std::runtime_error("Config file not found: " + expanded_path);
  }

  std::ifstream ifs(expanded_path);
  if (!ifs.is_open()) {
    throw std::runtime_error("Cannot open config file: " + expanded_path);
  }

  std::string content((std::istreambuf_iterator<char>(ifs)), {});
  nlohmann::json root = nlohmann::json::parse(content);
  return FromJson(root);
}

int AgentConfig::DynamicMaxIterations() const {
  // Scale linearly: 32K -> 32 iterations, 200K -> 160 iterations
  if (context_window <= kContextWindow32K)
    return kMinMaxIterations;
  if (context_window >= kContextWindow200K)
    return kMaxMaxIterations;

  double ratio = static_cast<double>(context_window - kContextWindow32K) /
                 (kContextWindow200K - kContextWindow32K);
  return kMinMaxIterations +
         static_cast<int>(ratio * (kMaxMaxIterations - kMinMaxIterations));
}

}  // namespace rimeclaw
