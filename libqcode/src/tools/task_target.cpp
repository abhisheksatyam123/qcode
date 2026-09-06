#include <qcode/tools/task_target.h>

#include <algorithm>
#include <cctype>

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
  return nullptr;
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
                         const std::vector<ProviderInfo>& providers) {
  SubagentTarget out;
  out.provider = find_provider(providers, spec.provider);
  if (!spec.provider.empty() && !out.provider) {
    out.error = "Unknown provider '" + spec.provider +
                "' (use provider:model from the catalog)";
    return out;
  }

  if (out.provider) {
    out.provider_id = out.provider->id;
    if (spec.model.empty()) {
      if (out.provider->models.empty()) {
        out.error = "Provider '" + out.provider_id + "' has no models";
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
    out.error = "Unknown model '" + spec.model +
                "' (pass provider:model, e.g. openrouter:deepseek/foo)";
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
    SubagentTarget t = bind_spec(spec, providers);
    if (t.error.empty() && t.provider) return t;
    if (!t.error.empty() && specs.size() == 1) return t;
  }

  SubagentTarget fallback;
  fallback.provider = find_provider(providers, default_provider_id);
  if (!fallback.provider && !providers.empty()) {
    fallback.provider = &providers.front();
  }
  if (!fallback.provider) {
    fallback.error = "No available AI provider configured for subagent";
    return fallback;
  }
  fallback.provider_id = fallback.provider->id;
  fallback.model_info = find_model(*fallback.provider, default_model_id);
  if (fallback.model_info) {
    fallback.model_id = fallback.model_info->id;
  } else if (!std::string(default_model_id).empty() &&
             !is_inherit_model_id(default_model_id)) {
    fallback.model_id = std::string(default_model_id);
  } else if (!fallback.provider->models.empty()) {
    fallback.model_info = &fallback.provider->models.front();
    fallback.model_id = fallback.model_info->id;
  } else {
    fallback.error = "No available AI provider configured for subagent";
  }
  return fallback;
}

}  // namespace qcode
