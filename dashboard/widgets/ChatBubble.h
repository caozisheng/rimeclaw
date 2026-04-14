#pragma once
#include "../DashboardState.h"
#include <imgui.h>

namespace rimeclaw {

class ChatBubble {
public:
    static void render(const ChatMessage& msg, float maxWidth);
};

} // namespace rimeclaw
