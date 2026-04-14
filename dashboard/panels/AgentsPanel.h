#pragma once
#include "../DashboardState.h"
#include <imgui.h>

namespace rimeclaw {

class AgentsPanel {
public:
    void render(DashboardState& state, ImVec2 pos, ImVec2 size,
                class DashboardController& controller);
};

} // namespace rimeclaw
