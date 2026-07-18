#pragma once

#include <qcode/errors/errors.h>
#include <qcode/logger/logger.h>
#include <qcode/types/generate_options.h>

#include <string>

#include <nlohmann/json.hpp>

namespace qcode {
namespace utils {

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