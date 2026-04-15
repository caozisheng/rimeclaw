// Copyright 2025-2026 RimeClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "rimeclaw/providers/provider_registry.hpp"

#include <algorithm>
#include <cstdlib>

#include <spdlog/spdlog.h>

#include "rimeclaw/providers/anthropic_provider.hpp"
#include "rimeclaw/providers/openai_provider.hpp"

#ifdef BUILD_LLAMA_LOCAL_PROVIDER
#include "rimeclaw/providers/llama_local_provider.hpp"
#endif

namespace rimeclaw {

// --- ModelRef ---

ModelRef ModelRef::parse(const std::string& raw,
                         const std::string& default_provider) {
  ModelRef ref;
  auto slash = raw.find('/');
  if (slash != std::string::npos) {
    ref.provider = raw.substr(0, slash);
    ref.model = raw.substr(slash + 1);
  } else {
    ref.provider = default_provider;
    ref.model = raw;
  }
  return ref;
}

// --- ProviderRegistry ---

ProviderRegistry::ProviderRegistry() {}

void ProviderRegistry::RegisterFactory(const std::string& provider_id,
                                       ProviderFactory factory) {
  factories_[provider_id] = std::move(factory);
}

void ProviderRegistry::RegisterBuiltinFactories() {
  // OpenAI-compatible factory (also works for Ollama, Together, etc.)
  RegisterFactory("openai", [](const ProviderEntry& entry) {
    std::string url =
        entry.base_url.empty() ? "https://api.openai.com/v1" : entry.base_url;
    return std::make_shared<OpenAIProvider>(entry.api_key, url, entry.timeout, entry.proxy);
  });

  // Anthropic
  RegisterFactory("anthropic", [](const ProviderEntry& entry) {
    std::string url =
        entry.base_url.empty() ? "https://api.anthropic.com" : entry.base_url;
    return std::make_shared<AnthropicProvider>(entry.api_key, url,
                                               entry.timeout, entry.proxy);
  });

  // Ollama (uses OpenAI-compatible API)
  RegisterFactory("ollama", [](const ProviderEntry& entry) {
    std::string url =
        entry.base_url.empty() ? "http://localhost:11434/v1" : entry.base_url;
    return std::make_shared<OpenAIProvider>(entry.api_key, url, entry.timeout, entry.proxy);
  });

  // Gemini / Google (uses OpenAI-compatible API via base_url override)
  RegisterFactory("gemini", [](const ProviderEntry& entry) {
    std::string url =
        entry.base_url.empty()
            ? "https://generativelanguage.googleapis.com/v1beta/openai"
            : entry.base_url;
    return std::make_shared<OpenAIProvider>(entry.api_key, url, entry.timeout, entry.proxy);
  });

  // Google alias
  RegisterFactory("google", factories_["gemini"]);

  // Bedrock (uses OpenAI-compatible gateway)
  RegisterFactory("bedrock", [](const ProviderEntry& entry) {
    std::string url =
        entry.base_url.empty() ? "http://localhost:8080/v1" : entry.base_url;
    return std::make_shared<OpenAIProvider>(entry.api_key, url, entry.timeout, entry.proxy);
  });

  // OpenRouter
  RegisterFactory("openrouter", [](const ProviderEntry& entry) {
    std::string url = entry.base_url.empty() ? "https://openrouter.ai/api/v1"
                                             : entry.base_url;
    return std::make_shared<OpenAIProvider>(entry.api_key, url, entry.timeout, entry.proxy);
  });

  // Together
  RegisterFactory("together", [](const ProviderEntry& entry) {
    std::string url =
        entry.base_url.empty() ? "https://api.together.xyz/v1" : entry.base_url;
    return std::make_shared<OpenAIProvider>(entry.api_key, url, entry.timeout, entry.proxy);
  });

#ifdef BUILD_LLAMA_LOCAL_PROVIDER
  // Local inference via llama.cpp
  RegisterFactory("local", [](const ProviderEntry& entry) {
    LlamaLocalConfig cfg;
    if (!entry.extra.empty()) {
      cfg = LlamaLocalConfig::FromExtra(entry.extra);
    } else {
      spdlog::error("Local provider entry has empty extra (id='{}', api='{}')",
                    entry.id, entry.api);
    }
    // Allow model_path override from base_url (convenience)
    if (cfg.model_path.empty() && !entry.base_url.empty()) {
      cfg.model_path = entry.base_url;
    }
    if (cfg.model_path.empty()) {
      spdlog::error("Local provider model_path is empty after resolution "
                    "(extra={}, base_url='{}')",
                    entry.extra.dump(), entry.base_url);
    }
    return std::make_shared<LlamaLocalProvider>(cfg);
  });
#endif
}

void ProviderRegistry::AddProvider(const ProviderEntry& entry) {
  entries_[entry.id] = entry;
}

void ProviderRegistry::AddAlias(const std::string& alias,
                                const std::string& target) {
  alias_map_[alias] = target;
}

void ProviderRegistry::LoadFromConfig(const nlohmann::json& providers_json) {
  if (!providers_json.is_object())
    return;

  for (auto& [id, val] : providers_json.items()) {
    ProviderEntry entry;
    entry.id = id;
    entry.display_name = val.value("displayName", id);
    entry.base_url = val.value("baseUrl", std::string{});
    if (entry.base_url.empty()) {
      entry.base_url = val.value("base_url", std::string{});
    }
    entry.api_key = val.value("apiKey", std::string{});
    if (entry.api_key.empty()) {
      entry.api_key = val.value("api_key", std::string{});
    }
    entry.api_key_env = val.value("apiKeyEnv", std::string{});
    if (entry.api_key_env.empty()) {
      entry.api_key_env = val.value("api_key_env", std::string{});
    }
    entry.proxy = val.value("proxy", std::string{});
    entry.timeout = val.value("timeout", 30);
    if (val.contains("extra")) {
      entry.extra = val["extra"];
    }

    // Resolve API key from env if needed
    if (entry.api_key.empty()) {
      entry.api_key = resolve_api_key(entry);
    }

    entries_[id] = entry;
    spdlog::debug("Loaded provider: {}", id);
  }
}

void ProviderRegistry::LoadAliases(const nlohmann::json& aliases_json) {
  if (!aliases_json.is_object())
    return;

  for (auto& [model_ref, val] : aliases_json.items()) {
    if (val.is_object() && val.contains("alias")) {
      alias_map_[val["alias"].get<std::string>()] = model_ref;
    } else if (val.is_string()) {
      alias_map_[val.get<std::string>()] = model_ref;
    }
  }
}

ModelRef
ProviderRegistry::ResolveModel(const std::string& raw,
                               const std::string& default_provider) const {
  // Check alias first
  auto it = alias_map_.find(raw);
  if (it != alias_map_.end()) {
    return ModelRef::parse(it->second, default_provider);
  }
  // If the raw string has no '/' and matches a known provider, treat it as
  // provider-only (e.g. "local" → {provider:"local", model:""})
  if (raw.find('/') == std::string::npos) {
    if (factories_.count(raw) > 0 || entries_.count(raw) > 0) {
      return ModelRef{raw, ""};
    }
  }
  return ModelRef::parse(raw, default_provider);
}

std::shared_ptr<LLMProvider>
ProviderRegistry::GetProvider(const std::string& provider_id) {
  // Return cached instance if available
  auto it = instances_.find(provider_id);
  if (it != instances_.end())
    return it->second;

  // Find entry first (needed to check api field)
  auto eit = entries_.find(provider_id);
  if (eit == entries_.end()) {
    // Create minimal entry with env-based defaults
    ProviderEntry entry;
    entry.id = provider_id;
    entry.api_key = resolve_api_key(entry);
    entries_[provider_id] = entry;
    eit = entries_.find(provider_id);
  }

  // Find factory: first try provider_id directly, then resolve from api field
  auto fit = factories_.find(provider_id);
  if (fit == factories_.end() && !eit->second.api.empty()) {
    // Map api field to factory key:
    //   "anthropic-messages" -> "anthropic"
    //   "openai-completions" -> "openai"
    //   "google-generative-ai" -> "gemini"
    std::string factory_key = resolve_factory_from_api(eit->second.api);
    if (!factory_key.empty()) {
      fit = factories_.find(factory_key);
      spdlog::debug("Provider '{}' using api '{}' -> factory '{}'",
                     provider_id, eit->second.api, factory_key);
    }
  }
  if (fit == factories_.end()) {
    spdlog::error("No factory registered for provider: {} (api: {})",
                   provider_id, eit->second.api);
    return nullptr;
  }

  auto provider = fit->second(eit->second);
  instances_[provider_id] = provider;
  return provider;
}

std::shared_ptr<LLMProvider>
ProviderRegistry::GetProviderForModel(const ModelRef& ref) {
  return GetProvider(ref.provider);
}

std::shared_ptr<LLMProvider>
ProviderRegistry::GetProviderWithKey(const std::string& provider_id,
                                     const std::string& api_key) {
  auto fit = factories_.find(provider_id);
  std::string api_type;
  
  if (fit == factories_.end()) {
    auto eit = entries_.find(provider_id);
    if (eit != entries_.end() && !eit->second.api.empty()) {
      std::string factory_key = resolve_factory_from_api(eit->second.api);
      if (!factory_key.empty()) {
        fit = factories_.find(factory_key);
        api_type = eit->second.api;
      }
    }
    
    if (fit == factories_.end()) {
      spdlog::error("No factory for provider: {}", provider_id);
      return nullptr;
    }
  }

  // Build a temporary entry with the given API key
  ProviderEntry entry;
  auto eit = entries_.find(provider_id);
  if (eit != entries_.end()) {
    entry = eit->second;
  } else {
    entry.id = provider_id;
    entry.api = api_type;
  }
  
  // Override the API key with the one provided (from the profile/failover)
  // If api_key is empty, it means we want to use the default fallback mechanism
  // which was already handled when `entry` was copied from `eit->second`.
  if (!api_key.empty()) {
    entry.api_key = api_key;
  } else if (entry.api_key.empty()) {
    // If it's still empty, try to resolve it from the environment
    entry.api_key = resolve_api_key(entry);
  }
  
  auto provider = fit->second(entry);
  return provider;
}

std::vector<std::string> ProviderRegistry::ProviderIds() const {
  std::vector<std::string> ids;
  for (const auto& [id, _] : entries_) {
    ids.push_back(id);
  }
  std::sort(ids.begin(), ids.end());
  return ids;
}

std::vector<ModelAlias> ProviderRegistry::Aliases() const {
  std::vector<ModelAlias> result;
  for (const auto& [alias, target] : alias_map_) {
    result.push_back({alias, target});
  }
  return result;
}

bool ProviderRegistry::HasProvider(const std::string& provider_id) const {
  return factories_.count(provider_id) > 0 || entries_.count(provider_id) > 0;
}

const ProviderEntry*
ProviderRegistry::GetEntry(const std::string& provider_id) const {
  auto it = entries_.find(provider_id);
  return it != entries_.end() ? &it->second : nullptr;
}

void ProviderRegistry::LoadModelProviders(
    const std::unordered_map<std::string, ProviderConfig>& model_providers) {
  for (const auto& [id, prov] : model_providers) {
    auto it = entries_.find(id);
    if (it != entries_.end()) {
      // Merge into existing entry: update all non-empty fields
      for (const auto& m : prov.models) {
        it->second.models.push_back(m);
      }
      if (!prov.api.empty()) {
        it->second.api = prov.api;
      }
      if (!prov.base_url.empty()) {
        it->second.base_url = prov.base_url;
      }
      if (!prov.api_key.empty()) {
        it->second.api_key = prov.api_key;
      }
      if (!prov.proxy.empty()) {
        it->second.proxy = prov.proxy;
      }
      if (prov.timeout > 0) {
        it->second.timeout = prov.timeout;
      }
      if (!prov.extra.empty()) {
        it->second.extra = prov.extra;
      }
    } else {
      // Create new entry
      ProviderEntry entry;
      entry.id = id;
      entry.display_name = id;
      entry.api_key = prov.api_key;
      entry.base_url = prov.base_url;
      entry.api = prov.api;
      entry.proxy = prov.proxy;
      entry.timeout = prov.timeout;
      entry.extra = prov.extra;
      entry.models = prov.models;

      // Resolve API key from env if needed
      if (entry.api_key.empty()) {
        entry.api_key = resolve_api_key(entry);
      }

      entries_[id] = entry;
    }
    spdlog::debug("Loaded model provider: {} ({} models, base_url={})",
                   id, prov.models.size(), prov.base_url);
  }
}

nlohmann::json ProviderRegistry::ModelCatalogEntry::ToJson() const {
  nlohmann::json j;
  j["id"] = id;
  j["name"] = name;
  j["provider"] = provider;
  if (context_window > 0)
    j["contextWindow"] = context_window;
  j["reasoning"] = reasoning;
  if (!input.empty())
    j["input"] = input;
  if (max_tokens > 0)
    j["maxTokens"] = max_tokens;
  if (cost.input > 0 || cost.output > 0) {
    j["cost"] = {{"input", cost.input}, {"output", cost.output}};
    if (cost.cache_read > 0)
      j["cost"]["cacheRead"] = cost.cache_read;
    if (cost.cache_write > 0)
      j["cost"]["cacheWrite"] = cost.cache_write;
  }
  return j;
}

std::vector<ProviderRegistry::ModelCatalogEntry>
ProviderRegistry::GetModelCatalog() const {
  std::vector<ModelCatalogEntry> catalog;
  for (const auto& [pid, entry] : entries_) {
    for (const auto& m : entry.models) {
      ModelCatalogEntry ce;
      ce.id = m.id;
      ce.name = m.name;
      ce.provider = pid;
      ce.context_window = m.context_window;
      ce.reasoning = m.reasoning;
      ce.input = m.input;
      ce.cost = m.cost;
      ce.max_tokens = m.max_tokens;
      catalog.push_back(std::move(ce));
    }
  }
  return catalog;
}

std::string
ProviderRegistry::resolve_api_key(const ProviderEntry& entry) const {
  // Direct value
  if (!entry.api_key.empty())
    return entry.api_key;

  // Explicit env var
  if (!entry.api_key_env.empty()) {
    const char* val = std::getenv(entry.api_key_env.c_str());
    if (val)
      return val;
  }

  // Convention-based env vars
  std::string upper_id = entry.id;
  std::transform(upper_id.begin(), upper_id.end(), upper_id.begin(), ::toupper);

  // Try PROVIDER_API_KEY (e.g. OPENAI_API_KEY)
  std::string env_name = upper_id + "_API_KEY";
  const char* val = std::getenv(env_name.c_str());
  if (val)
    return val;

  // Try PROVIDER_KEY
  env_name = upper_id + "_KEY";
  val = std::getenv(env_name.c_str());
  if (val)
    return val;

  return "";
}

std::string
ProviderRegistry::resolve_factory_from_api(const std::string& api) const {
  // Map api type string to builtin factory key
  if (api == "anthropic-messages" || api == "anthropic")
    return "anthropic";
  if (api == "openai-completions" || api == "openai-chat" || api == "openai")
    return "openai";
  if (api == "google-generative-ai" || api == "google" || api == "gemini")
    return "gemini";
  if (api == "ollama")
    return "ollama";
  if (api == "openrouter")
    return "openrouter";
  if (api == "together")
    return "together";
  if (api == "bedrock")
    return "bedrock";
  if (api == "local" || api == "llamacpp" || api == "llama.cpp")
    return "local";
  return "";
}

}  // namespace rimeclaw
