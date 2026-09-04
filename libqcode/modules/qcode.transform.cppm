module;

#include <qcode/transform/gemini_transform.h>
#include <qcode/transform/provider_transform.h>

export module qcode.transform;

export namespace qcode {
using ::qcode::JsonValue;
using ::qcode::Messages;
using ::qcode::Model;
using ::qcode::ModelInfo;
using ::qcode::ProviderInfo;
}  // namespace qcode

export namespace qcode::ProviderTransform {
using ::qcode::ProviderTransform::OUTPUT_TOKEN_MAX;
using ::qcode::ProviderTransform::ChatTransport;
using ::qcode::ProviderTransform::apply_reasoning_defaults;
using ::qcode::ProviderTransform::apply_reasoning_options;
using ::qcode::ProviderTransform::apply_stream_options;
using ::qcode::ProviderTransform::build_options;
using ::qcode::ProviderTransform::build_small_options;
using ::qcode::ProviderTransform::chat_transport_for;
using ::qcode::ProviderTransform::chat_wire_model_id;
using ::qcode::ProviderTransform::clamp_variant;
using ::qcode::ProviderTransform::cursor_family_id;
using ::qcode::ProviderTransform::cursor_picker_id;
using ::qcode::ProviderTransform::cursor_wire_model_id;
using ::qcode::ProviderTransform::default_variant;
using ::qcode::ProviderTransform::interleaved_replay_field;
using ::qcode::ProviderTransform::is_allowed_variant;
using ::qcode::ProviderTransform::is_opus_family;
using ::qcode::ProviderTransform::is_reasoning_model_id;
using ::qcode::ProviderTransform::max_output_tokens;
using ::qcode::ProviderTransform::next_variant;
using ::qcode::ProviderTransform::normalize_messages;
using ::qcode::ProviderTransform::normalize_schema;
using ::qcode::ProviderTransform::openrouter_wire_model_id;
using ::qcode::ProviderTransform::reasoning_variants;
using ::qcode::ProviderTransform::resolve_session_variant;
using ::qcode::ProviderTransform::sdk_key;
using ::qcode::ProviderTransform::temperature;
using ::qcode::ProviderTransform::top_k;
using ::qcode::ProviderTransform::top_p;
using ::qcode::ProviderTransform::wrap_provider_options;
using ::qcode::ProviderTransform::zen_api_protocol;
using ::qcode::ProviderTransform::zen_completions_path;
using ::qcode::ProviderTransform::zen_wire_model_id;
}  // namespace qcode::ProviderTransform

export namespace qcode::gemini {
using ::qcode::gemini::convert_openai_to_gemini;
using ::qcode::gemini::new_uuid;
using ::qcode::gemini::normalize_gemini_response;
using ::qcode::gemini::random_hex;
using ::qcode::gemini::unwrap_envelope;
using ::qcode::gemini::wrap_antigravity_envelope;
}  // namespace qcode::gemini
