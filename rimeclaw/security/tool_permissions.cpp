// Copyright 2025-2026 RimeClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "rimeclaw/security/tool_permissions.hpp"

#include <algorithm>

namespace rimeclaw {

// Group definitions
static const std::unordered_map<std::string, std::vector<std::string>> kGroups =
    {
        {"fs", {"read", "write", "edit"}},
        {"runtime", {"exec"}},
        {"all", {"read", "write", "edit", "exec", "message"}},
};

ToolPermissionChecker::ToolPermissionChecker(
    const ToolPermissionConfig& config) {
  // Parse allow list
  for (const auto& entry : config.allow) {
    if (entry.substr(0, 6) == "group:") {
      std::string group_name = entry.substr(6);
      if (group_name == "all") {
        allow_all_ = true;
      }
      auto it = kGroups.find(group_name);
      if (it != kGroups.end()) {
        for (const auto& tool : it->second) {
          allowed_tools_.insert(tool);
        }
      }
    } else if (entry.substr(0, 4) == "mcp:") {
      std::string mcp_entry = entry.substr(4);
      if (mcp_entry == "*") {
        mcp_allow_all_ = true;
      } else {
        allowed_mcp_.insert(mcp_entry);
      }
    } else {
      allowed_tools_.insert(entry);
    }
  }

  // Parse deny list
  for (const auto& entry : config.deny) {
    if (entry.substr(0, 6) == "group:") {
      std::string group_name = entry.substr(6);
      auto it = kGroups.find(group_name);
      if (it != kGroups.end()) {
        for (const auto& tool : it->second) {
          denied_tools_.insert(tool);
        }
      }
    } else if (entry.substr(0, 4) == "mcp:") {
      denied_mcp_.insert(entry.substr(4));
    } else {
      denied_tools_.insert(entry);
    }
  }
}

bool ToolPermissionChecker::IsAllowed(const std::string& tool_name) const {
  // Deny takes precedence
  if (denied_tools_.count(tool_name)) {
    return false;
  }

  if (allow_all_) {
    return true;
  }

  return allowed_tools_.count(tool_name) > 0;
}

bool ToolPermissionChecker::IsMcpToolAllowed(
    const std::string& server_name,
    const std::string& tool_name) const {
  // Build keys for lookup
  std::string server_wildcard = server_name + ":*";
  std::string specific = server_name + ":" + tool_name;

  // Deny takes precedence
  if (denied_mcp_.count("*") || denied_mcp_.count(server_wildcard) ||
      denied_mcp_.count(specific)) {
    return false;
  }

  if (mcp_allow_all_) {
    return true;
  }

  return allowed_mcp_.count(server_wildcard) ||
         allowed_mcp_.count(specific);
}

}  // namespace rimeclaw
