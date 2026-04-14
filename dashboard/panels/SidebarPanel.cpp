#include "SidebarPanel.h"
#include "../DashboardTheme.h"
#include <glad/glad.h>
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace rimeclaw {

struct NavItem {
    const char* icon;
    const char* label;
    NavPage page;
};

static const NavItem kNavItems[] = {
    { "[C]",  "Chat",      NavPage::Chat },
    { "[S]",  "Sessions",  NavPage::Sessions },
    { "[A]",  "Agents",    NavPage::Agents },
    { "[K]",  "Skills",    NavPage::Skills },
    { "[R]",  "Cron",      NavPage::Cron },
    { "[*]",  "Config",    NavPage::Config },
};

void SidebarPanel::init(const char* icoPath) {
    int channels = 0;
    unsigned char* data = stbi_load(icoPath, &m_logoW, &m_logoH, &channels, 4);
    if (!data) return;

    glGenTextures(1, &m_logoTexture);
    glBindTexture(GL_TEXTURE_2D, m_logoTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, m_logoW, m_logoH, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    stbi_image_free(data);
}

void SidebarPanel::shutdown() {
    if (m_logoTexture) {
        glDeleteTextures(1, &m_logoTexture);
        m_logoTexture = 0;
    }
}

void SidebarPanel::render(DashboardState& state, ImVec2 pos, ImVec2 size) {
    using namespace DashboardTheme;

    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, toVec4(kSidebarBg));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                             ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking;

    if (ImGui::Begin("##Sidebar", nullptr, flags)) {
        ImDrawList* draw = ImGui::GetWindowDrawList();

        // Logo area — image centered, text below
        float logoSize = 128.0f;
        float logoX = pos.x + (size.x - logoSize) * 0.5f;
        float logoY = pos.y + 12.0f;

        if (m_logoTexture) {
            draw->AddImage((ImTextureID)(intptr_t)m_logoTexture,
                           ImVec2(logoX, logoY),
                           ImVec2(logoX + logoSize, logoY + logoSize));
        }

        // Title text centered below logo
        float textY = logoY + logoSize + 6.0f;
        {
            const char* title = "RimeClaw";
            float titleW = ImGui::CalcTextSize(title).x;
            draw->AddText(ImVec2(pos.x + (size.x - titleW) * 0.5f, textY),
                          kSidebarActive, title);
        }
        textY += ImGui::GetTextLineHeight() + 2.0f;
        {
            const char* subtitle = "Dashboard";
            float subW = ImGui::CalcTextSize(subtitle).x;
            draw->AddText(ImVec2(pos.x + (size.x - subW) * 0.5f, textY),
                          kTextHint, subtitle);
        }

        // Separator
        float sepY = textY + ImGui::GetTextLineHeight() + 10.0f;
        ImGui::SetCursorScreenPos(ImVec2(pos.x, sepY));
        ImVec2 sepMin = ImGui::GetCursorScreenPos();
        draw->AddLine(ImVec2(sepMin.x + 16, sepMin.y),
                      ImVec2(sepMin.x + size.x - 16, sepMin.y),
                      IM_COL32(60, 60, 80, 128));

        // Navigation items
        ImGui::SetCursorScreenPos(ImVec2(pos.x, sepY + 10.0f));
        float itemH = 40.0f;
        float padX = 12.0f;

        for (const auto& item : kNavItems) {
            ImVec2 cursorScreen = ImGui::GetCursorScreenPos();
            ImVec2 itemMin = ImVec2(cursorScreen.x + 4, cursorScreen.y);
            ImVec2 itemMax = ImVec2(cursorScreen.x + size.x - 4, cursorScreen.y + itemH);
            bool isActive = (state.currentPage == item.page);
            bool hovered = ImGui::IsMouseHoveringRect(itemMin, itemMax);

            // Background
            if (isActive) {
                draw->AddRectFilled(itemMin, itemMax, kSidebarActiveBg, kCornerRadius);
            } else if (hovered) {
                draw->AddRectFilled(itemMin, itemMax, kSidebarHover, kCornerRadius);
            }

            // Click
            ImGui::SetCursorScreenPos(itemMin);
            ImGui::InvisibleButton(item.label, ImVec2(size.x - 8, itemH));
            if (ImGui::IsItemClicked()) {
                state.currentPage = item.page;
            }

            // Icon + label
            ImU32 textCol = isActive ? kSidebarActive : kSidebarText;
            float textY = itemMin.y + (itemH - ImGui::GetTextLineHeight()) * 0.5f;
            draw->AddText(ImVec2(itemMin.x + padX, textY), textCol, item.icon);
            draw->AddText(ImVec2(itemMin.x + padX + 28, textY), textCol, item.label);
        }

        // Console toggle button
        float versionY = pos.y + size.y - 30;
        ImGui::SetCursorScreenPos(ImVec2(pos.x + 12, versionY - 30));
        if (ImGui::SmallButton(state.consoleVisible ? "Hide Console" : "Show Console")) {
            state.consoleVisible = !state.consoleVisible;
        }

        // Version at bottom
        draw->AddText(ImVec2(pos.x + 16, versionY), kTextHint,
                      claw_version() ? claw_version() : "v0.1.0");
    }
    ImGui::End();

    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);
}

} // namespace rimeclaw
