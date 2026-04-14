// Copyright 2025-2026 RimeClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <memory>
#include <string>
#include <vector>

namespace rimeclaw {

class MemoryManager;
class SkillLoader;
class ToolRegistry;
struct SkillMetadata;

struct AgentConfig;
struct RimeClawConfig;

class PromptBuilder {
 public:
  PromptBuilder(std::shared_ptr<MemoryManager> memory_manager,
                std::shared_ptr<SkillLoader> skill_loader,
                std::shared_ptr<ToolRegistry> tool_registry,
                const RimeClawConfig* config = nullptr);

  // Full system prompt: SOUL + AGENTS + TOOLS + skills + memory + runtime info
  std::string BuildFull(
      const std::string& agent_id = "default",
      const std::vector<SkillMetadata>& skills = {}) const;

  // Minimal system prompt: identity + tools only
  std::string BuildMinimal(const std::string& agent_id = "default") const;

 private:
  std::shared_ptr<MemoryManager> memory_manager_;
  std::shared_ptr<SkillLoader> skill_loader_;
  std::shared_ptr<ToolRegistry> tool_registry_;
  const RimeClawConfig* config_;

  std::string get_soul_section() const;
  std::string get_runtime_info() const;
};

}  // namespace rimeclaw
