#include "CronPanel.h"
#include "../DashboardTheme.h"
#include "../DashboardController.h"
#include <cstring>

namespace rimeclaw {

void CronPanel::render(DashboardState& state, ImVec2 pos, ImVec2 size,
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

    if (ImGui::Begin("##Cron", nullptr, flags)) {
        ImGui::PushStyleColor(ImGuiCol_Text, toVec4(kTextPrimary));
        ImGui::Text("Cron Jobs");
        ImGui::PopStyleColor();
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Job table
        if (ImGui::BeginTable("CronTable", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg,
                              ImVec2(0, size.y * 0.4f))) {
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Cron Expr", ImGuiTableColumnFlags_WidthFixed, 140);
            ImGui::TableSetupColumn("Session", ImGuiTableColumnFlags_WidthFixed, 140);
            ImGui::TableSetupColumn("Message", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("##Act", ImGuiTableColumnFlags_WidthFixed, 30);
            ImGui::TableHeadersRow();

            for (int i = 0; i < (int)state.cronJobs.size(); i++) {
                auto& job = state.cronJobs[i];
                ImGui::TableNextRow();

                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%s", job.name.c_str());

                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%s", job.cronExpr.c_str());

                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%s", job.sessionKey.c_str());

                ImGui::TableSetColumnIndex(3);
                ImGui::Text("%s", job.message.c_str());

                ImGui::TableSetColumnIndex(4);
                ImGui::PushID(i);
                ImGui::PushStyleColor(ImGuiCol_Text, toVec4(kError));
                if (ImGui::SmallButton("X")) {
                    controller.removeCronJob(state, job.id);
                }
                ImGui::PopStyleColor();
                ImGui::PopID();
            }
            ImGui::EndTable();
        }

        if (state.cronJobs.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, toVec4(kTextHint));
            ImGui::Text("No cron jobs configured. (Requires C API extension)");
            ImGui::PopStyleColor();
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // New job form
        ImGui::PushStyleColor(ImGuiCol_Text, toVec4(kTextSecondary));
        ImGui::Text("New Job");
        ImGui::PopStyleColor();
        ImGui::Spacing();

        ImGui::PushStyleColor(ImGuiCol_FrameBg, toVec4(kInputBg));
        ImGui::PushStyleColor(ImGuiCol_Text, toVec4(kTextPrimary));

        ImGui::SetNextItemWidth(300);
        ImGui::InputText("Name", m_name, sizeof(m_name));

        ImGui::SetNextItemWidth(300);
        ImGui::InputText("Cron Expr", m_cronExpr, sizeof(m_cronExpr));
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, toVec4(kTextHint));
        ImGui::Text("(e.g. \"0 9 * * 1-5\")");
        ImGui::PopStyleColor();

        // Session dropdown
        ImGui::SetNextItemWidth(300);
        if (ImGui::BeginCombo("Session", m_sessionIdx < (int)state.sessions.size()
                              ? state.sessions[m_sessionIdx].key.c_str() : "")) {
            for (int i = 0; i < (int)state.sessions.size(); i++) {
                if (ImGui::Selectable(state.sessions[i].key.c_str(), i == m_sessionIdx)) {
                    m_sessionIdx = i;
                }
            }
            ImGui::EndCombo();
        }

        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("Message", m_message, sizeof(m_message));

        ImGui::PopStyleColor(2);

        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Button, toVec4(kPrimary));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, toVec4(kPrimaryLight));
        ImGui::PushStyleColor(ImGuiCol_Text, toVec4(kTextPrimary));
        if (ImGui::Button("Create") && m_name[0] && m_cronExpr[0]) {
            CronJob job;
            job.name = m_name;
            job.cronExpr = m_cronExpr;
            job.message = m_message;
            if (m_sessionIdx < (int)state.sessions.size()) {
                job.sessionKey = state.sessions[m_sessionIdx].key;
            }
            controller.addCronJob(state, job);
            std::memset(m_name, 0, sizeof(m_name));
            std::memset(m_cronExpr, 0, sizeof(m_cronExpr));
            std::memset(m_message, 0, sizeof(m_message));
        }
        ImGui::PopStyleColor(3);
    }
    ImGui::End();

    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
}

} // namespace rimeclaw
