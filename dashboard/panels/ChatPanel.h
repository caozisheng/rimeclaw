#pragma once
#include "../DashboardState.h"
#include <imgui.h>

namespace rimeclaw {

class ChatPanel {
public:
    void render(DashboardState& state, ImVec2 pos, ImVec2 size,
                class DashboardController& controller);

private:
    void renderSessionList(DashboardState& state, ImVec2 pos, ImVec2 size,
                           DashboardController& controller);
    void renderMessages(DashboardState& state, ImVec2 pos, ImVec2 size);
    void renderInputBar(DashboardState& state, ImVec2 pos, ImVec2 size,
                        DashboardController& controller);
    void renderMessageBubble(DashboardState& state, const ChatMessage& msg, float width);
    void renderToolCall(const ChatMessage::ToolCall& tc);

    bool m_showNewSessionPopup = false;
    char m_newSessionName[256] = {};
};

} // namespace rimeclaw
