#pragma once

#include <nlohmann/json.hpp>
#include <string>

namespace ai {
namespace gemini {

/// Random helpers (UUID v4 + hex). Shared by the Antigravity/Vertex envelope
/// and by provider-specific header construction (e.g. Qualcomm turn/session
/// IDs). Kept here so they are defined once rather than duplicated per provider.
std::string new_uuid();
std::string random_hex(size_t len);

/// Convert an OpenAI-style chat-completion request JSON into the Gemini
/// generateContent request JSON (contents / systemInstruction /
/// generationConfig). Mirrors the wire format Antigravity/Vertex expects.
nlohmann::json convert_openai_to_gemini(const nlohmann::json& openai_req);

/// Wrap a Gemini request in the Antigravity/Vertex envelope
/// (project / requestId / request / model / userAgent / requestType).
/// Maps "gemini-3-flash" -> "gemini-3-flash-agent".
nlohmann::json wrap_antigravity_envelope(const nlohmann::json& gemini_req,
                                         const std::string& model);

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
}  // namespace ai
