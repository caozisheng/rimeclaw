// Copyright 2025-2026 RimeClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include "rimeclaw/common/noncopyable.hpp"
#include "rimeclaw/core/content_block.hpp"

namespace rimeclaw {

// --- Session Key Utilities ---

struct ParsedSessionKey {
  std::string agent_id;
  std::string rest;
};

// Parse a session key into agent_id and rest.
// Returns nullopt if the key doesn't match the "agent:<agentId>:<rest>" format.
std::optional<ParsedSessionKey> ParseAgentSessionKey(const std::string& key);

// Normalize a session key to canonical format.
// - Plain keys like "my-session" -> "agent:main:my-session"
// - Keys missing the "agent:" prefix get it added
std::string NormalizeSessionKey(const std::string& key);

// --- Session Data ---

struct SessionData {
  std::string session_key;  // normalized key
  std::string session_id;   // unique internal id
  std::string display_name;
  std::string channel;
  std::string created_at;
  std::string updated_at;
  int message_count = 0;
};

// --- Message ---

struct SessionMessage {
  std::string role;  // "user" | "assistant" | "system"
  std::vector<ContentBlock> content;

  nlohmann::json ToJson() const;
  nlohmann::json ToJsonl() const;
  static SessionMessage FromJson(const nlohmann::json& j);
};

// --- Handle (view into a session) ---

struct SessionHandle {
  std::string session_key;
  std::string session_id;
  bool is_new = false;  // true if just created
};

// --- Options ---

struct SessionCreateOptions {
  std::string display_name;
  std::string channel;
};

// --- Manager ---

class SessionManager : private Noncopyable {
 public:
  explicit SessionManager(const std::filesystem::path& sessions_dir);

  // Get or create a session by key.
  SessionHandle GetOrCreate(const std::string& session_key,
                            const std::string& display_name = "",
                            const std::string& channel = "");
  SessionHandle GetOrCreate(const std::string& session_key,
                            const SessionCreateOptions& opts);

  // Append a message to the session transcript.
  void AppendMessage(const std::string& session_key,
                     const SessionMessage& msg);

  // Load full transcript for a session.
  std::vector<SessionMessage> LoadTranscript(
      const std::string& session_key) const;

  // List all known sessions.
  std::vector<SessionData> ListSessions() const;

  // Delete a session (removes transcript file and store entry).
  bool DeleteSession(const std::string& session_key);

  // Clear a session's transcript but keep the session entry.
  bool ClearTranscript(const std::string& session_key);

  // Get session data.
  std::optional<SessionData> GetSession(
      const std::string& session_key) const;

 private:
  std::filesystem::path sessions_dir_;
  mutable std::shared_mutex mutex_;

  std::unordered_map<std::string, SessionData> store_;

  std::filesystem::path transcript_path(
      const std::string& session_id) const;
  std::string get_timestamp() const;
  std::string generate_session_id() const;

  void LoadStore();
  void SaveStore() const;

  std::filesystem::path store_path() const;
};

}  // namespace rimeclaw
