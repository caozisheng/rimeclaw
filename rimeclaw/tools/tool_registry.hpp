// Copyright 2025-2026 RimeClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <nlohmann/json.hpp>

#include "rimeclaw/config.hpp"

namespace rimeclaw {

class ToolPermissionChecker;
class ExecApprovalManager;
class SubagentManager;

class ToolRegistry {
 public:
  struct ToolSchema {
    std::string name;
    std::string description;
    nlohmann::json parameters;
  };

  // Background process session (for 'process' tool)
  struct BgSession {
    std::string id;
    std::string command;
    std::future<std::string> future;  // captured output (or error)
    std::atomic<bool> done{false};
    std::string output;  // final output once done
    std::string error;
    int exit_code = -1;
    std::chrono::system_clock::time_point started_at;
    bool exited = false;
  };

 private:
  std::unordered_map<std::string,
                     std::function<std::string(const nlohmann::json&)>>
      tools_;
  std::vector<ToolSchema> tool_schemas_;
  std::shared_ptr<ToolPermissionChecker> permission_checker_;
  std::shared_ptr<ExecApprovalManager> approval_manager_;
  SubagentManager* subagent_manager_ = nullptr;
  std::string current_session_key_;
  std::unordered_set<std::string> external_tools_;

  // Background process registry (for 'process' tool)
  mutable std::mutex bg_mu_;
  std::unordered_map<std::string, std::shared_ptr<BgSession>> bg_sessions_;

  // Workspace root for file-tool path validation
  std::string workspace_path_ = "~/.rimeclaw/workspace";

 public:
  ToolRegistry();

  // Register built-in tools
  void RegisterBuiltinTools();

  // Register an external tool (from MCP server)
  void RegisterExternalTool(
      const std::string& name, const std::string& description,
      const nlohmann::json& parameters,
      std::function<std::string(const nlohmann::json&)> executor);

  // Unregister an external tool.  Returns true if removed, false if not found
  // or if the tool is a built-in (only external tools can be removed).
  bool UnregisterExternalTool(const std::string& name);

  // Register the chain meta-tool
  void RegisterChainTool();

  // Set permission checker (filters GetToolSchemas and ExecuteTool)
  void SetPermissionChecker(std::shared_ptr<ToolPermissionChecker> checker);

  // Set exec approval manager (for exec tool approval flow)
  void SetApprovalManager(std::shared_ptr<ExecApprovalManager> manager);

  // Set subagent manager and register spawn_subagent tool
  void SetSubagentManager(SubagentManager* manager,
                          const std::string& session_key = "");

  // Set workspace root used for file-tool path validation
  void SetWorkspace(const std::string& path);

  // Execute a tool by name (with permission check)
  std::string ExecuteTool(const std::string& tool_name,
                          const nlohmann::json& parameters);

  // Get tool schemas for LLM function calling (filtered by permissions)
  std::vector<ToolSchema> GetToolSchemas() const;

  // Check if tool is available
  bool HasTool(const std::string& tool_name) const;

  // Check if tool is external
  bool IsExternalTool(const std::string& tool_name) const;

 private:
  // Permission check helper
  bool check_permission(const std::string& tool_name) const;

  // Helper to (re-)register a tool without duplicating its schema
  void register_tool(const std::string& name, const std::string& description,
                     nlohmann::json params_schema,
                     std::function<std::string(const nlohmann::json&)> handler);

  // Built-in tool implementations
  std::string read_file_tool(const nlohmann::json& params);
  std::string write_file_tool(const nlohmann::json& params);
  std::string edit_file_tool(const nlohmann::json& params);
  std::string exec_tool(const nlohmann::json& params);
  std::string message_tool(const nlohmann::json& params);
  std::string apply_patch_tool(const nlohmann::json& params);
  std::string process_tool(const nlohmann::json& params);
  std::string memory_search_tool(const nlohmann::json& params);
  std::string memory_get_tool(const nlohmann::json& params);
};

}  // namespace rimeclaw
