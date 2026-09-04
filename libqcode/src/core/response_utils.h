#pragma once

#include <qcode/core/errors.h>
#include <qcode/core/logger.h>
#include <qcode/core/generate_options.h>

#include <string>

#include <nlohmann/json.hpp>

namespace qcode {
namespace utils {

// OpenRouter (Stealth / ox-alpha) sometimes returns HTTP 200 with
// finish_reason=stop and native_finish_reason=network_error plus a null
// message. Treat that as a dropped upstream, not a successful empty reply.
inline bool is_empty_upstream_network_drop(const nlohmann::json& response) {
  if (!response.contains("choices") || !response["choices"].is_array() ||
      response["choices"].empty() || !response["choices"][0].is_object()) {
    return false;
  }
  const auto& choice = response["choices"][0];
  if (choice.value("native_finish_reason", std::string{}) != "network_error") {
    return false;
  }

  const nlohmann::json* payload = nullptr;
  if (choice.contains("message") && choice["message"].is_object()) {
    payload = &choice["message"];
  } else if (choice.contains("delta") && choice["delta"].is_object()) {
    payload = &choice["delta"];
  }
  if (payload == nullptr) return true;

  if (payload->contains("content") && (*payload)["content"].is_string() &&
      !(*payload)["content"].get<std::string>().empty()) {
    return false;
  }
  if (payload->contains("tool_calls") && (*payload)["tool_calls"].is_array() &&
      !(*payload)["tool_calls"].empty()) {
    return false;
  }
  return true;
}

// Parse a generic error response that follows the common pattern of
// { "error": { "message": "...", "type": "..." } }
inline GenerateResult parse_standard_error_response(
    const std::string& provider_name,
    int status_code,
    const std::string& body) {
  LOG_DEBUG("Parsing error response - status: {}, body: {}",
                        status_code, body);

  GenerateResult result;
  result.is_retryable = qcode::is_status_code_retryable(status_code);

  try {
    auto json = nlohmann::json::parse(body);
    if (json.contains("error")) {
      auto& error = json["error"];
      std::string message = error.value("message", "Unknown error");
      std::string type = error.value("type", "");

      std::string full_error = provider_name + " API error (" +
                               std::to_string(status_code) + "): " + message +
                               (type.empty() ? "" : " [" + type + "]");
      LOG_ERROR("{} API error parsed: {}", provider_name,
                            full_error);

      result.error = full_error;
      return result;
    }
  } catch (...) {
    // If JSON parsing fails, return raw error
    LOG_DEBUG("Failed to parse error response as JSON");
  }

  std::string raw_error =
      "HTTP " + std::to_string(status_code) + " error: " + body;
  LOG_ERROR("{} API raw error: {}", provider_name, raw_error);

  result.error = raw_error;
  return result;
}

}  // namespace utils
}  // namespace qcode