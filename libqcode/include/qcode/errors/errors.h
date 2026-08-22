#pragma once

#include <stdexcept>
#include <string>

namespace qcode {

/// Check if an HTTP status code indicates a retryable error
inline bool is_status_code_retryable(int status_code) {
  return status_code == 408 ||  // Request Timeout
         status_code == 409 ||  // Conflict
         status_code == 425 ||  // Too Early
         status_code == 429 ||  // Too Many Requests
         status_code >= 500;    // Server errors (5xx)
}

/// Check if an error message body contains transient/rate-limit error indicators
/// (handles providers returning transient capacity issues under 400 or 403).
inline bool is_error_message_retryable(std::string_view msg) {
  if (msg.empty()) return false;
  std::string lower;
  lower.reserve(msg.size());
  for (char c : msg) lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  return lower.find("rate limit") != std::string::npos ||
         lower.find("rate_limit") != std::string::npos ||
         lower.find("too many requests") != std::string::npos ||
         lower.find("resource_exhausted") != std::string::npos ||
         lower.find("overloaded") != std::string::npos ||
         lower.find("capacity") != std::string::npos ||
         lower.find("temporarily unavailable") != std::string::npos ||
         lower.find("try again later") != std::string::npos;
}

/// Base class for all qcode errors
class AIError : public std::runtime_error {
 public:
  explicit AIError(const std::string& message) : std::runtime_error(message) {}
  explicit AIError(const char* message) : std::runtime_error(message) {}
};

/// Error related to API communication
class APIError : public AIError {
 private:
  int status_code_;

 public:
  APIError(int status_code, const std::string& message)
      : AIError("API Error (" + std::to_string(status_code) + "): " + message),
        status_code_(status_code) {}

  int status_code() const { return status_code_; }

  /// Check if the error is retryable based on status code
  bool is_retryable() const {
    return qcode::is_status_code_retryable(status_code_);
  }
};

/// Error related to authentication/authorization
class AuthenticationError : public APIError {
 public:
  explicit AuthenticationError(const std::string& message)
      : APIError(401, "Authentication failed: " + message) {}
};

/// Error related to request rate limiting
class RateLimitError : public APIError {
 public:
  explicit RateLimitError(const std::string& message)
      : APIError(429, "Rate limit exceeded: " + message) {}
};

/// Error related to invalid configuration
class ConfigurationError : public AIError {
 public:
  explicit ConfigurationError(const std::string& message)
      : AIError("Configuration error: " + message) {}
};

/// Error related to network/connection issues
class NetworkError : public AIError {
 public:
  explicit NetworkError(const std::string& message)
      : AIError("Network error: " + message) {}
};

/// Error related to invalid model or unsupported operations
class ModelError : public AIError {
 public:
  explicit ModelError(const std::string& message)
      : AIError("Model error: " + message) {}
};

}  // namespace qcode