#include "DashboardMainWindow.h"
#include "DashboardTheme.h"
#include <curl/curl.h>
#include <imgui_impl_opengl3.h>
#include <spdlog/spdlog.h>
#include <filesystem>
#include <fstream>

namespace rimeclaw {

static std::string g_fontPath = "resources/fonts";
static std::string g_logoPath = "resources/rimeclaw.jpg";
static std::string g_configPath = "rimeclaw_config.json";

static size_t writeFileCallback(void* ptr, size_t size, size_t nmemb, void* stream) {
    auto* out = static_cast<std::ofstream*>(stream);
    out->write(static_cast<const char*>(ptr), static_cast<std::streamsize>(size * nmemb));
    return size * nmemb;
}

struct DownloadProgress {
    int lastLoggedMilestone = -1;
};

static int progressCallback(void* clientp, curl_off_t dltotal, curl_off_t dlnow,
                             curl_off_t /*ultotal*/, curl_off_t /*ulnow*/) {
    if (dltotal > 0) {
        auto* prog = static_cast<DownloadProgress*>(clientp);
        int percent = static_cast<int>(dlnow * 100 / dltotal);
        int milestone = percent / 20 * 20;
        if (milestone > prog->lastLoggedMilestone) {
            prog->lastLoggedMilestone = milestone;
            spdlog::info("[dashboard] Downloading MSYH.TTC ... {}%", percent);
        }
    }
    return 0;
}

static bool downloadFont(const std::string& url, const std::string& destPath) {
    CURL* curl = curl_easy_init();
    if (!curl) return false;

    std::ofstream ofs(destPath, std::ios::binary);
    if (!ofs.is_open()) {
        curl_easy_cleanup(curl);
        return false;
    }

    DownloadProgress prog;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeFileCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ofs);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progressCallback);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &prog);

    CURLcode res = curl_easy_perform(curl);
    long httpCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
    curl_easy_cleanup(curl);
    ofs.close();

    if (res != CURLE_OK || httpCode != 200) {
        std::filesystem::remove(destPath);
        return false;
    }
    return true;
}

DashboardMainWindow::DashboardMainWindow()
    : ImApp::MainWindow("RimeClaw Dashboard") {
}

DashboardMainWindow::~DashboardMainWindow() = default;

void DashboardMainWindow::beforeLoop() {
    applyTheme();

    // Load Chinese font (Microsoft YaHei) for CJK support
    std::string fontFile = g_fontPath + "/MSYH.TTC";

    if (std::filesystem::exists(fontFile)) {
        auto& io = ImGui::GetIO();
        io.Fonts->Clear();
        io.Fonts->AddFontFromFileTTF(fontFile.c_str(), 16.0f, nullptr,
            io.Fonts->GetGlyphRangesChineseFull());
        io.Fonts->Build();
        m_fontLoaded = true;
        spdlog::info("[dashboard] Loaded Chinese font: {}", fontFile);
    } else {
        spdlog::info("[dashboard] MSYH.TTC not found, starting background download...");
        std::filesystem::create_directories(g_fontPath);
        m_fontDownloadFuture = std::async(std::launch::async, [fontFile]() {
            const char* url =
                "https://raw.githubusercontent.com/L-King-H/msyh.ttc/"
                "refs/heads/main/%E5%BE%AE%E8%BD%AF%E9%9B%85%E9%BB%91/MSYH.TTC";
            return downloadFont(url, fontFile);
        });
    }

    // Find config file: look in current directory, then exe directory
    std::string configPath = g_configPath;
    if (!std::filesystem::exists(configPath)) {
        configPath = "rimeclaw_config.json";
    }

    if (std::filesystem::exists(configPath)) {
        m_controller.init(m_state, configPath);
    } else {
        spdlog::warn("[dashboard] No config file found. Start with Config page to set path.");
    }

    spdlog::info("[dashboard] RimeClaw Dashboard initialized");

    m_sidebar.init(g_logoPath.c_str());
    m_console.init();
}

void DashboardMainWindow::paint() {
    // Check if background font download completed
    if (!m_fontLoaded && m_fontDownloadFuture.valid()) {
        if (m_fontDownloadFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            bool ok = m_fontDownloadFuture.get();
            std::string fontFile = g_fontPath + "/MSYH.TTC";
            if (ok && std::filesystem::exists(fontFile)) {
                auto& io = ImGui::GetIO();
                io.Fonts->Clear();
                io.Fonts->AddFontFromFileTTF(fontFile.c_str(), 16.0f, nullptr,
                    io.Fonts->GetGlyphRangesChineseFull());
                io.Fonts->Build();
                ImGui_ImplOpenGL3_DestroyFontsTexture();
                ImGui_ImplOpenGL3_CreateFontsTexture();
                m_fontLoaded = true;
                spdlog::info("[dashboard] Downloaded and loaded Chinese font: {}", fontFile);
            } else {
                spdlog::warn("[dashboard] Failed to download MSYH.TTC, Chinese text may display as ???");
            }
        }
    }

    using namespace DashboardTheme;

    // Flush streaming events
    if (m_state.initialized) {
        m_controller.pollEvents(m_state);
    }

    auto& io = ImGui::GetIO();
    float w = io.DisplaySize.x;
    float h = io.DisplaySize.y;

    float statusBarH = kStatusBarHeight;
    float sidebarW = kSidebarWidth;
    float contentX = sidebarW;
    float contentW = w - sidebarW;
    float consoleH = m_state.consoleVisible ? m_state.consoleHeight : 0;
    float contentH = h - statusBarH - consoleH;

    // 1. Sidebar
    m_sidebar.render(m_state, ImVec2(0, 0), ImVec2(sidebarW, h - statusBarH));

    // 2. Content area
    ImVec2 contentPos(contentX, 0);
    ImVec2 contentSize(contentW, contentH);

    switch (m_state.currentPage) {
        case NavPage::Chat:
            m_chatPanel.render(m_state, contentPos, contentSize, m_controller);
            break;
        case NavPage::Sessions:
            m_sessionsPanel.render(m_state, contentPos, contentSize, m_controller);
            break;
        case NavPage::Agents:
            m_agentsPanel.render(m_state, contentPos, contentSize, m_controller);
            break;
        case NavPage::Skills:
            m_skillsPanel.render(m_state, contentPos, contentSize, m_controller);
            break;
        case NavPage::Config:
            m_configPanel.render(m_state, contentPos, contentSize, m_controller);
            break;
        case NavPage::Cron:
            m_cronPanel.render(m_state, contentPos, contentSize, m_controller);
            break;
    }

    // 3. Console (below content, right of sidebar)
    if (m_state.consoleVisible) {
        m_console.render(
            ImVec2(contentX, contentH),
            ImVec2(contentW, consoleH));
    }

    // 4. Status bar
    renderStatusBar(ImVec2(0, h - statusBarH), ImVec2(w, statusBarH));
}

void DashboardMainWindow::beforeQuit() {
    if (m_fontDownloadFuture.valid())
        m_fontDownloadFuture.wait();
    m_console.shutdown();
    m_sidebar.shutdown();
    spdlog::info("[dashboard] RimeClaw Dashboard shutting down");
    m_controller.shutdown(m_state);
}

void DashboardMainWindow::applyTheme() {
    using namespace DashboardTheme;

    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 0.0f;
    style.FrameRounding = kCornerRadius * 0.5f;
    style.GrabRounding = 4.0f;
    style.ScrollbarRounding = 4.0f;
    style.TabRounding = 4.0f;
    style.WindowBorderSize = 0.0f;
    style.FrameBorderSize = 1.0f;
    style.ItemSpacing = ImVec2(kItemSpacing, kItemSpacing);
    style.ScrollbarSize = 10.0f;

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_WindowBg]       = toVec4(kMainBg);
    colors[ImGuiCol_ChildBg]        = toVec4(kMainBg);
    colors[ImGuiCol_PopupBg]        = toVec4(kCardBg);
    colors[ImGuiCol_Border]         = toVec4(kInputBorder);
    colors[ImGuiCol_FrameBg]        = toVec4(kInputBg);
    colors[ImGuiCol_FrameBgHovered] = toVec4(kSidebarHover);
    colors[ImGuiCol_FrameBgActive]  = toVec4(kSidebarActiveBg);
    colors[ImGuiCol_TitleBg]        = toVec4(kSidebarBg);
    colors[ImGuiCol_TitleBgActive]  = toVec4(kSidebarBg);
    colors[ImGuiCol_MenuBarBg]      = toVec4(kSidebarBg);
    colors[ImGuiCol_ScrollbarBg]    = toVec4(kMainBg);
    colors[ImGuiCol_ScrollbarGrab]  = toVec4(kInputBorder);
    colors[ImGuiCol_ScrollbarGrabHovered] = toVec4(kSidebarHover);
    colors[ImGuiCol_ScrollbarGrabActive]  = toVec4(kSidebarActiveBg);
    colors[ImGuiCol_CheckMark]      = toVec4(kPrimary);
    colors[ImGuiCol_SliderGrab]     = toVec4(kPrimary);
    colors[ImGuiCol_SliderGrabActive] = toVec4(kPrimaryLight);
    colors[ImGuiCol_Button]         = toVec4(kPrimary);
    colors[ImGuiCol_ButtonHovered]  = toVec4(kPrimaryLight);
    colors[ImGuiCol_ButtonActive]   = toVec4(kSidebarActiveBg);
    colors[ImGuiCol_Header]         = toVec4(kCardBg);
    colors[ImGuiCol_HeaderHovered]  = toVec4(kSidebarHover);
    colors[ImGuiCol_HeaderActive]   = toVec4(kSidebarActiveBg);
    colors[ImGuiCol_Separator]      = toVec4(kInputBorder);
    colors[ImGuiCol_SeparatorHovered] = toVec4(kPrimary);
    colors[ImGuiCol_ResizeGrip]     = toVec4(kInputBorder);
    colors[ImGuiCol_Tab]            = toVec4(kCardBg);
    colors[ImGuiCol_TabHovered]     = toVec4(kPrimary);
    colors[ImGuiCol_TableHeaderBg]  = toVec4(kCardBg);
    colors[ImGuiCol_TableBorderStrong] = toVec4(kInputBorder);
    colors[ImGuiCol_TableBorderLight]  = toVec4(kInputBorder);
    colors[ImGuiCol_TableRowBg]     = ImVec4(0, 0, 0, 0);
    colors[ImGuiCol_TableRowBgAlt]  = toVec4(kCardBg);
    colors[ImGuiCol_Text]           = toVec4(kTextPrimary);
    colors[ImGuiCol_TextDisabled]   = toVec4(kTextHint);
}

void DashboardMainWindow::renderStatusBar(ImVec2 pos, ImVec2 size) {
    using namespace DashboardTheme;

    ImDrawList* draw = ImGui::GetForegroundDrawList();
    draw->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), kSidebarBg);

    float textY = pos.y + (size.y - ImGui::GetTextLineHeight()) * 0.5f;
    float x = pos.x + 12.0f;

    // Connection status
    ImU32 statusCol = m_state.initialized ? kSuccess : kError;
    const char* statusText = m_state.initialized ? "Connected" : "Disconnected";
    draw->AddCircleFilled(ImVec2(x + 4, textY + ImGui::GetTextLineHeight() * 0.5f),
                          4.0f, statusCol);
    x += 14;
    draw->AddText(ImVec2(x, textY), kTextSecondary, statusText);
    x += ImGui::CalcTextSize(statusText).x + 20;

    // Model
    if (!m_state.agentConfig.model.empty()) {
        char buf[128];
        snprintf(buf, sizeof(buf), "Model: %s", m_state.agentConfig.model.c_str());
        draw->AddText(ImVec2(x, textY), kTextHint, buf);
        x += ImGui::CalcTextSize(buf).x + 20;
    }

    // Session count
    {
        char buf[64];
        snprintf(buf, sizeof(buf), "Sessions: %d", (int)m_state.sessions.size());
        draw->AddText(ImVec2(x, textY), kTextHint, buf);
    }

    // Streaming indicator (right side)
    if (m_state.isStreaming) {
        const char* streamText = "Streaming...";
        float streamW = ImGui::CalcTextSize(streamText).x;
        draw->AddText(ImVec2(pos.x + size.x - streamW - 12, textY), kWarning, streamText);
    }
}

} // namespace rimeclaw
