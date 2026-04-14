// Copyright 2025-2026 RimeClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "rimeclaw/tools/tool_registry.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <regex>
#include <sstream>
#include <thread>

#include <spdlog/spdlog.h>

#include "rimeclaw/core/memory_search.hpp"

namespace fs = std::filesystem;
#include "rimeclaw/core/subagent.hpp"
#include "rimeclaw/platform/process.hpp"
#include "rimeclaw/security/exec_approval.hpp"
#include "rimeclaw/security/sandbox.hpp"
#include "rimeclaw/security/tool_permissions.hpp"
#include "rimeclaw/tools/tool_chain.hpp"

namespace rimeclaw {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::string generate_id(const std::string& prefix = "bg") {
  thread_local static std::mt19937 rng(std::random_device{}());
  std::uniform_int_distribution<uint32_t> dist;
  std::ostringstream ss;
  ss << prefix << "_" << std::hex << dist(rng);
  return ss.str();
}

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

ToolRegistry::ToolRegistry() {
  spdlog::info("ToolRegistry initialized");
}

// ---------------------------------------------------------------------------
// register_tool helper (deduplicates schema on re-register)
// ---------------------------------------------------------------------------

void ToolRegistry::register_tool(
    const std::string& name, const std::string& description,
    nlohmann::json params_schema,
    std::function<std::string(const nlohmann::json&)> handler) {
  tools_[name] = std::move(handler);
  tool_schemas_.erase(
      std::remove_if(tool_schemas_.begin(), tool_schemas_.end(),
                     [&name](const ToolSchema& s) { return s.name == name; }),
      tool_schemas_.end());
  tool_schemas_.push_back({name, description, std::move(params_schema)});
}

// ---------------------------------------------------------------------------
// RegisterBuiltinTools
// ---------------------------------------------------------------------------

void ToolRegistry::RegisterBuiltinTools() {
  // ---- read ----
  register_tool(
      "read", "Read the contents of a file",
      nlohmann::json::parse(
          R"({"type":"object","properties":{"path":{"type":"string","description":"Path to the file to read"}},"required":["path"]})"),
      [this](const nlohmann::json& p) { return read_file_tool(p); });

  // ---- write ----
  register_tool(
      "write", "Write content to a file",
      nlohmann::json::parse(
          R"({"type":"object","properties":{"path":{"type":"string","description":"Path to write"},"content":{"type":"string","description":"Content to write"}},"required":["path","content"]})"),
      [this](const nlohmann::json& p) { return write_file_tool(p); });

  // ---- edit ----
  register_tool(
      "edit", "Edit a file by replacing exact text",
      nlohmann::json::parse(
          R"({"type":"object","properties":{"path":{"type":"string"},"oldText":{"type":"string","description":"Exact text to replace"},"newText":{"type":"string","description":"Replacement text"}},"required":["path","oldText","newText"]})"),
      [this](const nlohmann::json& p) { return edit_file_tool(p); });

  // ---- exec ----
  register_tool(
      "exec", "Execute a shell command and return its output",
      nlohmann::json::parse(
          R"JSON({"type":"object","properties":{"command":{"type":"string","description":"Shell command to execute"},"workdir":{"type":"string","description":"Working directory (optional)"},"timeout":{"type":"integer","description":"Timeout in seconds (default 30)"}},"required":["command"]})JSON"),
      [this](const nlohmann::json& p) { return exec_tool(p); });

  // ---- bash (alias for exec) ----
  register_tool(
      "bash", "Execute a shell command (alias for exec)",
      nlohmann::json::parse(
          R"JSON({"type":"object","properties":{"command":{"type":"string","description":"Shell command to execute"},"timeout":{"type":"integer","description":"Timeout in seconds (default 30)"}},"required":["command"]})JSON"),
      [this](const nlohmann::json& p) { return exec_tool(p); });

  // ---- apply_patch ----
  register_tool(
      "apply_patch",
      "Apply a multi-file patch in *** Begin Patch / *** End Patch format. "
      "Supports: *** Add File, *** Update File (with unified diff hunks), *** "
      "Delete File.",
      nlohmann::json::parse(
          R"({"type":"object","properties":{"patch":{"type":"string","description":"Patch text in *** Begin Patch ... *** End Patch format"}},"required":["patch"]})"),
      [this](const nlohmann::json& p) { return apply_patch_tool(p); });

  // ---- process ----
  register_tool(
      "process",
      "Manage long-running background shell sessions. "
      "Actions: start, list, log, poll, write, send-keys, kill, clear, remove.",
      nlohmann::json::parse(
          R"JSON({"type":"object","properties":{"action":{"type":"string","enum":["start","list","log","poll","write","send-keys","kill","clear","remove"],"description":"Action to perform"},"command":{"type":"string","description":"Shell command (required for start)"},"id":{"type":"string","description":"Session ID (required for all actions except start/list)"},"input":{"type":"string","description":"Text to write to stdin (for write/send-keys)"},"timeout":{"type":"integer","description":"Max wait ms for poll (default 5000)"}},"required":["action"]})JSON"),
      [this](const nlohmann::json& p) { return process_tool(p); });

  // ---- message ----
  register_tool(
      "message", "Send a message to a channel",
      nlohmann::json::parse(
          R"({"type":"object","properties":{"channel":{"type":"string","description":"Channel to send to"},"message":{"type":"string","description":"Message content"},"action":{"type":"string","description":"Action: send (default), reply, react, pin, edit, delete"}},"required":["channel","message"]})"),
      [this](const nlohmann::json& p) { return message_tool(p); });

  // ---- memory_search ----
  register_tool(
      "memory_search",
      "Search agent memory files (MEMORY.md and workspace docs) using BM25 "
      "full-text search.",
      nlohmann::json::parse(
          R"JSON({"type":"object","properties":{"query":{"type":"string","description":"Search query"},"maxResults":{"type":"integer","description":"Max results to return (default 10)"}},"required":["query"]})JSON"),
      [this](const nlohmann::json& p) { return memory_search_tool(p); });

  // ---- memory_get ----
  register_tool(
      "memory_get",
      "Read a specific file from the agent workspace (MEMORY.md, notes, etc.).",
      nlohmann::json::parse(
          R"({"type":"object","properties":{"path":{"type":"string","description":"Relative path within the workspace, e.g. MEMORY.md or memory/notes.md"}},"required":["path"]})"),
      [this](const nlohmann::json& p) { return memory_get_tool(p); });

  spdlog::info("Registered {} built-in tools", tools_.size());
}

// ---------------------------------------------------------------------------
// RegisterChainTool
// ---------------------------------------------------------------------------

void ToolRegistry::RegisterChainTool() {
  tools_["chain"] = [this](const nlohmann::json& params) -> std::string {
    auto chain_def = ToolChainExecutor::ParseChain(params);
    ToolExecutorFn executor = [this](const std::string& name,
                                     const nlohmann::json& args) {
      return ExecuteTool(name, args);
    };
    ToolChainExecutor chain_executor(executor);
    auto result = chain_executor.Execute(chain_def);
    return ToolChainExecutor::ResultToJson(result).dump();
  };

  nlohmann::json chain_params;
  chain_params["type"] = "object";
  chain_params["properties"] = {
      {"name", {{"type", "string"}, {"description", "Name of the chain"}}},
      {"steps",
       {{"type", "array"},
        {"items",
         {{"type", "object"},
          {"properties",
           {{"tool", {{"type", "string"}, {"description", "Tool name"}}},
            {"arguments",
             {{"type", "object"},
              {"description",
               "Args, may use {{prev.result}} or {{steps[N].result}}"}}}}},
          {"required", {"tool"}}}},
        {"description", "Ordered tool invocations"}}},
      {"error_policy",
       {{"type", "string"},
        {"enum", {"stop_on_error", "continue_on_error", "retry"}}}},
      {"max_retries", {{"type", "integer"}}}};
  chain_params["required"] = {"steps"};

  tool_schemas_.erase(
      std::remove_if(tool_schemas_.begin(), tool_schemas_.end(),
                     [](const ToolSchema& s) { return s.name == "chain"; }),
      tool_schemas_.end());
  tool_schemas_.push_back({"chain",
                           "Execute a pipeline of tools in sequence. Each step "
                           "can reference previous results via "
                           "{{prev.result}} or {{steps[N].result}} templates.",
                           chain_params});

  spdlog::info("Registered chain tool");
}

// ---------------------------------------------------------------------------
// SetPermissionChecker / SetApprovalManager
// ---------------------------------------------------------------------------

void ToolRegistry::SetPermissionChecker(
    std::shared_ptr<ToolPermissionChecker> checker) {
  permission_checker_ = std::move(checker);
}

void ToolRegistry::SetApprovalManager(
    std::shared_ptr<ExecApprovalManager> manager) {
  approval_manager_ = std::move(manager);
}

// ---------------------------------------------------------------------------
// SetSubagentManager -- registers spawn_subagent tool
// ---------------------------------------------------------------------------

void ToolRegistry::SetSubagentManager(SubagentManager* manager,
                                      const std::string& session_key) {
  subagent_manager_ = manager;
  current_session_key_ = session_key;
  if (!manager)
    return;

  tools_["spawn_subagent"] =
      [this](const nlohmann::json& params) -> std::string {
    if (!subagent_manager_)
      throw std::runtime_error("Subagent manager not configured");

    SpawnParams sp;
    sp.task = params.value("task", "");
    if (sp.task.empty())
      throw std::runtime_error("Missing required parameter: task");
    sp.label = params.value("label", "");
    sp.agent_id = params.value("agent_id", "");
    sp.model = params.value("model", "");
    sp.thinking = params.value("thinking", "off");
    sp.timeout_seconds = params.value("timeout", 300);
    sp.mode = spawn_mode_from_string(params.value("mode", "run"));
    sp.cleanup = params.value("cleanup", true);

    auto run = subagent_manager_->Spawn(sp, current_session_key_);

    nlohmann::json r;
    r["status"] = (run.status == SubagentRunStatus::kPending ||
                   run.status == SubagentRunStatus::kRunning)
                      ? "accepted"
                  : (run.status == SubagentRunStatus::kFailed) ? "error"
                                                               : "completed";
    if (!run.run_id.empty())
      r["run_id"] = run.run_id;
    r["mode"] = spawn_mode_to_string(sp.mode);
    if (!run.error.empty())
      r["error"] = run.error;
    if (!run.result.empty())
      r["result"] = run.result;
    return r.dump();
  };

  nlohmann::json sp;
  sp["type"] = "object";
  sp["properties"] = {
      {"task", {{"type", "string"}, {"description", "Task for the subagent"}}},
      {"label", {{"type", "string"}, {"description", "Human-readable label"}}},
      {"agent_id", {{"type", "string"}, {"description", "Target agent ID"}}},
      {"model", {{"type", "string"}, {"description", "Model override"}}},
      {"thinking",
       {{"type", "string"},
        {"description", "Thinking level: off|low|medium|high"}}},
      {"timeout", {{"type", "integer"}, {"description", "Timeout in seconds"}}},
      {"mode", {{"type", "string"}, {"enum", {"run", "session"}}}},
      {"cleanup",
       {{"type", "boolean"}, {"description", "Auto-delete on completion"}}}};
  sp["required"] = {"task"};

  tool_schemas_.erase(std::remove_if(tool_schemas_.begin(), tool_schemas_.end(),
                                     [](const ToolSchema& s) {
                                       return s.name == "spawn_subagent";
                                     }),
                      tool_schemas_.end());
  tool_schemas_.push_back({"spawn_subagent",
                           "Spawn a subagent to handle a subtask autonomously.",
                           sp});
  spdlog::info("Subagent manager set, spawn_subagent tool registered");
}

void ToolRegistry::SetWorkspace(const std::string& path) {
  // Store canonical absolute path so relative file paths resolve correctly
  std::error_code ec;
  auto abs = fs::weakly_canonical(fs::absolute(path), ec);
  workspace_path_ = ec ? path : abs.string();
}

// ---------------------------------------------------------------------------
// RegisterExternalTool
// ---------------------------------------------------------------------------

void ToolRegistry::RegisterExternalTool(
    const std::string& name, const std::string& description,
    const nlohmann::json& parameters,
    std::function<std::string(const nlohmann::json&)> executor) {
  tools_[name] = std::move(executor);
  tool_schemas_.erase(
      std::remove_if(tool_schemas_.begin(), tool_schemas_.end(),
                     [&name](const ToolSchema& s) { return s.name == name; }),
      tool_schemas_.end());
  tool_schemas_.push_back({name, description, parameters});
  external_tools_.insert(name);
  spdlog::info("Registered external tool: {}", name);
}

// ---------------------------------------------------------------------------
// UnregisterExternalTool
// ---------------------------------------------------------------------------

bool ToolRegistry::UnregisterExternalTool(const std::string& name) {
  if (external_tools_.find(name) == external_tools_.end())
    return false;
  tools_.erase(name);
  tool_schemas_.erase(
      std::remove_if(tool_schemas_.begin(), tool_schemas_.end(),
                     [&name](const ToolSchema& s) { return s.name == name; }),
      tool_schemas_.end());
  external_tools_.erase(name);
  spdlog::info("Unregistered external tool: {}", name);
  return true;
}

// ---------------------------------------------------------------------------
// Permission checks / ExecuteTool / GetToolSchemas / HasTool
// ---------------------------------------------------------------------------

bool ToolRegistry::check_permission(const std::string& tool_name) const {
  if (!permission_checker_)
    return true;
  return permission_checker_->IsAllowed(tool_name);
}

std::string ToolRegistry::ExecuteTool(const std::string& tool_name,
                                      const nlohmann::json& parameters) {
  if (!HasTool(tool_name))
    throw std::runtime_error("Tool not found: " + tool_name);
  if (!check_permission(tool_name))
    throw std::runtime_error("Permission denied: tool '" + tool_name +
                             "' is not allowed");
  spdlog::debug("Executing tool: {} params: {}", tool_name, parameters.dump());
  try {
    auto result = tools_[tool_name](parameters);
    spdlog::debug("Tool {} succeeded", tool_name);
    return result;
  } catch (const std::exception& e) {
    spdlog::error("Tool {} failed: {}", tool_name, e.what());
    throw;
  }
}

std::vector<ToolRegistry::ToolSchema> ToolRegistry::GetToolSchemas() const {
  if (!permission_checker_)
    return tool_schemas_;
  std::vector<ToolSchema> filtered;
  for (const auto& schema : tool_schemas_) {
    if (permission_checker_->IsAllowed(schema.name))
      filtered.push_back(schema);
  }
  return filtered;
}

bool ToolRegistry::HasTool(const std::string& tool_name) const {
  return tools_.find(tool_name) != tools_.end();
}

bool ToolRegistry::IsExternalTool(const std::string& tool_name) const {
  return external_tools_.find(tool_name) != external_tools_.end();
}

// ---------------------------------------------------------------------------
// File tools
// ---------------------------------------------------------------------------

std::string ToolRegistry::read_file_tool(const nlohmann::json& params) {
  if (!params.contains("path"))
    throw std::runtime_error("Missing required parameter: path");
  std::string path = params["path"].get<std::string>();
  // Resolve relative paths against workspace
  if (fs::path(path).is_relative())
    path = (fs::path(workspace_path_) / path).string();
  if (!rimeclaw::SecuritySandbox::ValidateFilePath(path, workspace_path_))
    throw std::runtime_error("Access denied: path outside workspace: " + path);
  if (!std::filesystem::exists(path))
    throw std::runtime_error("File not found: " + path);
  std::ifstream f(path);
  if (!f)
    throw std::runtime_error("Failed to open: " + path);
  return std::string(std::istreambuf_iterator<char>(f), {});
}

std::string ToolRegistry::write_file_tool(const nlohmann::json& params) {
  if (!params.contains("path") || !params.contains("content"))
    throw std::runtime_error("Missing required parameters: path, content");
  std::string path = params["path"].get<std::string>();
  std::string content = params["content"].get<std::string>();
  // Resolve relative paths against workspace
  if (fs::path(path).is_relative())
    path = (fs::path(workspace_path_) / path).string();
  if (!rimeclaw::SecuritySandbox::ValidateFilePath(path, workspace_path_))
    throw std::runtime_error("Access denied: path outside workspace: " + path);
  std::filesystem::create_directories(
      std::filesystem::path(path).parent_path());
  std::ofstream f(path);
  if (!f)
    throw std::runtime_error("Failed to write: " + path);
  f << content;
  return "Successfully wrote to file: " + path;
}

std::string ToolRegistry::edit_file_tool(const nlohmann::json& params) {
  if (!params.contains("path") || !params.contains("oldText") ||
      !params.contains("newText"))
    throw std::runtime_error(
        "Missing required parameters: path, oldText, newText");
  std::string path = params["path"].get<std::string>();
  std::string old_text = params["oldText"].get<std::string>();
  std::string new_text = params["newText"].get<std::string>();
  // Resolve relative paths against workspace
  if (fs::path(path).is_relative())
    path = (fs::path(workspace_path_) / path).string();
  if (!rimeclaw::SecuritySandbox::ValidateFilePath(path, workspace_path_))
    throw std::runtime_error("Access denied: path outside workspace: " + path);
  std::ifstream f(path);
  if (!f)
    throw std::runtime_error("Failed to open: " + path);
  std::string content(std::istreambuf_iterator<char>(f), {});
  size_t pos = content.find(old_text);
  if (pos == std::string::npos)
    throw std::runtime_error("Text not found in file: " + old_text);
  content.replace(pos, old_text.size(), new_text);
  std::ofstream out(path);
  if (!out)
    throw std::runtime_error("Failed to write edited file: " + path);
  out << content;
  return "Successfully edited file: " + path;
}

// ---------------------------------------------------------------------------
// exec_tool
// ---------------------------------------------------------------------------

std::string ToolRegistry::exec_tool(const nlohmann::json& params) {
  if (!params.contains("command"))
    throw std::runtime_error("Missing required parameter: command");
  std::string command = params["command"].get<std::string>();
  int timeout = params.value("timeout", 30);
  std::string workdir = params.value("workdir", "");

  // Default empty workdir to workspace so commands run inside sandbox
  if (workdir.empty()) {
    workdir = workspace_path_;
  }

  // Resolve relative workdir against workspace
  std::string resolved_workdir = workdir;
  if (fs::path(workdir).is_relative()) {
    workdir = (fs::path(workspace_path_) / workdir).string();
    resolved_workdir = workdir;
  }

  // Validate workdir stays inside the workspace
  if (!rimeclaw::SecuritySandbox::ValidateFilePath(workdir, workspace_path_)) {
    throw std::runtime_error("Access denied: workdir outside workspace: " +
                             workdir);
  }

  // Canonicalize for exec_capture
  {
    std::error_code ec;
    fs::path wd_abs = fs::weakly_canonical(workdir, ec);
    if (!ec) resolved_workdir = wd_abs.string();
  }

  if (!rimeclaw::SecuritySandbox::ValidateShellCommand(command)) {
    throw std::runtime_error("Command not allowed: " + command);
  }

  if (approval_manager_) {
    auto decision = approval_manager_->RequestApproval(command);
    if (decision == ApprovalDecision::kDenied) {
      throw std::runtime_error("Command execution denied: " + command);
    }
    if (decision == ApprovalDecision::kTimeout) {
      throw std::runtime_error("Approval timed out: " + command);
    }
  }

  spdlog::info("Executing command: {}", command);

  auto result = platform::exec_capture(command, timeout, resolved_workdir);
  if (result.exit_code == -1)
    throw std::runtime_error("Failed to execute: " + command);
  if (result.exit_code == -2)
    throw std::runtime_error("Command timeout: " + command);
  if (result.exit_code != 0)
    throw std::runtime_error("Command exited " +
                             std::to_string(result.exit_code) + ": " +
                             result.output);
  return result.output;
}

// ---------------------------------------------------------------------------
// message_tool
// ---------------------------------------------------------------------------

std::string ToolRegistry::message_tool(const nlohmann::json& params) {
  if (!params.contains("channel") || !params.contains("message"))
    throw std::runtime_error("Missing required parameters: channel, message");
  std::string channel = params["channel"].get<std::string>();
  std::string message = params["message"].get<std::string>();
  std::string action = params.value("action", "send");

  // Route "agent:<id>" channels through SubagentManager
  if (subagent_manager_ && channel.rfind("agent:", 0) == 0) {
    SpawnParams sp;
    sp.task = message;
    sp.agent_id = channel.substr(6);  // strip "agent:" prefix
    sp.mode = SpawnMode::kRun;
    auto run = subagent_manager_->Spawn(sp, current_session_key_);
    nlohmann::json r;
    r["status"] = "delivered";
    r["run_id"] = run.run_id;
    if (!run.error.empty())
      r["error"] = run.error;
    if (!run.result.empty())
      r["result"] = run.result;
    return r.dump();
  }

  // Other channels: log and acknowledge
  spdlog::info("Message to channel {}: {}", channel, message);
  nlohmann::json r;
  r["status"] = "sent";
  r["channel"] = channel;
  return r.dump();
}

// ---------------------------------------------------------------------------
// apply_patch_tool
// Supports: *** Begin Patch / *** End Patch wrapper
//   *** Add File: <path>      -> create file with content below
//   *** Update File: <path>   -> apply unified diff hunks
//   *** Delete File: <path>   -> remove file
// ---------------------------------------------------------------------------

std::string ToolRegistry::apply_patch_tool(const nlohmann::json& params) {
  if (!params.contains("patch"))
    throw std::runtime_error("patch is required");
  std::string patch = params["patch"].get<std::string>();

  // Find Begin/End markers
  const std::string kBegin = "*** Begin Patch";
  const std::string kEnd = "*** End Patch";
  size_t begin_pos = patch.find(kBegin);
  size_t end_pos = patch.find(kEnd);
  if (begin_pos == std::string::npos)
    throw std::runtime_error("Missing '*** Begin Patch' marker");
  std::string body = (end_pos != std::string::npos)
                         ? patch.substr(begin_pos + kBegin.size(),
                                        end_pos - begin_pos - kBegin.size())
                         : patch.substr(begin_pos + kBegin.size());

  // Split into lines
  std::vector<std::string> lines;
  std::istringstream iss(body);
  std::string line;
  while (std::getline(iss, line)) {
    if (!line.empty() && line.back() == '\r')
      line.pop_back();
    lines.push_back(line);
  }

  int applied = 0;
  std::string current_file;
  enum class FileOp { kNone, kAdd, kUpdate, kDelete } op = FileOp::kNone;
  std::vector<std::string> add_content;
  std::vector<std::string> diff_hunks;

  auto flush_file = [&]() {
    if (current_file.empty())
      return;
    if (op == FileOp::kAdd) {
      std::filesystem::create_directories(
          std::filesystem::path(current_file).parent_path());
      std::ofstream f(current_file);
      for (const auto& l : add_content)
        f << l << "\n";
      ++applied;
    } else if (op == FileOp::kDelete) {
      if (std::filesystem::exists(current_file))
        std::filesystem::remove(current_file);
      ++applied;
    } else if (op == FileOp::kUpdate && !diff_hunks.empty()) {
      // Read current file content
      std::ifstream f(current_file);
      if (!f)
        throw std::runtime_error("Cannot open for update: " + current_file);
      std::vector<std::string> file_lines;
      std::string fl;
      while (std::getline(f, fl)) {
        if (!fl.empty() && fl.back() == '\r')
          fl.pop_back();
        file_lines.push_back(fl);
      }
      f.close();

      // Apply hunks (simple line-based application)
      // Each hunk starts with @@ -start,count +start,count @@
      std::vector<std::string> result_lines = file_lines;
      int line_offset = 0;
      size_t i = 0;
      while (i < diff_hunks.size()) {
        std::string& hl = diff_hunks[i];
        if (hl.size() >= 2 && hl.substr(0, 2) == "@@") {
          // Parse @@ -old_start,old_count +new_start,new_count @@
          int old_start = 0;
          int old_count = 0;
          sscanf(hl.c_str(), "@@ -%d,%d", &old_start, &old_count);
          int apply_at = old_start - 1 + line_offset;  // 0-indexed

          // Collect hunk lines
          std::vector<std::string> removed, added;
          size_t j = i + 1;
          while (j < diff_hunks.size() && diff_hunks[j].size() >= 2 &&
                 diff_hunks[j].substr(0, 2) != "@@") {
            char ch = diff_hunks[j][0];
            std::string content = diff_hunks[j].substr(1);
            if (ch == '-')
              removed.push_back(content);
            else if (ch == '+')
              added.push_back(content);
            ++j;
          }

          // Splice: replace removed lines with added lines
          if (apply_at >= 0 &&
              apply_at <= static_cast<int>(result_lines.size())) {
            result_lines.erase(result_lines.begin() + apply_at,
                               result_lines.begin() + apply_at +
                                   static_cast<int>(removed.size()));
            result_lines.insert(result_lines.begin() + apply_at, added.begin(),
                                added.end());
            line_offset += static_cast<int>(added.size()) -
                           static_cast<int>(removed.size());
          }
          i = j;
        } else {
          ++i;
        }
      }

      std::ofstream out(current_file);
      if (!out)
        throw std::runtime_error("Cannot write: " + current_file);
      for (const auto& l : result_lines)
        out << l << "\n";
      ++applied;
    }

    current_file.clear();
    op = FileOp::kNone;
    add_content.clear();
    diff_hunks.clear();
  };

  for (const auto& l : lines) {
    if (l.substr(0, 16) == "*** Add File: ") {
      flush_file();
      current_file = l.substr(14);
      op = FileOp::kAdd;
    } else if (l.substr(0, 19) == "*** Update File: ") {
      flush_file();
      current_file = l.substr(17);
      op = FileOp::kUpdate;
    } else if (l.substr(0, 19) == "*** Delete File: ") {
      flush_file();
      current_file = l.substr(17);
      op = FileOp::kDelete;
    } else if (op == FileOp::kAdd) {
      add_content.push_back(l);
    } else if (op == FileOp::kUpdate) {
      diff_hunks.push_back(l);
    }
  }
  flush_file();

  return "Applied patch: " + std::to_string(applied) + " file(s) modified.";
}

// ---------------------------------------------------------------------------
// process_tool -- background process management
// ---------------------------------------------------------------------------

std::string ToolRegistry::process_tool(const nlohmann::json& params) {
  std::string action = params.value("action", "list");

  if (action == "list") {
    std::lock_guard<std::mutex> lk(bg_mu_);
    nlohmann::json sessions = nlohmann::json::array();
    for (auto& [sid, sess] : bg_sessions_) {
      // Poll future without blocking
      if (!sess->exited && sess->future.valid() &&
          sess->future.wait_for(std::chrono::seconds(0)) ==
              std::future_status::ready) {
        try {
          sess->output = sess->future.get();
        } catch (const std::exception& e) {
          sess->error = e.what();
        }
        sess->exited = true;
      }
      sessions.push_back({{"id", sid},
                          {"command", sess->command},
                          {"running", !sess->exited},
                          {"error", sess->error}});
    }
    return nlohmann::json{{"sessions", sessions}}.dump();
  }

  if (action == "start") {
    std::string command = params.value("command", "");
    if (command.empty())
      throw std::runtime_error("command is required for process start");

    auto sess = std::make_shared<BgSession>();
    sess->id = generate_id("proc");
    sess->command = command;
    auto started = std::chrono::system_clock::now();
    sess->started_at = started;

    // Run command asynchronously
    sess->future = std::async(std::launch::async, [command]() -> std::string {
      auto r = platform::exec_capture(command, 300);  // 5-minute max
      if (r.exit_code != 0 && r.exit_code != -1 && r.exit_code != -2) {
        return r.output + "\n[exit " + std::to_string(r.exit_code) + "]";
      }
      return r.output;
    });

    std::string id = sess->id;
    {
      std::lock_guard<std::mutex> lk(bg_mu_);
      bg_sessions_[id] = std::move(sess);
    }
    return nlohmann::json{{"ok", true}, {"id", id}}.dump();
  }

  std::string id = params.value("id", "");
  if (id.empty())
    throw std::runtime_error("id is required for action: " + action);

  std::shared_ptr<BgSession> sess;
  {
    std::lock_guard<std::mutex> lk(bg_mu_);
    auto it = bg_sessions_.find(id);
    if (it == bg_sessions_.end())
      throw std::runtime_error("No session with id: " + id);
    sess = it->second;
  }

  if (action == "log" || action == "poll") {
    int timeout_ms = params.value("timeout", 5000);
    if (!sess->exited && sess->future.valid()) {
      auto status =
          sess->future.wait_for(std::chrono::milliseconds(timeout_ms));
      if (status == std::future_status::ready) {
        try {
          sess->output = sess->future.get();
        } catch (const std::exception& e) {
          sess->error = e.what();
        }
        sess->exited = true;
      }
    }
    return nlohmann::json{{"id", id},
                          {"running", !sess->exited},
                          {"output", sess->output},
                          {"error", sess->error}}
        .dump();
  }

  if (action == "kill") {
    // Best-effort: mark as done
    sess->exited = true;
    sess->error = "killed by user";
    return nlohmann::json{{"ok", true}, {"id", id}}.dump();
  }

  if (action == "clear") {
    sess->output.clear();
    return nlohmann::json{{"ok", true}}.dump();
  }

  if (action == "remove") {
    std::lock_guard<std::mutex> lk(bg_mu_);
    bg_sessions_.erase(id);
    return nlohmann::json{{"ok", true}}.dump();
  }

  if (action == "write" || action == "send-keys") {
    // Cannot write to stdin without pipe infrastructure; acknowledge gracefully
    spdlog::warn("process send-keys: stdin write not supported in this build");
    return nlohmann::json{{"ok", false}, {"note", "stdin write not supported"}}
        .dump();
  }

  throw std::runtime_error("Unknown process action: " + action);
}

// ---------------------------------------------------------------------------
// memory_search_tool
// ---------------------------------------------------------------------------

std::string ToolRegistry::memory_search_tool(const nlohmann::json& params) {
  std::string query = params.value("query", "");
  int max_results = params.value("maxResults", 10);
  if (query.empty())
    throw std::runtime_error("query is required");

  MemorySearch search;
  search.IndexDirectory(workspace_path_);
  auto results = search.Search(query, max_results);

  nlohmann::json arr = nlohmann::json::array();
  for (const auto& r : results) {
    nlohmann::json entry;
    entry["source"] = r.source;
    entry["content"] = r.content;
    entry["score"] = r.score;
    entry["lineNumber"] = r.line_number;
    arr.push_back(entry);
  }
  return nlohmann::json{
      {"results", arr}, {"count", arr.size()}, {"query", query}}
      .dump();
}

// ---------------------------------------------------------------------------
// memory_get_tool
// ---------------------------------------------------------------------------

std::string ToolRegistry::memory_get_tool(const nlohmann::json& params) {
  std::string rel_path = params.value("path", "");
  if (rel_path.empty())
    throw std::runtime_error("path is required");

  auto full_path = std::filesystem::path(workspace_path_) / rel_path;

  // Security: must remain inside workspace
  auto canonical = std::filesystem::weakly_canonical(full_path);
  auto ws_canon = std::filesystem::weakly_canonical(workspace_path_);
  if (canonical.string().substr(0, ws_canon.string().size()) !=
      ws_canon.string()) {
    throw std::runtime_error("Access denied: path outside workspace");
  }

  if (!std::filesystem::exists(full_path)) {
    throw std::runtime_error("File not found: " + rel_path);
  }

  std::ifstream f(full_path);
  if (!f)
    throw std::runtime_error("Cannot read: " + rel_path);
  std::string content(std::istreambuf_iterator<char>(f), {});
  return nlohmann::json{{"path", rel_path}, {"content", content}}.dump();
}

}  // namespace rimeclaw
