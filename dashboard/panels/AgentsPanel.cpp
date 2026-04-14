#include "AgentsPanel.h"
#include "../DashboardTheme.h"
#include "../DashboardController.h"

namespace rimeclaw {

void AgentsPanel::render(DashboardState& state, ImVec2 pos, ImVec2 size,
                         DashboardController& controller) {
    using namespace DashboardTheme;

    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(size);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, toVec4(kMainBg));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20, 16));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                             ImGuiWindowFlags_NoDocking;

    if (ImGui::Begin("##Agents", nullptr, flags)) {
        ImGui::PushStyleColor(ImGuiCol_Text, toVec4(kTextPrimary));
        ImGui::Text("Agent Configuration");
        ImGui::PopStyleColor();
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        auto& cfg = state.agentConfig;

        // Model
        ImGui::PushStyleColor(ImGuiCol_Text, toVec4(kTextSecondary));
        ImGui::Text("Model");
        ImGui::PopStyleColor();

        ImGui::PushStyleColor(ImGuiCol_FrameBg, toVec4(kInputBg));
        ImGui::PushStyleColor(ImGuiCol_Text, toVec4(kTextPrimary));
        char modelBuf[256];
        strncpy(modelBuf, cfg.model.c_str(), sizeof(modelBuf) - 1);
        modelBuf[sizeof(modelBuf) - 1] = '\0';
        if (ImGui::InputText("##Model", modelBuf, sizeof(modelBuf))) {
            cfg.model = modelBuf;
        }
        ImGui::PopStyleColor(2);

        ImGui::Spacing();

        // Sliders
        ImGui::PushStyleColor(ImGuiCol_FrameBg, toVec4(kInputBg));
        ImGui::PushStyleColor(ImGuiCol_SliderGrab, toVec4(kPrimary));
        ImGui::PushStyleColor(ImGuiCol_Text, toVec4(kTextPrimary));

        ImGui::SliderFloat("Temperature", &cfg.temperature, 0.0f, 2.0f, "%.2f");
        ImGui::SliderInt("Max Tokens", &cfg.maxTokens, 256, 65536);
        ImGui::SliderInt("Max Iterations", &cfg.maxIterations, 1, 128);
        ImGui::SliderInt("Context Window", &cfg.contextWindow, 4096, 200000);

        ImGui::PopStyleColor(3);

        ImGui::Spacing();

        // Thinking mode
        ImGui::PushStyleColor(ImGuiCol_Text, toVec4(kTextSecondary));
        ImGui::Text("Thinking Mode");
        ImGui::PopStyleColor();

        ImGui::PushStyleColor(ImGuiCol_Text, toVec4(kTextPrimary));
        const char* modes[] = {"off", "low", "medium", "high"};
        int currentMode = 0;
        for (int i = 0; i < 4; i++) {
            if (cfg.thinking == modes[i]) currentMode = i;
        }
        for (int i = 0; i < 4; i++) {
            if (i > 0) ImGui::SameLine();
            if (ImGui::RadioButton(modes[i], currentMode == i)) {
                cfg.thinking = modes[i];
            }
        }
        ImGui::PopStyleColor();

        ImGui::Spacing();

        // Fallback models
        ImGui::PushStyleColor(ImGuiCol_Text, toVec4(kTextSecondary));
        ImGui::Text("Fallback Models");
        ImGui::PopStyleColor();

        ImGui::PushStyleColor(ImGuiCol_FrameBg, toVec4(kInputBg));
        for (int i = 0; i < (int)cfg.fallbacks.size(); i++) {
            ImGui::PushID(i);
            char buf[256];
            strncpy(buf, cfg.fallbacks[i].c_str(), sizeof(buf) - 1);
            buf[sizeof(buf) - 1] = '\0';
            ImGui::SetNextItemWidth(-60);
            if (ImGui::InputText("##fb", buf, sizeof(buf))) {
                cfg.fallbacks[i] = buf;
            }
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, toVec4(kError));
            if (ImGui::SmallButton("X")) {
                cfg.fallbacks.erase(cfg.fallbacks.begin() + i);
                i--;
            }
            ImGui::PopStyleColor();
            ImGui::PopID();
        }
        ImGui::PopStyleColor();

        ImGui::PushStyleColor(ImGuiCol_Button, toVec4(kCardBg));
        ImGui::PushStyleColor(ImGuiCol_Text, toVec4(kTextSecondary));
        if (ImGui::Button("+ Add Fallback")) {
            cfg.fallbacks.emplace_back("");
        }
        ImGui::PopStyleColor(2);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Save / Reset
        ImGui::PushStyleColor(ImGuiCol_Button, toVec4(kPrimary));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, toVec4(kPrimaryLight));
        ImGui::PushStyleColor(ImGuiCol_Text, toVec4(kTextPrimary));
        if (ImGui::Button("Save")) {
            controller.saveConfig(state);
        }
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, toVec4(kCardBg));
        if (ImGui::Button("Reset to Defaults")) {
            cfg.model = "anthropic/claude-sonnet-4-6";
            cfg.temperature = 0.7f;
            cfg.maxTokens = 8192;
            cfg.maxIterations = 32;
            cfg.contextWindow = 128000;
            cfg.thinking = "off";
            cfg.fallbacks.clear();
        }
        ImGui::PopStyleColor(4);
    }
    ImGui::End();

    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
}

} // namespace rimeclaw
