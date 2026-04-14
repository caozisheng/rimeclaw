// Copyright 2025-2026 RimeClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace rimeclaw::mcp {

struct Tool {
  std::string name;
  std::string description;
  nlohmann::json parameters;
};

struct MCPResponse {
  nlohmann::json result;
  std::string error;
};

/// Stub MCP client for future expansion.
/// All methods return empty/default values.
class MCPClient {
 public:
  MCPClient(const std::string& server_url,
            const nlohmann::json& /*config*/ = {})
      : server_url_(server_url) {}

  /// Attempt to connect to the MCP server. Returns false (stub).
  bool Connect() { return false; }

  /// Disconnect from the MCP server. No-op (stub).
  void Disconnect() {}

  /// List available tools on the server. Returns empty vector (stub).
  std::vector<Tool> ListTools() { return {}; }

  /// Call a tool by name. Returns empty response (stub).
  MCPResponse CallTool(const std::string& /*tool_name*/,
                       const nlohmann::json& /*arguments*/) {
    return {};
  }

 private:
  std::string server_url_;
};

}  // namespace rimeclaw::mcp
