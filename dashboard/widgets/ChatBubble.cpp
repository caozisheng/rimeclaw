#include "ChatBubble.h"
#include "../DashboardTheme.h"

namespace rimeclaw {

void ChatBubble::render(const ChatMessage& msg, float maxWidth) {
    using namespace DashboardTheme;

    ImDrawList* draw = ImGui::GetWindowDrawList();
    bool isUser = (msg.role == "user");
    bool isSystem = (msg.role == "system");

    ImU32 bubbleBg = isUser ? kUserBubbleBg : (isSystem ? kError : kAssistBubbleBg);
    float bubbleMaxW = maxWidth * 0.8f;
    float padX = 12.0f;
    float padY = 8.0f;

    // Role label
    ImGui::PushStyleColor(ImGuiCol_Text, toVec4(kTextHint));
    ImGui::Text("%s", isUser ? "You" : (isSystem ? "System" : "Assistant"));
    ImGui::PopStyleColor();

    // Content
    ImVec2 textSize = ImGui::CalcTextSize(msg.content.c_str(), nullptr, false,
                                           bubbleMaxW - padX * 2);
    float bubbleW = textSize.x + padX * 2;
    float bubbleH = textSize.y + padY * 2;

    ImVec2 cursorPos = ImGui::GetCursorScreenPos();
    float bubbleX = isUser ? (cursorPos.x + maxWidth - bubbleW - 8) : cursorPos.x;
    ImVec2 bubbleMin(bubbleX, cursorPos.y);
    ImVec2 bubbleMax(bubbleX + bubbleW, cursorPos.y + bubbleH);

    draw->AddRectFilled(bubbleMin, bubbleMax, bubbleBg, kCornerRadius);

    ImGui::SetCursorScreenPos(ImVec2(bubbleMin.x + padX, bubbleMin.y + padY));
    ImGui::PushTextWrapPos(bubbleMin.x + bubbleW - padX);
    ImGui::PushStyleColor(ImGuiCol_Text, toVec4(kTextPrimary));
    ImGui::TextWrapped("%s", msg.content.c_str());
    ImGui::PopStyleColor();
    ImGui::PopTextWrapPos();

    ImGui::SetCursorScreenPos(ImVec2(cursorPos.x, bubbleMax.y + 4));
}

} // namespace rimeclaw
