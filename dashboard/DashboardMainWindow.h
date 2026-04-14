#pragma once
#include "imapp/ImAppMainWindow.h"
#include "DashboardState.h"
#include "DashboardController.h"
#include <future>
#include "panels/SidebarPanel.h"
#include "panels/ChatPanel.h"
#include "panels/SessionsPanel.h"
#include "panels/AgentsPanel.h"
#include "panels/SkillsPanel.h"
#include "panels/ConfigPanel.h"
#include "panels/CronPanel.h"
#include "widgets/ConsoleWidget.h"

namespace rimeclaw {

class DashboardMainWindow : public ImApp::MainWindow {
public:
    DashboardMainWindow();
    ~DashboardMainWindow() override;

protected:
    void beforeLoop() override;
    void paint() override;
    void beforeQuit() override;

private:
    DashboardState m_state;
    DashboardController m_controller;

    // Panels
    SidebarPanel m_sidebar;
    ChatPanel m_chatPanel;
    SessionsPanel m_sessionsPanel;
    AgentsPanel m_agentsPanel;
    SkillsPanel m_skillsPanel;
    ConfigPanel m_configPanel;
    CronPanel m_cronPanel;
    ConsoleWidget m_console;

    void applyTheme();
    void renderStatusBar(ImVec2 pos, ImVec2 size);

    std::future<bool> m_fontDownloadFuture;
    bool m_fontLoaded = false;
};

} // namespace rimeclaw
