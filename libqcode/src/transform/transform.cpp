#include <qcode/providers/provider_transform.h>

#include <algorithm>
#include <cctype>
#include <set>
#include <string>
#include <unordered_map>

namespace qcode {
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

// ── Reasoning variants ──
// Ported from opencode's transform.ts reasoningVariants/effortVariants and the
// @ai-sdk/openai-compatible fallback (WIDELY_SUPPORTED_EFFORTS + max for
// deepseek-v4).

std::vector<std::string> reasoning_variants(const ModelInfo& model) {
  if (!model.reasoning_efforts.empty()) return model.reasoning_efforts;
  if (!model.reasoning) return {};
  const std::string id = to_lower(model.id);
  std::vector<std::string> efforts = {"low", "medium", "high"};
  if (contains(id, "deepseek-v4")) efforts.push_back("max");
  return efforts;
}

std::string default_variant(const ModelInfo& model) {
  if (!model.reasoning) return "off";
  const auto efforts = reasoning_variants(model);
  const std::string id = to_lower(model.id);
  // Upstream options(): gpt-5.x defaults to medium effort.
  const bool gpt5 = contains(id, "gpt-5");
  const std::string preferred = gpt5 ? "medium" : "high";
  if (!efforts.empty()) {
    if (std::find(efforts.begin(), efforts.end(), preferred) != efforts.end()) {
      return preferred;
    }
    // Fallback order keeps thinking meaningful when the preferred effort is
    // not declared (e.g. gpt-5 ids with only low/high).
    for (const std::string candidate : {"high", "medium", "max"}) {
      if (std::find(efforts.begin(), efforts.end(), candidate) != efforts.end()) {
        return candidate;
      }
    }
    return efforts.front();
  }
  return gpt5 ? "medium" : "high";
}

// ── Chat transport flavors ──

ChatTransport chat_transport_for(const std::string& base_url) {
  const std::string url = to_lower(base_url);
  if (url.find("openrouter.ai") != std::string::npos) return ChatTransport::kOpenRouter;
  if (url.find("opencode.ai") != std::string::npos ||
      url.find("opencode.ai/zen") != std::string::npos) {
    return ChatTransport::kOpenCodeZen;
  }
  if (url.find("api.openai.com") != std::string::npos) return ChatTransport::kOpenAI;
  return ChatTransport::kCompatible;
}

void apply_reasoning_options(nlohmann::json& body,
                             ChatTransport transport,
                             const std::optional<std::string>& effort) {
  if (!effort.has_value() || effort->empty() || *effort == "off") return;
  if (transport == ChatTransport::kOpenRouter) {
    // @openrouter/ai-sdk-provider maps variants to reasoning:{effort}.
    body["reasoning"] = {{"effort", *effort}};
  } else {
    // @ai-sdk/openai-compatible lowers variants to reasoning_effort.
    body["reasoning_effort"] = *effort;
  }
}

void apply_stream_options(nlohmann::json& body,
                          ChatTransport transport,
                          bool stream) {
  if (!stream) return;
  // Upstream openai-chat always sends stream_options.include_usage so cached
  // / reasoning token accounting survives streaming.
  body["stream_options"] = {{"include_usage", true}};
  (void)transport;
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

// Mirrors upstream sanitizeSurrogates: lone surrogates \uD800-\uDBFF -> \uFFFD.
// In UTF-8, surrogates appear as overlong 3-byte sequences ED A0 80 - ED BF BF;
// replace those bytes with the UTF-8 replacement character EF BF BD.
static std::string sanitize_surrogates(std::string s) {
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size();) {
    unsigned char c = static_cast<unsigned char>(s[i]);
    // Check for 3-byte surrogate range ED A0 80 .. ED BF BF
    if (c == 0xED && i + 2 < s.size()) {
      unsigned char c1 = static_cast<unsigned char>(s[i + 1]);
      unsigned char c2 = static_cast<unsigned char>(s[i + 2]);
      if (c1 >= 0xA0 && c1 <= 0xBF && (c2 & 0xC0) == 0x80) {
        out += "\xEF\xBF\xBD";
        i += 3;
        continue;
      }
    }
    out.push_back(s[i++]);
  }
  return out;
}

static std::string scrub_tool_id(const std::string& id) {
  std::string out = id;
  for (char& ch : out) {
    if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
          (ch >= '0' && ch <= '9') || ch == '_' || ch == '-')) {
      ch = '_';
    }
  }
  return out;
}

static bool is_empty_text(const std::string& t) {
  for (char c : t) if (c != ' ' && c != '\t' && c != '\n' && c != '\r') return false;
  return true;
}

Messages normalize_messages(const Messages& messages, const Model& model) {
  Messages result;
  const std::string prov = to_lower(model.provider);
  const std::string mid = to_lower(model.name);
  const bool is_claude = prov == "anthropic" || contains(mid, "claude");
  const bool is_bedrock = prov == "bedrock" || prov == "amazon" || contains(mid, "bedrock");

  for (const auto& msg : messages) {
    MessageContent filtered_content;
    for (const auto& part : msg.content) {
      if (std::holds_alternative<TextContentPart>(part)) {
        auto tp = std::get<TextContentPart>(part);
        tp.text = sanitize_surrogates(tp.text);
        // Anthropic/Bedrock empty-filter: drop empty text parts
        if ((is_claude || is_bedrock) && is_empty_text(tp.text)) continue;
        filtered_content.push_back(std::move(tp));
      } else if (std::holds_alternative<ToolCallContentPart>(part)) {
        auto tc = std::get<ToolCallContentPart>(part);
        tc.id = scrub_tool_id(tc.id);
        // Sanitize arguments JSON string values if present
        if (tc.arguments.is_string()) {
          std::string s = tc.arguments.get<std::string>();
          s = sanitize_surrogates(s);
          tc.arguments = s;
        } else if (tc.arguments.is_object() || tc.arguments.is_array()) {
          // Dump and sanitize string values inside — lightweight: stringify then sanitize
          std::string dumped = tc.arguments.dump();
          dumped = sanitize_surrogates(dumped);
          try { tc.arguments = nlohmann::json::parse(dumped); } catch (...) {}
        }
        filtered_content.push_back(std::move(tc));
      } else if (std::holds_alternative<ToolResultContentPart>(part)) {
        auto tr = std::get<ToolResultContentPart>(part);
        tr.tool_call_id = scrub_tool_id(tr.tool_call_id);
        tr.result = sanitize_surrogates(tr.result);
        if ((is_claude || is_bedrock) && is_empty_text(tr.result)) continue;
        filtered_content.push_back(std::move(tr));
      } else if (std::holds_alternative<ReasoningContentPart>(part)) {
        auto rp = std::get<ReasoningContentPart>(part);
        rp.text = sanitize_surrogates(rp.text);
        if ((is_claude || is_bedrock) && is_empty_text(rp.text) && rp.signature.empty()) continue;
        filtered_content.push_back(std::move(rp));
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
}  // namespace qcode
