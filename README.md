# rimeclaw
An embedded claw library by c++, forked and simplified from [QuantClaw](https://github.com/QuantClaw/QuantClaw).

If you're looking for a lightweight, low-dependency, cross-platform C++ agent library that's easy to integrate into any project, this is for you.

# Introduction

**RimeClaw** is an **embedded agent library**—invoked by the host application via C ABI, with a complete core loop but without involving network, deployment, or multi-user functionality.

RimeClaw is inspired and forked from QuantClaw (https://github.com/QuantClaw/QuantClaw), which is a **standalone deployable agent service**—built on the same agent core, adding a Gateway (WS RPC, authentication, RBAC), CommandQueue (5 concurrency modes), Channel Adapters (Discord/Telegram subprocesses), Plugin Platform (24 hooks, Node.js sidecar), and Daemon lifecycle management, targeting production operations with multi-user and multi-channel scenarios.

Comparison to QuantClaw is quickly shown as follows.

```text
                    RimeClaw                          QuantClaw
              ┌─────────────────┐          ┌──────────────────────────┐
              │   C ABI facade  │          │    CLI / Web Dashboard   │
              │  claw_init/send │          │    Gateway (WS RPC)      │
              │  claw_shutdown  │          │    Channel Adapters      │
              └───────┬─────────┘          └──────────┬───────────────┘
                      │                               │
              ┌───────┴─────────┐          ┌──────────┴───────────────┐
              │   AgentLoop     │          │   CommandQueue           │
              │   (same core)   │          │    → AgentLoop           │
              └───────┬─────────┘          └──────────┬───────────────┘
                      │                               │
              ┌───────┴──────────────────────────────-┴───────────────┐
              │    Kernel share (Provider/Tool/Session/Memory)        │
              └───────────────────────────────────────────────────────┘
                                          │
                                ┌─────────┴─────────--─┐
                                │  QuantClaw specific  │
                                │  Plugin/Hook/Sidecar │
                                │  RBAC/RateLimiter    │
                                │  MCP                 │
                                │  Daemon/Service      │
                                └──────────────────────┘
```

## Overview

RimeClaw includes:
- Session persistence
- Prompt assembly
- Tool invocation
- Provider adaptation
- Context trimming/compression
- Runtime event streams

It exposes its capabilities externally through a very thin C interface:
- `claw_init`
- `claw_send_msg`
- `claw_stop`
- `claw_shutdown`
- `claw_version`
- `claw_free_string`
- `claw_session_list` / `claw_session_get` / `claw_session_delete` / `claw_session_clear`
- `claw_skill_list` / `claw_skill_reload`
- `claw_tool_list` / `claw_tool_register` / `claw_tool_remove`

The real core scheduling logic resides in the internal C objects. 

E.g., `claw_init` is as follows,

```text
C API
  claw_init / claw_send_msg
    -> RimeClawConfig
    -> MemoryManager
    -> SessionManager
    -> SkillLoader  (+ LoadSkillsFromDirectory / LoadSkills + CheckSkillGating)
    -> loaded_skills  (gating-filtered SkillMetadata list)
    -> ProviderRegistry  (providers + model_providers)
    -> ToolRegistry  (+ ToolPermissionChecker + ExecApprovalManager)
    -> PromptBuilder
    -> AgentLoop  (+ FailoverResolver + ProviderRegistry injected)
    -> UsageAccumulator  (AgentLoop)
    -> chain tool  (ToolRegistry)
    -> SubagentManager  (AgentLoop + ToolRegistry, spawn_subagent)
    -> CronScheduler  (Start + cron_jobs.json)
    -> SessionMaintenance  (RunCycle)
    -> DefaultContextEngine  (+ SummaryFn, AgentLoop)
```

## Installation from source

Third-party dependencies of rimeclaw lib includes
· curl-8.17.0
· nlohmann-json-3.12.0
· spdlog-1.16.0

Build system uses [vcpkg](https://github.com/microsoft/vcpkg) to manage dependencies.

To build from source,

1. install vcpkg and set $ENV{VCPKG_ROOT} as 
git clone https://github.com/microsoft/vcpkg.git

2. cmake

  cmake --preset build-lib        # only lib
  
  cmake --preset build-test       # lib + test
  
  cmake --preset build-dashboard  # lib + dashboard(GUI)
  
  cmake --preset build-all        # all

  cmake --build --preset build-lib  # build