// Copyright 2025-2026 RimeClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// RimeClaw C interface
//
// Usage pattern:
//   RimeClawHandle h = claw_init("path/to/rimeclaw_config.json");
//   claw_send_msg(h, "my-session", "Hello!", my_callback, userdata);
//   claw_stop(h);
//   claw_shutdown(h);
// ---------------------------------------------------------------------------

typedef void* RimeClawHandle;

// Event types delivered to the callback.
// "text_delta"   - incremental assistant text chunk (data = UTF-8 string)
// "tool_use"     - tool invocation started       (data = JSON object)
// "tool_result"  - tool result returned           (data = JSON object)
// "message_end"  - turn complete                  (data = NULL)
// "error"        - unrecoverable error             (data = error message string)
typedef void (*RimeClawEventCallback)(const char* event_type,
                                     const char* data,
                                     void* userdata);

// Initialise a RimeClaw instance from a JSON config file.
// Returns NULL on failure.
RimeClawHandle claw_init(const char* config_path);

// Send a user message and process it synchronously (streaming events fired on
// calling thread).  session_key identifies the conversation; pass NULL or ""
// for a default session.
// Returns 0 on success, non-zero on error.
int claw_send_msg(RimeClawHandle handle,
                  const char* session_key,
                  const char* message,
                  RimeClawEventCallback callback,
                  void* userdata);

// Request the agent loop to stop after the current tool call (non-blocking).
void claw_stop(RimeClawHandle handle);

// Destroy the instance and free all resources.
void claw_shutdown(RimeClawHandle handle);

// Return the library version string.
const char* claw_version(void);

// ---------------------------------------------------------------------------
// String management
// ---------------------------------------------------------------------------

// Free a string returned by claw_session_* or claw_skill_* functions.
void claw_free_string(const char* str);

// ---------------------------------------------------------------------------
// Session management
// ---------------------------------------------------------------------------

// List all sessions.  Returns a JSON array string.
// Caller must free with claw_free_string.  Returns NULL on error.
const char* claw_session_list(RimeClawHandle handle);

// Get session info by key.  Returns a JSON object string or NULL if not found.
// Caller must free with claw_free_string.
const char* claw_session_get(RimeClawHandle handle,
                             const char* session_key);

// Load full transcript for a session.  Returns a JSON array of messages:
//   [{"role":"user","content":"Hello"}, {"role":"assistant","content":"Hi"}, ...]
// Text content blocks are flattened to plain strings; tool_use/tool_result
// blocks are included as nested objects.
// Caller must free with claw_free_string.  Returns NULL on error.
const char* claw_session_transcript(RimeClawHandle handle,
                                    const char* session_key);

// Delete a session and its transcript.  Returns 0 on success, -1 on error.
int claw_session_delete(RimeClawHandle handle,
                        const char* session_key);

// Clear a session's transcript but keep the session entry.
// Returns 0 on success, -1 on error.
int claw_session_clear(RimeClawHandle handle,
                       const char* session_key);

// ---------------------------------------------------------------------------
// Skill management
// ---------------------------------------------------------------------------

// List loaded skills.  Returns a JSON array string.
// Caller must free with claw_free_string.  Returns NULL on error.
const char* claw_skill_list(RimeClawHandle handle);

// Reload skills from disk (picks up new/changed SKILL.md files).
// Returns number of skills loaded, or -1 on error.
int claw_skill_reload(RimeClawHandle handle);

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Extract plain text from an event data string.
// For text_delta events the data is JSON like {"text":"..."} — this returns
// the "text" value.  For a plain string (no JSON wrapper) it returns a copy
// of the input unchanged.  Returns NULL if data is NULL.
// Caller must free with claw_free_string.
const char* claw_extract_event_text(const char* event_data);

// ---------------------------------------------------------------------------
// Tool management
// ---------------------------------------------------------------------------

// List registered tools.  Returns a JSON array string.
// Each element: {"name","description","is_external"}.
// Caller must free with claw_free_string.  Returns NULL on error.
const char* claw_tool_list(RimeClawHandle handle);

// Register an external tool at runtime.
// schema_json: JSON string with keys "name","description","parameters".
// Returns 0 on success, -1 on error.
int claw_tool_register(RimeClawHandle handle, const char* schema_json);

// Remove a previously registered external tool.
// Built-in tools cannot be removed.
// Returns 0 on success, -1 on error (not found or built-in).
int claw_tool_remove(RimeClawHandle handle, const char* tool_name);

#ifdef __cplusplus
}
#endif
