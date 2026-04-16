# RimeClaw

An embedded claw library by c++, forked and simplified from [QuantClaw](https://github.com/QuantClaw/QuantClaw).

If you're looking for a light-weight, low-dependency, cross-platform C++ agent library that's easy to integrate into any project, this is for you.

# Introduction

**RimeClaw** is an **embedded agent library**—invoked by the host application via C ABI, with a complete core loop but without involving network, deployment, or multi-user functionality.

RimeClaw is inspired and forked from [QuantClaw](https://github.com/QuantClaw/QuantClaw), which is a **standalone deployable agent service**—built on the same agent core, adding a Gateway (WS RPC, authentication, RBAC), CommandQueue (5 concurrency modes), Channel Adapters (Discord/Telegram subprocesses), Plugin Platform (24 hooks, Node.js sidecar), and Daemon lifecycle management, targeting production operations with multi-user and multi-channel scenarios.

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

## Features

**Session**

| Module                 | Description                                                                                                                               |
| ---------------------- | ----------------------------------------------------------------------------------------------------------------------------------------- |
| SessionManager         | File system-based session persistence, transcripts stored in JSONL format                                                                 |
| Session Key Convention | Format `agent:<agentId>:<rest>`, automatic normalization                                                                                  |
| CRUD Operations        | GetOrCreate / Delete / Clear / List / LoadTranscript                                                                                      |
| SessionMaintenance     | Automatic pruning by time, count/cap limits, archive old sessions<br>- Modes: enforce / warn<br>- Supports duration parsing: 7d, 24h, 30m |
| UsageAccumulator       | Tracks token usage (input/output/turns) per session and globally                                                                          |

**Prompt**

| Module        | Description                                                                                                                                       |
| ------------- | ------------------------------------------------------------------------------------------------------------------------------------------------- |
| PromptBuilder | Builds system prompts: SOUL + AGENTS + TOOLS + Skills + Memory + Runtime<br>- BuildFull(): full prompt<br>- BuildMinimal(): identity + tools only |
| MemoryManager | Loads SOUL.md, USER.md, AGENTS.md, TOOLS.md<br>- File change watcher with hot-reload callbacks                                                    |
| ContentBlock  | Structured message blocks: text / tool_use / tool_result / thinking                                                                               |

**Tools**

| Module       | Description                                                                                                                                                                                                                               |
| ------------ | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| ToolRegistry | Built-in and external MCP tool registration<br>- Built-in: read_file, write_file, edit_file, exec, message, apply_patch, process, memory_search, memory_get<br>- Dynamic register/remove via C API: claw_tool_register / claw_tool_remove |
| ToolChain    | Multi-step tool orchestration<br>- Template engine: {{prev.result}}, {{steps[N].result}}<br>- Error policies: StopOnError / ContinueOnError / Retry                                                                                       |
| BgSession    | Background process management with async execution and status query                                                                                                                                                                       |

**Providers**

| Module                | Description                                                                                                  |
| --------------------- | ------------------------------------------------------------------------------------------------------------ |
| LLMProvider Interface | Dual modes: ChatCompletion + ChatCompletionStream                                                            |
| Implemented Providers | AnthropicProvider, OpenAIProvider, LlamaLocalProvider                                                        |
| ProviderRegistry      | `provider/model` format, model aliases, ModelCatalog (cost/context_window/reasoning), HTTP proxy support     |
| FailoverResolver      | Multi-key rotation, session pinning, model fallback chain                                                    |
| CooldownTracker       | Exponential backoff, Retry-After support, recovery probing                                                   |
| ProviderError         | Error types: RateLimit / Auth / Billing / Transient / ModelNotFound / Timeout / ContextOverflow / BadRequest |
| EmbeddingProvider     | Embedding interface for vector search                                                                        |

**Context**

| Module               | Description                                                                                           |
| -------------------- | ----------------------------------------------------------------------------------------------------- |
| ContextEngine        | Pluggable context strategy, lifecycle: Bootstrap → Assemble → CompactOverflow → AfterTurn             |
| DefaultContextEngine | Combines ContextPruner + SessionCompaction                                                            |
| ContextPruner        | Hierarchical tool result pruning, recent message protection, soft/hard pruning, budget-based trimming |
| SessionCompaction    | Compression on message/token limit, via LLM summary or truncation                                     |
| MultiStageCompaction | Staged compression for large contexts: chunking → summarization → merging                             |
| AgentConfig          | Iteration limit scales linearly with context_window (32K→32, 200K→160)                                |

**Security**

| Module                | Description                                                                         |
| --------------------- | ----------------------------------------------------------------------------------- |
| ToolPermissionChecker | Allow/deny lists with group support                                                 |
| Sandbox               | Path and command allow/denylists, regex, resource limits                            |
| ExecApprovalManager   | Approval modes: off / on_miss / always, glob allowlist, async approval with timeout |
| SecurityConfig        | Global security levels: auto / strict / permissive                                  |

**Others**

| Module          | Description                                                                            |
| --------------- | -------------------------------------------------------------------------------------- |
| SkillLoader     | Loads SKILL.md (YAML frontmatter + Markdown), env guards, auto-install, slash commands |
| SubagentManager | Subagent scheduling: run/session modes, depth/child limits, role tagging               |
| CronScheduler   | Standard 5-field cron, persistent storage, background scheduling                       |
| MemorySearch    | Hybrid search: BM25, vector similarity, time decay, MMR reranking                      |
| C API           | Pure C interface (rimeclaw.h), FFI compatible                                          |
| Cross-platform  | Windows and Unix implementations                                                       |
| SignalHandler   | Graceful stop via signal handling                                                      |

## Installation from source

Third-party dependencies of rimeclaw lib includes

· curl-8.17.0

· nlohmann-json-3.12.0

· spdlog-1.16.0

Build system uses [vcpkg](https://github.com/microsoft/vcpkg) to manage dependencies.

To build from source,

1. install vcpkg and set $ENV{VCPKG_ROOT}
   
   ```
   git clone https://github.com/microsoft/vcpkg.git
   ```

2. cmake presets
   
   ```
   cmake --preset build-lib        # only lib
   cmake --preset build-test       # lib + test
   cmake --preset build-dashboard  # lib + dashboard(GUI)
   cmake --preset build-all        # all
   ```

3. config providers

```
"providers": {
    "minimax": {
      "api": "openai-completions",
      "api_key": "YOUR_API_KEY",
      "base_url": "https://api.minimax.io/v1",
      "timeout": 30
    },
    ...
```

4. Put local models inside 3rd/models

```
"local": {
      "api": "local",
      "extra": {
        "model_path": "models/Qwen3.5-0.8B-Q4_K_M.gguf",
        "n_ctx": 4096,
        "n_gpu_layers": 0,
        "n_threads": 0,
        "n_batch": 512
      }
    }
```

- Qwen2.5-0.5B-Instruct-Q4_K_M: https://huggingface.co/Qwen/Qwen2.5-0.5B-Instruct-GGUF/resolve/main/qwen2.5-0.5b-instruct-q4_k_m.gguf

- Qwen3.5-0.8B-Q4_K_M: https://huggingface.co/unsloth/Qwen3.5-0.8B-GGUF/resolve/main/Qwen3.5-0.8B-Q4_K_M.gguf
4. build
   
   ```
   cmake --build --preset build-all  
   ```
   
   By-default path of rimeclaw_config.json and .rimeclaw
   
   ```
   # Rimeclaw-output-binary-folder 
   ├── binary-files
   ├── rimeclaw_config.json
   ├── .rimeclaw/
   └── models/
   ```