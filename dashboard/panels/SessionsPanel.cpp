#include "SessionsPanel.h"
#include "../DashboardTheme.h"
#include "../DashboardController.h"

namespace rimeclaw {

void SessionsPanel::render(DashboardState& state, ImVec2 pos, ImVec2 size,
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

    if (ImGui::Begin("##Sessions", nullptr, flags)) {
        if (state.sessionsDirty) {
            controller.refreshSessions(state);
        }

        // Header
        ImGui::PushStyleColor(ImGuiCol_Text, toVec4(kTextPrimary));
        ImGui::Text("Session Management");
        ImGui::PopStyleColor();
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Table
        ImGuiTableFlags tableFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                     ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY;

        if (ImGui::BeginTable("SessionTable", 5, tableFlags, ImVec2(0, size.y - 120))) {
            ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Display Name", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Messages", ImGuiTableColumnFlags_WidthFixed, 80);
            ImGui::TableSetupColumn("Updated", ImGuiTableColumnFlags_WidthFixed, 120);
            ImGui::TableSetupColumn("##Action", ImGuiTableColumnFlags_WidthFixed, 40);
            ImGui::TableHeadersRow();

            for (int i = 0; i < (int)state.sessions.size(); i++) {
                auto& s = state.sessions[i];
                ImGui::TableNextRow();

                ImGui::TableSetColumnIndex(0);
                bool selected = (m_selectedRow == i);
                if (ImGui::Selectable(s.key.c_str(), selected,
                                      ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap)) {
                    m_selectedRow = i;
                }

                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%s", s.displayName.c_str());

                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%d", s.messageCount);

                ImGui::TableSetColumnIndex(3);
                ImGui::Text("%s", s.updatedAt.c_str());

                ImGui::TableSetColumnIndex(4);
                ImGui::PushID(i);
                ImGui::PushStyleColor(ImGuiCol_Text, toVec4(kError));
                if (ImGui::SmallButton("X")) {
                    m_confirmDelete = true;
                    m_deleteKey = s.key;
                }
                ImGui::PopStyleColor();
                ImGui::PopID();
            }
            ImGui::EndTable();
        }

        // Action buttons
        ImGui::Spacing();
        if (m_selectedRow >= 0 && m_selectedRow < (int)state.sessions.size()) {
            auto& sel = state.sessions[m_selectedRow];
            ImGui::PushStyleColor(ImGuiCol_Text, toVec4(kTextSecondary));
            ImGui::Text("Selected: %s", sel.key.c_str());
            ImGui::PopStyleColor();

            ImGui::PushStyleColor(ImGuiCol_Button, toVec4(kPrimary));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, toVec4(kPrimaryLight));
            ImGui::PushStyleColor(ImGuiCol_Text, toVec4(kTextPrimary));
            if (ImGui::Button("Open in Chat")) {
                controller.switchSession(state, sel.key);
                state.currentPage = NavPage::Chat;
            }
            ImGui::SameLine();
            if (ImGui::Button("Clear Transcript")) {
                controller.clearSession(state, sel.key);
            }
            ImGui::PopStyleColor(3);
        }

        // Delete confirmation
        if (m_confirmDelete) {
            ImGui::OpenPopup("ConfirmDelete");
            m_confirmDelete = false;
        }
        if (ImGui::BeginPopupModal("ConfirmDelete", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Delete session '%s'?", m_deleteKey.c_str());
            if (ImGui::Button("Delete")) {
                controller.deleteSession(state, m_deleteKey);
                m_selectedRow = -1;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }
    ImGui::End();

    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
}

} // namespace rimeclaw
