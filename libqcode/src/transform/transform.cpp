#include <qcode/providers/provider_transform.h>
#include <qcode/providers/zen_route.h>

#include <algorithm>
#include <cctype>
#include <set>
#include <string>
#include <string_view>
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

bool is_reasoning_model_id(const std::string& model_id) {
  const std::string id = to_lower(model_id);
  return contains(id, "muse-spark") || contains(id, "ox-alpha") ||
         contains(id, "x-preview-f-free") || contains(id, "deepseek-v4") ||
         contains(id, "gpt-5") || contains(id, "grok") ||
         contains(id, "gemini-3") || contains(id, "gemini-2.5") ||
         contains(id, "thinking") || contains(id, "kimi-k2");
}

void apply_reasoning_defaults(ModelInfo& model) {
  if (!model.reasoning && (is_reasoning_model_id(model.id) ||
                           is_reasoning_model_id(model.name))) {
    model.reasoning = true;
  }
  if (model.reasoning && model.reasoning_efforts.empty()) {
    // Ox Alpha catalog / OpenCode docs: low, high, max (no medium).
    if (contains(model.id, "ox-alpha") || contains(model.id, "x-preview-f-free") ||
        contains(model.id, "x-preview-f")) {
      model.reasoning_efforts = {"low", "high", "max"};
    } else {
      model.reasoning_efforts = {"low", "medium", "high"};
      if (contains(model.id, "deepseek-v4")) {
        model.reasoning_efforts.push_back("max");
      }
    }
  }
  if (model.reasoning && model.reasoning_field.empty()) {
    model.reasoning_field = contains(model.id, "muse-spark")
                                ? "reasoning"
                                : "reasoning_content";
  }
}

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
  const bool grok = contains(id, "grok");
  const std::string preferred = (gpt5 || grok) ? "medium" : "high";
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

std::string clamp_variant(const ModelInfo& model, const std::string& requested) {
  if (requested.empty() || requested == "off") return "off";
  const auto allowed = reasoning_variants(model);
  if (allowed.empty()) return requested;
  if (std::find(allowed.begin(), allowed.end(), requested) != allowed.end()) {
    return requested;
  }
  const std::vector<std::string> fallbacks =
      requested == "max" || requested == "xhigh"
          ? std::vector<std::string>{"xhigh", "max", "high", "medium", "low"}
      : requested == "medium" ? std::vector<std::string>{"high", "low", "max", "xhigh"}
      : requested == "low" || requested == "minimal"
          ? std::vector<std::string>{"minimal", "medium", "high", "max"}
          : std::vector<std::string>{"high", "medium", "max", "xhigh", "low"};
  for (const auto& candidate : fallbacks) {
    if (std::find(allowed.begin(), allowed.end(), candidate) != allowed.end()) {
      return candidate;
    }
  }
  return allowed.front();
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

static std::string trim_copy(std::string model_id) {
  while (!model_id.empty() &&
         (model_id.front() == ' ' || model_id.front() == '\t')) {
    model_id.erase(model_id.begin());
  }
  while (!model_id.empty() &&
         (model_id.back() == ' ' || model_id.back() == '\t')) {
    model_id.pop_back();
  }
  return model_id;
}

std::string zen_wire_model_id(std::string model_id) {
  // Trim whitespace — an empty/blank id is what Zen renders as
  // "Model  is not supported" (401 ModelError).
  model_id = trim_copy(std::move(model_id));
  if (model_id.empty()) return {};

  const std::string lower = to_lower(model_id);
  static constexpr const char* kPrefix = "opencode/";
  if (lower.rfind(kPrefix, 0) == 0) {
    model_id = model_id.substr(std::char_traits<char>::length(kPrefix));
  }

  const std::string id = to_lower(model_id);
  // Retired Zen free-pool ids 401 as ModelError. Snap them onto a live
  // keyless model so old sessions / configs keep working.
  if (contains(id, "ox-alpha") || contains(id, "ox alpha") ||
      contains(id, "x-preview-f-free") || contains(id, "x-preview-f")) {
    return "big-pickle";
  }
  if (contains(id, "hy3-free") || contains(id, "hy3-preview-free") ||
      id == "hy3") {
    return "laguna-s-2.1-free";
  }
  // Paid Zen slugs (muse-spark-1.2) and free-pool slugs
  // (muse-spark-1.2-contributor-free, muse-spark-1.3-contributor-free) are
  // already wire ids — keep them. OpenRouter / display aliases (meta/...)
  // map onto the current free pool so a cross-provider pick does not 401.
  if (contains(id, "muse-spark")) {
    if (contains(id, "meta/") || contains(id, "openrouter/")) {
      if (contains(id, "1.3")) return "muse-spark-1.3-contributor-free";
      return "muse-spark-1.2-contributor-free";
    }
    if (id == "muse-spark-1.2") return "muse-spark-1.2";
    if (id == "muse-spark-1.3") return "muse-spark-1.3";
    if (contains(id, "1.3")) return "muse-spark-1.3-contributor-free";
    return "muse-spark-1.2-contributor-free";
  }
  return model_id;
}

std::string zen_api_protocol(const std::string& model_id) {
  return wire_protocol_id(zen_model_route(model_id).protocol);
}

std::string zen_completions_path(const std::string& model_id) {
  return zen_model_route(model_id).path;
}

std::string openrouter_wire_model_id(std::string model_id) {
  model_id = trim_copy(std::move(model_id));
  if (model_id.empty()) return {};
  const std::string id = to_lower(model_id);
  if (contains(id, "ox-alpha") || contains(id, "ox alpha") ||
      contains(id, "x-preview-f-free") || contains(id, "x-preview-f")) {
    return "stealth/ox-alpha";
  }
  if (contains(id, "muse-spark")) {
    if (contains(id, "1.1")) return "meta/muse-spark-1.1";
    if (contains(id, "contributor")) return "meta/muse-spark-1.2-contributor";
    return "meta/muse-spark-1.2";
  }
  return model_id;
}

std::string chat_wire_model_id(ChatTransport transport, std::string model_id) {
  if (transport == ChatTransport::kOpenCodeZen) {
    return zen_wire_model_id(std::move(model_id));
  }
  if (transport == ChatTransport::kOpenRouter) {
    return openrouter_wire_model_id(std::move(model_id));
  }
  return trim_copy(std::move(model_id));
}

std::string cursor_family_id(const std::string& model_id) {
  const std::string id = to_lower(model_id);
  if (id.find("grok-4.6") != std::string::npos ||
      id.find("grok-4-6") != std::string::npos) {
    return "grok-4.6";
  }
  if (id.find("opus-5") != std::string::npos ||
      id.find("claude-5-opus") != std::string::npos ||
      id.find("claude-opus-5") != std::string::npos) {
    return "claude-opus-5";
  }
  return {};
}

std::string cursor_picker_id(const std::string& model_id) {
  const auto family = cursor_family_id(model_id);
  if (!family.empty()) return family;

  const std::string id = to_lower(model_id);
  static constexpr std::string_view kSuffixes[] = {
      "-thinking-low", "-thinking-medium", "-thinking-high",
      "-xhigh-fast",   "-high-fast",       "-medium-fast",    "-low-fast",
      "-fast",         "-xhigh",           "-high",           "-medium",
      "-low",
  };
  for (const auto suffix : kSuffixes) {
    if (id.size() > suffix.size() &&
        id.compare(id.size() - suffix.size(), suffix.size(), suffix) == 0) {
      return model_id.substr(0, model_id.size() - suffix.size());
    }
  }
  return model_id;
}

std::string cursor_wire_model_id(const std::string& model_id,
                                 const std::optional<std::string>& effort) {
  const std::string family = cursor_family_id(model_id);
  const std::string base = family.empty() ? cursor_picker_id(model_id)
                                          : family;

  std::string level = effort.value_or("");
  if (level == "off") level.clear();
  if (level == "max") level = "high";

  if (base.empty()) return model_id;

  const std::string id = to_lower(base);
  const bool claude = contains(id, "claude") || contains(id, "opus") ||
                      contains(id, "sonnet") || contains(id, "fable") ||
                      contains(id, "haiku");
  const bool grok = contains(id, "grok");

  if (grok) {
    // Cursor CLI uses modelId grok-4.6 with effort as a parameter. Agent
    // GetUsableModels lists suffixed SKUs; low/high must be on the slug or
    // the run sits idle. Medium is the family default (bare id).
    if (level == "low" || level == "high") return base + "-" + level;
    return base;
  }

  if (claude) {
    if (level == "low" || level == "high") {
      return base + "-thinking-" + level;
    }
    return base;
  }

  // Composer / GPT-5.6 / other Agent SKUs use a trailing effort suffix.
  if (level == "low" || level == "medium" || level == "high") {
    return base + "-" + level;
  }
  return base;
}

void apply_reasoning_options(nlohmann::json& body,
                             ChatTransport transport,
                             const std::optional<std::string>& effort) {
  if (!effort.has_value() || effort->empty() || *effort == "off") return;
  if (transport == ChatTransport::kOpenRouter) {
    // transform.ts variants() / reasoningEffort(): { reasoning: { effort } }.
    body["reasoning"] = {{"effort", *effort}};
  } else {
    // @ai-sdk/openai-compatible: { reasoningEffort } → reasoning_effort.
    body["reasoning_effort"] = *effort;
  }
}

std::string interleaved_replay_field(ChatTransport transport,
                                     const std::string& model_id) {
  // OpenCode only injects interleaved.field for openai-compatible, and
  // explicitly skips @openrouter/ai-sdk-provider.
  if (transport == ChatTransport::kOpenRouter) return {};
  if (contains(model_id, "muse-spark")) return "reasoning";
  return "reasoning_content";
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

// Tool call arguments and tool results are often JSON objects (bash output,
// file reads). nlohmann throws type_error.302 if those are passed to a
// std::string helper — sanitize by dumping, then re-parse.
static JsonValue sanitize_json_value(JsonValue value) {
  if (value.is_string()) {
    return sanitize_surrogates(value.get<std::string>());
  }
  if (value.is_object() || value.is_array()) {
    std::string dumped = value.dump();
    dumped = sanitize_surrogates(dumped);
    try {
      return nlohmann::json::parse(dumped);
    } catch (...) {
      return value;
    }
  }
  return value;
}

static bool is_empty_json_text(const JsonValue& value) {
  if (value.is_null()) return true;
  if (value.is_string()) return is_empty_text(value.get<std::string>());
  return false;
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
        tc.arguments = sanitize_json_value(std::move(tc.arguments));
        filtered_content.push_back(std::move(tc));
      } else if (std::holds_alternative<ToolResultContentPart>(part)) {
        auto tr = std::get<ToolResultContentPart>(part);
        tr.tool_call_id = scrub_tool_id(tr.tool_call_id);
        tr.result = sanitize_json_value(std::move(tr.result));
        if ((is_claude || is_bedrock) && is_empty_json_text(tr.result)) continue;
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
