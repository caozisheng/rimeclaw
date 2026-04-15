// Copyright 2025-2026 RimeClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "rimeclaw/rimeclaw.h"

#include <cstring>
#include <memory>
#include <string>

#include <spdlog/spdlog.h>

#include "rimeclaw/config.hpp"
#include "rimeclaw/constants.hpp"
#include "rimeclaw/core/agent_loop.hpp"
#include "rimeclaw/core/content_block.hpp"
#include "rimeclaw/core/memory_manager.hpp"
#include "rimeclaw/core/prompt_builder.hpp"
#include "rimeclaw/core/skill_loader.hpp"
#include "rimeclaw/providers/provider_registry.hpp"
#include "rimeclaw/providers/failover_resolver.hpp"
#include "rimeclaw/session/session_manager.hpp"
#include "rimeclaw/tools/tool_registry.hpp"
#include "rimeclaw/security/exec_approval.hpp"
#include "rimeclaw/security/sandbox.hpp"
#include "rimeclaw/security/tool_permissions.hpp"

// Extended circuit includes
#include "rimeclaw/core/cron_scheduler.hpp"
#include "rimeclaw/core/default_context_engine.hpp"
#include "rimeclaw/core/subagent.hpp"
#include "rimeclaw/core/usage_accumulator.hpp"
#include "rimeclaw/session/session_maintenance.hpp"

namespace {

struct RimeClawContext {
  rimeclaw::RimeClawConfig config;
  std::shared_ptr<rimeclaw::SessionManager> session_manager;
  std::shared_ptr<rimeclaw::MemoryManager> memory_manager;
  std::shared_ptr<rimeclaw::SkillLoader> skill_loader;
  std::shared_ptr<rimeclaw::ToolRegistry> tool_registry;
  std::shared_ptr<rimeclaw::ProviderRegistry> provider_registry;
  std::shared_ptr<rimeclaw::FailoverResolver> failover_resolver;
  std::shared_ptr<rimeclaw::PromptBuilder> prompt_builder;
  std::shared_ptr<rimeclaw::AgentLoop> agent_loop;
  std::vector<rimeclaw::SkillMetadata> loaded_skills;

  // Resolved paths (needed for skill reload)
  std::filesystem::path rimeclaw_root;
  std::filesystem::path workspace_path;

  // Extended circuits
  std::shared_ptr<rimeclaw::UsageAccumulator> usage_accumulator;
  std::shared_ptr<rimeclaw::SubagentManager> subagent_manager;
  std::shared_ptr<rimeclaw::CronScheduler> cron_scheduler;
  std::shared_ptr<rimeclaw::SessionMaintenance> session_maintenance;
  std::shared_ptr<rimeclaw::DefaultContextEngine> context_engine;
  std::filesystem::path cron_storage_path;
};

}  // namespace

extern "C" {

RimeClawHandle claw_init(const char* config_path) {
  if (!config_path) {
    spdlog::error("[rimeclaw] claw_init: config_path is NULL");
    return nullptr;
  }

  auto ctx = std::make_unique<RimeClawContext>();

  try {
    ctx->config = rimeclaw::RimeClawConfig::LoadFromFile(config_path);
  } catch (const std::exception& e) {
    spdlog::error("[rimeclaw] Failed to load config '{}': {}", config_path, e.what());
    return nullptr;
  }

  // Resolve .rimeclaw root:
  //   home has value and non-empty -> use that path
  //   home has value but empty ("") -> use config file's directory
  //   home is nullopt (field absent) -> use ~/
  std::filesystem::path rimeclaw_root;
  if (ctx->config.system.home.has_value()) {
    if (ctx->config.system.home->empty()) {
      rimeclaw_root =
          std::filesystem::path(config_path).parent_path() / ".rimeclaw";
    } else {
      rimeclaw_root =
          std::filesystem::path(*ctx->config.system.home) / ".rimeclaw";
    }
  } else {
    rimeclaw_root =
        rimeclaw::RimeClawConfig::ExpandHome("~/.rimeclaw");
  }

  // Ensure rimeclaw_root is always an absolute, normalized path.
  // Case B (home="") can produce a relative path when config_path has no
  // directory component; Case A (nullopt) may have mixed separators on Windows.
  {
    std::error_code ec;
    auto abs =
        std::filesystem::weakly_canonical(std::filesystem::absolute(rimeclaw_root), ec);
    if (!ec)
      rimeclaw_root = abs;
  }

  // Unified multi-agent layout: all per-agent data lives under agents/<id>/
  constexpr const char* kDefaultAgentId = "main";
  std::filesystem::path agent_dir = rimeclaw_root / "agents" / kDefaultAgentId;
  std::filesystem::path workspace_path = agent_dir / "workspace";
  std::filesystem::path sessions_dir = agent_dir / "sessions";
  std::filesystem::create_directories(workspace_path);
  std::filesystem::create_directories(sessions_dir);

  ctx->rimeclaw_root = rimeclaw_root;
  ctx->workspace_path = workspace_path;

  ctx->memory_manager =
      std::make_shared<rimeclaw::MemoryManager>(workspace_path);
  ctx->session_manager =
      std::make_shared<rimeclaw::SessionManager>(sessions_dir);

  ctx->skill_loader = std::make_shared<rimeclaw::SkillLoader>();

  // Load skills from config and filter by gating.
  // If skills.path is not configured, fall back to <rimeclaw_root>/skills.
  {
    std::vector<rimeclaw::SkillMetadata> all_skills;
    if (!ctx->config.skills.path.empty()) {
      all_skills =
          ctx->skill_loader->LoadSkills(ctx->config.skills, workspace_path);
    } else {
      auto default_skills_dir = rimeclaw_root / "skills";
      all_skills =
          ctx->skill_loader->LoadSkillsFromDirectory(default_skills_dir);
    }
    for (auto& skill : all_skills) {
      if (ctx->skill_loader->CheckSkillGating(skill)) {
        ctx->loaded_skills.push_back(std::move(skill));
      }
    }
    if (!ctx->loaded_skills.empty()) {
      spdlog::info("[rimeclaw] Loaded {} skill(s)", ctx->loaded_skills.size());
    }
  }

  // Resolve relative model_path in local provider config to absolute path.
  // Rules: home non-empty → relative to home, home="" → relative to config dir,
  //        home absent → relative to ~/
  for (auto& [id, prov] : ctx->config.providers) {
    if (prov.extra.contains("model_path")) {
      std::string mp = prov.extra.value("model_path", "");
      if (!mp.empty() && std::filesystem::path(mp).is_relative()) {
        std::filesystem::path base;
        if (ctx->config.system.home.has_value()) {
          if (ctx->config.system.home->empty()) {
            base = std::filesystem::path(config_path).parent_path();
          } else {
            base = std::filesystem::path(*ctx->config.system.home);
          }
        } else {
          base = rimeclaw::RimeClawConfig::ExpandHome("~");
        }
        std::error_code ec;
        auto abs = std::filesystem::weakly_canonical(base / mp, ec);
        if (!ec) {
          prov.extra["model_path"] = abs.string();
        }
      }
    }
  }

  ctx->provider_registry = std::make_shared<rimeclaw::ProviderRegistry>();
  ctx->provider_registry->RegisterBuiltinFactories();
  ctx->provider_registry->LoadModelProviders(ctx->config.providers);
  ctx->provider_registry->LoadModelProviders(ctx->config.model_providers);

  auto model_ref =
      ctx->provider_registry->ResolveModel(ctx->config.agent.model);
  auto provider = ctx->provider_registry->GetProviderForModel(model_ref);
  if (!provider) {
    spdlog::error("[rimeclaw] Failed to resolve provider for model: {}",
                   ctx->config.agent.model);
    return nullptr;
  }
  // Keep full "provider/model" format in agent config so that
  // FailoverResolver / ProviderRegistry can resolve the correct provider.
  // resolve_provider() inside AgentLoop will strip to bare model name
  // before building the API request.

  ctx->failover_resolver = std::make_shared<rimeclaw::FailoverResolver>(
      ctx->provider_registry.get());

  ctx->tool_registry = std::make_shared<rimeclaw::ToolRegistry>();
  ctx->tool_registry->RegisterBuiltinTools();
  ctx->tool_registry->SetWorkspace(workspace_path.string());

  // Wire tool permission checker from config (tools.allow / tools.deny).
  // Only inject when at least one rule is configured; otherwise all tools
  // remain exposed (backward-compatible default).
  if (!ctx->config.tools_permission.allow.empty() ||
      !ctx->config.tools_permission.deny.empty()) {
    auto perm_checker = std::make_shared<rimeclaw::ToolPermissionChecker>(
        ctx->config.tools_permission);
    ctx->tool_registry->SetPermissionChecker(perm_checker);
    spdlog::info("[rimeclaw] Tool permission checker installed (allow={} deny={})",
                  ctx->config.tools_permission.allow.size(),
                  ctx->config.tools_permission.deny.size());
  }

  // Wire exec approval manager from config (tools.exec.ask / allowlist).
  // Parses the raw JSON stored by config loader into ExecApprovalConfig.
  if (!ctx->config.exec_approval_config.is_null() &&
      ctx->config.exec_approval_config.is_object()) {
    const auto& eaj = ctx->config.exec_approval_config;
    rimeclaw::ExecApprovalConfig eac;
    eac.ask = rimeclaw::AskModeFromString(
        eaj.value("ask", "on_miss"));
    eac.timeout_seconds = eaj.value("timeout", 30);
    if (eaj.contains("allowlist") && eaj["allowlist"].is_array()) {
      for (const auto& p : eaj["allowlist"]) {
        if (p.is_string())
          eac.allowlist_patterns.push_back(p.get<std::string>());
      }
    }
    auto approval_mgr =
        std::make_shared<rimeclaw::ExecApprovalManager>(std::move(eac));
    ctx->tool_registry->SetApprovalManager(approval_mgr);
    spdlog::info("[rimeclaw] Exec approval manager installed (ask={} allowlist={})",
                  rimeclaw::AskModeToString(approval_mgr->GetConfig().ask),
                  approval_mgr->GetConfig().allowlist_patterns.size());
  }

  ctx->prompt_builder = std::make_shared<rimeclaw::PromptBuilder>(
      ctx->memory_manager, ctx->skill_loader, ctx->tool_registry,
      &ctx->config);

  ctx->agent_loop = std::make_shared<rimeclaw::AgentLoop>(
      ctx->memory_manager, ctx->skill_loader, ctx->tool_registry, provider,
      ctx->config.agent);

  // Wire failover into AgentLoop so resolve_provider() can use the full
  // three-tier resolution: FailoverResolver → ProviderRegistry → injected provider.
  ctx->agent_loop->SetProviderRegistry(ctx->provider_registry.get());
  ctx->agent_loop->SetFailoverResolver(ctx->failover_resolver.get());

  // Configure fallback chain from agent config (e.g. ["openai/gpt-4o", ...])
  if (!ctx->config.agent.fallbacks.empty()) {
    ctx->failover_resolver->SetFallbackChain(ctx->config.agent.fallbacks);
  }

  // Load auth profiles from provider configs for key rotation / cooldown
  auto load_profiles = [&](const auto& provider_map) {
    for (const auto& [provider_id, provider_cfg] : provider_map) {
      if (provider_cfg.profiles.empty()) continue;
      std::vector<rimeclaw::AuthProfile> profiles;
      profiles.reserve(provider_cfg.profiles.size());
      for (const auto& p : provider_cfg.profiles) {
        profiles.push_back({p.id, p.api_key, p.api_key_env, p.priority});
      }
      ctx->failover_resolver->SetProfiles(provider_id, profiles);
    }
  };
  load_profiles(ctx->config.providers);
  load_profiles(ctx->config.model_providers);

  // =====================================================================
  // Extended circuits wiring
  // =====================================================================

  // --- P1: UsageAccumulator ---
  ctx->usage_accumulator = std::make_shared<rimeclaw::UsageAccumulator>();
  ctx->agent_loop->SetUsageAccumulator(ctx->usage_accumulator);

  // --- P1: Chain tool ---
  ctx->tool_registry->RegisterChainTool();

  // --- P2: SubagentManager ---
  {
    rimeclaw::SubagentConfig subagent_cfg;
    if (!ctx->config.subagent_config.is_null() &&
        ctx->config.subagent_config.is_object()) {
      subagent_cfg =
          rimeclaw::SubagentConfig::FromJson(ctx->config.subagent_config);
    }

    auto* raw_ctx = ctx.get();
    rimeclaw::AgentRunFn runner = [raw_ctx](
                                      const rimeclaw::SpawnParams& params,
                                      const std::string& /*run_id*/,
                                      const std::string& session_key)
        -> std::string {
      std::string sys = raw_ctx->prompt_builder->BuildFull(
          "default", raw_ctx->loaded_skills);
      std::vector<rimeclaw::Message> history;
      try {
        auto msgs = raw_ctx->agent_loop->ProcessMessage(params.task, history,
                                                        sys, session_key);
        std::string output;
        for (auto it = msgs.rbegin(); it != msgs.rend(); ++it) {
          if (it->role == "assistant") {
            // Extract text from first text content block
            for (const auto& cb : it->content) {
              if (cb.type == "text") {
                output = cb.text;
                break;
              }
            }
            if (!output.empty())
              break;
          }
        }
        nlohmann::json r;
        r["status"] = "completed";
        r["output"] = output;
        return r.dump();
      } catch (const std::exception& e) {
        nlohmann::json r;
        r["status"] = "error";
        r["error"] = e.what();
        return r.dump();
      }
    };

    ctx->subagent_manager =
        std::make_shared<rimeclaw::SubagentManager>(subagent_cfg, runner);
    ctx->agent_loop->SetSubagentManager(ctx->subagent_manager.get());
    ctx->tool_registry->SetSubagentManager(ctx->subagent_manager.get());
  }

  // --- P3: CronScheduler ---
  {
    ctx->cron_scheduler = std::make_shared<rimeclaw::CronScheduler>();
    ctx->cron_storage_path = agent_dir / "cron_jobs.json";
    if (std::filesystem::exists(ctx->cron_storage_path)) {
      ctx->cron_scheduler->Load(ctx->cron_storage_path.string());
    }

    auto* cron_ctx = ctx.get();
    ctx->cron_scheduler->Start([cron_ctx](const rimeclaw::CronJob& job) {
      spdlog::info("[rimeclaw] Cron job fired: {} -> {}", job.name, job.session_key);
      try {
        auto history =
            cron_ctx->session_manager->LoadTranscript(job.session_key);
        std::vector<rimeclaw::Message> msgs;
        msgs.reserve(history.size());
        for (const auto& sm : history) {
          rimeclaw::Message m;
          m.role = sm.role;
          m.content = sm.content;
          msgs.push_back(std::move(m));
        }
        std::string sys = cron_ctx->prompt_builder->BuildFull(
            "default", cron_ctx->loaded_skills);
        cron_ctx->agent_loop->ProcessMessage(job.message, msgs, sys,
                                             job.session_key);
      } catch (const std::exception& e) {
        spdlog::error("[rimeclaw] Cron job {} error: {}", job.id, e.what());
      }
    });
  }

  // --- P4: SessionMaintenance ---
  if (!ctx->config.session_maintenance_config.is_null() &&
      ctx->config.session_maintenance_config.is_object()) {
    auto maint_cfg = rimeclaw::SessionMaintenanceConfig::FromJson(
        ctx->config.session_maintenance_config);
    ctx->session_maintenance =
        std::make_shared<rimeclaw::SessionMaintenance>(maint_cfg, sessions_dir);
    ctx->session_maintenance->RunCycle();
    spdlog::info("[rimeclaw] Session maintenance: initial cycle complete");
  }

  // --- P5: DefaultContextEngine + SummaryFn ---
  {
    auto ce = std::make_shared<rimeclaw::DefaultContextEngine>(
        ctx->config.agent);
    auto llm_for_summary = provider;  // capture shared_ptr while in scope
    ce->SetSummaryFn(
        [llm_for_summary](
            const std::vector<rimeclaw::Message>& messages) -> std::string {
          std::vector<rimeclaw::Message> req;
          rimeclaw::Message sys_msg;
          sys_msg.role = "system";
          sys_msg.content = {rimeclaw::ContentBlock::MakeText(
              "Summarize the following conversation concisely, "
              "preserving key facts, decisions, and code references.")};
          req.push_back(std::move(sys_msg));
          for (const auto& m : messages) {
            if (m.role != "system")
              req.push_back(m);
          }
          rimeclaw::ChatCompletionRequest creq;
          creq.messages = req;
          creq.max_tokens = 2048;
          auto resp = llm_for_summary->ChatCompletion(creq);
          return resp.content;
        });
    ctx->context_engine = ce;
    ctx->agent_loop->SetContextEngine(ce);
  }

  spdlog::info("[rimeclaw] Initialised. model={}", ctx->config.agent.model);
  return static_cast<RimeClawHandle>(ctx.release());
}

int claw_send_msg(RimeClawHandle handle, const char* session_key,
                  const char* message, RimeClawEventCallback callback,
                  void* userdata) {
  if (!handle || !message) {
    spdlog::error("[rimeclaw] claw_send_msg: invalid arguments");
    return -1;
  }

  auto* ctx = static_cast<RimeClawContext*>(handle);
  const std::string key =
      (session_key && session_key[0]) ? session_key : "default";
  const std::string msg_str(message);

  rimeclaw::SessionHandle sh = ctx->session_manager->GetOrCreate(key);
  std::vector<rimeclaw::SessionMessage> history =
      ctx->session_manager->LoadTranscript(sh.session_key);

  // Set session key so FailoverResolver can do session pinning
  ctx->agent_loop->SetSessionKey(key);

  // Sync session key for subagent spawn context
  if (ctx->subagent_manager) {
    ctx->tool_registry->SetSubagentManager(ctx->subagent_manager.get(), key);
  }

  std::vector<rimeclaw::Message> history_msgs;
  history_msgs.reserve(history.size());
  for (const auto& sm : history) {
    rimeclaw::Message m;
    m.role = sm.role;
    m.content = sm.content;
    history_msgs.push_back(std::move(m));
  }

  const std::string system_prompt =
      ctx->prompt_builder->BuildFull("default", ctx->loaded_skills);

  rimeclaw::AgentEventCallback event_cb;
  if (callback) {
    event_cb = [callback, userdata](const rimeclaw::AgentEvent& ev) {
      try {
        const std::string data_str = ev.data.is_null()
                                         ? ""
                                         : (ev.data.is_string()
                                                ? ev.data.get<std::string>()
                                                : ev.data.dump());
        callback(ev.type.c_str(),
                 ev.data.is_null() ? nullptr : data_str.c_str(), userdata);
      } catch (const std::exception& e) {
        spdlog::error("[rimeclaw] event callback exception: {}", e.what());
      } catch (...) {
        spdlog::error("[rimeclaw] event callback unknown exception");
      }
    };
  }

  std::vector<rimeclaw::Message> new_messages;
  try {
    if (event_cb) {
      new_messages = ctx->agent_loop->ProcessMessageStream(
          msg_str, history_msgs, system_prompt, event_cb, key);
    } else {
      new_messages = ctx->agent_loop->ProcessMessage(msg_str, history_msgs,
                                                     system_prompt, key);
    }
  } catch (const std::exception& e) {
    spdlog::error("[rimeclaw] ProcessMessage error: {}", e.what());
    if (callback) {
      callback("error", e.what(), userdata);
    }
    return -1;
  } catch (...) {
    spdlog::error("[rimeclaw] ProcessMessage unknown exception (non-std)");
    if (callback) {
      callback("error", "Unknown internal error", userdata);
    }
    return -1;
  }

  rimeclaw::SessionMessage user_msg;
  user_msg.role = "user";
  user_msg.content.push_back(rimeclaw::ContentBlock::MakeText(msg_str));
  ctx->session_manager->AppendMessage(sh.session_key, user_msg);

  for (const auto& m : new_messages) {
    rimeclaw::SessionMessage sm;
    sm.role = m.role;
    sm.content = m.content;
    ctx->session_manager->AppendMessage(sh.session_key, sm);
  }

  return 0;
}

void claw_stop(RimeClawHandle handle) {
  if (!handle) return;
  static_cast<RimeClawContext*>(handle)->agent_loop->RequestStop();
}

void claw_shutdown(RimeClawHandle handle) {
  if (!handle) return;
  auto* ctx = static_cast<RimeClawContext*>(handle);

  // Stop cron scheduler and persist jobs before teardown
  if (ctx->cron_scheduler) {
    ctx->cron_scheduler->Stop();
    if (!ctx->cron_storage_path.empty()) {
      ctx->cron_scheduler->Save(ctx->cron_storage_path.string());
    }
  }

  // Final session maintenance cycle
  if (ctx->session_maintenance) {
    ctx->session_maintenance->RunCycle();
  }

  delete ctx;
  spdlog::info("[rimeclaw] Shutdown complete.");
}

const char* claw_version(void) {
  return rimeclaw::kVersion;
}

// ---------------------------------------------------------------------------
// Helper: copy std::string to caller-owned char*
// ---------------------------------------------------------------------------
static const char* to_c_string(const std::string& s) {
  char* buf = new char[s.size() + 1];
  std::memcpy(buf, s.c_str(), s.size() + 1);
  return buf;
}

void claw_free_string(const char* str) {
  delete[] str;
}

// ---------------------------------------------------------------------------
// Session management
// ---------------------------------------------------------------------------

const char* claw_session_list(RimeClawHandle handle) {
  if (!handle) return nullptr;
  auto* ctx = static_cast<RimeClawContext*>(handle);
  auto sessions = ctx->session_manager->ListSessions();
  nlohmann::json arr = nlohmann::json::array();
  for (const auto& s : sessions) {
    arr.push_back({
        {"key", s.session_key},
        {"id", s.session_id},
        {"display_name", s.display_name},
        {"channel", s.channel},
        {"message_count", s.message_count},
        {"created_at", s.created_at},
        {"updated_at", s.updated_at},
    });
  }
  return to_c_string(arr.dump());
}

const char* claw_session_get(RimeClawHandle handle, const char* session_key) {
  if (!handle || !session_key) return nullptr;
  auto* ctx = static_cast<RimeClawContext*>(handle);
  auto data = ctx->session_manager->GetSession(session_key);
  if (!data.has_value()) return nullptr;
  nlohmann::json j = {
      {"key", data->session_key},
      {"id", data->session_id},
      {"display_name", data->display_name},
      {"channel", data->channel},
      {"message_count", data->message_count},
      {"created_at", data->created_at},
      {"updated_at", data->updated_at},
  };
  return to_c_string(j.dump());
}

const char* claw_session_transcript(RimeClawHandle handle,
                                    const char* session_key) {
  if (!handle || !session_key) return nullptr;
  auto* ctx = static_cast<RimeClawContext*>(handle);
  auto messages = ctx->session_manager->LoadTranscript(session_key);
  nlohmann::json arr = nlohmann::json::array();
  for (const auto& sm : messages) {
    nlohmann::json msg;
    msg["role"] = sm.role;
    // Flatten content blocks
    std::string text_content;
    nlohmann::json tool_calls = nlohmann::json::array();
    for (const auto& cb : sm.content) {
      if (cb.type == "text" || cb.type == "thinking") {
        if (!text_content.empty()) text_content += "\n";
        text_content += cb.text;
      } else if (cb.type == "tool_use") {
        tool_calls.push_back({
            {"id", cb.id}, {"name", cb.name},
            {"input", cb.input.is_null() ? "" : cb.input.dump(2)}});
      } else if (cb.type == "tool_result") {
        tool_calls.push_back({
            {"tool_use_id", cb.tool_use_id}, {"content", cb.content},
            {"completed", true}});
      }
    }
    msg["content"] = text_content;
    if (!tool_calls.empty()) msg["tool_calls"] = tool_calls;
    arr.push_back(std::move(msg));
  }
  return to_c_string(arr.dump());
}

int claw_session_delete(RimeClawHandle handle, const char* session_key) {
  if (!handle || !session_key) return -1;
  auto* ctx = static_cast<RimeClawContext*>(handle);
  return ctx->session_manager->DeleteSession(session_key) ? 0 : -1;
}

int claw_session_clear(RimeClawHandle handle, const char* session_key) {
  if (!handle || !session_key) return -1;
  auto* ctx = static_cast<RimeClawContext*>(handle);
  return ctx->session_manager->ClearTranscript(session_key) ? 0 : -1;
}

// ---------------------------------------------------------------------------
// Skill management
// ---------------------------------------------------------------------------

const char* claw_skill_list(RimeClawHandle handle) {
  if (!handle) return nullptr;
  auto* ctx = static_cast<RimeClawContext*>(handle);
  nlohmann::json arr = nlohmann::json::array();
  for (const auto& sk : ctx->loaded_skills) {
    nlohmann::json cmds = nlohmann::json::array();
    for (const auto& cmd : sk.commands) {
      cmds.push_back({
          {"name", cmd.name},
          {"description", cmd.description},
          {"tool_name", cmd.tool_name},
      });
    }
    arr.push_back({
        {"name", sk.name},
        {"description", sk.description},
        {"always", sk.always},
        {"root_dir", sk.root_dir.generic_string()},
        {"commands", cmds},
    });
  }
  return to_c_string(arr.dump());
}

int claw_skill_reload(RimeClawHandle handle) {
  if (!handle) return -1;
  auto* ctx = static_cast<RimeClawContext*>(handle);

  std::vector<rimeclaw::SkillMetadata> all_skills;
  if (!ctx->config.skills.path.empty()) {
    all_skills =
        ctx->skill_loader->LoadSkills(ctx->config.skills, ctx->workspace_path);
  } else {
    auto default_skills_dir = ctx->rimeclaw_root / "skills";
    all_skills =
        ctx->skill_loader->LoadSkillsFromDirectory(default_skills_dir);
  }

  ctx->loaded_skills.clear();
  for (auto& skill : all_skills) {
    if (ctx->skill_loader->CheckSkillGating(skill)) {
      ctx->loaded_skills.push_back(std::move(skill));
    }
  }

  spdlog::info("[rimeclaw] Reloaded {} skill(s)", ctx->loaded_skills.size());
  return static_cast<int>(ctx->loaded_skills.size());
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

const char* claw_extract_event_text(const char* event_data) {
  if (!event_data) return nullptr;

  // Fast-path: if it doesn't start with '{', it's already plain text.
  if (event_data[0] != '{') {
    return to_c_string(event_data);
  }

  // Try to parse as JSON and extract "text" field.
  try {
    auto j = nlohmann::json::parse(event_data);
    if (j.is_object() && j.contains("text") && j["text"].is_string()) {
      return to_c_string(j["text"].get<std::string>());
    }
  } catch (...) {
    // Not valid JSON — fall through and return as-is.
  }

  return to_c_string(event_data);
}

// ---------------------------------------------------------------------------
// Tool management
// ---------------------------------------------------------------------------

const char* claw_tool_list(RimeClawHandle handle) {
  if (!handle) return nullptr;
  auto* ctx = static_cast<RimeClawContext*>(handle);
  auto schemas = ctx->tool_registry->GetToolSchemas();
  nlohmann::json arr = nlohmann::json::array();
  for (const auto& s : schemas) {
    arr.push_back({
        {"name", s.name},
        {"description", s.description},
        {"is_external", ctx->tool_registry->IsExternalTool(s.name)},
    });
  }
  return to_c_string(arr.dump());
}

int claw_tool_register(RimeClawHandle handle, const char* schema_json) {
  if (!handle || !schema_json) return -1;
  auto* ctx = static_cast<RimeClawContext*>(handle);
  try {
    auto j = nlohmann::json::parse(schema_json);
    std::string name = j.value("name", "");
    std::string desc = j.value("description", "");
    if (name.empty()) return -1;
    nlohmann::json params = j.value("parameters", nlohmann::json::object());

    // Register with a no-op executor (host app provides execution via callback)
    ctx->tool_registry->RegisterExternalTool(
        name, desc, params,
        [name](const nlohmann::json&) -> std::string {
          return nlohmann::json{
              {"error", "External tool '" + name + "' has no executor"}}.dump();
        });
    return 0;
  } catch (const std::exception& e) {
    spdlog::error("[rimeclaw] claw_tool_register failed: {}", e.what());
    return -1;
  }
}

int claw_tool_remove(RimeClawHandle handle, const char* tool_name) {
  if (!handle || !tool_name) return -1;
  auto* ctx = static_cast<RimeClawContext*>(handle);
  return ctx->tool_registry->UnregisterExternalTool(tool_name) ? 0 : -1;
}

}  // extern "C"
