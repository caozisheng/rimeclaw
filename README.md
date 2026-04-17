# RimeClaw

An embedded claw library by c++, forked and simplified from [QuantClaw](https://github.com/QuantClaw/QuantClaw).

RimeClaw supports Anthropic compatible providers, OpenAI compatible providers and local providers of .GGUF models via llama.cpp.

If you're looking for a light-weight, low-dependency, cross-platform C++ agent library that's easy to integrate into any project, this is for you.

# Introduction

**RimeClaw** is an **embedded agent library**—invoked by the host application via C ABI, with a complete core loop but without involving network, deployment, or multi-user functionality.

```text
      ┌─────────────────┐
      │   C ABI facade  │
      │  claw_init/send │
      │  claw_shutdown  │
      └───────┬─────────┘
              │          
  ┌─────────────────────────────┐
  │        Agent Loop           │  ← main loop drives LLM inferrence + tool calling
  ├─────────────────────────────┤
  │     Context Engine          │  ← context build/compress/trim
  ├─────────────────────────────┤
  │    Provider Registry        │  ← multi Provider registry/parser/factory
  ├──────────┬──────────────────┤
  │ Failover │  Cooldown        │  ← Failover + cooldown
  │ Resolver │  Tracker         │
  ├──────────┴──────────────────┤
  │  LLM Providers              │  ← Anthropic / OpenAI / Ollama / Local(llama.cpp)
  └─────────────────────────────┘
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

· llama.cpp-8798

Dashboard GUI uese [ImGUI](https://github.com/ocornut/imgui) and its extensions.

Build system uses [vcpkg](https://github.com/microsoft/vcpkg) to manage dependencies.

To build from source,

```
cd rimeclaw
```

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
- [Qwen2.5-0.5B-Instruct-Q4_K_M](https://huggingface.co/Qwen/Qwen2.5-0.5B-Instruct-GGUF/resolve/main/qwen2.5-0.5b-instruct-q4_k_m.gguf)

- [Qwen3.5-0.8B-Q4_K_M](https://huggingface.co/unsloth/Qwen3.5-0.8B-GGUF/resolve/main/Qwen3.5-0.8B-Q4_K_M.gguf)
4. build
   
   ```
   cmake --build --preset build-all --config Release 
   ```
   
   Post-build will copy rimeclaw_config.json, .rimeclaw and models/ to binary path.
   
   ```
   # Rimeclaw-output-binary-folder 
   ├── binary-files
   ├── rimeclaw_config.json
   ├── .rimeclaw/
   └── models/
   ```
   
   ```
   cd Rimeclaw-output-binary-folder 
   ./test
   ```