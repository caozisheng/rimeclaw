#include "ConsoleWidget.h"
#include "../DashboardTheme.h"

namespace rimeclaw {

// -- TerminalHelper --

static void cmd_clear(ImTerm::argument_t<ImTerm::terminal<TerminalHelper>>& arg) {
    arg.term.clear();
}

TerminalHelper::TerminalHelper() : Base("gfdashboard") {
    add_command_({"clear", "Clear console", cmd_clear, nullptr});
}

// -- ConsoleWidget --

void ConsoleWidget::init() {
    m_helper = std::make_shared<TerminalHelper>();
    m_terminal = std::make_unique<ImTerm::terminal<TerminalHelper>>(
        "Console", 0, 0, m_helper);
    m_terminal->set_max_log_len(10000);
    // Dock the terminal: no title bar, no resize, no move, no collapse
    m_terminal->set_flags(ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                          ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                          ImGuiWindowFlags_NoDocking);

    // Attach as spdlog sink to capture ALL log output
    auto sink = std::static_pointer_cast<spdlog::sinks::sink>(m_helper);
    spdlog::default_logger()->sinks().push_back(sink);
    spdlog::apply_all([&sink](std::shared_ptr<spdlog::logger> logger) {
        auto& sinks = logger->sinks();
        if (std::find(sinks.begin(), sinks.end(), sink) == sinks.end()) {
            sinks.push_back(sink);
        }
    });
}

void ConsoleWidget::shutdown() {
    if (!m_helper) return;
    auto sink = std::static_pointer_cast<spdlog::sinks::sink>(m_helper);
    spdlog::apply_all([&sink](std::shared_ptr<spdlog::logger> logger) {
        auto& sinks = logger->sinks();
        sinks.erase(std::remove(sinks.begin(), sinks.end(), sink), sinks.end());
    });
}

void ConsoleWidget::render(ImVec2 pos, ImVec2 size) {
    if (!visible || !m_terminal) return;

    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(size);
    m_terminal->show();
}

} // namespace rimeclaw
