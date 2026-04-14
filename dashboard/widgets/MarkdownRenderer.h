#pragma once
#include <imgui.h>
#include <string>

namespace rimeclaw {

class MarkdownRenderer {
public:
    static void render(const std::string& text, float wrapWidth);

private:
    static void renderCodeBlock(const std::string& code, const std::string& lang);
    static void renderInlineCode(const std::string& code);
};

} // namespace rimeclaw
