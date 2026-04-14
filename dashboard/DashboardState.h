#pragma once
#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include "rimeclaw.h"

namespace rimeclaw {

enum class NavPage {
    Chat,
    Sessions,
    Agents,
    Skills,
    Config,
    Cron,
    Log
};

// ---------- Chat ----------
struct ChatMessage {
    std::string role;        // "user" | "assistant" | "system"
    std::string content;
    bool isStreaming = false;

    struct ToolCall {
        std::string id;
        std::string name;
        std::string input;
        std::string result;
        bool completed = false;
    };
    std::vector<ToolCall> toolCalls;

    std::string thinkingText;
    bool thinkingCollapsed = true;
};

struct SessionInfo {
    std::string key;
    std::string displayName;
    std::string channel;
    std::string createdAt;
    std::string updatedAt;
    int messageCount = 0;
};

// ---------- Skills ----------
struct SkillInfo {
    std::string name;
    std::string description;
    bool enabled = true;
};

// ---------- Tools ----------
struct ToolInfo {
    std::string name;
    std::string description;
    bool isExternal = false;
};

// ---------- Cron ----------
struct CronJob {
    std::string id;
    std::string name;
    std::string cronExpr;
    std::string message;
    std::string sessionKey;
};

// ---------- Log ----------
struct LogEntry {
    std::string timestamp;
    std::string level;
    std::string message;
};

// ========== Main State ==========
struct DashboardState {
    // Navigation
    NavPage currentPage = NavPage::Chat;

    // RimeClaw handle
    RimeClawHandle handle = nullptr;
    bool initialized = false;
    std::string configPath;

    // Sessions
    std::vector<SessionInfo> sessions;
    std::string activeSessionKey;
    bool sessionsDirty = true;

    // Chat
    std::vector<ChatMessage> messages;
    char inputBuf[4096] = {};
    bool isStreaming = false;
    bool autoScroll = true;
    bool scrollToBottom = false;

    // Skills
    std::vector<SkillInfo> skills;
    bool skillsDirty = true;

    // Tools
    std::vector<ToolInfo> tools;
    bool toolsDirty = true;

    // Cron
    std::vector<CronJob> cronJobs;
    bool cronDirty = true;

    // Config
    std::string configJson;
    char configEditBuf[65536] = {};
    bool configDirty = true;
    bool configModified = false;

    // Agents
    struct AgentConfig {
        std::string model;
        int maxIterations = 32;
        float temperature = 0.7f;
        int maxTokens = 8192;
        int contextWindow = 128000;
        std::string thinking = "off";
        std::vector<std::string> fallbacks;
    };
    AgentConfig agentConfig;

    // Console
    float consoleHeight = 200.0f;
    bool  consoleVisible = true;

    // Log
    std::vector<LogEntry> logEntries;
    int logLevelFilter = 0;  // 0=ALL, 1=DEBUG, 2=INFO, 3=WARN, 4=ERROR
    bool logAutoScroll = true;

    // Thread sync
    std::mutex chatMutex;
    std::string pendingTextDelta;
    bool messageEndPending = false;
    std::string pendingError;
};

} // namespace rimeclaw
