#include "MarkdownRenderer.h"
#include "../DashboardTheme.h"
#include <sstream>

namespace rimeclaw {

void MarkdownRenderer::render(const std::string& text, float wrapWidth) {
    using namespace DashboardTheme;

    std::istringstream stream(text);
    std::string line;
    bool inCodeBlock = false;
    std::string codeBlockContent;
    std::string codeBlockLang;

    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + wrapWidth);

    while (std::getline(stream, line)) {
        // Code block start/end
        if (line.substr(0, 3) == "```") {
            if (inCodeBlock) {
                renderCodeBlock(codeBlockContent, codeBlockLang);
                codeBlockContent.clear();
                codeBlockLang.clear();
                inCodeBlock = false;
            } else {
                inCodeBlock = true;
                codeBlockLang = line.substr(3);
            }
            continue;
        }

        if (inCodeBlock) {
            if (!codeBlockContent.empty()) codeBlockContent += "\n";
            codeBlockContent += line;
            continue;
        }

        // Bullet points
        if (line.size() >= 2 && line[0] == '-' && line[1] == ' ') {
            ImGui::Bullet();
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, toVec4(kTextPrimary));
            ImGui::TextWrapped("%s", line.c_str() + 2);
            ImGui::PopStyleColor();
            continue;
        }

        // Bold: **text**
        // For simplicity, just render as-is (full parsing is Phase 4)
        if (line.empty()) {
            ImGui::Spacing();
        } else {
            ImGui::PushStyleColor(ImGuiCol_Text, toVec4(kTextPrimary));
            ImGui::TextWrapped("%s", line.c_str());
            ImGui::PopStyleColor();
        }
    }

    // Unclosed code block
    if (inCodeBlock && !codeBlockContent.empty()) {
        renderCodeBlock(codeBlockContent, codeBlockLang);
    }

    ImGui::PopTextWrapPos();
}

void MarkdownRenderer::renderCodeBlock(const std::string& code, const std::string& lang) {
    using namespace DashboardTheme;

    ImVec2 cursorPos = ImGui::GetCursorScreenPos();
    ImVec2 textSize = ImGui::CalcTextSize(code.c_str());
    float padX = 8.0f, padY = 6.0f;

    ImVec2 blockMin(cursorPos.x, cursorPos.y);
    ImVec2 blockMax(cursorPos.x + textSize.x + padX * 2,
                    cursorPos.y + textSize.y + padY * 2);

    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(blockMin, blockMax, kToolCallBg, 4.0f);

    if (!lang.empty()) {
        draw->AddText(ImVec2(blockMin.x + padX, blockMin.y + 2), kTextHint, lang.c_str());
    }

    ImGui::SetCursorScreenPos(ImVec2(blockMin.x + padX, blockMin.y + padY));
    ImGui::PushStyleColor(ImGuiCol_Text, toVec4(kTextPrimary));
    ImGui::TextUnformatted(code.c_str());
    ImGui::PopStyleColor();

    ImGui::SetCursorScreenPos(ImVec2(cursorPos.x, blockMax.y + 4));
}

void MarkdownRenderer::renderInlineCode(const std::string& code) {
    using namespace DashboardTheme;

    ImVec2 textSize = ImGui::CalcTextSize(code.c_str());
    ImVec2 cursorPos = ImGui::GetCursorScreenPos();
    float pad = 3.0f;

    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(ImVec2(cursorPos.x - pad, cursorPos.y - pad),
                        ImVec2(cursorPos.x + textSize.x + pad, cursorPos.y + textSize.y + pad),
                        kToolCallBg, 3.0f);

    ImGui::PushStyleColor(ImGuiCol_Text, toVec4(kTextPrimary));
    ImGui::TextUnformatted(code.c_str());
    ImGui::PopStyleColor();
}

} // namespace rimeclaw
