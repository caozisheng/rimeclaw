// Copyright 2025-2026 RimeClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "rimeclaw/security/sandbox.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <regex>

namespace rimeclaw {

Sandbox::Sandbox(const std::filesystem::path& workspace_path,
                 const std::vector<std::string>& allowed_paths,
                 const std::vector<std::string>& denied_paths,
                 const std::vector<std::string>& allowed_commands,
                 const std::vector<std::string>& denied_commands)
    : workspace_path_(workspace_path),
      allowed_paths_(allowed_paths),
      denied_paths_(denied_paths),
      allowed_commands_(allowed_commands),
      denied_commands_(denied_commands) {
  for (const auto& cmd : denied_commands_) {
    denied_cmd_patterns_.push_back(
        std::regex(cmd, std::regex_constants::icase));
  }
}

bool Sandbox::IsPathAllowed(const std::string& path) const {
  std::filesystem::path resolved_path = std::filesystem::absolute(path);

  for (const auto& denied_path : denied_paths_) {
    std::filesystem::path denied_resolved =
        std::filesystem::absolute(denied_path);
    if (resolved_path.string().find(denied_resolved.string()) == 0) {
      return false;
    }
  }

  if (!allowed_paths_.empty()) {
    for (const auto& allowed_path : allowed_paths_) {
      std::filesystem::path allowed_resolved =
          std::filesystem::absolute(allowed_path);
      if (resolved_path.string().find(allowed_resolved.string()) == 0) {
        return true;
      }
    }
    return false;
  }

  return true;
}

bool Sandbox::IsCommandAllowed(const std::string& command) const {
  for (const auto& pattern : denied_cmd_patterns_) {
    if (std::regex_search(command, pattern)) {
      return false;
    }
  }

  if (!allowed_commands_.empty()) {
    for (const auto& allowed_cmd : allowed_commands_) {
      if (command.find(allowed_cmd) != std::string::npos) {
        return true;
      }
    }
    return false;
  }

  return true;
}

std::string Sandbox::SanitizePath(const std::string& path) const {
  std::filesystem::path p(path);
  if (p.is_absolute()) {
    return p.string();
  }
  return (workspace_path_ / p).string();
}

bool Sandbox::ValidateFilePath(const std::string& path,
                               const std::string& workspace) {
  namespace fs = std::filesystem;
  try {
    fs::path resolved = fs::weakly_canonical(fs::absolute(path));
    fs::path ws = fs::weakly_canonical(fs::absolute(workspace));
    // Path must be within workspace
    auto ws_str = ws.string();
    auto path_str = resolved.string();
    return path_str.find(ws_str) == 0;
  } catch (...) {
    return false;
  }
}

bool Sandbox::ValidateShellCommand(const std::string& command) {
  static const std::vector<std::regex> dangerous_patterns = {
      std::regex(R"(\brm\s+-rf\s+/)", std::regex_constants::icase),
      std::regex(R"(\bmkfs\b)", std::regex_constants::icase),
      std::regex(R"(\bdd\s+if=)", std::regex_constants::icase),
  };

  for (const auto& pattern : dangerous_patterns) {
    if (std::regex_search(command, pattern)) {
      return false;
    }
  }
  return true;
}

void Sandbox::ApplyResourceLimits() {
  // No-op; enforcement lives in the platform process layer.
}

}  // namespace rimeclaw
