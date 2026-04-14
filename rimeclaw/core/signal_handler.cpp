// Copyright 2025-2026 RimeClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "rimeclaw/core/signal_handler.hpp"

#include <chrono>
#include <csignal>
#include <cstdlib>
#include <thread>

namespace rimeclaw {

std::atomic<bool> SignalHandler::shutdown_requested_{false};
SignalHandler::ShutdownCallback SignalHandler::shutdown_callback_;
SignalHandler::ReloadCallback SignalHandler::reload_callback_;

void SignalHandler::Install(ShutdownCallback on_shutdown,
                            ReloadCallback on_reload) {
  shutdown_callback_ = std::move(on_shutdown);
  reload_callback_   = std::move(on_reload);
  shutdown_requested_ = false;

  std::signal(SIGINT,  signal_handler);
  std::signal(SIGTERM, signal_handler);
#ifndef _WIN32
  std::signal(SIGUSR1, signal_handler);
  std::signal(SIGHUP,  SIG_IGN);
#endif
}

void SignalHandler::WaitForShutdown() {
  while (!shutdown_requested_) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
}

bool SignalHandler::ShouldShutdown() {
  return shutdown_requested_.load();
}

void SignalHandler::signal_handler(int signum) {
#ifndef _WIN32
  if (signum == SIGUSR1) {
    if (reload_callback_) reload_callback_();
    return;
  }
#endif
  shutdown_requested_ = true;
  if (shutdown_callback_) shutdown_callback_();
}

}  // namespace rimeclaw
