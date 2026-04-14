// Copyright 2025-2026 RimeClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <memory>
#include <string>
#include <vector>

namespace rimeclaw {

/// Minimal plugin descriptor for the registry.
struct PluginInfo {
  std::string name;
  std::string version;
  std::string description;
};

/// Stub plugin registry for future expansion.
/// Discovers and manages available plugins.
class PluginRegistry {
 public:
  PluginRegistry() = default;
  ~PluginRegistry() = default;

  /// Scan plugin directories for available plugins.
  /// Returns empty vector (stub).
  std::vector<PluginInfo> DiscoverPlugins(
      const std::string& /*search_path*/ = {}) {
    return {};
  }

  /// Get info for a specific plugin by name.
  /// Returns nullptr (stub).
  const PluginInfo* GetPlugin(const std::string& /*name*/) const {
    return nullptr;
  }

  /// Get all registered plugins. Returns empty vector (stub).
  std::vector<PluginInfo> GetAll() const { return {}; }

  /// Number of registered plugins. Returns 0 (stub).
  size_t Count() const { return 0; }
};

}  // namespace rimeclaw
