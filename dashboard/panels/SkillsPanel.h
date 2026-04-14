#pragma once
#include "../DashboardState.h"
#include <imgui.h>

namespace rimeclaw {

class SkillsPanel {
public:
    void render(DashboardState& state, ImVec2 pos, ImVec2 size,
                class DashboardController& controller);
private:
    char m_toolSchemaBuf[4096] = {};
};

} // namespace rimeclaw
