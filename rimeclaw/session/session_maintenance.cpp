// Copyright 2025-2026 RimeClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "rimeclaw/session/session_maintenance.hpp"

#include <algorithm>
#include <fstream>
#include <regex>

namespace rimeclaw {

// --- MaintenanceMode ---

MaintenanceMode MaintenanceModeFromString(const std::string& s) {
  if (s == "warn")
    return MaintenanceMode::kWarn;
  return MaintenanceMode::kEnforce;
}

// --- SessionMaintenanceConfig ---

SessionMaintenanceConfig
SessionMaintenanceConfig::FromJson(const nlohmann::json& j) {
  SessionMaintenanceConfig c;
  if (j.contains("mode") && j["mode"].is_string()) {
    c.mode = MaintenanceModeFromString(j["mode"].get<std::string>());
  }
  if (j.contains("pruneAfter")) {
    if (j["pruneAfter"].is_string()) {
      c.prune_after_seconds = SessionMaintenance::ParseDurationSeconds(
          j["pruneAfter"].get<std::string>());
    } else if (j["pruneAfter"].is_number()) {
      c.prune_after_seconds = j["pruneAfter"].get<int64_t>();
    }
  }
  if (j.contains("archiveAfter")) {
    if (j["archiveAfter"].is_string()) {
      c.archive_after_seconds = SessionMaintenance::ParseDurationSeconds(
          j["archiveAfter"].get<std::string>());
    } else if (j["archiveAfter"].is_number()) {
      c.archive_after_seconds = j["archiveAfter"].get<int64_t>();
    }
  }
  if (j.contains("maxTotalSizeBytes") && j["maxTotalSizeBytes"].is_number()) {
    c.max_total_size_bytes = j["maxTotalSizeBytes"].get<int64_t>();
  }
  if (j.contains("maxSessions") && j["maxSessions"].is_number()) {
    c.max_sessions = j["maxSessions"].get<int>();
  }
  return c;
}

// --- SessionMaintenance ---

SessionMaintenance::SessionMaintenance(
    const SessionMaintenanceConfig& config,
    const std::filesystem::path& sessions_dir)
    : config_(config), sessions_dir_(sessions_dir) {}

void SessionMaintenance::RunCycle() {
  if (!std::filesystem::exists(sessions_dir_)) {
    return;
  }

  if (config_.prune_after_seconds > 0) {
    prune_old_sessions();
  }
  if (config_.max_sessions > 0) {
    enforce_max_sessions();
  }
  if (config_.max_total_size_bytes > 0) {
    enforce_max_size();
  }
}

int64_t SessionMaintenance::ParseDurationSeconds(const std::string& s) {
  if (s.empty())
    return 0;

  std::regex re(R"(^(\d+)([dhms]?)$)");
  std::smatch m;
  if (!std::regex_match(s, m, re))
    return 0;

  int64_t value = std::stoll(m[1].str());
  std::string unit = m[2].str();

  if (unit == "d")
    return value * 86400;
  if (unit == "h")
    return value * 3600;
  if (unit == "m")
    return value * 60;
  if (unit == "s" || unit.empty())
    return value;
  return 0;
}

void SessionMaintenance::prune_old_sessions() {
  auto now = std::chrono::system_clock::now();
  auto cutoff = now - std::chrono::seconds(config_.prune_after_seconds);
  auto files = get_session_files();

  for (const auto& info : files) {
    if (info.mtime < cutoff) {
      if (config_.mode == MaintenanceMode::kWarn) {
        continue;
      }
      try {
        std::filesystem::remove(info.path);
      } catch (const std::exception&) {
      }
    }
  }
}

void SessionMaintenance::enforce_max_sessions() {
  auto files = get_session_files();
  int total = static_cast<int>(files.size());
  if (total <= config_.max_sessions)
    return;

  int to_remove = total - config_.max_sessions;

  for (int i = 0; i < to_remove && i < static_cast<int>(files.size()); ++i) {
    if (config_.mode == MaintenanceMode::kWarn) {
      continue;
    }
    try {
      std::filesystem::remove(files[i].path);
    } catch (const std::exception&) {
    }
  }
}

void SessionMaintenance::enforce_max_size() {
  auto total = total_session_size();
  if (total <= config_.max_total_size_bytes)
    return;

  auto files = get_session_files();
  for (const auto& info : files) {
    if (total <= config_.max_total_size_bytes)
      break;

    if (config_.mode == MaintenanceMode::kWarn) {
      total -= info.size;
      continue;
    }

    try {
      total -= info.size;
      std::filesystem::remove(info.path);
    } catch (const std::exception&) {
    }
  }
}

void SessionMaintenance::archive_file(const std::filesystem::path& path) {
  auto now = std::chrono::system_clock::now();
  auto time_t = std::chrono::system_clock::to_time_t(now);
  struct tm tm {};
#ifdef _WIN32
  localtime_s(&tm, &time_t);
#else
  localtime_r(&time_t, &tm);
#endif

  char buf[32];
  std::strftime(buf, sizeof(buf), "%Y%m%d_%H%M%S", &tm);

  auto archive_name = path.stem().string() + "." + buf + ".archived.jsonl";
  auto archive_path = path.parent_path() / archive_name;

  try {
    std::filesystem::rename(path, archive_path);
  } catch (const std::exception&) {
  }
}

int64_t SessionMaintenance::total_session_size() const {
  int64_t total = 0;
  if (!std::filesystem::exists(sessions_dir_))
    return 0;
  for (const auto& entry : std::filesystem::directory_iterator(sessions_dir_)) {
    if (entry.is_regular_file()) {
      total += static_cast<int64_t>(entry.file_size());
    }
  }
  return total;
}

std::vector<SessionMaintenance::SessionFileInfo>
SessionMaintenance::get_session_files() const {
  std::vector<SessionFileInfo> files;
  if (!std::filesystem::exists(sessions_dir_))
    return files;

  for (const auto& entry : std::filesystem::directory_iterator(sessions_dir_)) {
    if (!entry.is_regular_file())
      continue;
    if (entry.path().extension() != ".jsonl" &&
        entry.path().extension() != ".json") {
      continue;
    }

    SessionFileInfo info;
    info.path = entry.path();
    info.session_key = entry.path().stem().string();
    auto ftime = entry.last_write_time();
    auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        ftime - std::filesystem::file_time_type::clock::now() +
        std::chrono::system_clock::now());
    info.mtime = sctp;
    info.size = static_cast<int64_t>(entry.file_size());
    files.push_back(info);
  }

  std::sort(files.begin(), files.end(),
            [](const SessionFileInfo& a, const SessionFileInfo& b) {
              return a.mtime < b.mtime;
            });

  return files;
}

}  // namespace rimeclaw
