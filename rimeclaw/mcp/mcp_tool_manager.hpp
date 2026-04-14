// Copyright 2025-2026 RimeClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "rimeclaw/config.hpp"
#include "rimeclaw/mcp/mcp_client.hpp"

namespace rimeclaw::mcp {

/// Stub MCP tool manager for future expansion.
/// Discovers and dispatches tools from external MCP servers.
class MCPToolManager {
 public:
  MCPToolManager() = default;

  /// Initialize the manager. No-op (stub).
  void Initialize() {}

  /// Connect to configured MCP servers and discover tools.
  /// Returns empty vector (stub).
  std::vector<Tool> DiscoverTools(const MCPConfig& /*config*/) { return {}; }

  /// Execute an external tool by qualified name.
  /// Returns empty string (stub).
  std::string ExecuteTool(const std::string& /*qualified_name*/,
                          const nlohmann::json& /*arguments*/) {
    return {};
  }

  /// Shut down all MCP connections. No-op (stub).
  void Shutdown() {}

  /// Check if a name refers to an external MCP tool.
  bool IsExternalTool(const std::string& /*name*/) const { return false; }

  /// Get count of discovered tools.
  size_t ToolCount() const { return 0; }

  /// Build qualified name: mcp__{server}__{tool}
  static std::string MakeQualifiedName(const std::string& server_name,
                                       const std::string& tool_name) {
    return "mcp__" + server_name + "__" + tool_name;
  }
};

}  // namespace rimeclaw::mcp
