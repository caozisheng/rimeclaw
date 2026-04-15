#include "ChatPanel.h"
#include "../DashboardTheme.h"
#include "../DashboardController.h"
#include <cmath>
#include <cstring>

namespace rimeclaw {

void ChatPanel::render(DashboardState& state, ImVec2 pos, ImVec2 size,
                       DashboardController& controller) {
    using namespace DashboardTheme;

    float sessionListW = kSessionListWidth;
    float inputBarH = 60.0f;

    // Session list (left)
    renderSessionList(state, pos, ImVec2(sessionListW, size.y), controller);

    // Messages area (center)
    ImVec2 msgPos(pos.x + sessionListW, pos.y);
    ImVec2 msgSize(size.x - sessionListW, size.y - inputBarH);
    renderMessages(state, msgPos, msgSize);

    // Input bar (bottom)
    ImVec2 inputPos(pos.x + sessionListW, pos.y + size.y - inputBarH);
    ImVec2 inputSize(size.x - sessionListW, inputBarH);
    renderInputBar(state, inputPos, inputSize, controller);
}

void ChatPanel::renderSessionList(DashboardState& state, ImVec2 pos, ImVec2 size,
                                  DashboardController& controller) {
    using namespace DashboardTheme;

    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(size);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, toVec4(kCardBg));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 8));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                             ImGuiWindowFlags_NoDocking;

    if (ImGui::Begin("##SessionList", nullptr, flags)) {
        ImGui::PushStyleColor(ImGuiCol_Text, toVec4(kTextPrimary));
        ImGui::Text("Sessions");
        ImGui::PopStyleColor();

        ImGui::Spacing();

        // New chat button
        ImGui::PushStyleColor(ImGuiCol_Button, toVec4(kPrimary));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, toVec4(kPrimaryLight));
        ImGui::PushStyleColor(ImGuiCol_Text, toVec4(kTextPrimary));
        if (ImGui::Button("+ New Chat", ImVec2(-1, 28))) {
            m_showNewSessionPopup = true;
            std::memset(m_newSessionName, 0, sizeof(m_newSessionName));
        }
        ImGui::PopStyleColor(3);

        // New session popup
        if (m_showNewSessionPopup) {
            ImGui::OpenPopup("NewSession");
            m_showNewSessionPopup = false;
        }
        if (ImGui::BeginPopup("NewSession")) {
            ImGui::Text("Session Name:");
            ImGui::InputText("##NewName", m_newSessionName, sizeof(m_newSessionName));
            if (ImGui::Button("Create") && m_newSessionName[0]) {
                controller.createSession(state, m_newSessionName);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        ImGui::Separator();
        ImGui::Spacing();

        // Session list
        if (state.sessionsDirty) {
            controller.refreshSessions(state);
        }

        for (auto& session : state.sessions) {
            bool isActive = (session.key == state.activeSessionKey);

            ImGui::PushID(session.key.c_str());
            if (isActive) {
                ImGui::PushStyleColor(ImGuiCol_Header, toVec4(kSidebarActiveBg));
                ImGui::PushStyleColor(ImGuiCol_Text, toVec4(kSidebarActive));
            } else {
                ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0, 0, 0, 0));
                ImGui::PushStyleColor(ImGuiCol_Text, toVec4(kTextSecondary));
            }
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, toVec4(kSidebarHover));

            if (ImGui::Selectable(session.displayName.c_str(), isActive,
                                  ImGuiSelectableFlags_None, ImVec2(0, 24))) {
                controller.switchSession(state, session.key);
            }

            ImGui::PopStyleColor(3);
            ImGui::PopID();
        }
    }
    ImGui::End();

    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
}

void ChatPanel::renderMessages(DashboardState& state, ImVec2 pos, ImVec2 size) {
    using namespace DashboardTheme;

    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(size);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, toVec4(kMainBg));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16, 12));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                             ImGuiWindowFlags_NoDocking;

    if (ImGui::Begin("##Messages", nullptr, flags)) {
        float contentW = size.x - 48;

        for (size_t i = 0; i < state.messages.size(); i++) {
            ImGui::PushID(static_cast<int>(i));
            renderMessageBubble(state, state.messages[i], contentW);
            ImGui::PopID();
            ImGui::Spacing();
        }

        // Streaming indicator
        if (state.isStreaming) {
            ImGui::PushStyleColor(ImGuiCol_Text, toVec4(kTextHint));
            float time = (float)ImGui::GetTime();
            int dots = ((int)(time * 3.0f)) % 4;
            const char* dotStr[] = {"", ".", "..", "..."};
            ImGui::Text("Thinking%s", dotStr[dots]);
            ImGui::PopStyleColor();
        }

        // Auto-scroll
        if (state.scrollToBottom) {
            ImGui::SetScrollHereY(1.0f);
            state.scrollToBottom = false;
        }
    }
    ImGui::End();

    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
}

void ChatPanel::renderMessageBubble(DashboardState& state, const ChatMessage& msg, float width) {
    using namespace DashboardTheme;

    ImDrawList* draw = ImGui::GetWindowDrawList();
    bool isUser = (msg.role == "user");
    bool isSystem = (msg.role == "system");

    ImU32 bubbleBg = isUser ? kUserBubbleBg : (isSystem ? kError : kAssistBubbleBg);
    float bubbleMaxW = width * 0.8f;
    float padX = 12.0f;
    float padY = 8.0f;

    // Role label
    ImGui::PushStyleColor(ImGuiCol_Text, toVec4(kTextHint));
    ImGui::Text("%s", isUser ? "You" : (isSystem ? "System" : "Assistant"));
    ImGui::PopStyleColor();

    // Content bubble
    ImVec2 textSize = ImGui::CalcTextSize(msg.content.c_str(), nullptr, false, bubbleMaxW - padX * 2);
    float bubbleW = textSize.x + padX * 2;
    float bubbleH = textSize.y + padY * 2;

    ImVec2 cursorPos = ImGui::GetCursorScreenPos();
    float bubbleX = isUser ? (cursorPos.x + width - bubbleW - 8) : cursorPos.x;
    ImVec2 bubbleMin(bubbleX, cursorPos.y);
    ImVec2 bubbleMax(bubbleX + bubbleW, cursorPos.y + bubbleH);

    draw->AddRectFilled(bubbleMin, bubbleMax, bubbleBg, kCornerRadius);

    // Text content
    ImGui::SetCursorScreenPos(ImVec2(bubbleMin.x + padX, bubbleMin.y + padY));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Text, toVec4(kTextPrimary));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
    ImGui::InputTextMultiline("##msg_content", (char*)msg.content.c_str(), msg.content.size() + 1,
                              ImVec2(textSize.x, textSize.y), 
                              ImGuiInputTextFlags_ReadOnly | ImGuiInputTextFlags_NoHorizontalScroll);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);

    // Advance cursor past bubble
    ImGui::SetCursorScreenPos(ImVec2(cursorPos.x, bubbleMax.y + 4));

    // Tool calls
    int tc_idx = 0;
    for (const auto& tc : msg.toolCalls) {
        ImGui::PushID(tc_idx++);
        renderToolCall(tc);
        ImGui::PopID();
    }

    // Streaming cursor
    if (msg.isStreaming) {
        float time = (float)ImGui::GetTime();
        float alpha = 0.5f + 0.5f * std::sin(time * 6.0f);
        ImU32 cursorCol = IM_COL32(226, 228, 236, (int)(alpha * 255));
        ImVec2 cPos = ImGui::GetCursorScreenPos();
        draw->AddRectFilled(cPos, ImVec2(cPos.x + 8, cPos.y + 16), cursorCol);
    }
}

void ChatPanel::renderToolCall(const ChatMessage::ToolCall& tc) {
    using namespace DashboardTheme;

    ImGui::PushStyleColor(ImGuiCol_Header, toVec4(kToolCallBg));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, toVec4(kToolCallBg));
    ImGui::PushStyleColor(ImGuiCol_Text, toVec4(kTextSecondary));

    const char* status = tc.completed ? "done" : "running...";
    char label[256];
    snprintf(label, sizeof(label), "[%s: %s] %s", tc.name.c_str(), status,
             tc.completed ? "" : "");

    if (ImGui::TreeNode(label)) {
        ImGui::PushStyleColor(ImGuiCol_Text, toVec4(kTextHint));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
        
        if (!tc.input.empty()) {
            ImGui::Text("Input:");
            ImVec2 inSize = ImGui::CalcTextSize(tc.input.c_str(), nullptr, false, ImGui::GetContentRegionAvail().x);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
            ImGui::InputTextMultiline("##tc_in", (char*)tc.input.c_str(), tc.input.size() + 1,
                                      ImVec2(-1, inSize.y),
                                      ImGuiInputTextFlags_ReadOnly | ImGuiInputTextFlags_NoHorizontalScroll);
            ImGui::PopStyleVar();
        }
        if (tc.completed && !tc.result.empty()) {
            ImGui::Text("Result:");
            // Truncate long results
            std::string resStr = tc.result.size() > 500 ? tc.result.substr(0, 500) + "..." : tc.result;
            ImVec2 outSize = ImGui::CalcTextSize(resStr.c_str(), nullptr, false, ImGui::GetContentRegionAvail().x);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
            ImGui::InputTextMultiline("##tc_out", (char*)resStr.c_str(), resStr.size() + 1,
                                      ImVec2(-1, outSize.y),
                                      ImGuiInputTextFlags_ReadOnly | ImGuiInputTextFlags_NoHorizontalScroll);
            ImGui::PopStyleVar();
        }
        ImGui::PopStyleColor(2);
        ImGui::TreePop();
    }

    ImGui::PopStyleColor(3);
}

void ChatPanel::renderInputBar(DashboardState& state, ImVec2 pos, ImVec2 size,
                               DashboardController& controller) {
    using namespace DashboardTheme;

    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(size);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, toVec4(kCardBg));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 8));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                             ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking;

    if (ImGui::Begin("##InputBar", nullptr, flags)) {
        float btnW = 70.0f;
        float inputW = size.x - btnW - 40;

        ImGui::PushStyleColor(ImGuiCol_FrameBg, toVec4(kInputBg));
        ImGui::PushStyleColor(ImGuiCol_Border, toVec4(kInputBorder));
        ImGui::PushStyleColor(ImGuiCol_Text, toVec4(kTextPrimary));
        ImGui::PushItemWidth(inputW);

        ImGuiInputTextFlags inputFlags = ImGuiInputTextFlags_EnterReturnsTrue;
        bool enterPressed = ImGui::InputText("##ChatInput", state.inputBuf,
                                              sizeof(state.inputBuf), inputFlags);
        ImGui::PopItemWidth();
        ImGui::PopStyleColor(3);

        ImGui::SameLine();

        if (state.isStreaming) {
            ImGui::PushStyleColor(ImGuiCol_Button, toVec4(kError));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.9f, 0.2f, 0.2f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, toVec4(kTextPrimary));
            if (ImGui::Button("Stop", ImVec2(btnW, 0))) {
                controller.stopGeneration(state);
            }
            ImGui::PopStyleColor(3);
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, toVec4(kPrimary));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, toVec4(kPrimaryLight));
            ImGui::PushStyleColor(ImGuiCol_Text, toVec4(kTextPrimary));
            bool sendClicked = ImGui::Button("Send", ImVec2(btnW, 0));

            if ((enterPressed || sendClicked) && state.inputBuf[0]) {
                std::string text = state.inputBuf;
                std::memset(state.inputBuf, 0, sizeof(state.inputBuf));
                controller.sendMessage(state, text);
            }
            ImGui::PopStyleColor(3);
        }
    }
    ImGui::End();

    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
}

} // namespace rimeclaw
