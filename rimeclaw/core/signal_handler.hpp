// Copyright 2025-2026 RimeClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <atomic>
#include <functional>

namespace rimeclaw {

class SignalHandler {
 public:
  using ShutdownCallback = std::function<void()>;
  using ReloadCallback = std::function<void()>;

  static void Install(ShutdownCallback on_shutdown,
                      ReloadCallback on_reload = nullptr);
  static void WaitForShutdown();
  static bool ShouldShutdown();

 private:
  static std::atomic<bool> shutdown_requested_;
  static ShutdownCallback shutdown_callback_;
  static ReloadCallback reload_callback_;
  static void signal_handler(int signum);
};

}  // namespace rimeclaw
