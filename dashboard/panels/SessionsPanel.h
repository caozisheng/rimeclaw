#pragma once
#include "../DashboardState.h"
#include <imgui.h>

namespace rimeclaw {

class SessionsPanel {
public:
    void render(DashboardState& state, ImVec2 pos, ImVec2 size,
                class DashboardController& controller);
private:
    int m_selectedRow = -1;
    bool m_confirmDelete = false;
    std::string m_deleteKey;
};

} // namespace rimeclaw
