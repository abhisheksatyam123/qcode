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

bool matches_orchestrator(
    std::string_view prov, std::string_view mod,
    std::string_view orch_prov, std::string_view orch_mod) {
  if (orch_prov.empty() || orch_mod.empty()) return false;
  const bool prov_match = (to_lower(std::string(prov)) == to_lower(std::string(orch_prov)));
  const bool mod_match = (to_lower(std::string(mod)) == to_lower(std::string(orch_mod)));
  return prov_match && mod_match;
}

SubagentTarget pick_alternate_working_target(
    const std::vector<ProviderInfo>& providers,
    std::string_view orchestrator_provider_id,
    std::string_view orchestrator_model_id,
    bool allow_cursor) {
  struct Cand {
    const ProviderInfo* p = nullptr;
    const ModelInfo* m = nullptr;
  };
  std::vector<Cand> candidates;

  for (const auto& pr : providers) {
    if (!is_provider_authenticated(pr)) continue;
    const bool is_cursor = (pr.id == "cursor" || pr.id.find("cursor") != std::string::npos);
    if (is_cursor && !allow_cursor) continue;

    for (const auto& mo : pr.models) {
      if (mo.id.empty()) continue;
      if (!is_model_working(pr, mo)) continue;
      if (matches_orchestrator(pr.id, mo.id, orchestrator_provider_id, orchestrator_model_id)) {
        continue;
      }
      candidates.push_back({&pr, &mo});
    }
  }

  if (candidates.empty()) {
    // If only 1 model configured in entire catalog, return whatever is available
    for (const auto& pr : providers) {
      for (const auto& mo : pr.models) {
        if (!mo.id.empty()) {
          SubagentTarget t;
          t.provider = &pr;
          t.provider_id = pr.id;
          t.model_info = &mo;
          t.model_id = mo.id;
          return t;
        }
      }
    }
    SubagentTarget err;
    err.error = "No available AI provider configured for subagent";
    return err;
  }

  // Preference ranking:
  // 1. Different provider than orchestrator (+100 penalty for same provider)
  // 2. High-speed reasoning / tool providers (antigravity > openrouter > opencode > cursor)
  auto prio = [&](const Cand& c) {
    int p_val = 0;
    if (to_lower(c.p->id) == to_lower(std::string(orchestrator_provider_id))) {
      p_val += 100;
    }
    if (c.p->id.find("antigravity") != std::string::npos) p_val += 1;
    else if (c.p->id == "openrouter") p_val += 2;
    else if (c.p->id == "opencode") p_val += 3;
    else p_val += 4;
    return p_val;
  };

  std::stable_sort(candidates.begin(), candidates.end(),
                   [&](const Cand& a, const Cand& b) { return prio(a) < prio(b); });

  const auto& best = candidates.front();
  SubagentTarget out;
  out.provider = best.p;
  out.provider_id = best.p->id;
  out.model_info = best.m;
  out.model_id = best.m->id;
  return out;
}

SubagentTarget resolve_subagent_target(
    const nlohmann::json& args,
    const std::vector<ProviderInfo>& providers,
    std::string_view default_provider_id,
    std::string_view default_model_id) {
  std::vector<Spec> specs;
  const std::string provider_arg = args.value("provider", "");
  std::string model_arg;
  if (args.contains("model") && args["model"].is_string()) {
    model_arg = args["model"].get<std::string>();
  }

  if (!provider_arg.empty()) {
    Spec spec;
    spec.provider = provider_arg;
    spec.raw = provider_arg;
    if (!model_arg.empty() && !is_inherit_model_id(model_arg)) {
      const auto colon = model_arg.find(':');
      if (colon != std::string::npos) {
        const std::string prefix = model_arg.substr(0, colon);
        if (prefix == provider_arg ||
            find_provider(providers, prefix) == find_provider(providers, provider_arg)) {
          spec.model = model_arg.substr(colon + 1);
        } else {
          spec.model = model_arg;
        }
      } else {
        spec.model = model_arg;
      }
      spec.raw += ":" + spec.model;
    }
    specs.push_back(std::move(spec));
  } else if (!model_arg.empty()) {
    parse_combo(model_arg, providers, specs);
  }

  if (args.contains("models") && args["models"].is_array()) {
    for (const auto& m : args["models"]) {
      if (m.is_string()) parse_combo(m.get<std::string>(), providers, specs);
    }
  }

  for (const auto& spec : specs) {
    SubagentTarget t = bind_spec(spec, providers, default_provider_id, default_model_id);
    if (!t.error.empty() && specs.size() == 1) return t;
    if (!t.error.empty() || !t.provider) continue;

    // Check if target matches the orchestrator
    if (matches_orchestrator(t.provider_id, t.model_id, default_provider_id, default_model_id)) {
      if (specs.size() > 1) {
        continue;  // Try remaining fallback specs first
      }
      // If caller specifically requested the orchestrator model, steer to alternate working model
      return pick_alternate_working_target(providers, default_provider_id, default_model_id);
    }
    return t;
  }

  // No specific alternate model resolved from specs (or all matched orchestrator / inherit / empty).
  // Automatically select the best alternate working model different from the orchestrator.
  return pick_alternate_working_target(providers, default_provider_id, default_model_id);
}

}  // namespace qcode
