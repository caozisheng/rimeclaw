// Copyright 2025-2026 RimeClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "rimeclaw/core/prompt_builder.hpp"

#include <chrono>
#include <filesystem>
#include <iomanip>
#include <sstream>

#include <spdlog/spdlog.h>

#include "rimeclaw/config.hpp"
#include "rimeclaw/core/memory_manager.hpp"
#include "rimeclaw/core/skill_loader.hpp"
#include "rimeclaw/tools/tool_registry.hpp"

namespace rimeclaw {

PromptBuilder::PromptBuilder(std::shared_ptr<MemoryManager> memory_manager,
                             std::shared_ptr<SkillLoader> skill_loader,
                             std::shared_ptr<ToolRegistry> tool_registry,
                             const RimeClawConfig* config)
    : memory_manager_(std::move(memory_manager)),
      skill_loader_(std::move(skill_loader)),
      tool_registry_(std::move(tool_registry)),
      config_(config) {}

std::string PromptBuilder::BuildFull(
    const std::string& /*agent_id*/,
    const std::vector<SkillMetadata>& skills) const {
  std::ostringstream prompt;

  // 1. Identity section
  prompt << get_soul_section();

  // 2. Tool descriptions
  if (tool_registry_) {
    auto tools = tool_registry_->GetToolSchemas();
    if (!tools.empty()) {
      prompt << "\n\n## Available Tools\n\n";
      for (const auto& t : tools) {
        prompt << "### " << t.name << "\n" << t.description << "\n\n";
      }
    }
  }

  // 3. Skills
  if (skill_loader_ && !skills.empty()) {
    auto skills_ctx = skill_loader_->GetSkillContext(skills);
    if (!skills_ctx.empty()) {
      spdlog::info("[PromptBuilder] Injecting {} skill(s) into system prompt ({} chars)",
                   skills.size(), skills_ctx.size());
      prompt << "\n\n## Skills\n\n";
      prompt << skills_ctx;
    }
  }

  // 4. Memory context
  if (memory_manager_) {
    auto mem = memory_manager_->SearchMemory("context");
    if (!mem.empty()) {
      prompt << "\n\n## Memory Context\n\n";
      for (const auto& entry : mem) {
        prompt << entry << "\n";
      }
    }
  }

  // 5. Runtime info
  prompt << "\n\n## Runtime Info\n\n";
  prompt << get_runtime_info();

  return prompt.str();
}

std::string PromptBuilder::BuildMinimal(
    const std::string& /*agent_id*/) const {
  std::ostringstream prompt;

  // Identity + tools only
  prompt << get_soul_section();

  if (tool_registry_) {
    auto tools = tool_registry_->GetToolSchemas();
    if (!tools.empty()) {
      prompt << "\n\n## Available Tools\n\n";
      for (const auto& t : tools) {
        prompt << "### " << t.name << "\n" << t.description << "\n\n";
      }
    }
  }

  return prompt.str();
}

std::string PromptBuilder::get_soul_section() const {
  if (config_ && !config_->system.name.empty()) {
    return "You are " + config_->system.name + ", an AI assistant.";
  }
  return "You are RimeClaw, an AI assistant.";
}

std::string PromptBuilder::get_runtime_info() const {
  // Only include the timezone, not a ticking clock.  Volatile timestamps
  // embedded in the system prompt invalidate KV cache / prompt cache on
  // every turn.  Agents that need the current time should call a tool
  // (e.g. exec "date") instead.
  auto now = std::chrono::system_clock::now();
  std::time_t t = std::chrono::system_clock::to_time_t(now);
  std::ostringstream ss;
  ss << "Timezone: "
     << std::put_time(std::localtime(&t), "%Z");
  return ss.str();
}

}  // namespace rimeclaw
