#pragma once

#include <qcode/core/message.h>
#include <qcode/core/model.h>
#include <qcode/config/provider_info.h>
#include <qcode/core/tool.h>

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

namespace qcode {

using JsonValue = nlohmann::json;

/// ProviderTransform — single unified layer on top of all providers.
///
/// Normalizes messages, provides model-family-specific defaults
/// (temperature/topP/topK), wraps provider options, normalizes
/// JSON schemas per provider quirks, and clamps output tokens.
///
/// Ported from opencode's src/provider/transform.ts (ProviderTransform
/// namespace) — the single transformer layer that all provider calls
/// flow through.
///
/// Usage:
///   auto msgs = ProviderTransform::normalize_messages(raw_messages, model);
///   double temp = ProviderTransform::temperature(model);
///   auto opts = ProviderTransform::build_options(model, reasoning_effort);
///   auto schema = ProviderTransform::normalize_schema(raw_schema, model);
///
namespace ProviderTransform {


// ── Model-family-specific defaults ──

/// Returns model-family-specific temperature default, or nullopt for default
std::optional<double> temperature(const Model& model);

/// Returns model-family-specific topP default, or nullopt for default  
std::optional<double> top_p(const Model& model);

/// Returns model-family-specific topK default, or nullopt for default
std::optional<int> top_k(const Model& model);

// ── Message normalization ──

/// Normalize messages for a specific provider/model:
/// - Removes unsupported content parts (audio, video, etc. if unsupported)
/// - Canonicalizes tool_call ids to [A-Za-z0-9_-]{1,64} (OpenAI Responses
///   rejects longer call_id; required when switching Cursor/Grok → Muse Spark)
/// - Drops reasoning / thought_signature that the target family cannot replay
///   (unsigned thoughts 400 Gemini; signed Gemini/Claude blobs 400 Muse/OpenAI)
/// - Closes unpaired tool calls (see close_unpaired_tool_calls)
/// - Applies provider-specific adjustments
/// Takes the history by value: pass an rvalue to avoid copying it.
Messages normalize_messages(Messages messages, const Model& model);

/// Scrub + bound a tool-call id so every provider can replay it. Stable.
[[nodiscard]] std::string canonicalize_tool_call_id(std::string_view id);

/// Insert synthetic error tool results immediately after assistant tool_calls
/// that have no matching result. OpenAI/Claude 400 when a function_call is
/// replayed without an output — common after a TUI restart or abort mid-tool.
Messages close_unpaired_tool_calls(Messages messages);

/// Every request builder runs this before serializing, so none of them looks
/// inside tool-result JSON. Each `image` tool result (ImageTool) becomes a
/// short text result, and its pixels a typed ImageContentPart in one user
/// message placed after that run of tool-result messages: OpenAI Chat and
/// Responses reject images in tool output and need tool messages contiguous,
/// and Anthropic needs tool_result blocks before other user content.
/// Formats outside png/jpeg/webp/gif (old sessions) are described, not sent.
Messages lift_tool_result_images(Messages messages);

/// True when lift_tool_result_images would change `messages`, i.e. some tool
/// result is an `image` tool result. Lets callers holding a const history
/// skip copying it.
[[nodiscard]] bool has_tool_result_images(const Messages& messages);

// ── Provider options ──

/// Build standard provider request options
/// @param model Target model
/// @param reasoning_effort Optional reasoning effort level ("low", "medium", "high")
/// @param budget_tokens Optional reasoning budget tokens
/// @return JSON object with provider options
JsonValue build_options(const Model& model,
                        const std::optional<std::string>& reasoning_effort = std::nullopt,
                        std::optional<int> budget_tokens = std::nullopt);

// ── Token management ──

/// Default request max_tokens from opencode.json: the model's "max_tokens"
/// (inherited from model_defaults) capped by limit.output, else limit.output,
/// else nullopt (the provider's own default). No built-in cap.
[[nodiscard]] std::optional<int> max_output_tokens(const ModelInfo& model);

// ── Schema normalization ──

/// Normalize JSON schema for provider quirks:
/// - Anthropic/Bedrock: flatten top-level anyOf/oneOf
/// - All providers: exclusiveMinimum/Maximum → inclusive min/max; drop
///   $schema and other keys LLM tool APIs commonly reject
/// - Gemini: additional sanitization in convert_openai_to_gemini
JsonValue normalize_schema(const JsonValue& schema, const Model& model);

// ── Utility ──

/// Check if model ID belongs to the Opus family
[[nodiscard]] bool is_opus_family(std::string_view model_id);

// ── Reasoning variants (mirrors transform.ts reasoningVariants) ──

/// True for ids that think even when the catalog omitted reasoning=true
/// (Muse Spark, Ox Alpha, DeepSeek V4, GPT-5, Grok, Gemini 2.5/3, ...).
[[nodiscard]] bool is_reasoning_model_id(std::string_view model_id);

/// Mark a catalog/config entry as a reasoner and fill effort levels / field
/// when they were omitted. Safe to call more than once.
void apply_reasoning_defaults(ModelInfo& model);

/// Effort levels a model supports. Config-declared reasoning_efforts win;
/// reasoning=true with no list falls back to low/medium/high.
[[nodiscard]] std::vector<std::string> reasoning_variants(const ModelInfo& model);

/// Configured variant spec (opencode.json "variants" object) for `id`, or
/// nullptr when the model only lists plain effort ids.
[[nodiscard]] const VariantInfo* find_variant(const ModelInfo& model,
                                              std::string_view id);

/// Effort sent on the wire for variant `id`: the configured "effort" when
/// set (e.g. ultra -> max), else the id itself. "off" -> empty.
[[nodiscard]] std::string variant_wire_effort(const ModelInfo& model,
                                              std::string_view id);

/// Configured default effort (`reasoning_default` if advertised, else first
/// effort). "off" when the model cannot reason (or its default is off and
/// thinking can be disabled).
[[nodiscard]] std::string default_variant(const ModelInfo& model);

/// Map a user-selected effort onto one the model actually advertises.
/// "off" stays "off" unless the model cannot disable thinking. Unknown
/// levels snap along low<medium<high<xhigh<max<ultra: the nearest lower
/// advertised level first (never exceed what was asked), then the nearest
/// higher one; ids off that ladder fall back to the model default.
[[nodiscard]] std::string clamp_variant(const ModelInfo& model,
                                        std::string_view requested);

/// True if `requested` is "off" (when allowed) or a configured effort.
[[nodiscard]] bool is_allowed_variant(const ModelInfo& model,
                                      std::string_view requested);

/// Next id in [off] + configured efforts. Cycles. Unknown current → first.
[[nodiscard]] std::string next_variant(const ModelInfo& model,
                                       std::string_view current);

/// Empty current → model's configured default. "off" stays off when the
/// model allows it. Anything else is clamped onto the advertised efforts.
[[nodiscard]] std::string resolve_session_variant(const ModelInfo& model,
                                                  std::string_view current);

// ── Chat transport flavors (mirrors providerID / api.npm dispatch) ──

enum class ChatTransport {
  kOpenAI,        // api.openai.com — Responses-style chat quirks
  kOpenRouter,    // openrouter.ai — reasoning:{effort}, usage:{include:true}
  kOpenCodeZen,   // opencode.ai/zen — plain openai-compatible
  kCompatible,    // every other OpenAI-compatible endpoint
};

[[nodiscard]] ChatTransport chat_transport_for(std::string_view base_url);

/// Canonical chat/completions id for this transport. Cross-maps the aliases
/// users pick (ox-alpha, stealth/ox-alpha, x-preview-f-free, Muse Spark
/// slugs) onto the id that endpoint actually validates.
std::string chat_wire_model_id(ChatTransport transport, std::string model_id);

/// Wire id for OpenCode Zen chat/completions. Strips an `opencode/` prefix
/// and remaps catalog/display aliases (ox-alpha, hy3-free) onto ids the
/// current Zen `/v1/models` list accepts.
std::string zen_wire_model_id(std::string model_id);

/// Zen API flavor from OpenCode's zen.mdx endpoint table:
/// - responses: Muse Spark, GPT-5.x, Grok
/// - messages: Claude, Qwen 3.x
/// - google: Gemini (`/models/{id}:generateContent`)
/// - chat_completions: Ox Alpha, DeepSeek, Kimi, GLM, MiniMax, free pool
[[nodiscard]] std::string zen_api_protocol(std::string_view model_id);

/// Path under `https://opencode.ai/zen/v1` for that model.
[[nodiscard]] std::string zen_completions_path(std::string_view model_id);

/// Wire id for OpenRouter. Maps Zen free-pool aliases onto stealth/ox-alpha
/// and meta/muse-spark-1.2.
std::string openrouter_wire_model_id(std::string model_id);

/// Cursor Agent exposes many effort SKUs (cursor-grok-4.6-high,
/// claude-opus-5-thinking-low). Collapse those to the family ids the picker
/// should show.
[[nodiscard]] std::string cursor_family_id(std::string_view model_id);

/// Picker id for a Cursor Agent SKU: known families first, then strip a
/// trailing effort suffix (-thinking-low/medium/high or -low/medium/high).
[[nodiscard]] std::string cursor_picker_id(std::string_view model_id);

/// Map family + /variant effort onto the slug AgentService accepts.
/// Grok: cursor-grok-4.6-{low,medium,high,xhigh} (medium default, max->xhigh)
/// Opus: claude-opus-5 / claude-opus-5-thinking-{low,high}
[[nodiscard]] std::string cursor_wire_model_id(
    std::string_view model_id,
    const std::optional<std::string>& effort = std::nullopt);

/// Place reasoning-effort options the way OpenCode's transform.ts does:
/// - OpenRouter (`@openrouter/ai-sdk-provider`): {"reasoning": {"effort": e}}
/// - Zen / openai-compatible: {"reasoning_effort": e}
/// Do not add include_reasoning or reasoning.exclude — OpenCode does not.
void apply_reasoning_options(nlohmann::json& body,
                             ChatTransport transport,
                             const std::optional<std::string>& effort);

/// Assistant-message field used to replay interleaved thinking, matching
/// OpenCode's `capabilities.interleaved.field` injection. Empty means skip
/// (OpenRouter — OpenCode does not inject the field for that SDK).
[[nodiscard]] std::string interleaved_replay_field(ChatTransport transport,
                                                   std::string_view model_id);

/// Streaming/usage body options: stream_options.include_usage always with
/// streaming; OpenRouter additionally gets usage.include so cached-token
/// accounting is returned.
void apply_stream_options(nlohmann::json& body,
                          ChatTransport transport,
                          bool stream);

}  // namespace ProviderTransform
}  // namespace qcode
