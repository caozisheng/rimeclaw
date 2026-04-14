// Copyright 2025-2026 RimeClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "rimeclaw/security/exec_approval.hpp"

#include <random>
#include <sstream>

namespace rimeclaw {

// --- AskMode ---

AskMode AskModeFromString(const std::string& s) {
  if (s == "on_miss" || s == "on-miss" || s == "onMiss")
    return AskMode::kOnMiss;
  if (s == "always")
    return AskMode::kAlways;
  return AskMode::kOff;
}

std::string AskModeToString(AskMode m) {
  switch (m) {
    case AskMode::kOnMiss:
      return "on_miss";
    case AskMode::kAlways:
      return "always";
    default:
      return "off";
  }
}

// --- ApprovalDecision ---

std::string ApprovalDecisionToString(ApprovalDecision d) {
  switch (d) {
    case ApprovalDecision::kApproved:
      return "approved";
    case ApprovalDecision::kDenied:
      return "denied";
    case ApprovalDecision::kTimeout:
      return "timeout";
    default:
      return "pending";
  }
}

// --- ExecAllowlist ---

void ExecAllowlist::AddPattern(const std::string& pattern) {
  patterns_.push_back(pattern);
}

bool ExecAllowlist::Matches(const std::string& command) const {
  for (const auto& pattern : patterns_) {
    if (glob_match(pattern, command)) {
      return true;
    }
  }
  return false;
}

void ExecAllowlist::LoadFromJson(const nlohmann::json& j) {
  if (!j.is_array()) return;
  for (const auto& item : j) {
    if (item.is_string()) {
      AddPattern(item.get<std::string>());
    }
  }
}

bool ExecAllowlist::glob_match(const std::string& pattern,
                               const std::string& text) {
  // Simple glob: * matches any sequence, ? matches single char
  size_t pi = 0, ti = 0;
  size_t star_p = std::string::npos, star_t = 0;

  while (ti < text.size()) {
    if (pi < pattern.size() &&
        (pattern[pi] == text[ti] || pattern[pi] == '?')) {
      ++pi;
      ++ti;
    } else if (pi < pattern.size() && pattern[pi] == '*') {
      star_p = pi++;
      star_t = ti;
    } else if (star_p != std::string::npos) {
      pi = star_p + 1;
      ti = ++star_t;
    } else {
      return false;
    }
  }

  while (pi < pattern.size() && pattern[pi] == '*') ++pi;
  return pi == pattern.size();
}

// --- ExecApprovalManager ---

ExecApprovalManager::ExecApprovalManager(ExecApprovalConfig config)
    : config_(std::move(config)) {
  // Load allowlist patterns
  for (const auto& p : config_.allowlist_patterns) {
    allowlist_.AddPattern(p);
  }
}

void ExecApprovalManager::SetApprovalHandler(ApprovalCallback handler) {
  std::lock_guard<std::mutex> lock(mu_);
  approval_handler_ = std::move(handler);
}

ApprovalDecision ExecApprovalManager::RequestApproval(
    const std::string& command,
    const std::string& cwd,
    const std::string& agent_id,
    const std::string& session_key) {
  // If ask mode is off, auto-approve
  if (config_.ask == AskMode::kOff) {
    return ApprovalDecision::kApproved;
  }

  // If on_miss and command matches allowlist, auto-approve
  if (config_.ask == AskMode::kOnMiss && allowlist_.Matches(command)) {
    return ApprovalDecision::kApproved;
  }

  // Build request
  ApprovalRequest req;
  req.id = generate_request_id();
  req.command = command;
  req.cwd = cwd;
  req.agent_id = agent_id;
  req.session_key = session_key;
  req.created_at = std::chrono::steady_clock::now();
  req.expires_at = req.created_at +
                   std::chrono::seconds(config_.timeout_seconds);

  // Record as pending
  {
    std::lock_guard<std::mutex> lock(mu_);
    pending_[req.id] = req;
  }

  // Notify handler if present
  {
    std::lock_guard<std::mutex> lock(mu_);
    if (approval_handler_) {
      approval_handler_(req);
    }
  }

  // For now, auto-approve (real impl would block/poll)
  Resolve(req.id, ApprovalDecision::kApproved, "auto");
  return ApprovalDecision::kApproved;
}

bool ExecApprovalManager::Resolve(const std::string& request_id,
                                  ApprovalDecision decision,
                                  const std::string& resolved_by) {
  std::lock_guard<std::mutex> lock(mu_);

  auto it = pending_.find(request_id);
  if (it == pending_.end()) {
    return false;
  }

  ApprovalResolved res;
  res.id = request_id;
  res.decision = decision;
  res.resolved_by = resolved_by;
  res.resolved_at = std::chrono::steady_clock::now();

  resolved_.push_back(res);
  pending_.erase(it);
  return true;
}

std::vector<ApprovalRequest> ExecApprovalManager::PendingRequests() const {
  std::lock_guard<std::mutex> lock(mu_);
  std::vector<ApprovalRequest> result;
  result.reserve(pending_.size());
  for (const auto& [id, req] : pending_) {
    result.push_back(req);
  }
  return result;
}

std::vector<ApprovalResolved> ExecApprovalManager::ResolvedHistory() const {
  std::lock_guard<std::mutex> lock(mu_);
  return resolved_;
}

void ExecApprovalManager::PruneExpired() {
  std::lock_guard<std::mutex> lock(mu_);
  auto now = std::chrono::steady_clock::now();

  std::vector<std::string> to_expire;
  for (const auto& [id, req] : pending_) {
    if (now >= req.expires_at) {
      to_expire.push_back(id);
    }
  }

  for (const auto& id : to_expire) {
    ApprovalResolved res;
    res.id = id;
    res.decision = ApprovalDecision::kTimeout;
    res.resolved_by = "system";
    res.resolved_at = now;
    resolved_.push_back(res);
    pending_.erase(id);
  }
}

std::string ExecApprovalManager::generate_request_id() const {
  thread_local static std::mt19937 gen(std::random_device{}());
  thread_local static std::uniform_int_distribution<uint64_t> dist;
  std::ostringstream ss;
  ss << "req_" << std::hex << dist(gen);
  return ss.str();
}

}  // namespace rimeclaw
