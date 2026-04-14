// Copyright 2025-2026 RimeClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

// ============================================================
// RimeClaw project-wide named constants
//
// All "magic numbers" that appear across multiple files must be
// defined here so they can be changed in one place.
// ============================================================

// Fallbacks in case the file is included without CMake's compile definitions
// (e.g., in a standalone IDE configuration or unit test binary).
#ifndef RIMECLAW_VERSION
#define RIMECLAW_VERSION "dev"
#endif
#ifndef RIMECLAW_BUILD_DATE
#define RIMECLAW_BUILD_DATE "unknown"
#endif
#ifndef RIMECLAW_GIT_COMMIT
#define RIMECLAW_GIT_COMMIT "unknown"
#endif

namespace rimeclaw {

// ------------------------------------------------------------
// Version information (injected by CMake at build time)
// ------------------------------------------------------------

/// Release version string (semver, e.g. "0.1.0")
inline constexpr const char* kVersion = RIMECLAW_VERSION;

/// Build date (ISO 8601, e.g. "2026-03-25")
inline constexpr const char* kBuildDate = RIMECLAW_BUILD_DATE;

/// Short git commit hash (e.g. "a1b2c3d")
inline constexpr const char* kGitCommit = RIMECLAW_GIT_COMMIT;

// ------------------------------------------------------------
// Agent / LLM defaults
// ------------------------------------------------------------

/// Hard limit on the number of agent reasoning iterations per request.
/// Scales dynamically 32-160 based on context window.
inline constexpr int kDefaultMaxIterations = 32;
inline constexpr int kMinMaxIterations = 32;
inline constexpr int kMaxMaxIterations = 160;

/// Default LLM temperature (0 = deterministic, 1 = very creative)
inline constexpr double kDefaultTemperature = 0.7;

/// Maximum output tokens to request from the LLM (default: 8192)
inline constexpr int kDefaultMaxTokens = 8192;

/// Context window guard: refuse to send if remaining tokens below this
inline constexpr int kContextWindowMinTokens = 16384;

/// Context window sizes for known model families (tokens)
inline constexpr int kContextWindow4K = 4096;
inline constexpr int kContextWindow8K = 8192;
inline constexpr int kContextWindow16K = 16384;
inline constexpr int kContextWindow32K = 32768;
inline constexpr int kContextWindow128K = 131072;
inline constexpr int kContextWindow200K = 200000;

/// Default context window when model is unknown
inline constexpr int kDefaultContextWindow = 128000;

/// Tool result truncation: max chars for a single tool result
inline constexpr int kToolResultMaxChars = 30000;

/// Tool result truncation: lines to keep at head/tail
inline constexpr int kToolResultKeepLines = 20;

/// Overflow compaction: max retry attempts
inline constexpr int kOverflowCompactionMaxRetries = 3;

/// Maximum transient error retries before giving up
inline constexpr int kMaxTransientRetries = 3;

// ------------------------------------------------------------
// Session compaction defaults
// ------------------------------------------------------------

/// Trigger compaction when message history exceeds this count
inline constexpr int kDefaultCompactMaxMessages = 100;

/// Number of recent messages to retain after compaction
inline constexpr int kDefaultCompactKeepRecent = 20;

/// Trigger compaction when estimated token count exceeds this value
inline constexpr int kDefaultCompactMaxTokens = 100000;

// ------------------------------------------------------------
// Timeout defaults (seconds)
// ------------------------------------------------------------

/// Default per-request timeout for LLM provider HTTP calls
inline constexpr int kDefaultProviderTimeoutSec = 30;

/// Default tool execution timeout
inline constexpr int kDefaultToolTimeoutSec = 30;

/// Default MCP server request timeout
inline constexpr int kDefaultMcpTimeoutSec = 30;

}  // namespace rimeclaw
