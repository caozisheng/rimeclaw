#pragma once
#include "DashboardState.h"
#include <thread>
#include <atomic>
#include <string>

namespace rimeclaw {

class DashboardController {
public:
    ~DashboardController();

    // Lifecycle
    bool init(DashboardState& state, const std::string& configPath);
    void shutdown(DashboardState& state);

    // Per-frame: flush pending events from callback thread -> state
    void pollEvents(DashboardState& state);

    // Sessions
    void refreshSessions(DashboardState& state);
    void switchSession(DashboardState& state, const std::string& key);
    void createSession(DashboardState& state, const std::string& name);
    void deleteSession(DashboardState& state, const std::string& key);
    void clearSession(DashboardState& state, const std::string& key);

    // Chat
    void sendMessage(DashboardState& state, const std::string& text);
    void stopGeneration(DashboardState& state);

    // Skills
    void refreshSkills(DashboardState& state);
    void reloadSkills(DashboardState& state);

    // Tools
    void refreshTools(DashboardState& state);
    void registerTool(DashboardState& state, const std::string& schemaJson);
    void removeTool(DashboardState& state, const std::string& name);

    // Config
    void loadConfig(DashboardState& state);
    void saveConfig(DashboardState& state);

    // Cron (placeholder - requires C API extension)
    void refreshCronJobs(DashboardState& state);
    void addCronJob(DashboardState& state, const CronJob& job);
    void removeCronJob(DashboardState& state, const std::string& id);

    // Log
    void refreshLogs(DashboardState& state);

private:
    std::thread m_chatThread;
    std::atomic<bool> m_chatRunning{false};

    static void streamCallback(const char* eventType, const char* data, void* userdata);
};

} // namespace rimeclaw
