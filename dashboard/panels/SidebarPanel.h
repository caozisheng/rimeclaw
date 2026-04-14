#pragma once
#include "../DashboardState.h"
#include <imgui.h>

namespace rimeclaw {

class SidebarPanel {
public:
    void init(const char* icoPath);
    void shutdown();
    void render(DashboardState& state, ImVec2 pos, ImVec2 size);

private:
    unsigned int m_logoTexture = 0;
    int m_logoW = 0;
    int m_logoH = 0;
};

} // namespace rimeclaw
