#pragma once
#include "../DashboardState.h"
#include <imgui.h>

namespace rimeclaw {

class CronPanel {
public:
    void render(DashboardState& state, ImVec2 pos, ImVec2 size,
                class DashboardController& controller);
private:
    char m_name[256] = {};
    char m_cronExpr[64] = {};
    char m_message[1024] = {};
    int m_sessionIdx = 0;
};

} // namespace rimeclaw
