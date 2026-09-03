#pragma once

#include <qcode/core/message.h>
#include <qcode/core/model.h>
#include <qcode/core/state.h>
#include <qcode/core/tool.h>

#include <functional>
#include <map>
#include <optional>
#include <string>
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

/// Global max output token cap (mirrors opencode's OUTPUT_TOKEN_MAX = 32000)
constexpr int OUTPUT_TOKEN_MAX = 32000;

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
/// - Flattens tool_call_id sequences
/// - Applies provider-specific adjustments
Messages normalize_messages(const Messages& messages, const Model& model);

// ── Provider options ──

/// Build standard provider request options
/// @param model Target model
/// @param reasoning_effort Optional reasoning effort level ("low", "medium", "high")
/// @param budget_tokens Optional reasoning budget tokens
/// @return JSON object with provider options
JsonValue build_options(const Model& model,
                        const std::optional<std::string>& reasoning_effort = std::nullopt,
                        std::optional<int> budget_tokens = std::nullopt);

/// Build reduced options for compact/fast mode
JsonValue build_small_options(const Model& model);

/// Wrap options under the correct provider SDK key
/// Maps provider name to SDK key (e.g., "qpilot" -> "openai", etc.)
JsonValue wrap_provider_options(const Model& model, const JsonValue& options);

// ── Token management ──

/// Clamp max output tokens to min(model.limit, OUTPUT_TOKEN_MAX)
int max_output_tokens(int model_limit);

// ── Schema normalization ──

/// Normalize JSON schema for provider quirks:
/// - Anthropic/Bedrock: flatten top-level anyOf/oneOf
/// - Gemini: sanitize schema
/// - Qualcomm Vertex: extra processing
JsonValue normalize_schema(const JsonValue& schema, const Model& model);

// ── Utility ──

/// Check if model ID belongs to the Opus family
bool is_opus_family(const std::string& model_id);

/// Extract SDK key from provider name
std::string sdk_key(const std::string& provider_name);

// ── Reasoning variants (mirrors transform.ts reasoningVariants) ──

/// True for ids that think even when the catalog omitted reasoning=true
/// (Muse Spark, Ox Alpha, DeepSeek V4, GPT-5, Grok, Gemini 2.5/3, ...).
bool is_reasoning_model_id(const std::string& model_id);

/// Mark a catalog/config entry as a reasoner and fill effort levels / field
/// when they were omitted. Safe to call more than once.
void apply_reasoning_defaults(ModelInfo& model);

/// Effort levels a model supports. Catalog-declared reasoning_options win;
/// falls back to upstream's widely-supported set for known families.
std::vector<std::string> reasoning_variants(const ModelInfo& model);

/// Default effort with thinking enabled (upstream turns reasoning on by
/// default per family: medium for gpt-5.x, high elsewhere). "off" when the
/// model cannot reason.
std::string default_variant(const ModelInfo& model);

/// Map a user-selected effort onto one the model actually advertises.
/// "off" stays "off". Unknown levels snap to the nearest supported effort.
std::string clamp_variant(const ModelInfo& model, const std::string& requested);

// ── Chat transport flavors (mirrors providerID / api.npm dispatch) ──

enum class ChatTransport {
  kOpenAI,        // api.openai.com — Responses-style chat quirks
  kOpenRouter,    // openrouter.ai — reasoning:{effort}, usage:{include:true}
  kOpenCodeZen,   // opencode.ai/zen — plain openai-compatible
  kCompatible,    // every other OpenAI-compatible endpoint
};

ChatTransport chat_transport_for(const std::string& base_url);

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
std::string zen_api_protocol(const std::string& model_id);

/// Path under `https://opencode.ai/zen/v1` for that model.
std::string zen_completions_path(const std::string& model_id);

/// Wire id for OpenRouter. Maps Zen free-pool aliases onto stealth/ox-alpha
/// and meta/muse-spark-1.2.
std::string openrouter_wire_model_id(std::string model_id);

/// Cursor Agent exposes many effort SKUs (grok-4.6-high, opus-5-thinking-low).
/// Collapse those to the two family ids the picker should show.
std::string cursor_family_id(const std::string& model_id);

/// Picker id for a Cursor Agent SKU: known families first, then strip a
/// trailing effort suffix (-thinking-low/medium/high or -low/medium/high).
std::string cursor_picker_id(const std::string& model_id);

/// Map family + /variant effort onto the slug AgentService accepts.
/// Grok: grok-4.6 / grok-4.6-low / grok-4.6-high
/// Opus: claude-opus-5 / claude-opus-5-thinking-{low,high}
std::string cursor_wire_model_id(
    const std::string& model_id,
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
std::string interleaved_replay_field(ChatTransport transport,
                                     const std::string& model_id);

/// Streaming/usage body options: stream_options.include_usage always with
/// streaming; OpenRouter additionally gets usage.include so cached-token
/// accounting is returned.
void apply_stream_options(nlohmann::json& body,
                          ChatTransport transport,
                          bool stream);

}  // namespace ProviderTransform
}  // namespace qcode
