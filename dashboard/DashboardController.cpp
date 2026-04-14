#include "DashboardController.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <cstring>
#include <spdlog/spdlog.h>

using json = nlohmann::json;

namespace rimeclaw {

DashboardController::~DashboardController() {
    if (m_chatThread.joinable()) m_chatThread.join();
}

bool DashboardController::init(DashboardState& state, const std::string& configPath) {
    state.configPath = configPath;
    state.handle = claw_init(configPath.c_str());
    if (!state.handle) {
        spdlog::error("[dashboard] Failed to init RimeClaw from: {}", configPath);
        return false;
    }
    state.initialized = true;
    spdlog::info("[dashboard] RimeClaw initialized from: {}", configPath);

    // Load initial data
    refreshSessions(state);
    refreshSkills(state);
    refreshTools(state);
    loadConfig(state);

    // Auto-select first session or create default
    if (state.sessions.empty()) {
        createSession(state, "default");
    }
    if (!state.sessions.empty() && state.activeSessionKey.empty()) {
        state.activeSessionKey = state.sessions[0].key;
    }
    return true;
}

void DashboardController::shutdown(DashboardState& state) {
    stopGeneration(state);
    if (m_chatThread.joinable()) m_chatThread.join();
    if (state.handle) {
        claw_shutdown(state.handle);
        state.handle = nullptr;
    }
    state.initialized = false;
}

// ---------- Per-frame event polling ----------

void DashboardController::pollEvents(DashboardState& state) {
    std::lock_guard<std::mutex> lock(state.chatMutex);

    if (!state.pendingTextDelta.empty()) {
        if (!state.messages.empty()) {
            state.messages.back().content += state.pendingTextDelta;
        }
        state.pendingTextDelta.clear();
        if (state.autoScroll) state.scrollToBottom = true;
    }

    if (state.messageEndPending) {
        if (!state.messages.empty()) {
            state.messages.back().isStreaming = false;
        }
        state.isStreaming = false;
        state.messageEndPending = false;
        state.sessionsDirty = true;
    }

    if (!state.pendingError.empty()) {
        ChatMessage errMsg;
        errMsg.role = "system";
        errMsg.content = "Error: " + state.pendingError;
        state.messages.push_back(std::move(errMsg));
        state.pendingError.clear();
        state.isStreaming = false;
    }
}

// ---------- Sessions ----------

void DashboardController::refreshSessions(DashboardState& state) {
    if (!state.handle) return;

    const char* jsonStr = claw_session_list(state.handle);
    if (!jsonStr) return;

    try {
        auto arr = json::parse(jsonStr);
        state.sessions.clear();
        for (auto& item : arr) {
            SessionInfo s;
            s.key         = item.value("key", "");
            s.displayName = item.value("display_name", s.key);
            s.channel     = item.value("channel", "");
            s.createdAt   = item.value("created_at", "");
            s.updatedAt   = item.value("updated_at", "");
            s.messageCount = item.value("message_count", 0);
            state.sessions.push_back(std::move(s));
        }
    } catch (const json::exception& e) {
        spdlog::error("[dashboard] Failed to parse session list: {}", e.what());
    }

    claw_free_string(jsonStr);
    state.sessionsDirty = false;
}

void DashboardController::switchSession(DashboardState& state, const std::string& key) {
    state.activeSessionKey = key;
    state.messages.clear();

    if (!state.handle) return;

    const char* jsonStr = claw_session_transcript(state.handle, key.c_str());
    if (!jsonStr) return;

    try {
        auto arr = json::parse(jsonStr);
        for (auto& item : arr) {
            ChatMessage msg;
            msg.role = item.value("role", "");
            msg.content = item.value("content", "");

            if (item.contains("tool_calls") && item["tool_calls"].is_array()) {
                for (auto& tc : item["tool_calls"]) {
                    if (tc.contains("tool_use_id")) {
                        // tool_result entry — match to existing tool call
                        std::string tuId = tc.value("tool_use_id", "");
                        for (auto& existing : msg.toolCalls) {
                            if (existing.id == tuId) {
                                existing.result = tc.value("content", "");
                                existing.completed = true;
                                break;
                            }
                        }
                    } else {
                        ChatMessage::ToolCall call;
                        call.id = tc.value("id", "");
                        call.name = tc.value("name", "");
                        call.input = tc.value("input", "");
                        call.completed = true;
                        msg.toolCalls.push_back(std::move(call));
                    }
                }
            }

            state.messages.push_back(std::move(msg));
        }
    } catch (const json::exception& e) {
        spdlog::error("[dashboard] Failed to parse transcript: {}", e.what());
    }

    claw_free_string(jsonStr);
    state.scrollToBottom = true;
}

void DashboardController::createSession(DashboardState& state, const std::string& name) {
    // The C API creates sessions on first message send.
    // We just add a local entry and set it active.
    std::string key = "agent:main:" + name;
    SessionInfo s;
    s.key = key;
    s.displayName = name;
    state.sessions.push_back(s);
    switchSession(state, key);
}

void DashboardController::deleteSession(DashboardState& state, const std::string& key) {
    if (!state.handle) return;
    claw_session_delete(state.handle, key.c_str());
    state.sessionsDirty = true;
    refreshSessions(state);

    if (state.activeSessionKey == key) {
        state.activeSessionKey = state.sessions.empty() ? "" : state.sessions[0].key;
        state.messages.clear();
    }
}

void DashboardController::clearSession(DashboardState& state, const std::string& key) {
    if (!state.handle) return;
    claw_session_clear(state.handle, key.c_str());
    if (state.activeSessionKey == key) {
        state.messages.clear();
    }
    state.sessionsDirty = true;
}

// ---------- Chat ----------

void DashboardController::sendMessage(DashboardState& state, const std::string& text) {
    if (state.isStreaming || text.empty() || !state.handle) return;

    {
        std::lock_guard<std::mutex> lock(state.chatMutex);
        ChatMessage userMsg;
        userMsg.role = "user";
        userMsg.content = text;
        state.messages.push_back(std::move(userMsg));

        ChatMessage asstMsg;
        asstMsg.role = "assistant";
        asstMsg.isStreaming = true;
        state.messages.push_back(std::move(asstMsg));
        state.isStreaming = true;
        state.scrollToBottom = true;
    }

    if (m_chatThread.joinable()) m_chatThread.join();
    m_chatRunning = true;

    m_chatThread = std::thread([this, &state, text]() {
        claw_send_msg(state.handle, state.activeSessionKey.c_str(),
                      text.c_str(), streamCallback, &state);
        m_chatRunning = false;
    });
}

void DashboardController::stopGeneration(DashboardState& state) {
    if (state.handle && state.isStreaming) {
        claw_stop(state.handle);
    }
}

void DashboardController::streamCallback(const char* eventType, const char* data, void* userdata) {
    auto* state = static_cast<DashboardState*>(userdata);
    std::lock_guard<std::mutex> lock(state->chatMutex);

    if (std::strcmp(eventType, "text_delta") == 0) {
        // data may be JSON {"text":"..."} — extract plain text
        const char* plain = claw_extract_event_text(data);
        if (plain) {
            state->pendingTextDelta += plain;
            claw_free_string(plain);
        }
    }
    else if (std::strcmp(eventType, "tool_use") == 0) {
        try {
            auto j = json::parse(data);
            ChatMessage::ToolCall tc;
            tc.id    = j.value("id", "");
            tc.name  = j.value("name", "");
            tc.input = j.contains("input") ? j["input"].dump(2) : "";
            if (!state->messages.empty()) {
                state->messages.back().toolCalls.push_back(std::move(tc));
            }
        } catch (...) {}
    }
    else if (std::strcmp(eventType, "tool_result") == 0) {
        try {
            auto j = json::parse(data);
            std::string toolUseId = j.value("tool_use_id", "");
            if (!state->messages.empty()) {
                for (auto& tc : state->messages.back().toolCalls) {
                    if (tc.id == toolUseId) {
                        tc.result = j.value("content", "");
                        tc.completed = true;
                        break;
                    }
                }
            }
        } catch (...) {}
    }
    else if (std::strcmp(eventType, "message_end") == 0) {
        state->messageEndPending = true;
    }
    else if (std::strcmp(eventType, "error") == 0) {
        state->pendingError = data ? data : "Unknown error";
    }
}

// ---------- Skills ----------

void DashboardController::refreshSkills(DashboardState& state) {
    if (!state.handle) return;

    const char* jsonStr = claw_skill_list(state.handle);
    if (!jsonStr) return;

    try {
        auto arr = json::parse(jsonStr);
        state.skills.clear();
        for (auto& item : arr) {
            SkillInfo s;
            s.name        = item.value("name", "");
            s.description = item.value("description", "");
            s.enabled     = item.value("enabled", true);
            state.skills.push_back(std::move(s));
        }
    } catch (...) {}

    claw_free_string(jsonStr);
    state.skillsDirty = false;
}

void DashboardController::reloadSkills(DashboardState& state) {
    if (!state.handle) return;
    claw_skill_reload(state.handle);
    refreshSkills(state);
}

// ---------- Tools ----------

void DashboardController::refreshTools(DashboardState& state) {
    if (!state.handle) return;

    const char* jsonStr = claw_tool_list(state.handle);
    if (!jsonStr) return;

    try {
        auto arr = json::parse(jsonStr);
        state.tools.clear();
        for (auto& item : arr) {
            ToolInfo t;
            t.name        = item.value("name", "");
            t.description = item.value("description", "");
            t.isExternal  = item.value("is_external", false);
            state.tools.push_back(std::move(t));
        }
    } catch (...) {}

    claw_free_string(jsonStr);
    state.toolsDirty = false;
}

void DashboardController::registerTool(DashboardState& state, const std::string& schemaJson) {
    if (!state.handle) return;
    claw_tool_register(state.handle, schemaJson.c_str());
    refreshTools(state);
}

void DashboardController::removeTool(DashboardState& state, const std::string& name) {
    if (!state.handle) return;
    claw_tool_remove(state.handle, name.c_str());
    refreshTools(state);
}

// ---------- Config ----------

void DashboardController::loadConfig(DashboardState& state) {
    std::ifstream f(state.configPath);
    if (!f.is_open()) return;

    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());
    state.configJson = content;
    std::strncpy(state.configEditBuf, content.c_str(), sizeof(state.configEditBuf) - 1);
    state.configEditBuf[sizeof(state.configEditBuf) - 1] = '\0';
    state.configModified = false;
    state.configDirty = false;

    // Parse agent config from JSON
    try {
        auto j = json::parse(content);
        if (j.contains("agent") && j["agent"].is_object()) {
            auto& ag = j["agent"];
            state.agentConfig.model          = ag.value("model", "");
            state.agentConfig.maxIterations  = ag.value("max_iterations", 32);
            state.agentConfig.temperature    = ag.value("temperature", 0.7f);
            state.agentConfig.maxTokens      = ag.value("max_tokens", 8192);
            state.agentConfig.contextWindow  = ag.value("context_window", 128000);
            state.agentConfig.thinking       = ag.value("thinking", "off");
        }
    } catch (...) {}
}

void DashboardController::saveConfig(DashboardState& state) {
    // Validate JSON
    try {
        json::parse(state.configEditBuf);
    } catch (const json::exception& e) {
        spdlog::error("[dashboard] Invalid JSON: {}", e.what());
        return;
    }

    std::ofstream f(state.configPath);
    if (f.is_open()) {
        f << state.configEditBuf;
        state.configModified = false;
        spdlog::info("[dashboard] Config saved to: {}", state.configPath);
    }
}

// ---------- Cron (placeholder) ----------

void DashboardController::refreshCronJobs(DashboardState&) {
    // TODO: requires C API extension
}

void DashboardController::addCronJob(DashboardState&, const CronJob&) {
    // TODO: requires C API extension
}

void DashboardController::removeCronJob(DashboardState&, const std::string&) {
    // TODO: requires C API extension
}

// ---------- Log ----------

void DashboardController::refreshLogs(DashboardState&) {
    // TODO: hook into spdlog sink
}

} // namespace rimeclaw
