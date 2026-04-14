// Copyright 2025-2026 RimeClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string>

namespace rimeclaw {

/// Stub sidecar process manager for future expansion.
/// Manages companion processes alongside the main agent.
class SidecarManager {
 public:
  SidecarManager() = default;
  ~SidecarManager() = default;

  /// Start a sidecar process by name. Returns false (stub).
  bool Start(const std::string& /*name*/,
             const std::string& /*config*/ = {}) {
    return false;
  }

  /// Stop a running sidecar. No-op (stub).
  void Stop(const std::string& /*name*/) {}

  /// Stop all running sidecars. No-op (stub).
  void StopAll() {}

  /// Check if a sidecar is running. Returns false (stub).
  bool IsRunning(const std::string& /*name*/) const { return false; }
};

}  // namespace rimeclaw
