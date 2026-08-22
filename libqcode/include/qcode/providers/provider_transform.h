#pragma once

#include <qcode/core/message.h>
#include <qcode/core/model.h>
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

}  // namespace ProviderTransform
}  // namespace qcode
