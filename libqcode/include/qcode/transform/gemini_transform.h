#pragma once

#include <nlohmann/json.hpp>
#include <string>

namespace qcode {
namespace gemini {

/// Random helpers (UUID v4 + hex). Shared by the Antigravity/Vertex envelope
/// and by provider-specific header construction (e.g. Qualcomm turn/session
/// IDs). Kept here so they are defined once rather than duplicated per provider.
std::string new_uuid();
std::string random_hex(size_t len);

/// opencode.json settings the OpenAI-shaped request does not carry (or
/// carries in a transport-specific key). The request's reasoning_effort (the
/// variant's wire effort) becomes thinkingConfig.thinkingLevel verbatim;
/// nothing here is model-specific.
struct GeminiOptions {
  std::string type;           // thinking.type: "disabled" sends no thinkingConfig
  std::string display;        // thinking.display: "omitted" -> includeThoughts=false
  int budget_tokens = 0;      // variant budget_tokens -> thinkingBudget (0 = none)
  int max_output_tokens = 0;  // output budget -> maxOutputTokens (0 = request max_completion_tokens)
};

/// Convert an OpenAI-style chat-completion request JSON into the Gemini
/// generateContent request JSON (contents / systemInstruction /
/// generationConfig). Mirrors the wire format Antigravity/Vertex expects.
nlohmann::json convert_openai_to_gemini(const nlohmann::json& openai_req,
                                        const GeminiOptions& options = {});

/// Wrap a Gemini request in the Antigravity/Vertex envelope
/// (project / requestId / request / model / userAgent / requestType).
/// Maps "gemini-3-flash" -> "gemini-3-flash-agent" and Gemini 3.6/3.7/3.8
/// Flash onto effort SKUs (`-low` / `-medium` / `-high`). Takes the request
/// by value: pass an rvalue to move it into the envelope.
nlohmann::json wrap_antigravity_envelope(nlohmann::json gemini_req,
                                         const std::string& model,
                                         const std::string& project_id = "");

/// Unwrap an Antigravity SSE/HTTP envelope. If `json` carries an object under
/// the "response" key, return that inner payload; otherwise return `json`
/// unchanged. Lets downstream OpenAI/Gemini parsers see a uniform payload.
nlohmann::json unwrap_envelope(const nlohmann::json& json);

/// Normalize a Gemini generateContent response (possibly envelope-wrapped)
/// into an OpenAI-style chat-completion JSON (id / model / created / choices /
/// usage). If `response` has no `candidates`, it is returned unchanged so
/// already-OpenAI-shaped payloads pass through untouched.
nlohmann::json normalize_gemini_response(const nlohmann::json& response);

}  // namespace gemini
}  // namespace qcode
