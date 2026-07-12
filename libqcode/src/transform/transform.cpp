#include "ai/transform.h"

#include <algorithm>
#include <cctype>
#include <set>
#include <string>
#include <unordered_map>

namespace ai {
namespace ProviderTransform {

// ── Helpers ──

static std::string to_lower(const std::string& s) {
  std::string out = s;
  for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return out;
}

static bool contains(const std::string& s, const std::string& sub) {
  return to_lower(s).find(to_lower(sub)) != std::string::npos;
}

// ── Model-family-specific defaults ──
// Ported from opencode's ProviderTransform.temperature/topP/topK

std::optional<double> temperature(const Model& model) {
  const std::string id = to_lower(model.name);
  if (contains(id, "qwen")) return 0.55;
  if (contains(id, "claude")) return std::nullopt;
  if (contains(id, "gemini") || contains(id, "glm-4.6") || contains(id, "glm-4.7") || contains(id, "minimax-m2"))
    return 1.0;
  if (contains(id, "kimi-k2"))
    return (contains(id, "thinking") || contains(id, "k2.") || contains(id, "k2p") || contains(id, "k2-5")) ? 1.0 : 0.6;
  return std::nullopt;
}

std::optional<double> top_p(const Model& model) {
  const std::string id = to_lower(model.name);
  if (contains(id, "qwen")) return 1.0;
  if (contains(id, "minimax-m2") || contains(id, "gemini") || contains(id, "kimi-k2.5") ||
      contains(id, "kimi-k2p5") || contains(id, "kimi-k2-5"))
    return 0.95;
  return std::nullopt;
}

std::optional<int> top_k(const Model& model) {
  const std::string id = to_lower(model.name);
  if (contains(id, "minimax-m2")) {
    return (contains(id, "m2.") || contains(id, "m25") || contains(id, "m21")) ? 40 : 20;
  }
  if (contains(id, "gemini")) return 64;
  return std::nullopt;
}

// ── Message normalization ──
// Ported from opencode's ProviderTransform.message() + request-shaping-helpers

static const std::set<std::string> UNSUPPORTED_ROLES = {"tool"};

Messages normalize_messages(const Messages& messages, const Model& model) {
  Messages result;
  (void)model; // Future: model-specific normalization

  for (const auto& msg : messages) {
    // Filter out unsupported roles
    if (msg.role == kMessageRoleSystem) {
      // Keep system messages
    }

    MessageContent filtered_content;
    for (const auto& part : msg.content) {
      // Strip unsupported content types
      if (std::holds_alternative<TextContentPart>(part) ||
          std::holds_alternative<ToolCallContentPart>(part) ||
          std::holds_alternative<ToolResultContentPart>(part)) {
        filtered_content.push_back(part);
      }
    }

    if (!filtered_content.empty()) {
      result.emplace_back(msg.role, std::move(filtered_content));
    }
  }

  return result;
}

// ── Provider options ──

JsonValue build_options(const Model& model,
                        const std::optional<std::string>& reasoning_effort,
                        std::optional<int> budget_tokens) {
  JsonValue opts = JsonValue::object();

  // Reasoning effort
  if (reasoning_effort.has_value()) {
    opts["reasoning_effort"] = *reasoning_effort;
  }

  // Budget tokens for reasoning models
  if (budget_tokens.has_value()) {
    opts["budget_tokens"] = *budget_tokens;
  }

  // Model-specific options
  const std::string id = to_lower(model.name);

  // Anthropic extended thinking
  if (contains(id, "claude") && budget_tokens.has_value()) {
    opts["thinking"] = {{"type", "enabled"}, {"budget_tokens", *budget_tokens}};
  }

  return opts;
}

JsonValue build_small_options(const Model& model) {
  (void)model;
  // Reduced options for compact/fast mode
  JsonValue opts = JsonValue::object();
  opts["max_tokens"] = 1024;
  return opts;
}

JsonValue wrap_provider_options(const Model& model, const JsonValue& options) {
  const std::string key = sdk_key(model.provider);
  JsonValue wrapped = JsonValue::object();
  wrapped[key] = options;
  return wrapped;
}

// ── Token management ──

int max_output_tokens(int model_limit) {
  return std::min(model_limit, OUTPUT_TOKEN_MAX);
}

// ── Schema normalization ──

JsonValue normalize_schema(const JsonValue& schema, const Model& model) {
  JsonValue result = schema;
  const std::string id = to_lower(model.name);
  const std::string provider = to_lower(model.provider);

  // Providers that reject top-level anyOf/oneOf (Anthropic, Bedrock, etc.)
  bool rejects_top_level_keywords =
      provider == "anthropic" || provider == "bedrock" || contains(id, "claude") ||
      contains(id, "amazon") || contains(id, "bedrock");

  // Flatten anyOf/oneOf for strict providers
  if (rejects_top_level_keywords) {
    if (result.contains("anyOf") || result.contains("oneOf") || result.contains("allOf")) {
      result.erase("anyOf");
      result.erase("oneOf");
      result.erase("allOf");
      result.erase("enum");
      result.erase("not");
      result["type"] = "object";
      if (!result.contains("properties")) {
        result["properties"] = JsonValue::object();
      }
    }
  }

  // Gemini-specific sanitization
  if (contains(id, "gemini") || provider == "google") {
    // Gemini doesn't support certain schema features
    if (result.contains("required") && result["required"].is_array()) {
      // Keep required, it's fine
    }
    // Remove null type entries
    if (result.contains("type") && result["type"].is_null()) {
      result.erase("type");
    }
  }

  return result;
}

// ── Utility ──

bool is_opus_family(const std::string& model_id) {
  const std::string id = to_lower(model_id);
  return id.find("opus-4-6") != std::string::npos ||
         id.find("opus-4.6") != std::string::npos ||
         id == "opus" ||
         (id.size() >= 5 && id.substr(id.size() - 5) == "/opus");
}

std::string sdk_key(const std::string& provider_name) {
  static const std::unordered_map<std::string, std::string> KEY_MAP = {
    {"amazon", "bedrock"},
    {"qpilot", "openai"},
    {"qgenie", "openai"},
    {"azure", "azure"},
    {"google", "google"},
    {"anthropic", "anthropic"},
    {"openai", "openai"},
    {"openrouter", "openrouter"},
    {"together", "togetherai"},
    {"groq", "groq"},
    {"mistral", "mistral"},
    {"xai", "xai"},
    {"perplexity", "perplexity"},
    {"deepinfra", "deepinfra"},
    {"cerebras", "cerebras"},
    {"cohere", "cohere"},
  };

  auto it = KEY_MAP.find(to_lower(provider_name));
  if (it != KEY_MAP.end()) return it->second;
  return provider_name;
}

}  // namespace ProviderTransform
}  // namespace ai
