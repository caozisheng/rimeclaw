#pragma once
#include "../DashboardState.h"
#include <imgui.h>

namespace rimeclaw {

class ToolCallWidget {
public:
    static void render(const ChatMessage::ToolCall& tc);
};

} // namespace rimeclaw
