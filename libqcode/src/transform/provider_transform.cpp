#include <qcode/transform/provider_transform.h>
#include <qcode/providers/zen_route.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <iterator>
#include <ranges>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>

namespace qcode {
namespace ProviderTransform {

// ── Helpers ──

static std::string to_lower(std::string_view s) {
  std::string out(s);
  std::ranges::transform(out, out.begin(), [](char c) {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  });
  return out;
}

static bool contains(std::string_view s, std::string_view sub) {
  return to_lower(s).find(to_lower(sub)) != std::string::npos;
}

// ── Reasoning variants ──
// Ported from opencode's transform.ts reasoningVariants/effortVariants and the
// @ai-sdk/openai-compatible fallback (WIDELY_SUPPORTED_EFFORTS + max for
// deepseek-v4).

bool is_reasoning_model_id(std::string_view model_id) {
  const auto id = to_lower(model_id);
  static constexpr std::array<std::string_view, 14> kNeedles{
      "muse-spark", "ox-alpha",     "x-preview-f-free", "deepseek-v4",
      "gpt-5",      "grok",         "gemini-3",         "gemini-2.5",
      "thinking",   "kimi-k",       "claude",           "composer",
      "nemotron",   "fable",
  };
  return std::ranges::any_of(
      kNeedles, [&](std::string_view needle) { return id.find(needle) != std::string::npos; });
}

void apply_reasoning_defaults(ModelInfo& model) {
  // Honor the header contract: ids that think even when the catalog omitted
  // reasoning=true (Grok, GPT-5, Claude, ...) are marked as reasoners so the
  // /variant picker and default-effort flow work without explicit config.
  if (!model.reasoning && (is_reasoning_model_id(model.id) ||
                            is_reasoning_model_id(model.name))) {
    model.reasoning = true;
  }
  if (model.reasoning && model.reasoning_efforts.empty()) {
    model.reasoning_efforts = {"low", "medium", "high"};
  }
  if (model.reasoning && model.reasoning_field.empty()) {
    model.reasoning_field = "reasoning_content";
  }
}

std::vector<std::string> reasoning_variants(const ModelInfo& model) {
  if (!model.reasoning_efforts.empty()) return model.reasoning_efforts;
  if (model.reasoning) return {"low", "medium", "high"};
  return {};
}

std::string default_variant(const ModelInfo& model) {
  if (!model.reasoning) return "off";
  const auto efforts = reasoning_variants(model);
  if (!model.reasoning_default.empty()) {
    if (model.reasoning_default == "off") return "off";
    if (std::ranges::find(efforts, model.reasoning_default) != efforts.end()) {
      return model.reasoning_default;
    }
  }
  if (efforts.empty()) return "off";
  return efforts.front();
}

std::string clamp_variant(const ModelInfo& model, std::string_view requested) {
  if (requested.empty() || requested == "off") return "off";
  const auto allowed = reasoning_variants(model);
  if (allowed.empty()) return std::string{requested};
  if (std::ranges::find(allowed, requested) != allowed.end()) {
    return std::string{requested};
  }
  const std::vector<std::string> fallbacks =
      requested == "max" || requested == "xhigh"
          ? std::vector<std::string>{"xhigh", "max", "high", "medium", "low"}
      : requested == "medium" ? std::vector<std::string>{"high", "low", "max", "xhigh"}
      : requested == "low" || requested == "minimal"
          ? std::vector<std::string>{"minimal", "medium", "high", "max"}
          : std::vector<std::string>{"high", "medium", "max", "xhigh", "low"};
  for (const auto& candidate : fallbacks) {
    if (std::ranges::find(allowed, candidate) != allowed.end()) {
      return candidate;
    }
  }
  return allowed.front();
}

static std::vector<std::string> variant_cycle(const ModelInfo& model) {
  std::vector<std::string> ids{"off"};
  std::ranges::copy_if(reasoning_variants(model), std::back_inserter(ids),
                       [](const std::string& effort) {
                         return !effort.empty() && effort != "off";
                       });
  return ids;
}

bool is_allowed_variant(const ModelInfo& model, std::string_view requested) {
  if (requested == "off") return true;
  const auto allowed = reasoning_variants(model);
  return std::ranges::find(allowed, requested) != allowed.end();
}

std::string next_variant(const ModelInfo& model, std::string_view current) {
  const auto ids = variant_cycle(model);
  const std::string cur = current.empty() ? "off" : std::string{current};
  if (const auto it = std::ranges::find(ids, cur); it != ids.end()) {
    const auto idx = static_cast<std::size_t>(it - ids.begin());
    return ids[(idx + 1) % ids.size()];
  }
  return ids.front();
}

std::string resolve_session_variant(const ModelInfo& model,
                                    std::string_view current) {
  if (current.empty()) return default_variant(model);
  if (current == "off") return "off";
  return clamp_variant(model, current);
}

// ── Chat transport flavors ──

ChatTransport chat_transport_for(std::string_view base_url) {
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
  if (lower.starts_with(kPrefix)) {
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

std::string zen_api_protocol(std::string_view model_id) {
  return wire_protocol_id(zen_model_route(std::string{model_id}).protocol);
}

std::string zen_completions_path(std::string_view model_id) {
  return zen_model_route(std::string{model_id}).path;
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

std::string cursor_family_id(std::string_view model_id) {
  const std::string id = to_lower(model_id);
  if (id.find("grok-4.6") != std::string::npos ||
      id.find("grok-4-6") != std::string::npos) {
    // Live GetUsableModels lists only cursor- prefixed Grok SKUs
    // (cursor-grok-4.6-{low,medium,high,xhigh}); the bare id 400s with
    // ERROR_BAD_MODEL_NAME, so the family keeps the required prefix.
    return "cursor-grok-4.6";
  }
  if (id.find("opus-5") != std::string::npos ||
      id.find("claude-5-opus") != std::string::npos ||
      id.find("claude-opus-5") != std::string::npos) {
    return "claude-opus-5";
  }
  return {};
}

std::string cursor_picker_id(std::string_view model_id) {
  const auto family = cursor_family_id(model_id);
  if (!family.empty()) return family;

  const std::string id = to_lower(model_id);
  static constexpr std::string_view kSuffixes[] = {
      "-thinking-low", "-thinking-medium", "-thinking-high", "-thinking-max",
      "-xhigh-fast",   "-high-fast",       "-medium-fast",   "-low-fast",
      "-fast",         "-xhigh",           "-high",          "-medium",
      "-low",          "-max",             "-none",
  };
  for (const auto suffix : kSuffixes) {
    if (id.size() > suffix.size() && id.ends_with(suffix)) {
      return std::string{model_id.substr(0, model_id.size() - suffix.size())};
    }
  }
  return std::string{model_id};
}

std::string cursor_wire_model_id(std::string_view model_id,
                                 const std::optional<std::string>& effort) {
  const std::string family = cursor_family_id(model_id);
  const std::string base = family.empty() ? cursor_picker_id(model_id)
                                          : family;

  std::string level = effort.value_or("");
  if (level == "off") level.clear();

  if (base.empty()) return std::string{model_id};

  const std::string id = to_lower(base);
  const bool claude = contains(id, "claude") || contains(id, "opus") ||
                      contains(id, "sonnet") || contains(id, "fable") ||
                      contains(id, "haiku");
  const bool grok = contains(id, "grok");

  if (grok) {
    // Agent GetUsableModels lists only cursor- prefixed SKUs; every level
    // (including the default) must be on the slug — the bare id 400s with
    // ERROR_BAD_MODEL_NAME. Medium is the balanced default; max maps onto
    // the xhigh SKU the catalog actually lists.
    if (level.empty()) level = "medium";
    if (level == "max") level = "xhigh";
    if (level == "low" || level == "medium" || level == "high" ||
        level == "xhigh") {
      return "cursor-grok-4.6-" + level;
    }
    return "cursor-grok-4.6-medium";
  }

  // Families without an xhigh SKU fold picker "max" onto high.
  if (level == "max") level = "high";

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
                                     std::string_view model_id) {
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

bool is_opus_family(std::string_view model_id) {
  const std::string id = to_lower(model_id);
  return id.find("opus-4-6") != std::string::npos ||
         id.find("opus-4.6") != std::string::npos ||
         id == "opus" ||
         id.ends_with("/opus");
}

std::string sdk_key(std::string_view provider_name) {
  static const std::unordered_map<std::string, std::string> KEY_MAP = {
    {"cursor", "cursor"},
    {"opencode", "opencode"},
    {"zen", "opencode"},
    {"openrouter", "openrouter"},
    {"antigravity", "antigravity"},
  };

  auto it = KEY_MAP.find(to_lower(provider_name));
  if (it != KEY_MAP.end()) return it->second;
  return std::string{provider_name};
}

}  // namespace ProviderTransform
}  // namespace qcode
