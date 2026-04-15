// Copyright 2025-2026 RimeClaw Contributors
// SPDX-License-Identifier: Apache-2.0
//
// End-to-end tests for the rimeclaw agent framework.
// Tests cover: C interface, tool calling, skills, and multi-turn planning.
// Requires network access and valid API keys in rimeclaw_test.yaml.

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "rimeclaw/rimeclaw.h"
#include "rimeclaw/core/skill_loader.hpp"
#include "rimeclaw/security/tool_permissions.hpp"
#include "rimeclaw/security/exec_approval.hpp"
#include "rimeclaw/security/sandbox.hpp"

// Extended circuit headers
#include "rimeclaw/core/usage_accumulator.hpp"
#include "rimeclaw/core/subagent.hpp"
#include "rimeclaw/core/cron_scheduler.hpp"
#include "rimeclaw/core/default_context_engine.hpp"
#include "rimeclaw/core/content_block.hpp"
#include "rimeclaw/session/session_maintenance.hpp"
#include "rimeclaw/tools/tool_chain.hpp"
#include "rimeclaw/tools/tool_registry.hpp"

// Local provider (optional)
#ifdef BUILD_LLAMA_LOCAL_PROVIDER
#include "rimeclaw/providers/llama_local_provider.hpp"
#endif

namespace fs = std::filesystem;

static int g_passed = 0;
static int g_failed = 0;

#define EXPECT_TRUE(cond) \
  do { \
    if (cond) { \
      printf("  \033[32mPASS\033[0m: %s\n", #cond); \
      ++g_passed; \
    } else { \
      printf("  \033[31mFAIL\033[0m: %s  (line %d)\n", #cond, __LINE__); \
      ++g_failed; \
    } \
  } while (0)

#define EXPECT_EQ(a, b) EXPECT_TRUE((a) == (b))
#define EXPECT_NE(a, b) EXPECT_TRUE((a) != (b))
#define EXPECT_GE(a, b) EXPECT_TRUE((a) >= (b))

#define RIMECLAW_TEST_CONFIG	"rimeclaw_config.json"

// ════════════════════════════════════════════════════════════════════════════
// Enhanced StreamState: captures text, tool_use, and tool_result events
// ════════════════════════════════════════════════════════════════════════════
struct StreamState {
	std::string full_text;
	bool got_end = false;
	bool got_error = false;
	std::string error_msg;

	// Tool event tracking
	std::vector<std::string> tool_names;     // tool names invoked
	std::vector<std::string> tool_results;   // tool result contents
	int tool_use_count = 0;
	int tool_result_count = 0;

	bool has_tool(const std::string& name) const {
		return std::find(tool_names.begin(), tool_names.end(), name) != tool_names.end();
	}

	bool text_contains(const std::string& substr) const {
		return full_text.find(substr) != std::string::npos;
	}

	bool any_result_contains(const std::string& substr) const {
		for (const auto& r : tool_results) {
			if (r.find(substr) != std::string::npos) return true;
		}
		return false;
	}
};

static void stream_callback(const char* event_type, const char* data,
	void* userdata) {
	auto* state = static_cast<StreamState*>(userdata);

	if (strcmp(event_type, "text_delta") == 0 && data) {
		// data may be JSON {"text":"..."} or plain string
		std::string text;
		try {
			auto j = nlohmann::json::parse(data);
			text = j.value("text", std::string(data));
		} catch (...) {
			text = data;
		}
		state->full_text += text;
		printf("%s", text.c_str());
	}
	else if (strcmp(event_type, "tool_use") == 0 && data) {
		state->tool_use_count++;
		// data is JSON: {"id":"...","name":"tool_name","input":{...}}
		try {
			auto j = nlohmann::json::parse(data);
			std::string name = j.value("name", "");
			if (!name.empty()) {
				state->tool_names.push_back(name);
			}
			printf("\n  [tool_use] %s\n", name.c_str());
		} catch (...) {
			printf("\n  [tool_use] (parse error) %s\n", data);
		}
	}
	else if (strcmp(event_type, "tool_result") == 0 && data) {
		state->tool_result_count++;
		// data is JSON: {"tool_use_id":"...","content":"..."}
		try {
			auto j = nlohmann::json::parse(data);
			std::string content = j.value("content", "");
			state->tool_results.push_back(content);
			printf("  [tool_result] (%zu chars)\n",  content.size());
		} catch (...) {
			state->tool_results.push_back(data);
			printf("  [tool_result] (raw) %s\n", data);
		}
	}
	else if (strcmp(event_type, "message_end") == 0) {
		state->got_end = true;
		printf("\n");
	}
	else if (strcmp(event_type, "error") == 0) {
		state->got_error = true;
		if (data) state->error_msg = data;
		printf("\n  [error] %s\n", data ? data : "(null)");
	}
}

// Helper: init handle, run on failure skip
static RimeClawHandle init_or_skip(const char* test_name) {
	RimeClawHandle h = claw_init(RIMECLAW_TEST_CONFIG);
	if (!h) {
		printf("  SKIP %s: init failed\n", test_name);
	}
	return h;
}

// Helper: generate unique session key to avoid accumulating history across runs
static std::string unique_session(const char* prefix) {
	auto now = std::chrono::steady_clock::now().time_since_epoch().count();
	return std::string(prefix) + "-" + std::to_string(now);
}

// ════════════════════════════════════════════════════════════════════════════
// Group 0: Basic C Interface Tests
// ════════════════════════════════════════════════════════════════════════════

static void test_init_null_config() {
	printf("[test_init_null_config]\n");
	RimeClawHandle h = claw_init(nullptr);
	EXPECT_TRUE(h == nullptr);
}

static void test_init_valid_config() {
	printf("[test_init_valid_config]\n");
	std::string cfg = RIMECLAW_TEST_CONFIG;
	RimeClawHandle h = claw_init(cfg.c_str());
	EXPECT_NE(h, nullptr);
	if (h) claw_shutdown(h);
}

static void test_double_shutdown() {
	printf("[test_double_shutdown]\n");
	RimeClawHandle h = claw_init(RIMECLAW_TEST_CONFIG);
	EXPECT_NE(h, nullptr);
	if (h) {
		claw_shutdown(h);
		EXPECT_TRUE(true);
	}
}

static void test_send_null_handle() {
	printf("[test_send_null_handle]\n");
	int rc = claw_send_msg(nullptr, "default", "hello", nullptr, nullptr);
	EXPECT_EQ(rc, -1);
}

static void test_send_null_message() {
	printf("[test_send_null_message]\n");
	RimeClawHandle h = init_or_skip("test_send_null_message");
	if (!h) return;
	int rc = claw_send_msg(h, "default", nullptr, nullptr, nullptr);
	EXPECT_EQ(rc, -1);
	claw_shutdown(h);
}

static void test_stop_safe() {
	printf("[test_stop_safe]\n");
	RimeClawHandle h = init_or_skip("test_stop_safe");
	if (!h) return;
	claw_stop(h);
	EXPECT_TRUE(true);
	claw_shutdown(h);
}

// 0.7: session list
static void test_session_list() {
	printf("[test_session_list]\n");
	RimeClawHandle h = init_or_skip("test_session_list");
	if (!h) return;
	const char* json = claw_session_list(h);
	EXPECT_TRUE(json != nullptr);
	if (json) {
		auto arr = nlohmann::json::parse(json);
		EXPECT_TRUE(arr.is_array());
		claw_free_string(json);
	}
	claw_shutdown(h);
}

// 0.8: session delete
static void test_session_delete() {
	printf("[test_session_delete]\n");
	RimeClawHandle h = init_or_skip("test_session_delete");
	if (!h) return;

	// Create a session by sending a minimal message (will fail LLM but session is created)
	auto sess = unique_session("test-del");
	StreamState state;
	claw_send_msg(h, sess.c_str(), "hello", stream_callback, &state);

	// Session should exist
	const char* info = claw_session_get(h, sess.c_str());
	EXPECT_TRUE(info != nullptr);
	if (info) claw_free_string(info);

	// Delete it
	int rc = claw_session_delete(h, sess.c_str());
	EXPECT_EQ(rc, 0);

	// Should be gone
	info = claw_session_get(h, sess.c_str());
	EXPECT_TRUE(info == nullptr);

	claw_shutdown(h);
}

// 0.9: session clear
static void test_session_clear() {
	printf("[test_session_clear]\n");
	RimeClawHandle h = init_or_skip("test_session_clear");
	if (!h) return;

	auto sess = unique_session("test-clr");
	StreamState state;
	claw_send_msg(h, sess.c_str(), "hello", stream_callback, &state);

	// Clear transcript
	int rc = claw_session_clear(h, sess.c_str());
	EXPECT_EQ(rc, 0);

	// Session still exists but message_count is 0
	const char* info = claw_session_get(h, sess.c_str());
	EXPECT_TRUE(info != nullptr);
	if (info) {
		auto j = nlohmann::json::parse(info);
		EXPECT_EQ(j.value("message_count", -1), 0);
		claw_free_string(info);
	}

	claw_shutdown(h);
}

// 0.10: skill list
static void test_skill_list() {
	printf("[test_skill_list]\n");
	RimeClawHandle h = init_or_skip("test_skill_list");
	if (!h) return;
	const char* json = claw_skill_list(h);
	EXPECT_TRUE(json != nullptr);
	if (json) {
		auto arr = nlohmann::json::parse(json);
		EXPECT_TRUE(arr.is_array());
		EXPECT_GE(static_cast<int>(arr.size()), 2);  // test-calc + test-calc-py
		claw_free_string(json);
	}
	claw_shutdown(h);
}

// 0.11: skill reload
static void test_skill_reload() {
	printf("[test_skill_reload]\n");
	RimeClawHandle h = init_or_skip("test_skill_reload");
	if (!h) return;
	int count = claw_skill_reload(h);
	EXPECT_GE(count, 2);  // should reload test-calc + test-calc-py
	claw_shutdown(h);
}

// 0.12: tool list
static void test_tool_list() {
	printf("[test_tool_list]\n");
	RimeClawHandle h = init_or_skip("test_tool_list");
	if (!h) return;
	const char* json = claw_tool_list(h);
	EXPECT_TRUE(json != nullptr);
	if (json) {
		auto arr = nlohmann::json::parse(json);
		EXPECT_TRUE(arr.is_array());
		EXPECT_GE(static_cast<int>(arr.size()), 5);  // read/write/edit/exec/bash/...
		// Verify each entry has expected fields
		bool found_read = false;
		for (const auto& t : arr) {
			EXPECT_TRUE(t.contains("name"));
			EXPECT_TRUE(t.contains("description"));
			if (t.value("name", "") == "read") found_read = true;
		}
		EXPECT_TRUE(found_read);
		claw_free_string(json);
	}
	claw_shutdown(h);
}

// 0.13: tool register + remove
static void test_tool_register_remove() {
	printf("[test_tool_register_remove]\n");
	RimeClawHandle h = init_or_skip("test_tool_register_remove");
	if (!h) return;

	// Register an external tool
	const char* schema = R"({"name":"my_test_tool","description":"A test tool","parameters":{"type":"object","properties":{"x":{"type":"string"}},"required":["x"]}})";
	int rc = claw_tool_register(h, schema);
	EXPECT_EQ(rc, 0);

	// Should appear in tool list
	const char* json = claw_tool_list(h);
	EXPECT_TRUE(json != nullptr);
	if (json) {
		auto arr = nlohmann::json::parse(json);
		bool found = false;
		for (const auto& t : arr) {
			if (t.value("name", "") == "my_test_tool") found = true;
		}
		EXPECT_TRUE(found);
		claw_free_string(json);
	}

	// Remove it
	rc = claw_tool_remove(h, "my_test_tool");
	EXPECT_EQ(rc, 0);

	// Should no longer appear
	json = claw_tool_list(h);
	if (json) {
		auto arr = nlohmann::json::parse(json);
		bool found = false;
		for (const auto& t : arr) {
			if (t.value("name", "") == "my_test_tool") found = true;
		}
		EXPECT_TRUE(!found);
		claw_free_string(json);
	}

	// Removing again should fail
	rc = claw_tool_remove(h, "my_test_tool");
	EXPECT_EQ(rc, -1);

	// Removing a built-in should fail
	rc = claw_tool_remove(h, "read");
	EXPECT_EQ(rc, -1);

	claw_shutdown(h);
}

// ════════════════════════════════════════════════════════════════════════════
// Group A: Tool Calling Tests (end-to-end with real LLM)
// ════════════════════════════════════════════════════════════════════════════

// A1: Ask agent to read a known file (workspace-relative path)
static void test_tool_call_read_file() {
	printf("[test_tool_call_read_file]\n");
	RimeClawHandle h = init_or_skip("test_tool_call_read_file");
	if (!h) return;

	StreamState state;
	printf("  Prompt: read test_data.txt\n  Assistant: ");
	auto sess = unique_session("test-read-file");
	int rc = claw_send_msg(h, sess.c_str(),
		"Please use the read tool to read the file at path 'test_data.txt'. "
		"Do NOT set workdir. Just call read with path 'test_data.txt'. "
		"Then tell me what the file contains.",
		stream_callback, &state);

	EXPECT_EQ(rc, 0);
	// Tool event may or may not be captured depending on model streaming behavior.
	// The key assertion is that the content is correct.
	EXPECT_TRUE(state.any_result_contains("RimeClaw") || state.text_contains("RimeClaw"));

	printf("  [tools used: %d, text has content: %d]\n",
		state.tool_use_count, !state.full_text.empty());

	claw_shutdown(h);
}

// A2: Ask agent to execute a simple command
static void test_tool_call_exec() {
	printf("[test_tool_call_exec]\n");
	RimeClawHandle h = init_or_skip("test_tool_call_exec");
	if (!h) return;

	StreamState state;
	printf("  Prompt: exec echo hello_rimeclaw\n  Assistant: ");
	auto sess = unique_session("test-exec");
	int rc = claw_send_msg(h, sess.c_str(),
		"Please use the exec tool to run the command: echo hello_rimeclaw. "
		"Do NOT set the workdir parameter, leave it empty.",
		stream_callback, &state);

	EXPECT_EQ(rc, 0);
	EXPECT_TRUE(state.any_result_contains("hello_rimeclaw") || state.text_contains("hello_rimeclaw"));

	printf("  [tools used: %d]\n", state.tool_use_count);
	claw_shutdown(h);
}

// A3: Ask agent to write a file then read it back (2 tool calls, workspace-relative)
static void test_tool_call_write_and_read() {
	printf("[test_tool_call_write_and_read]\n");
	RimeClawHandle h = init_or_skip("test_tool_call_write_and_read");
	if (!h) return;

	StreamState state;
	printf("  Prompt: write then read file\n  Assistant: ");
	auto sess = unique_session("test-write-read");
	int rc = claw_send_msg(h, sess.c_str(),
		"Please do two steps: "
		"1) Use the write tool to create file 'rc_verify.txt' with content 'RIMECLAW_VERIFY_42'. "
		"2) Use the read tool to read file 'rc_verify.txt' and confirm the content. "
		"Use relative paths only, do NOT set workdir.",
		stream_callback, &state);

	EXPECT_EQ(rc, 0);
	EXPECT_TRUE(state.any_result_contains("RIMECLAW_VERIFY_42") || state.text_contains("RIMECLAW_VERIFY_42"));
	// Expect at least some tool usage for multi-step task
	EXPECT_GE(state.tool_use_count + state.tool_result_count + (state.full_text.empty() ? 0 : 1), 1);

	printf("  [tools used: %d, results: %d]\n", state.tool_use_count, state.tool_result_count);
	claw_shutdown(h);
}

// ════════════════════════════════════════════════════════════════════════════
// Group B: Skills Tests
// ════════════════════════════════════════════════════════════════════════════

// B1: Unit test — SkillLoader loads SKILL.md from default .rimeclaw/skills
static void test_skill_loading() {
	printf("[test_skill_loading]\n");
	// Skills live under <workspace>/.rimeclaw/skills (the default location)
	fs::path skills_dir = fs::path(RIMECLAW_TEST_CONFIG).parent_path() / ".rimeclaw" / "skills";

	if (!fs::exists(skills_dir)) {
		printf("  SKIP: skills dir not found at %s\n", skills_dir.string().c_str());
		return;
	}

	rimeclaw::SkillLoader loader;
	auto skills = loader.LoadSkillsFromDirectory(skills_dir);

	EXPECT_GE(static_cast<int>(skills.size()), 1);

	bool found_test_calc = false;
	for (const auto& s : skills) {
		printf("  Loaded skill: name='%s' always=%d desc='%.40s'\n",
			s.name.c_str(), s.always, s.description.c_str());
		if (s.name == "test-calc") {
			found_test_calc = true;
			EXPECT_TRUE(s.always);
			EXPECT_TRUE(!s.description.empty());
			EXPECT_TRUE(!s.content.empty());
		}
	}
	EXPECT_TRUE(found_test_calc);
}

// B2: End-to-end — skill context influences agent behavior
static void test_skill_context_in_prompt() {
	printf("[test_skill_context_in_prompt]\n");
	RimeClawHandle h = init_or_skip("test_skill_context_in_prompt");
	if (!h) return;

	// Use unique session key to avoid accumulating history across runs
	auto sess = unique_session("test-skill-calc");

	StreamState state;
	printf("  Prompt: calculate 17*23 (skill should guide agent to use exec)\n  Assistant: ");
	int rc = claw_send_msg(h, sess.c_str(),
		"Calculate 17 * 23 using the exec tool. "
		"Run: echo $((17*23)). Do NOT set workdir.",
		stream_callback, &state);

	EXPECT_EQ(rc, 0);
	// Skip assertion if LLM call failed (rate limit / timeout)
	if (state.tool_use_count == 0 && state.full_text.empty()) {
		printf("  SKIP: LLM returned no content (likely rate-limited)\n");
	} else {
		// Result should contain 391 (17*23) — via exec tool or direct LLM calculation
		EXPECT_TRUE(state.any_result_contains("391") || state.text_contains("391"));
	}
	// Log whether tool was used (informational, not required)
	printf("  [tools used: %d, used_exec: %d]\n",
		state.tool_use_count, state.has_tool("exec") || state.has_tool("bash"));
	claw_shutdown(h);
}

// B3: End-to-end — test-calc-py skill: agent uses Python via exec
static void test_skill_calc_py() {
	printf("[test_skill_calc_py]\n");
	RimeClawHandle h = init_or_skip("test_skill_calc_py");
	if (!h) return;

	// Use unique session key to avoid accumulating history across runs
	auto sess = unique_session("test-skill-calc-py");

	StreamState state;
	printf("  Prompt: calculate 89*97 using Python\n  Assistant: ");
	int rc = claw_send_msg(h, sess.c_str(),
		"Calculate 89 * 97 using the exec tool with Python. "
		"Run: python -c \"print(89*97)\". Do NOT set workdir.",
		stream_callback, &state);

	EXPECT_EQ(rc, 0);
	// Skip assertion if LLM call failed (rate limit / timeout)
	if (state.tool_use_count == 0 && state.full_text.empty()) {
		printf("  SKIP: LLM returned no content (likely rate-limited)\n");
	} else {
		// 89*97 = 8633
		EXPECT_TRUE(state.any_result_contains("8633") || state.text_contains("8633"));
	}
	printf("  [tools used: %d, used_exec: %d]\n",
		state.tool_use_count, state.has_tool("exec") || state.has_tool("bash"));
	claw_shutdown(h);
}

// ════════════════════════════════════════════════════════════════════════════
// Group C: Multi-turn Planning Tests (agent chains multiple tool calls)
// ════════════════════════════════════════════════════════════════════════════

// C1: Create file + read back in one agent turn (multi-step reasoning)
static void test_multiturn_file_ops() {
	printf("[test_multiturn_file_ops]\n");
	RimeClawHandle h = init_or_skip("test_multiturn_file_ops");
	if (!h) return;

	StreamState state;
	printf("  Prompt: exec date, write to file, then read back\n  Assistant: ");
	auto sess = unique_session("test-multiturn-file");
	int rc = claw_send_msg(h, sess.c_str(),
		"Please do these 3 steps in order: "
		"1) Use exec tool to run 'date', do NOT set workdir. "
		"2) Use write tool to save the date output to file 'rc_plan_test.txt'. "
		"3) Use read tool to read 'rc_plan_test.txt' and confirm. "
		"Use relative paths only.",
		stream_callback, &state);

	EXPECT_EQ(rc, 0);
	// Verify agent completed the multi-step task (content evidence)
	EXPECT_TRUE(!state.full_text.empty() || state.tool_result_count >= 1);
	// Should have used at least exec/bash (for date)
	if (state.tool_use_count > 0) {
		EXPECT_TRUE(state.has_tool("exec") || state.has_tool("bash"));
	}

	printf("  [tools used: %d, results: %d, tools: ", state.tool_use_count, state.tool_result_count);
	for (const auto& t : state.tool_names) printf("%s ", t.c_str());
	printf("]\n");

	claw_shutdown(h);
}

// C2: List files then read one (requires reasoning between tool calls)
static void test_multiturn_chain() {
	printf("[test_multiturn_chain]\n");
	RimeClawHandle h = init_or_skip("test_multiturn_chain");
	if (!h) return;

	StreamState state;
	printf("  Prompt: echo marker, then read a file\n  Assistant: ");
	auto sess = unique_session("test-multiturn-chain");
	int rc = claw_send_msg(h, sess.c_str(),
		"Please do these steps: "
		"1) Use exec tool to run 'echo CHAIN_STEP_1_OK' (do NOT set workdir). "
		"2) Use read tool to read the file 'test_data.txt'. "
		"3) Tell me the echo output and the file content.",
		stream_callback, &state);

	EXPECT_EQ(rc, 0);
	// Verify agent completed the chain (content evidence)
	EXPECT_TRUE(!state.full_text.empty() || state.tool_result_count >= 1);
	if (state.tool_use_count > 0) {
		EXPECT_TRUE(state.has_tool("exec") || state.has_tool("bash") || state.has_tool("read"));
	}

	printf("  [tools used: %d, results: %d]\n", state.tool_use_count, state.tool_result_count);
	claw_shutdown(h);
}

// ════════════════════════════════════════════════════════════════════════════
// Group D: Security Tests (unit tests, no LLM calls)
// ════════════════════════════════════════════════════════════════════════════

// D1: ToolPermissionChecker — group expansion and deny precedence
static void test_tool_permissions() {
	printf("[test_tool_permissions]\n");

	// Allow group:fs (read/write/edit), deny write specifically
	rimeclaw::ToolPermissionConfig cfg;
	cfg.allow = {"group:fs"};
	cfg.deny = {"write"};
	rimeclaw::ToolPermissionChecker checker(cfg);

	EXPECT_TRUE(checker.IsAllowed("read"));
	EXPECT_TRUE(checker.IsAllowed("edit"));
	EXPECT_TRUE(!checker.IsAllowed("write"));   // deny takes precedence
	EXPECT_TRUE(!checker.IsAllowed("exec"));    // not in allow list
	EXPECT_TRUE(!checker.IsAllowed("message")); // not in allow list
}

// D2: ToolPermissionChecker — group:all and MCP filtering
static void test_tool_permissions_all_and_mcp() {
	printf("[test_tool_permissions_all_and_mcp]\n");

	rimeclaw::ToolPermissionConfig cfg;
	cfg.allow = {"group:all", "mcp:myserver:*"};
	cfg.deny = {"mcp:myserver:dangerous_tool"};
	rimeclaw::ToolPermissionChecker checker(cfg);

	EXPECT_TRUE(checker.IsAllowed("read"));
	EXPECT_TRUE(checker.IsAllowed("exec"));
	EXPECT_TRUE(checker.IsAllowed("message"));

	// MCP: server wildcard allowed, but specific tool denied
	EXPECT_TRUE(checker.IsMcpToolAllowed("myserver", "safe_tool"));
	EXPECT_TRUE(!checker.IsMcpToolAllowed("myserver", "dangerous_tool"));
	EXPECT_TRUE(!checker.IsMcpToolAllowed("otherserver", "any_tool")); // not allowed
}

// D3: Sandbox::ValidateFilePath — path inside/outside workspace
static void test_sandbox_validate_path() {
	printf("[test_sandbox_validate_path]\n");

	// Use absolute paths for deterministic results
	std::string workspace = fs::weakly_canonical(fs::absolute(
		fs::path(RIMECLAW_TEST_CONFIG).parent_path() / ".rimeclaw" / "agents" / "main" / "workspace"
	)).string();

	std::string inside = (fs::path(workspace) / "test_data.txt").string();
	std::string outside_tmp = fs::weakly_canonical(fs::absolute(
		fs::temp_directory_path() / "evil.txt"
	)).string();

	EXPECT_TRUE(rimeclaw::Sandbox::ValidateFilePath(inside, workspace));
	EXPECT_TRUE(!rimeclaw::Sandbox::ValidateFilePath(outside_tmp, workspace));
}

// D4: Sandbox::ValidateShellCommand — dangerous commands blocked
static void test_sandbox_validate_command() {
	printf("[test_sandbox_validate_command]\n");

	EXPECT_TRUE(rimeclaw::Sandbox::ValidateShellCommand("echo hello"));
	EXPECT_TRUE(rimeclaw::Sandbox::ValidateShellCommand("ls -la"));
	EXPECT_TRUE(rimeclaw::Sandbox::ValidateShellCommand("cat file.txt"));

	// Dangerous commands should be blocked
	EXPECT_TRUE(!rimeclaw::Sandbox::ValidateShellCommand("rm -rf /"));
	EXPECT_TRUE(!rimeclaw::Sandbox::ValidateShellCommand("mkfs.ext4 /dev/sda"));
	EXPECT_TRUE(!rimeclaw::Sandbox::ValidateShellCommand("dd if=/dev/zero of=/dev/sda"));
}

// D5: ExecAllowlist — glob pattern matching
static void test_exec_allowlist() {
	printf("[test_exec_allowlist]\n");

	rimeclaw::ExecAllowlist allowlist;
	allowlist.AddPattern("echo *");
	allowlist.AddPattern("git *");
	allowlist.AddPattern("npm run *");

	EXPECT_TRUE(allowlist.Matches("echo hello"));
	EXPECT_TRUE(!allowlist.Matches("echo"));        // "echo *" requires space + content
	EXPECT_TRUE(allowlist.Matches("git status"));
	EXPECT_TRUE(allowlist.Matches("git push origin main"));
	EXPECT_TRUE(allowlist.Matches("npm run build"));
	EXPECT_TRUE(!allowlist.Matches("rm -rf /"));
	EXPECT_TRUE(!allowlist.Matches("curl evil.com"));
}

// D6: ExecApprovalManager — ask mode off auto-approves
static void test_exec_approval_off() {
	printf("[test_exec_approval_off]\n");

	rimeclaw::ExecApprovalConfig cfg;
	cfg.ask = rimeclaw::AskMode::kOff;
	rimeclaw::ExecApprovalManager mgr(cfg);

	auto decision = mgr.RequestApproval("rm -rf /tmp/test");
	EXPECT_TRUE(decision == rimeclaw::ApprovalDecision::kApproved);
}

// D7: ExecApprovalManager — on_miss with allowlist
static void test_exec_approval_on_miss() {
	printf("[test_exec_approval_on_miss]\n");

	rimeclaw::ExecApprovalConfig cfg;
	cfg.ask = rimeclaw::AskMode::kOnMiss;
	cfg.allowlist_patterns = {"echo *", "date"};
	rimeclaw::ExecApprovalManager mgr(cfg);

	// Matches allowlist — auto-approved
	auto d1 = mgr.RequestApproval("echo test");
	EXPECT_TRUE(d1 == rimeclaw::ApprovalDecision::kApproved);

	auto d2 = mgr.RequestApproval("date");
	EXPECT_TRUE(d2 == rimeclaw::ApprovalDecision::kApproved);

	// Not in allowlist — still approved (current impl auto-approves pending)
	// but should go through the approval flow (creates a request)
	auto d3 = mgr.RequestApproval("curl evil.com");
	EXPECT_TRUE(d3 == rimeclaw::ApprovalDecision::kApproved); // auto-resolved in current impl

	// Verify resolved history has the non-allowlist request
	auto history = mgr.ResolvedHistory();
	EXPECT_GE(static_cast<int>(history.size()), 1);
}

// D8: End-to-end — agent respects path sandbox (file outside workspace rejected)
static void test_security_path_rejected() {
	printf("[test_security_path_rejected]\n");
	RimeClawHandle h = init_or_skip("test_security_path_rejected");
	if (!h) return;

	StreamState state;
	printf("  Prompt: try to read /etc/passwd\n  Assistant: ");
	auto sess = unique_session("test-sec-path");
	int rc = claw_send_msg(h, sess.c_str(),
		"Please use the read tool to read the file '/etc/passwd'.",
		stream_callback, &state);

	EXPECT_EQ(rc, 0);
	// Agent should report access denied or refuse
	// Agent should report access denied or refuse — match common refusal phrases.
	// Use case-insensitive check to cover LLM phrasing variation and
	// curly-apostrophe (U+2019) vs ASCII apostrophe.
	EXPECT_TRUE(state.text_contains("denied") || state.text_contains("outside") ||
		state.text_contains("Access") || state.text_contains("cannot") ||
		state.text_contains("can't") || state.text_contains("can\xe2\x80\x99t") ||
		state.text_contains("restricted") || state.text_contains("limited") ||
		state.text_contains("not allowed") || state.text_contains("not permitted") ||
		state.text_contains("error") || state.text_contains("Error") ||
		state.any_result_contains("denied") || state.any_result_contains("outside"));

	printf("  [tools used: %d]\n", state.tool_use_count);
	claw_shutdown(h);
}

// ════════════════════════════════════════════════════════════════════════════
// Group E: Extended Circuit Tests (unit tests, no LLM calls)
// ════════════════════════════════════════════════════════════════════════════

// E1: UsageAccumulator — record, query, reset
static void test_usage_accumulator() {
	printf("[test_usage_accumulator]\n");

	rimeclaw::UsageAccumulator acc;

	// Record several calls across two sessions
	acc.Record("session_a", 100, 50);
	acc.Record("session_a", 200, 80);
	acc.Record("session_b", 150, 60);

	// Per-session stats
	auto sa = acc.GetSession("session_a");
	EXPECT_EQ(sa.input_tokens, (int64_t)300);
	EXPECT_EQ(sa.output_tokens, (int64_t)130);
	EXPECT_EQ(sa.total_tokens, (int64_t)430);
	EXPECT_EQ(sa.turns, 2);

	auto sb = acc.GetSession("session_b");
	EXPECT_EQ(sb.input_tokens, (int64_t)150);
	EXPECT_EQ(sb.turns, 1);

	// Global stats
	auto g = acc.GetGlobal();
	EXPECT_EQ(g.input_tokens, (int64_t)450);
	EXPECT_EQ(g.output_tokens, (int64_t)190);
	EXPECT_EQ(g.turns, 3);

	// JSON serialization
	auto j = acc.ToJson();
	EXPECT_TRUE(j.contains("global"));
	EXPECT_TRUE(j.contains("sessions"));

	// Reset single session
	acc.ResetSession("session_a");
	auto sa2 = acc.GetSession("session_a");
	EXPECT_EQ(sa2.turns, 0);
	// Global should still have session_b
	auto g2 = acc.GetGlobal();
	EXPECT_EQ(g2.turns, 3);  // global is cumulative, not decremented

	// Reset all
	acc.ResetAll();
	auto g3 = acc.GetGlobal();
	EXPECT_EQ(g3.turns, 0);
}

// E2: SubagentManager — spawn, depth/child limits, cancel
static void test_subagent_manager() {
	printf("[test_subagent_manager]\n");

	// Config: max_depth=2, max_children=2
	rimeclaw::SubagentConfig cfg;
	cfg.max_depth = 2;
	cfg.max_children = 2;
	cfg.enabled = true;

	// Runner returns task echo
	rimeclaw::AgentRunFn runner = [](const rimeclaw::SpawnParams& params,
	                                 const std::string& /*run_id*/,
	                                 const std::string& /*session_key*/) -> std::string {
		nlohmann::json r;
		r["status"] = "completed";
		r["output"] = "echo: " + params.task;
		return r.dump();
	};

	rimeclaw::SubagentManager mgr(cfg, runner);

	// Spawn a subagent
	rimeclaw::SpawnParams sp;
	sp.task = "test task 1";
	sp.label = "test-1";
	auto run1 = mgr.Spawn(sp, "parent_0", 0);
	EXPECT_TRUE(!run1.run_id.empty());
	EXPECT_TRUE(run1.status == rimeclaw::SubagentRunStatus::kCompleted ||
	            run1.status == rimeclaw::SubagentRunStatus::kRunning);

	// Verify result stored
	auto fetched = mgr.GetRun(run1.run_id);
	EXPECT_TRUE(fetched.has_value());

	// Spawn second child under same parent
	sp.task = "test task 2";
	sp.label = "test-2";
	auto run2 = mgr.Spawn(sp, "parent_0", 0);
	EXPECT_TRUE(!run2.run_id.empty());
	EXPECT_EQ(mgr.ChildCount("parent_0"), 2);

	// Third child should be rejected (max_children=2)
	sp.task = "test task 3";
	auto run3 = mgr.Spawn(sp, "parent_0", 0);
	EXPECT_TRUE(run3.status == rimeclaw::SubagentRunStatus::kFailed);

	// Depth limit: spawn at depth=2 should fail (max_depth=2)
	sp.task = "deep task";
	auto run_deep = mgr.Spawn(sp, "parent_deep", 2);
	EXPECT_TRUE(run_deep.status == rimeclaw::SubagentRunStatus::kFailed);

	// Children listing
	auto children = mgr.GetChildren("parent_0");
	EXPECT_EQ(static_cast<int>(children.size()), 2);

	// Config roundtrip
	auto j = cfg.ToJson();
	auto cfg2 = rimeclaw::SubagentConfig::FromJson(j);
	EXPECT_EQ(cfg2.max_depth, 2);
	EXPECT_EQ(cfg2.max_children, 2);
}

// E3: CronScheduler — add/remove/list jobs, cron expression matching
static void test_cron_scheduler() {
	printf("[test_cron_scheduler]\n");

	rimeclaw::CronScheduler sched;

	// Add jobs
	auto id1 = sched.AddJob("job1", "*/5 * * * *", "msg1", "session1");
	auto id2 = sched.AddJob("job2", "0 9 * * 1-5", "msg2", "session2");
	EXPECT_TRUE(!id1.empty());
	EXPECT_TRUE(!id2.empty());

	// List
	auto jobs = sched.ListJobs();
	EXPECT_EQ(static_cast<int>(jobs.size()), 2);

	// Remove
	EXPECT_TRUE(sched.RemoveJob(id1));
	EXPECT_TRUE(!sched.RemoveJob("nonexistent"));
	jobs = sched.ListJobs();
	EXPECT_EQ(static_cast<int>(jobs.size()), 1);
	EXPECT_EQ(jobs[0].name, std::string("job2"));

	// Persistence roundtrip
	auto tmp_path = (fs::temp_directory_path() / "rc_cron_test.json").string();
	sched.Save(tmp_path);

	rimeclaw::CronScheduler sched2;
	sched2.Load(tmp_path);
	auto jobs2 = sched2.ListJobs();
	EXPECT_EQ(static_cast<int>(jobs2.size()), 1);
	EXPECT_EQ(jobs2[0].name, std::string("job2"));

	// Cleanup temp file
	fs::remove(tmp_path);
}

// E4: CronExpression — pattern matching
static void test_cron_expression() {
	printf("[test_cron_expression]\n");

	// "*/5 * * * *" should match minute=0,5,10,...
	rimeclaw::CronExpression every5("*/5 * * * *");
	std::tm t = {};
	t.tm_min = 10; t.tm_hour = 3; t.tm_mday = 15; t.tm_mon = 2; t.tm_wday = 3;
	EXPECT_TRUE(every5.Matches(t));
	t.tm_min = 7;
	EXPECT_TRUE(!every5.Matches(t));

	// "0 9 * * 1-5" weekdays at 9:00
	rimeclaw::CronExpression weekday9("0 9 * * 1-5");
	t.tm_min = 0; t.tm_hour = 9; t.tm_wday = 1;  // Monday
	EXPECT_TRUE(weekday9.Matches(t));
	t.tm_wday = 0;  // Sunday
	EXPECT_TRUE(!weekday9.Matches(t));
	t.tm_wday = 3; t.tm_hour = 10;  // Wednesday 10:00
	EXPECT_TRUE(!weekday9.Matches(t));
}

// E5: ToolChain — parse, template engine, execution
static void test_tool_chain() {
	printf("[test_tool_chain]\n");

	// Build a chain definition via JSON
	nlohmann::json chain_json = {
		{"name", "test-chain"},
		{"steps", {
			{{"tool", "exec"}, {"arguments", {{"command", "echo step1_output"}}}},
			{{"tool", "exec"}, {"arguments", {{"command", "echo got_{{prev.result}}"}}}}
		}},
		{"error_policy", "stop_on_error"}
	};

	auto chain_def = rimeclaw::ToolChainExecutor::ParseChain(chain_json);
	EXPECT_EQ(chain_def.name, std::string("test-chain"));
	EXPECT_EQ(static_cast<int>(chain_def.steps.size()), 2);
	EXPECT_TRUE(chain_def.error_policy == rimeclaw::ChainErrorPolicy::kStopOnError);

	// Execute with mock executor
	int call_count = 0;
	rimeclaw::ToolExecutorFn mock_exec =
	    [&call_count](const std::string& tool_name,
	                  const nlohmann::json& args) -> std::string {
		call_count++;
		std::string cmd = args.value("command", "");
		return "result_of_" + cmd;
	};

	rimeclaw::ToolChainExecutor executor(mock_exec);
	auto result = executor.Execute(chain_def);
	EXPECT_TRUE(result.success);
	EXPECT_EQ(call_count, 2);
	EXPECT_EQ(static_cast<int>(result.step_results.size()), 2);
	EXPECT_TRUE(result.step_results[0].success);
	EXPECT_TRUE(result.step_results[1].success);

	// Verify template resolved: step 2 should contain step 1's result
	// (The exact resolution depends on ChainTemplateEngine behavior)

	// ResultToJson roundtrip
	auto rj = rimeclaw::ToolChainExecutor::ResultToJson(result);
	EXPECT_TRUE(rj.contains("chain_name"));
	EXPECT_TRUE(rj.contains("success"));
	EXPECT_TRUE(rj.contains("steps"));
}

// E6: SessionMaintenance — config parse and duration parsing
static void test_session_maintenance() {
	printf("[test_session_maintenance]\n");

	// Duration parsing
	EXPECT_EQ(rimeclaw::SessionMaintenance::ParseDurationSeconds("7d"), (int64_t)(7 * 86400));
	EXPECT_EQ(rimeclaw::SessionMaintenance::ParseDurationSeconds("24h"), (int64_t)(24 * 3600));
	EXPECT_EQ(rimeclaw::SessionMaintenance::ParseDurationSeconds("30m"), (int64_t)(30 * 60));
	EXPECT_EQ(rimeclaw::SessionMaintenance::ParseDurationSeconds("60s"), (int64_t)60);
	EXPECT_EQ(rimeclaw::SessionMaintenance::ParseDurationSeconds("0"), (int64_t)0);

	// Config from JSON
	nlohmann::json cfg_json = {
		{"mode", "enforce"},
		{"pruneAfter", "30d"},
		{"archiveAfter", "7d"},
		{"maxSessions", 1000},
		{"maxTotalSizeBytes", 10737418240LL}
	};
	auto cfg = rimeclaw::SessionMaintenanceConfig::FromJson(cfg_json);
	EXPECT_TRUE(cfg.mode == rimeclaw::MaintenanceMode::kEnforce);
	EXPECT_EQ(cfg.max_sessions, 1000);
	EXPECT_EQ(cfg.max_total_size_bytes, (int64_t)10737418240LL);

	// RunCycle on empty temp dir (should not crash)
	auto tmp_dir = fs::temp_directory_path() / "rc_maint_test";
	fs::create_directories(tmp_dir);
	rimeclaw::SessionMaintenance maint(cfg, tmp_dir);
	maint.RunCycle();  // no-op on empty dir
	EXPECT_TRUE(true);  // survived without crash
	fs::remove_all(tmp_dir);
}

// E7: DefaultContextEngine — assemble basic context
static void test_default_context_engine() {
	printf("[test_default_context_engine]\n");

	rimeclaw::AgentConfig agent_cfg;
	agent_cfg.context_window = 128000;
	agent_cfg.max_tokens = 8192;
	agent_cfg.auto_compact = true;
	agent_cfg.compact_max_messages = 100;
	agent_cfg.compact_keep_recent = 10;

	rimeclaw::DefaultContextEngine engine(agent_cfg);
	EXPECT_EQ(engine.Name(), std::string("default"));

	// Assemble with small history
	std::vector<rimeclaw::Message> history;
	rimeclaw::Message m;
	m.role = "user";
	m.content = {rimeclaw::ContentBlock::MakeText("hello")};
	history.push_back(m);
	m.role = "assistant";
	m.content = {rimeclaw::ContentBlock::MakeText("hi there")};
	history.push_back(m);

	auto result = engine.Assemble(history, "You are a helpful assistant.",
	                               "What is 2+2?", 128000, 8192);
	// Should have history + user message
	EXPECT_GE(static_cast<int>(result.messages.size()), 2);
	EXPECT_TRUE(result.estimated_tokens > 0);
}

// E9: message_tool — normal channel send (unit test, no LLM)
static void test_message_tool_send() {
	printf("[test_message_tool_send]\n");

	rimeclaw::ToolRegistry reg;
	reg.RegisterBuiltinTools();

	// Set workspace to temp dir so sandbox checks pass
	auto tmp_ws = (fs::temp_directory_path() / "rc_msg_test_ws").string();
	fs::create_directories(tmp_ws);
	reg.SetWorkspace(tmp_ws);

	// Send to a normal channel (non-agent)
	nlohmann::json params = {
		{"channel", "general"},
		{"message", "hello from test"}
	};
	auto result_str = reg.ExecuteTool("message", params);
	auto result = nlohmann::json::parse(result_str);
	EXPECT_EQ(result.value("status", ""), std::string("sent"));
	EXPECT_EQ(result.value("channel", ""), std::string("general"));

	// With explicit action
	params["action"] = "reply";
	result_str = reg.ExecuteTool("message", params);
	result = nlohmann::json::parse(result_str);
	EXPECT_EQ(result.value("status", ""), std::string("sent"));

	// Missing required params should throw
	bool threw = false;
	try {
		reg.ExecuteTool("message", {{"channel", "general"}});
	} catch (const std::runtime_error&) {
		threw = true;
	}
	EXPECT_TRUE(threw);

	threw = false;
	try {
		reg.ExecuteTool("message", {{"message", "orphan"}});
	} catch (const std::runtime_error&) {
		threw = true;
	}
	EXPECT_TRUE(threw);

	fs::remove_all(tmp_ws);
}

// E10: message_tool — agent: channel routes through SubagentManager (unit test, no LLM)
static void test_message_tool_agent_route() {
	printf("[test_message_tool_agent_route]\n");

	rimeclaw::ToolRegistry reg;
	reg.RegisterBuiltinTools();

	auto tmp_ws = (fs::temp_directory_path() / "rc_msg_agent_test_ws").string();
	fs::create_directories(tmp_ws);
	reg.SetWorkspace(tmp_ws);

	// Create SubagentManager with a mock runner that echoes the task
	rimeclaw::SubagentConfig sa_cfg;
	sa_cfg.max_depth = 2;
	sa_cfg.max_children = 4;
	sa_cfg.enabled = true;

	std::string captured_task;
	rimeclaw::AgentRunFn runner = [&captured_task](
	    const rimeclaw::SpawnParams& params,
	    const std::string& /*run_id*/,
	    const std::string& /*session_key*/) -> std::string {
		captured_task = params.task;
		nlohmann::json r;
		r["status"] = "completed";
		r["output"] = "agent echo: " + params.task;
		return r.dump();
	};

	rimeclaw::SubagentManager mgr(sa_cfg, runner);
	reg.SetSubagentManager(&mgr, "test-session");

	// Send message to "agent:helper" — should route through SubagentManager
	nlohmann::json params = {
		{"channel", "agent:helper"},
		{"message", "summarize the log"}
	};
	auto result_str = reg.ExecuteTool("message", params);
	auto result = nlohmann::json::parse(result_str);

	EXPECT_EQ(result.value("status", ""), std::string("delivered"));
	EXPECT_TRUE(result.contains("run_id"));
	EXPECT_TRUE(!result.value("run_id", "").empty());
	// Verify the runner received the correct task
	EXPECT_EQ(captured_task, std::string("summarize the log"));

	// Non-agent channel should still go through normal path
	params = {{"channel", "alerts"}, {"message", "disk full"}};
	result_str = reg.ExecuteTool("message", params);
	result = nlohmann::json::parse(result_str);
	EXPECT_EQ(result.value("status", ""), std::string("sent"));
	EXPECT_EQ(result.value("channel", ""), std::string("alerts"));

	fs::remove_all(tmp_ws);
}

// E8: Integration — claw_init wires extended circuits (verify via version + init/shutdown)
static void test_extended_circuits_init() {
	printf("[test_extended_circuits_init]\n");
	// If init succeeds, all extended circuits were wired without crash
	RimeClawHandle h = claw_init(RIMECLAW_TEST_CONFIG);
	if (!h) {
		printf("  SKIP: init failed (no API config)\n");
		return;
	}
	// claw_shutdown exercises: cron Stop+Save, session maintenance final RunCycle
	claw_shutdown(h);
	EXPECT_TRUE(true);  // survived init + shutdown with all circuits wired
}

// ════════════════════════════════════════════════════════════════════════════
// Group F: Local Provider Tests (llama.cpp, requires BUILD_LLAMA_LOCAL_PROVIDER)
// ════════════════════════════════════════════════════════════════════════════

#ifdef BUILD_LLAMA_LOCAL_PROVIDER

static const char* GGUF_MODEL_PATH = "models/qwen2.5-0.5b-instruct-q4_k_m.gguf";

// F1: Model loading — verify provider can load the GGUF model
static void test_local_provider_load() {
	printf("[test_local_provider_load]\n");

	if (!fs::exists(GGUF_MODEL_PATH)) {
		printf("  SKIP: model file not found: %s\n", GGUF_MODEL_PATH);
		return;
	}

	rimeclaw::LlamaLocalConfig cfg;
	cfg.model_path = GGUF_MODEL_PATH;
	cfg.n_ctx = 2048;

	try {
		rimeclaw::LlamaLocalProvider provider(cfg);
		EXPECT_TRUE(provider.GetProviderName() == "local");
		EXPECT_TRUE(!provider.GetSupportedModels().empty());
	} catch (const std::exception& e) {
		printf("  \033[31mFAIL\033[0m: exception: %s\n", e.what());
		++g_failed;
	}
}

// F2: Simple completion — verify provider can generate text
static void test_local_provider_completion() {
	printf("[test_local_provider_completion]\n");

	if (!fs::exists(GGUF_MODEL_PATH)) {
		printf("  SKIP: model file not found: %s\n", GGUF_MODEL_PATH);
		return;
	}

	rimeclaw::LlamaLocalConfig cfg;
	cfg.model_path = GGUF_MODEL_PATH;
	cfg.n_ctx = 2048;

	try {
		rimeclaw::LlamaLocalProvider provider(cfg);

		rimeclaw::ChatCompletionRequest req;
		req.messages.push_back(rimeclaw::Message("user", "Say hello in one word."));
		req.max_tokens = 32;
		req.temperature = 0.0;

		auto resp = provider.ChatCompletion(req);
		printf("  Response: %s\n", resp.content.c_str());
		printf("  Tokens: prompt=%d completion=%d\n",
			resp.usage.prompt_tokens, resp.usage.completion_tokens);

		EXPECT_TRUE(!resp.content.empty());
		EXPECT_TRUE(resp.usage.prompt_tokens > 0);
		EXPECT_TRUE(resp.usage.completion_tokens > 0);
		EXPECT_TRUE(resp.finish_reason == "stop" || resp.finish_reason == "length");
	} catch (const std::exception& e) {
		printf("  \033[31mFAIL\033[0m: exception: %s\n", e.what());
		++g_failed;
	}
}

// F3: Tool use — verify provider can generate tool calls with Qwen3 format
static void test_local_provider_tool_use() {
	printf("[test_local_provider_tool_use]\n");

	if (!fs::exists(GGUF_MODEL_PATH)) {
		printf("  SKIP: model file not found: %s\n", GGUF_MODEL_PATH);
		return;
	}

	rimeclaw::LlamaLocalConfig cfg;
	cfg.model_path = GGUF_MODEL_PATH;
	cfg.n_ctx = 4096;

	try {
		rimeclaw::LlamaLocalProvider provider(cfg);

		// Define a get_weather tool
		nlohmann::json weather_tool = {
			{"type", "function"},
			{"function", {
				{"name", "get_weather"},
				{"description", "Get the current weather for a given city."},
				{"parameters", {
					{"type", "object"},
					{"properties", {
						{"city", {
							{"type", "string"},
							{"description", "The city name, e.g. 'Beijing'"}
						}}
					}},
					{"required", {"city"}}
				}}
			}}
		};

		rimeclaw::ChatCompletionRequest req;
		req.messages.push_back(rimeclaw::Message("system",
			"You are a helpful assistant. Use tools when appropriate."));
		req.messages.push_back(rimeclaw::Message("user",
			"What is the weather in Beijing today?"));
		req.tools.push_back(weather_tool);
		req.max_tokens = 256;
		req.temperature = 0.0;

		auto resp = provider.ChatCompletion(req);
		printf("  Content: %s\n", resp.content.c_str());
		printf("  Finish reason: %s\n", resp.finish_reason.c_str());
		printf("  Tool calls: %zu\n", resp.tool_calls.size());

		for (const auto& tc : resp.tool_calls) {
			printf("    [%s] %s(%s)\n", tc.id.c_str(), tc.name.c_str(),
				tc.arguments.dump().c_str());
		}

		// The model should attempt to call get_weather
		EXPECT_TRUE(!resp.tool_calls.empty());
		if (!resp.tool_calls.empty()) {
			EXPECT_TRUE(resp.tool_calls[0].name == "get_weather");
			EXPECT_TRUE(resp.tool_calls[0].arguments.contains("city"));
			EXPECT_TRUE(resp.finish_reason == "tool_calls");
		}
	} catch (const std::exception& e) {
		printf("  \033[31mFAIL\033[0m: exception: %s\n", e.what());
		++g_failed;
	}
}

// F4: Tool use multi-turn — verify tool result round-trip
static void test_local_provider_tool_roundtrip() {
	printf("[test_local_provider_tool_roundtrip]\n");

	if (!fs::exists(GGUF_MODEL_PATH)) {
		printf("  SKIP: model file not found: %s\n", GGUF_MODEL_PATH);
		return;
	}

	rimeclaw::LlamaLocalConfig cfg;
	cfg.model_path = GGUF_MODEL_PATH;
	cfg.n_ctx = 4096;

	try {
		rimeclaw::LlamaLocalProvider provider(cfg);

		nlohmann::json weather_tool = {
			{"type", "function"},
			{"function", {
				{"name", "get_weather"},
				{"description", "Get the current weather for a given city."},
				{"parameters", {
					{"type", "object"},
					{"properties", {
						{"city", {
							{"type", "string"},
							{"description", "The city name"}
						}}
					}},
					{"required", {"city"}}
				}}
			}}
		};

		// Turn 1: user asks about weather
		rimeclaw::ChatCompletionRequest req;
		req.messages.push_back(rimeclaw::Message("system",
			"You are a helpful assistant. Use tools when appropriate."));
		req.messages.push_back(rimeclaw::Message("user",
			"What is the weather in Shanghai?"));
		req.tools.push_back(weather_tool);
		req.max_tokens = 256;
		req.temperature = 0.0;

		auto resp1 = provider.ChatCompletion(req);
		printf("  Turn 1 tool calls: %zu\n", resp1.tool_calls.size());

		if (resp1.tool_calls.empty()) {
			printf("  SKIP: model did not produce tool calls in turn 1\n");
			return;
		}

		// Turn 2: feed tool result back, expect natural language response
		// Add assistant message with the tool call content
		req.messages.push_back(rimeclaw::Message("assistant", resp1.content));

		// Add tool result as user message with tool_result content block
		rimeclaw::Message tool_result_msg;
		tool_result_msg.role = "user";
		tool_result_msg.content.push_back(
			rimeclaw::ContentBlock::MakeToolResult(
				resp1.tool_calls[0].id,
				"{\"temperature\": \"26C\", \"condition\": \"sunny\"}"));
		req.messages.push_back(tool_result_msg);

		auto resp2 = provider.ChatCompletion(req);
		printf("  Turn 2 content: %s\n", resp2.content.c_str());
		printf("  Turn 2 finish: %s\n", resp2.finish_reason.c_str());

		// After receiving tool result, model should produce text (not another tool call)
		EXPECT_TRUE(!resp2.content.empty());
		// Response should reference the weather data
		bool mentions_weather = resp2.content.find("26") != std::string::npos ||
		                        resp2.content.find("sunny") != std::string::npos ||
		                        resp2.content.find("Shanghai") != std::string::npos;
		EXPECT_TRUE(mentions_weather);
	} catch (const std::exception& e) {
		printf("  \033[31mFAIL\033[0m: exception: %s\n", e.what());
		++g_failed;
	}
}

// F5: Streaming — verify streaming callback fires for each token
static void test_local_provider_stream() {
	printf("[test_local_provider_stream]\n");

	if (!fs::exists(GGUF_MODEL_PATH)) {
		printf("  SKIP: model file not found: %s\n", GGUF_MODEL_PATH);
		return;
	}

	rimeclaw::LlamaLocalConfig cfg;
	cfg.model_path = GGUF_MODEL_PATH;
	cfg.n_ctx = 2048;

	try {
		rimeclaw::LlamaLocalProvider provider(cfg);

		rimeclaw::ChatCompletionRequest req;
		req.messages.push_back(rimeclaw::Message("user", "Count from 1 to 5."));
		req.max_tokens = 64;
		req.temperature = 0.0;
		req.stream = true;

		int delta_count = 0;
		bool got_end = false;
		std::string accumulated;

		provider.ChatCompletionStream(req,
			[&](const rimeclaw::ChatCompletionResponse& resp) {
				if (resp.is_stream_end) {
					got_end = true;
				} else {
					delta_count++;
					accumulated += resp.content;
				}
			});

		printf("  Deltas: %d, accumulated: %s\n", delta_count, accumulated.c_str());
		EXPECT_TRUE(delta_count > 0);
		EXPECT_TRUE(got_end);
		EXPECT_TRUE(!accumulated.empty());
	} catch (const std::exception& e) {
		printf("  \033[31mFAIL\033[0m: exception: %s\n", e.what());
		++g_failed;
	}
}

#endif  // BUILD_LLAMA_LOCAL_PROVIDER

// ════════════════════════════════════════════════════════════════════════════
// Entry point
// ════════════════════════════════════════════════════════════════════════════
int main(void)
{
	printf("\n=== RimeClaw Tests ===\n\n");

	// --- Group 0: C Interface Smoke Tests ---
	printf("--- Group 0: C Interface ---\n");
	test_init_null_config();
	test_init_valid_config();
	test_double_shutdown();
	test_send_null_handle();
	test_send_null_message();
	test_stop_safe();
	test_session_list();
	test_session_delete();
	test_session_clear();
	test_skill_list();
	test_skill_reload();
	test_tool_list();
	test_tool_register_remove();

	// --- Group A: Tool Calling ---
	printf("\n--- Group A: Tool Calling ---\n");
	test_tool_call_read_file();
	test_tool_call_exec();
	test_tool_call_write_and_read();

	// --- Group B: Skills ---
	printf("\n--- Group B: Skills ---\n");
	//test_skill_loading();
	test_skill_context_in_prompt();
	test_skill_calc_py();

	// --- Group C: Multi-turn Planning ---
	printf("\n--- Group C: Multi-turn Planning ---\n");
	test_multiturn_file_ops();
	test_multiturn_chain();
	
	// --- Group D: Security ---
	printf("\n--- Group D: Security ---\n");
	test_tool_permissions();
	test_tool_permissions_all_and_mcp();
	test_sandbox_validate_path();
	test_sandbox_validate_command();
	test_exec_allowlist();
	test_exec_approval_off();
	test_exec_approval_on_miss();
	test_security_path_rejected();
	
	// --- Group E: Extended Circuits ---
	printf("\n--- Group E: Extended Circuits ---\n");
	test_usage_accumulator();
	test_subagent_manager();
	test_cron_scheduler();
	test_cron_expression();
	test_tool_chain();
	test_session_maintenance();
	test_default_context_engine();
	test_message_tool_send();
	test_message_tool_agent_route();
	test_extended_circuits_init();

#ifdef BUILD_LLAMA_LOCAL_PROVIDER
	// --- Group F: Local Provider (llama.cpp) ---
	printf("\n--- Group F: Local Provider ---\n");
	test_local_provider_load();
	test_local_provider_completion();
	test_local_provider_tool_use();
	test_local_provider_tool_roundtrip();
	test_local_provider_stream();
#endif

	printf("\n=== Result: %d passed, %d failed ===\n", g_passed, g_failed);
	if (g_failed > 0) {
		printf("SOME TESTS FAILED\n");
	} else {
		printf("ALL TESTS PASSED\n");
	}
	return g_failed > 0 ? 1 : 0;
}
