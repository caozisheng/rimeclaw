// Copyright 2025-2026 RimeClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string>

namespace rimeclaw {

/// Stub plugin system for future expansion.
/// All methods are no-ops or return default values.
class PluginSystem {
 public:
  PluginSystem() = default;
  ~PluginSystem() = default;

  bool Initialize(const std::string& /*config_path*/) { return true; }
  void Shutdown() {}
  void Reload() {}
  bool IsInitialized() const { return false; }
};

}  // namespace rimeclaw
