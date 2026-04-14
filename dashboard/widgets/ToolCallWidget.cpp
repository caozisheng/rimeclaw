#include "ToolCallWidget.h"
#include "../DashboardTheme.h"

namespace rimeclaw {

void ToolCallWidget::render(const ChatMessage::ToolCall& tc) {
    using namespace DashboardTheme;

    ImGui::PushStyleColor(ImGuiCol_Header, toVec4(kToolCallBg));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, toVec4(kToolCallBg));
    ImGui::PushStyleColor(ImGuiCol_Text, toVec4(kTextSecondary));

    const char* status = tc.completed ? "done" : "running...";
    char label[256];
    snprintf(label, sizeof(label), "[%s] %s", tc.name.c_str(), status);

    if (ImGui::TreeNode(label)) {
        ImGui::PushStyleColor(ImGuiCol_Text, toVec4(kTextHint));
        if (!tc.input.empty()) {
            ImGui::Text("Input:");
            ImGui::TextWrapped("%s", tc.input.c_str());
        }
        if (tc.completed && !tc.result.empty()) {
            ImGui::Text("Result:");
            if (tc.result.size() > 500) {
                ImGui::TextWrapped("%s...", tc.result.substr(0, 500).c_str());
            } else {
                ImGui::TextWrapped("%s", tc.result.c_str());
            }
        }
        ImGui::PopStyleColor();
        ImGui::TreePop();
    }

    ImGui::PopStyleColor(3);
}

} // namespace rimeclaw
