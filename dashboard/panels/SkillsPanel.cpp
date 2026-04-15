#include "SkillsPanel.h"
#include "../DashboardTheme.h"
#include "../DashboardController.h"
#include <cstring>

namespace rimeclaw {

void SkillsPanel::render(DashboardState& state, ImVec2 pos, ImVec2 size,
                         DashboardController& controller) {
    using namespace DashboardTheme;

    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(size);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, toVec4(kMainBg));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20, 16));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                             ImGuiWindowFlags_NoDocking;

    if (ImGui::Begin("##Skills", nullptr, flags)) {
        // Header
        ImGui::PushStyleColor(ImGuiCol_Text, toVec4(kTextPrimary));
        ImGui::Text("Skills Management");
        ImGui::PopStyleColor();
        ImGui::SameLine(size.x - 120);
        ImGui::PushStyleColor(ImGuiCol_Button, toVec4(kPrimary));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, toVec4(kPrimaryLight));
        ImGui::PushStyleColor(ImGuiCol_Text, toVec4(kTextPrimary));
        if (ImGui::Button("Reload")) {
            controller.reloadSkills(state);
        }
        ImGui::PopStyleColor(3);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (state.skillsDirty) {
            controller.refreshSkills(state);
        }

        // Skills list
        ImGui::PushStyleColor(ImGuiCol_Text, toVec4(kTextSecondary));
        ImGui::Text("Loaded Skills (%d)", (int)state.skills.size());
        ImGui::PopStyleColor();
        ImGui::Spacing();

        for (int i = 0; i < (int)state.skills.size(); i++) {
            auto& skill = state.skills[i];
            ImGui::PushID(i);

            ImDrawList* draw = ImGui::GetWindowDrawList();
            ImVec2 cursorPos = ImGui::GetCursorScreenPos();
            float cardW = size.x - 56;
            float cardH = 60.0f;
            draw->AddRectFilled(cursorPos, ImVec2(cursorPos.x + cardW, cursorPos.y + cardH),
                                kCardBg, kCornerRadius);

            ImGui::SetCursorScreenPos(ImVec2(cursorPos.x + 12, cursorPos.y + 8));
            ImGui::PushStyleColor(ImGuiCol_Text, toVec4(kTextPrimary));
            ImGui::Text("%s", skill.name.c_str());
            ImGui::PopStyleColor();

            ImGui::SetCursorScreenPos(ImVec2(cursorPos.x + 12, cursorPos.y + 30));
            ImGui::PushStyleColor(ImGuiCol_Text, toVec4(kTextHint));
            ImGui::Text("%s", skill.description.c_str());
            ImGui::PopStyleColor();

            // Enable toggle (right side)
            ImGui::SetCursorScreenPos(ImVec2(cursorPos.x + cardW - 60, cursorPos.y + 18));
            ImGui::PushStyleColor(ImGuiCol_Text, skill.enabled ? toVec4(kSuccess) : toVec4(kError));
            ImGui::Text("%s", skill.enabled ? "On" : "Off");
            ImGui::PopStyleColor();

            ImGui::SetCursorScreenPos(ImVec2(cursorPos.x, cursorPos.y + cardH + 4));
            ImGui::PopID();
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Tools list
        if (state.toolsDirty) {
            controller.refreshTools(state);
        }

        ImGui::PushStyleColor(ImGuiCol_Text, toVec4(kTextSecondary));
        ImGui::Text("Registered Tools (%d)", (int)state.tools.size());
        ImGui::PopStyleColor();
        ImGui::Spacing();

        if (ImGui::BeginTable("ToolsTable", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 120);
            ImGui::TableSetupColumn("Description", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 60);
            ImGui::TableSetupColumn("##Act", ImGuiTableColumnFlags_WidthFixed, 30);
            ImGui::TableHeadersRow();

            for (int i = 0; i < (int)state.tools.size(); i++) {
                auto& tool = state.tools[i];
                ImGui::TableNextRow();

                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%s", tool.name.c_str());

                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%s", tool.description.c_str());

                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%s", tool.isExternal ? "ext" : "built");

                ImGui::TableSetColumnIndex(3);
                if (tool.isExternal) {
                    ImGui::PushID(i);
                    if (ImGui::SmallButton("X")) {
                        controller.removeTool(state, tool.name);
                    }
                    ImGui::PopID();
                }
            }
            ImGui::EndTable();
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Register external tool
        ImGui::PushStyleColor(ImGuiCol_Text, toVec4(kTextSecondary));
        ImGui::Text("Register External Tool");
        ImGui::PopStyleColor();

        ImGui::PushStyleColor(ImGuiCol_FrameBg, toVec4(kInputBg));
        ImGui::PushStyleColor(ImGuiCol_Text, toVec4(kTextPrimary));
        ImGui::InputTextMultiline("##ToolSchema", m_toolSchemaBuf, sizeof(m_toolSchemaBuf),
                                  ImVec2(-1, 80));
        ImGui::PopStyleColor(2);

        ImGui::PushStyleColor(ImGuiCol_Button, toVec4(kPrimary));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, toVec4(kPrimaryLight));
        ImGui::PushStyleColor(ImGuiCol_Text, toVec4(kTextPrimary));
        if (ImGui::Button("Register") && m_toolSchemaBuf[0]) {
            controller.registerTool(state, m_toolSchemaBuf);
            std::memset(m_toolSchemaBuf, 0, sizeof(m_toolSchemaBuf));
        }
        ImGui::PopStyleColor(3);
    }
    ImGui::End();

    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
}

} // namespace rimeclaw
