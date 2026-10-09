#include <qcode/core/logger.h>
#include <qcode/tools/task_target.h>
#include <qcode/transform/provider_transform.h>

#include <algorithm>
#include <cctype>
#include <sstream>

namespace qcode {
namespace {

std::string to_lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}

const ProviderInfo* find_provider(const std::vector<ProviderInfo>& providers,
                                  std::string_view query) {
  if (query.empty()) return nullptr;
  for (const auto& p : providers) {
    if (p.id == query || p.name == query) return &p;
  }
  const std::string q = to_lower(std::string(query));
  for (const auto& p : providers) {
    if (to_lower(p.id) == q || to_lower(p.name) == q) return &p;
  }
  return nullptr;
}

const ModelInfo* find_model(const ProviderInfo& provider, std::string_view query) {
  if (query.empty()) return nullptr;
  for (const auto& m : provider.models) {
    if (m.id == query || m.name == query) return &m;
  }
  // Cursor wire slugs encode effort (`cursor-grok-4.6-medium`). Catalog ids
  // are picker ids (`cursor-grok-4.6`) after remap_cursor_picker_ids.
  const std::string picked = ProviderTransform::cursor_picker_id(query);
  if (picked != query) {
    for (const auto& m : provider.models) {
      if (m.id == picked || m.name == picked) return &m;
    }
  }
  // Older Cursor wire slugs (cursor-grok-4.5-high) are not in the live catalog.
  // Bind to the current Grok picker id on this provider instead of failing spawn.
  const std::string q = std::string(query);
  if (q.rfind("cursor-grok", 0) == 0 || picked.rfind("cursor-grok", 0) == 0) {
    for (const auto& m : provider.models) {
      if (m.id.rfind("cursor-grok", 0) == 0) return &m;
    }
  }
  return nullptr;
}

std::string first_catalog_combo(const std::vector<ProviderInfo>& providers) {
  for (const auto& p : providers) {
    for (const auto& m : p.models) {
      if (!m.id.empty()) return p.id + ":" + m.id;
    }
  }
  return "openrouter:deepseek/foo";
}

std::string with_catalog_hint(std::string message,
                              const std::vector<ProviderInfo>& providers,
                              std::string_view current_provider_id = "",
                              std::string_view current_model_id = "") {
  const std::string catalog = format_provider_catalog_for_error(
      providers, current_provider_id, current_model_id);
  if (catalog.empty()) return message;
  if (!message.empty() && message.back() != '\n') message.push_back('\n');
  return message + catalog;
}

struct Spec {
  std::string provider;
  std::string model;
  std::string raw;
};

void parse_combo(std::string raw,
                 const std::vector<ProviderInfo>& providers,
                 std::vector<Spec>& out) {
  if (raw.empty() || is_inherit_model_id(raw)) return;
  Spec spec;
  spec.raw = raw;
  // Split on ':' or first '/' only when the prefix is a catalog provider.
  // Model ids themselves contain both ('deepseek/foo', 'nvidia/x:free').
  const auto colon = raw.find(':');
  if (colon != std::string::npos && colon > 0 && colon + 1 < raw.size()) {
    const std::string prefix = raw.substr(0, colon);
    if (find_provider(providers, prefix)) {
      spec.provider = prefix;
      spec.model = raw.substr(colon + 1);
      out.push_back(std::move(spec));
      return;
    }
  }
  const auto slash = raw.find('/');
  if (slash != std::string::npos && slash > 0) {
    const std::string prefix = raw.substr(0, slash);
    if (find_provider(providers, prefix)) {
      spec.provider = prefix;
      spec.model = raw.substr(slash + 1);
      out.push_back(std::move(spec));
      return;
    }
  }
  spec.model = std::move(raw);
  out.push_back(std::move(spec));
}

SubagentTarget bind_spec(const Spec& spec,
                         const std::vector<ProviderInfo>& providers,
                         std::string_view default_provider_id = "",
                         std::string_view default_model_id = "") {
  SubagentTarget out;
  out.provider = find_provider(providers, spec.provider);
  if (!spec.provider.empty() && !out.provider) {
    out.error = with_catalog_hint(
        "Unknown provider '" + spec.provider +
            "' (use provider:model from opencode.json)",
        providers, default_provider_id, default_model_id);
    return out;
  }

  if (out.provider) {
    out.provider_id = out.provider->id;
    if (spec.model.empty()) {
      if (out.provider->models.empty()) {
        out.error = with_catalog_hint(
            "Provider '" + out.provider_id + "' has no models", providers);
        return out;
      }
      out.model_info = &out.provider->models.front();
      out.model_id = out.model_info->id;
      return out;
    }
    out.model_info = find_model(*out.provider, spec.model);
    out.model_id = out.model_info ? out.model_info->id : spec.model;
    return out;
  }

  if (!spec.model.empty()) {
    for (const auto& p : providers) {
      if (const ModelInfo* m = find_model(p, spec.model)) {
        out.provider = &p;
        out.provider_id = p.id;
        out.model_info = m;
        out.model_id = m->id;
        return out;
      }
    }
    out.error = with_catalog_hint(
        "Unknown model '" + spec.model +
            "' (pass provider:model, e.g. " + first_catalog_combo(providers) + ")",
        providers, default_provider_id, default_model_id);
    return out;
  }

  out.error = "provider and model are empty";
  return out;
}

}  // namespace

bool is_inherit_model_id(std::string_view id) {
  return id == "inherit" || id == "parent" || id == "default";
}

SubagentTarget resolve_subagent_target(
    std::string_view spec,
    const std::vector<ProviderInfo>& providers,
    std::string_view default_provider_id,
    std::string_view default_model_id) {
  std::string raw(spec);
  if (raw.empty() || is_inherit_model_id(raw)) {
    raw = std::string(default_provider_id) + ":" + std::string(default_model_id);
  }
  std::vector<Spec> specs;
  parse_combo(raw, providers, specs);
  if (specs.empty()) {
    SubagentTarget out;
    out.error = "No model given and no default model to inherit.";
    return out;
  }
  return bind_spec(specs.front(), providers, default_provider_id, default_model_id);
}

}  // namespace qcode
