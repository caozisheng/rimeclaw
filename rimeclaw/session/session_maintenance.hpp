// Copyright 2025-2026 RimeClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace rimeclaw {

// Maintenance mode
enum class MaintenanceMode {
  kEnforce,  // Actively prune and rotate
  kWarn,     // Log warnings only
};

MaintenanceMode MaintenanceModeFromString(const std::string& s);

// Session maintenance configuration
struct SessionMaintenanceConfig {
  MaintenanceMode mode = MaintenanceMode::kEnforce;

  // Prune sessions older than this duration (0 = disabled)
  int64_t prune_after_seconds = 0;

  // Archive sessions older than this duration (0 = disabled)
  int64_t archive_after_seconds = 0;

  // Maximum total session storage (bytes, 0 = unlimited)
  int64_t max_total_size_bytes = 0;

  // Maximum sessions to keep (0 = unlimited)
  int max_sessions = 0;

  static SessionMaintenanceConfig FromJson(const nlohmann::json& j);
};

// Session maintenance runner
class SessionMaintenance {
 public:
  SessionMaintenance(const SessionMaintenanceConfig& config,
                     const std::filesystem::path& sessions_dir);

  // Run a full maintenance cycle.
  void RunCycle();

  // Parse a duration string like "7d", "24h", "30m", "60s" to seconds.
  static int64_t ParseDurationSeconds(const std::string& s);

 private:
  SessionMaintenanceConfig config_;
  std::filesystem::path sessions_dir_;

  void prune_old_sessions();
  void enforce_max_sessions();
  void enforce_max_size();
  void archive_file(const std::filesystem::path& path);

  // Get total size of session directory
  int64_t total_session_size() const;

  // Get session file info sorted by modification time (oldest first)
  struct SessionFileInfo {
    std::filesystem::path path;
    std::string session_key;
    std::chrono::system_clock::time_point mtime;
    int64_t size;
  };
  std::vector<SessionFileInfo> get_session_files() const;
};

}  // namespace rimeclaw
