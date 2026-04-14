// Copyright 2025-2026 RimeClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "rimeclaw/session/session_manager.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>

namespace rimeclaw {

// --- Session Key Utilities ---

static std::string to_lower(const std::string& s) {
  std::string result = s;
  std::transform(result.begin(), result.end(), result.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return result;
}

std::optional<ParsedSessionKey> ParseAgentSessionKey(const std::string& key) {
  if (key.empty())
    return std::nullopt;

  // Split by ':'
  std::vector<std::string> parts;
  std::string::size_type start = 0;
  while (start < key.size()) {
    auto pos = key.find(':', start);
    if (pos == std::string::npos) {
      parts.push_back(key.substr(start));
      break;
    }
    parts.push_back(key.substr(start, pos - start));
    start = pos + 1;
  }

  if (parts.size() < 3)
    return std::nullopt;
  if (to_lower(parts[0]) != "agent")
    return std::nullopt;

  ParsedSessionKey result;
  result.agent_id = parts[1];
  // Rest is everything after "agent:<agentId>:"
  auto prefix_len = parts[0].size() + 1 + parts[1].size() + 1;
  result.rest = key.substr(prefix_len);
  return result;
}

std::string NormalizeSessionKey(const std::string& key) {
  if (key.empty())
    return "agent:main:default";

  auto parsed = ParseAgentSessionKey(key);
  if (parsed.has_value()) {
    return key;  // Already in canonical form
  }

  // Check if it starts with "agent:" but only has two parts
  if (to_lower(key.substr(0, 6)) == "agent:") {
    // agent:<something> -> agent:main:<something>
    return "agent:main:" + key.substr(6);
  }

  return "agent:main:" + key;
}

// --- SessionMessage ---

nlohmann::json SessionMessage::ToJson() const {
  nlohmann::json j;
  j["role"] = role;
  nlohmann::json content_arr = nlohmann::json::array();
  for (const auto& block : content) {
    content_arr.push_back(block.ToJson());
  }
  j["content"] = content_arr;
  return j;
}

nlohmann::json SessionMessage::ToJsonl() const {
  nlohmann::json j;
  j["type"] = "message";
  j["message"] = ToJson();
  return j;
}

SessionMessage SessionMessage::FromJson(const nlohmann::json& j) {
  SessionMessage msg;
  if (j.contains("role"))
    msg.role = j["role"].get<std::string>();
  if (j.contains("content")) {
    if (j["content"].is_array()) {
      for (const auto& block : j["content"]) {
        msg.content.push_back(ContentBlock::FromJson(block));
      }
    } else if (j["content"].is_string()) {
      msg.content.push_back(
          ContentBlock::MakeText(j["content"].get<std::string>()));
    }
  }
  return msg;
}

// --- SessionManager ---

SessionManager::SessionManager(const std::filesystem::path& sessions_dir)
    : sessions_dir_(sessions_dir) {
  std::filesystem::create_directories(sessions_dir_);
  LoadStore();
}

SessionHandle SessionManager::GetOrCreate(const std::string& session_key,
                                          const std::string& display_name,
                                          const std::string& channel) {
  SessionCreateOptions opts;
  opts.display_name = display_name;
  opts.channel = channel;
  return GetOrCreate(session_key, opts);
}

SessionHandle SessionManager::GetOrCreate(const std::string& session_key,
                                          const SessionCreateOptions& opts) {
  std::unique_lock<std::shared_mutex> lock(mutex_);

  std::string normalized = NormalizeSessionKey(session_key);

  auto it = store_.find(normalized);
  if (it != store_.end()) {
    return {normalized, it->second.session_id, false};
  }

  // Create new session
  std::string sid = generate_session_id();
  std::string now = get_timestamp();
  SessionData data;
  data.session_key = normalized;
  data.session_id = sid;
  data.updated_at = now;
  data.created_at = now;
  data.display_name = opts.display_name.empty() ? normalized : opts.display_name;
  data.channel = opts.channel.empty() ? "cli" : opts.channel;
  data.message_count = 0;

  store_[normalized] = data;
  SaveStore();

  return {normalized, sid, true};
}

void SessionManager::AppendMessage(const std::string& session_key,
                                   const SessionMessage& msg) {
  std::unique_lock<std::shared_mutex> lock(mutex_);

  std::string normalized = NormalizeSessionKey(session_key);
  auto it = store_.find(normalized);
  if (it == store_.end()) {
    return;
  }

  auto path = transcript_path(it->second.session_id);
  std::ofstream file(path, std::ios::app);
  if (!file.is_open()) {
    return;
  }

  file << msg.ToJsonl().dump() << "\n";
  file.close();

  it->second.updated_at = get_timestamp();
  ++it->second.message_count;
  SaveStore();
}

std::vector<SessionMessage>
SessionManager::LoadTranscript(const std::string& session_key) const {
  std::shared_lock<std::shared_mutex> lock(mutex_);
  std::vector<SessionMessage> messages;

  std::string normalized = NormalizeSessionKey(session_key);
  auto it = store_.find(normalized);
  if (it == store_.end()) {
    return messages;
  }

  auto path = transcript_path(it->second.session_id);
  if (!std::filesystem::exists(path)) {
    return messages;
  }

  std::ifstream file(path);
  std::string line;
  while (std::getline(file, line)) {
    if (line.empty())
      continue;
    try {
      auto j = nlohmann::json::parse(line);
      if (!j.contains("type") || !j["type"].is_string() ||
          j["type"].get<std::string>() != "message")
        continue;
      if (j.contains("message")) {
        messages.push_back(SessionMessage::FromJson(j["message"]));
      }
    } catch (const std::exception& e) {
    }
  }

  return messages;
}

std::vector<SessionData> SessionManager::ListSessions() const {
  std::shared_lock<std::shared_mutex> lock(mutex_);
  std::vector<SessionData> result;
  result.reserve(store_.size());
  for (const auto& [key, data] : store_) {
    result.push_back(data);
  }
  // Sort by updated_at descending
  std::sort(result.begin(), result.end(),
            [](const SessionData& a, const SessionData& b) {
              return a.updated_at > b.updated_at;
            });
  return result;
}

bool SessionManager::DeleteSession(const std::string& session_key) {
  std::unique_lock<std::shared_mutex> lock(mutex_);

  std::string normalized = NormalizeSessionKey(session_key);
  auto it = store_.find(normalized);
  if (it == store_.end()) {
    return false;
  }

  // Remove transcript file
  auto path = transcript_path(it->second.session_id);
  if (std::filesystem::exists(path)) {
    std::filesystem::remove(path);
  }

  store_.erase(it);
  SaveStore();

  return true;
}

bool SessionManager::ClearTranscript(const std::string& session_key) {
  std::unique_lock<std::shared_mutex> lock(mutex_);

  std::string normalized = NormalizeSessionKey(session_key);
  auto it = store_.find(normalized);
  if (it == store_.end()) {
    return false;
  }

  // Truncate transcript file
  auto path = transcript_path(it->second.session_id);
  std::ofstream file(path, std::ios::trunc);
  file.close();

  it->second.message_count = 0;
  it->second.updated_at = get_timestamp();
  SaveStore();

  return true;
}

std::optional<SessionData>
SessionManager::GetSession(const std::string& session_key) const {
  std::shared_lock<std::shared_mutex> lock(mutex_);
  std::string normalized = NormalizeSessionKey(session_key);
  auto it = store_.find(normalized);
  if (it == store_.end())
    return std::nullopt;
  return it->second;
}

void SessionManager::SaveStore() const {
  auto path = store_path();
  nlohmann::json j = nlohmann::json::object();
  for (const auto& [key, data] : store_) {
    nlohmann::json entry;
    entry["sessionId"] = data.session_id;
    entry["displayName"] = data.display_name;
    entry["channel"] = data.channel;
    entry["createdAt"] = data.created_at;
    entry["updatedAt"] = data.updated_at;
    entry["messageCount"] = data.message_count;
    j[key] = entry;
  }
  std::ofstream file(path);
  if (file.is_open()) {
    file << j.dump(2);
    file.close();
  }
}

void SessionManager::LoadStore() {
  auto path = store_path();
  if (!std::filesystem::exists(path)) {
    return;
  }

  try {
    std::ifstream file(path);
    std::string content((std::istreambuf_iterator<char>(file)), {});
    file.close();
    nlohmann::json j = nlohmann::json::parse(content);

    for (const auto& [key, value] : j.items()) {
      SessionData data;
      data.session_key = key;
      data.session_id = value.value("sessionId", "");
      data.updated_at = value.value("updatedAt", "");
      data.created_at = value.value("createdAt", "");
      data.display_name = value.value("displayName", "");
      data.channel = value.value("channel", "cli");
      data.message_count = value.value("messageCount", 0);
      store_[key] = data;
    }

  } catch (const std::exception& e) {
  }
}

std::string SessionManager::generate_session_id() const {
  thread_local static std::random_device rd;
  thread_local static std::mt19937_64 gen(rd());
  thread_local static std::uniform_int_distribution<uint64_t> dis;

  auto now = std::chrono::system_clock::now();
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch())
                .count();

  std::ostringstream ss;
  ss << std::hex << std::setfill('0');
  ss << std::setw(12) << ms << std::setw(16) << dis(gen);
  return ss.str();
}

std::string SessionManager::get_timestamp() const {
  auto now = std::chrono::system_clock::now();
  auto time_t = std::chrono::system_clock::to_time_t(now);
  std::tm tm;
#ifdef _WIN32
  gmtime_s(&tm, &time_t);
#else
  gmtime_r(&time_t, &tm);
#endif
  std::ostringstream ss;
  ss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
  return ss.str();
}

std::filesystem::path
SessionManager::transcript_path(const std::string& session_id) const {
  return sessions_dir_ / (session_id + ".jsonl");
}

std::filesystem::path SessionManager::store_path() const {
  return sessions_dir_ / "sessions.json";
}

}  // namespace rimeclaw
