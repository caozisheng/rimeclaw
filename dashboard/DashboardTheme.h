#pragma once
#include <imgui.h>

namespace DashboardTheme {

// Sidebar
constexpr ImU32 kSidebarBg       = IM_COL32(28, 28, 46, 255);
constexpr ImU32 kSidebarText     = IM_COL32(176, 180, 196, 255);
constexpr ImU32 kSidebarActive   = IM_COL32(139, 154, 232, 255);
constexpr ImU32 kSidebarActiveBg = IM_COL32(45, 45, 74, 255);
constexpr ImU32 kSidebarHover    = IM_COL32(38, 38, 56, 255);

// Main
constexpr ImU32 kMainBg          = IM_COL32(26, 26, 44, 255);
constexpr ImU32 kCardBg          = IM_COL32(38, 38, 56, 255);
constexpr ImU32 kInputBg         = IM_COL32(42, 42, 60, 255);
constexpr ImU32 kInputBorder     = IM_COL32(58, 58, 78, 255);

// Accent
constexpr ImU32 kPrimary         = IM_COL32(91, 106, 191, 255);
constexpr ImU32 kPrimaryLight    = IM_COL32(124, 138, 219, 255);
constexpr ImU32 kSuccess         = IM_COL32(76, 175, 80, 255);
constexpr ImU32 kError           = IM_COL32(229, 57, 53, 255);
constexpr ImU32 kWarning         = IM_COL32(255, 167, 38, 255);

// Text
constexpr ImU32 kTextPrimary     = IM_COL32(226, 228, 236, 255);
constexpr ImU32 kTextSecondary   = IM_COL32(148, 152, 168, 255);
constexpr ImU32 kTextHint        = IM_COL32(112, 116, 136, 255);

// Chat bubbles
constexpr ImU32 kUserBubbleBg    = IM_COL32(59, 72, 127, 255);
constexpr ImU32 kAssistBubbleBg  = IM_COL32(45, 45, 65, 255);
constexpr ImU32 kToolCallBg      = IM_COL32(35, 40, 55, 255);
constexpr ImU32 kThinkingBg      = IM_COL32(40, 35, 55, 255);

// Layout constants
constexpr float kSidebarWidth     = 200.0f;
constexpr float kSessionListWidth = 240.0f;
constexpr float kCornerRadius     = 8.0f;
constexpr float kItemSpacing      = 6.0f;
constexpr float kStatusBarHeight  = 28.0f;

// Helper: ImU32 -> ImVec4
inline ImVec4 toVec4(ImU32 col) {
    return ImGui::ColorConvertU32ToFloat4(col);
}

} // namespace DashboardTheme
