#include "ConfigPanel.h"
#include "../DashboardTheme.h"
#include "../DashboardController.h"
#include <cstring>

namespace rimeclaw {

void ConfigPanel::render(DashboardState& state, ImVec2 pos, ImVec2 size,
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

    if (ImGui::Begin("##Config", nullptr, flags)) {
        if (state.configDirty) {
            controller.loadConfig(state);
        }

        // Header with buttons
        ImGui::PushStyleColor(ImGuiCol_Text, toVec4(kTextPrimary));
        ImGui::Text("Config Editor (%s)", state.configPath.c_str());
        ImGui::PopStyleColor();

        ImGui::SameLine(size.x - 200);
        ImGui::PushStyleColor(ImGuiCol_Button, toVec4(kPrimary));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, toVec4(kPrimaryLight));
        ImGui::PushStyleColor(ImGuiCol_Text, toVec4(kTextPrimary));
        if (ImGui::Button("Save")) {
            controller.saveConfig(state);
        }
        ImGui::SameLine();
        if (ImGui::Button("Reload")) {
            controller.loadConfig(state);
        }
        ImGui::PopStyleColor(3);

        if (state.configModified) {
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, toVec4(kWarning));
            ImGui::Text("(modified)");
            ImGui::PopStyleColor();
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // JSON editor (raw mode)
        float editorH = size.y - 100;
        ImGui::PushStyleColor(ImGuiCol_FrameBg, toVec4(kCardBg));
        ImGui::PushStyleColor(ImGuiCol_Text, toVec4(kTextPrimary));

        ImGuiInputTextFlags editFlags = ImGuiInputTextFlags_AllowTabInput;
        if (ImGui::InputTextMultiline("##ConfigEdit", state.configEditBuf,
                                      sizeof(state.configEditBuf),
                                      ImVec2(-1, editorH), editFlags)) {
            state.configModified = (std::strcmp(state.configEditBuf,
                                               state.configJson.c_str()) != 0);
        }

        ImGui::PopStyleColor(2);
    }
    ImGui::End();

    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
}

} // namespace rimeclaw
