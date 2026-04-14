// Copyright 2025-2026 RimeClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <functional>
#include <string>

namespace rimeclaw {

/// Stub hook manager for future expansion.
/// Allows registering and dispatching lifecycle hooks.
class HookManager {
 public:
  HookManager() = default;
  ~HookManager() = default;

  using HookCallback = std::function<void(const std::string&)>;

  /// Register a hook callback for a given event name. No-op (stub).
  void RegisterHook(const std::string& /*event*/,
                    HookCallback /*callback*/) {}

  /// Dispatch an event to all registered hooks. No-op (stub).
  void DispatchHook(const std::string& /*event*/,
                    const std::string& /*payload*/ = {}) {}

  /// Remove all hooks for a given event. No-op (stub).
  void ClearHooks(const std::string& /*event*/) {}

  /// Remove all registered hooks. No-op (stub).
  void ClearAll() {}
};

}  // namespace rimeclaw
