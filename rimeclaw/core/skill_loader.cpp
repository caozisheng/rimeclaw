// Copyright 2025-2026 RimeClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "rimeclaw/core/skill_loader.hpp"

#include <cstdlib>
#include <fstream>
#include <sstream>

#include <spdlog/spdlog.h>

namespace rimeclaw {

SkillLoader::SkillLoader() = default;

std::vector<SkillMetadata>
SkillLoader::LoadSkillsFromDirectory(const std::filesystem::path& skills_dir) {
  std::vector<SkillMetadata> skills;
  if (!std::filesystem::exists(skills_dir))
    return skills;

  for (const auto& entry :
       std::filesystem::recursive_directory_iterator(skills_dir)) {
    if (entry.is_regular_file() && entry.path().filename() == "SKILL.md") {
      try {
        auto skill = parse_skill_file(entry.path());
        skill.root_dir = entry.path().parent_path();
        skills.push_back(std::move(skill));
      } catch (const std::exception& e) {
        spdlog::warn("Failed to parse skill: {}: {}", entry.path().string(), e.what());
      }
    }
  }
  return skills;
}

std::vector<SkillMetadata>
SkillLoader::LoadSkills(const SkillsConfig& skills_config,
                        const std::filesystem::path& workspace_path) {
  std::filesystem::path skill_path(skills_config.path);
  if (skill_path.empty())
    return {};
  if (skill_path.is_relative())
    skill_path = workspace_path / skill_path;

  return LoadSkillsFromDirectory(skill_path);
}

bool SkillLoader::CheckSkillGating(const SkillMetadata& skill) {
  if (skill.always)
    return true;

  // Check OS restriction
  if (!skill.os_restrict.empty() && !check_os_restriction(skill.os_restrict))
    return false;

  // Check required binaries
  for (const auto& bin : skill.required_bins) {
    if (!is_binary_available(bin))
      return false;
  }

  // Check required env vars
  for (const auto& env : skill.required_envs) {
    if (!is_env_var_available(env))
      return false;
  }

  // Check any_bins: at least one must exist
  if (!skill.any_bins.empty()) {
    bool found = false;
    for (const auto& bin : skill.any_bins) {
      if (is_binary_available(bin)) {
        found = true;
        break;
      }
    }
    if (!found)
      return false;
  }

  return true;
}

std::string
SkillLoader::GetSkillContext(const std::vector<SkillMetadata>& skills) const {
  if (skills.empty())
    return "";

  std::ostringstream oss;
  oss << "# Available Skills\n\n";
  for (const auto& skill : skills) {
    oss << "## " << skill.name;
    if (!skill.emoji.empty())
      oss << " " << skill.emoji;
    oss << "\n";
    if (!skill.description.empty())
      oss << skill.description << "\n";
    if (!skill.content.empty()) {
      // Substitute {{SKILL_DIR}} with the skill's root directory
      std::string content = skill.content;
      const std::string placeholder = "{{SKILL_DIR}}";
      // Use forward slashes so the path survives JSON round-tripping
      // (Windows backslash sequences like \r, \t get misinterpreted).
      const std::string skill_dir = skill.root_dir.generic_string();
      std::string::size_type pos = 0;
      while ((pos = content.find(placeholder, pos)) != std::string::npos) {
        content.replace(pos, placeholder.size(), skill_dir);
        pos += skill_dir.size();
      }
      oss << "\n" << content << "\n";
    }
    oss << "\n";
  }
  return oss.str();
}

bool SkillLoader::InstallSkill(const SkillMetadata& skill) {
  for (const auto& install : skill.installs) {
    std::string method = install.EffectiveMethod();
    std::string formula = install.EffectiveFormula();
    if (method.empty() || formula.empty()) continue;

    std::string cmd;
    if (method == "node" || method == "npm")
      cmd = "npm install -g " + formula;
    else if (method == "go")
      cmd = "go install " + formula;
    else if (method == "uv" || method == "pip")
      cmd = "pip install " + formula;
    else {
      spdlog::warn("Unsupported install method: {}", method);
      continue;
    }

    spdlog::info("Installing skill dependency: {}", cmd);
    int rc = std::system(cmd.c_str());
    if (rc != 0) {
      spdlog::error("Install failed (exit {}): {}", rc, cmd);
      return false;
    }
  }
  return true;
}

std::vector<SkillCommand>
SkillLoader::GetAllCommands(const std::vector<SkillMetadata>& skills) const {
  std::vector<SkillCommand> cmds;
  for (const auto& skill : skills) {
    for (const auto& cmd : skill.commands)
      cmds.push_back(cmd);
  }
  return cmds;
}

// --- Private methods ---

SkillMetadata
SkillLoader::parse_skill_file(const std::filesystem::path& skill_file) const {
  std::ifstream f(skill_file);
  if (!f)
    throw std::runtime_error("Cannot open: " + skill_file.string());

  std::string content((std::istreambuf_iterator<char>(f)), {});

  SkillMetadata meta;
  meta.root_dir = skill_file.parent_path();

  // Check for resource directories
  auto root = meta.root_dir;
  if (std::filesystem::exists(root / "scripts"))
    meta.scripts_dir = (root / "scripts").string();
  if (std::filesystem::exists(root / "references"))
    meta.references_dir = (root / "references").string();
  if (std::filesystem::exists(root / "assets"))
    meta.assets_dir = (root / "assets").string();

  // Parse YAML frontmatter (between --- markers)
  size_t fm_start = content.find("---");
  if (fm_start != std::string::npos) {
    size_t fm_end = content.find("---", fm_start + 3);
    if (fm_end != std::string::npos) {
      std::string yaml_str =
          content.substr(fm_start + 3, fm_end - fm_start - 3);
      try {
        auto fm = parse_yaml_frontmatter(yaml_str);
        if (fm.contains("name"))
          meta.name = fm["name"].get<std::string>();
        if (fm.contains("description"))
          meta.description = fm["description"].get<std::string>();
        if (fm.contains("emoji"))
          meta.emoji = fm["emoji"].get<std::string>();
        if (fm.contains("always"))
          meta.always = fm["always"].get<bool>();
        if (fm.contains("homepage"))
          meta.homepage = fm["homepage"].get<std::string>();
      } catch (...) {
        // Ignore frontmatter parse errors; fall back to filename
      }
      // Content after frontmatter
      meta.content = content.substr(fm_end + 3);
    } else {
      meta.content = content;
    }
  } else {
    meta.content = content;
  }

  // Fallback name from directory
  if (meta.name.empty())
    meta.name = skill_file.parent_path().filename().string();

  return meta;
}

nlohmann::json
SkillLoader::parse_yaml_frontmatter(const std::string& yaml_str) const {
  // Simple key: value parser (no deep nesting needed for frontmatter)
  nlohmann::json result = nlohmann::json::object();
  std::istringstream stream(yaml_str);
  std::string line;
  while (std::getline(stream, line)) {
    // Trim
    size_t start = line.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) continue;
    line = line.substr(start);
    if (line.empty() || line[0] == '#') continue;

    size_t colon = line.find(':');
    if (colon == std::string::npos) continue;

    std::string key = line.substr(0, colon);
    std::string val = line.substr(colon + 1);

    // Trim key/val
    while (!key.empty() && key.back() == ' ') key.pop_back();
    size_t vs = val.find_first_not_of(" \t");
    if (vs != std::string::npos) val = val.substr(vs);
    while (!val.empty() && (val.back() == ' ' || val.back() == '\r'))
      val.pop_back();

    // Remove surrounding quotes
    if (val.size() >= 2 &&
        ((val.front() == '"' && val.back() == '"') ||
         (val.front() == '\'' && val.back() == '\''))) {
      val = val.substr(1, val.size() - 2);
    }

    if (val == "true")
      result[key] = true;
    else if (val == "false")
      result[key] = false;
    else
      result[key] = val;
  }
  return result;
}

bool SkillLoader::is_binary_available(const std::string& binary_name) const {
#ifdef _WIN32
  std::string cmd = "where " + binary_name + " >nul 2>&1";
#else
  std::string cmd = "which " + binary_name + " >/dev/null 2>&1";
#endif
  return std::system(cmd.c_str()) == 0;
}

bool SkillLoader::is_env_var_available(const std::string& env_var) const {
  return std::getenv(env_var.c_str()) != nullptr;
}

bool SkillLoader::check_os_restriction(
    const std::vector<std::string>& os_list) const {
  std::string current = get_current_os();
  for (const auto& os : os_list) {
    if (os == current) return true;
  }
  return false;
}

std::string SkillLoader::get_current_os() const {
#ifdef _WIN32
  return "win32";
#elif defined(__APPLE__)
  return "darwin";
#else
  return "linux";
#endif
}

}  // namespace rimeclaw
