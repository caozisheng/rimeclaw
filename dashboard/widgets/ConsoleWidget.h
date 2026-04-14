#pragma once
#include <imterm/terminal.hpp>
#include <imterm/terminal_helpers.hpp>
#include <spdlog/spdlog.h>
#include <mutex>
#include <memory>

namespace rimeclaw {

class TerminalHelper
    : public ImTerm::basic_spdlog_terminal_helper<TerminalHelper, void, std::mutex> {
    using Base = ImTerm::basic_spdlog_terminal_helper<TerminalHelper, void, std::mutex>;
public:
    TerminalHelper();
};

class ConsoleWidget {
public:
    void init();
    void shutdown();
    void render(ImVec2 pos, ImVec2 size);

    float consoleHeight = 200.0f;
    bool  visible = true;

private:
    std::shared_ptr<TerminalHelper> m_helper;
    std::unique_ptr<ImTerm::terminal<TerminalHelper>> m_terminal;
};

} // namespace rimeclaw
