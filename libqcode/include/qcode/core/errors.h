#pragma once

#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <string_view>

namespace qcode {

/**
 * @brief Peel provider JSON / retry dumps down to a single readable line so
 * the UI does not render an oversized blob.
 *
 * @param raw The raw error message or JSON payload.
 * @param max_chars Maximum length of the returned error string.
 * @return Formatted and sanitized single-line user-facing error message.
 */
[[nodiscard]] inline std::string format_user_facing_error(std::string_view raw,
                                                          size_t max_chars = 180) {
  // Trim leading whitespace
  const auto start = raw.find_first_not_of(" \t\n\r");
  if (start == std::string_view::npos) return {};
  raw.remove_prefix(start);

  std::string s{raw};
  const bool had_error_prefix =
      s.rfind("Error:", 0) == 0 || s.rfind("Exception:", 0) == 0;

  auto peel_json_error = [](const std::string& blob) -> std::string {
    const auto brace = blob.find('{');
    const auto end = blob.rfind('}');
    if (brace == std::string::npos || end == std::string::npos || end <= brace) {
      return {};
    }
    const std::string json_str = blob.substr(brace, end - brace + 1);
    nlohmann::json j = nlohmann::json::parse(json_str, nullptr, false);
    if (!j.is_object() && !j.is_array()) {
      return {};
    }

    // 1. Check nested details array (Cursor, Google Connect-RPC, Vertex)
    const nlohmann::json* details_ptr = nullptr;
    if (j.contains("error") && j["error"].is_object() && j["error"].contains("details")) {
      details_ptr = &j["error"]["details"];
    } else if (j.contains("details")) {
      details_ptr = &j["details"];
    }

    if (details_ptr && details_ptr->is_array()) {
      for (const auto& item : *details_ptr) {
        if (!item.is_object()) continue;
        if (item.contains("debug") && item["debug"].is_object()) {
          const auto& debug = item["debug"];
          if (debug.contains("details") && debug["details"].is_object()) {
            const auto& d = debug["details"];
            std::string title = d.value("title", "");
            std::string detail = d.value("detail", "");
            if (!title.empty() && !detail.empty()) {
              return title + ": " + detail;
            }
            if (!detail.empty()) return detail;
            if (!title.empty()) return title;
          }
          if (debug.contains("error") && debug["error"].is_string()) {
            std::string err = debug["error"].get<std::string>();
            if (!err.empty() && err != "Error" && err != "error") return err;
          }
        }
        if (item.contains("detail") && item["detail"].is_string()) {
          std::string d = item["detail"].get<std::string>();
          if (!d.empty() && d != "Error" && d != "error") return d;
        }
        if (item.contains("message") && item["message"].is_string()) {
          std::string m = item["message"].get<std::string>();
          if (!m.empty() && m != "Error" && m != "error") return m;
        }
      }
    }

    // 2. Check error object
    if (j.contains("error")) {
      if (j["error"].is_string()) {
        std::string e = j["error"].get<std::string>();
        if (!e.empty() && e != "Error" && e != "error") return e;
      } else if (j["error"].is_object()) {
        const auto& err = j["error"];
        if (err.contains("message") && err["message"].is_string()) {
          std::string msg = err["message"].get<std::string>();
          if (!msg.empty() && msg != "Error" && msg != "error") return msg;
        }
        if (err.contains("detail") && err["detail"].is_string()) {
          return err["detail"].get<std::string>();
        }
        if (err.contains("description") && err["description"].is_string()) {
          return err["description"].get<std::string>();
        }
        if (err.contains("code") && err["code"].is_string()) {
          std::string code = err["code"].get<std::string>();
          if (code == "resource_exhausted") return "Resource exhausted (rate limit or quota reached)";
          return code;
        }
      }
    }

    // 3. Check top-level message / detail / description
    if (j.contains("message") && j["message"].is_string()) {
      std::string msg = j["message"].get<std::string>();
      if (!msg.empty() && msg != "Error" && msg != "error") return msg;
    }
    if (j.contains("detail") && j["detail"].is_string()) {
      return j["detail"].get<std::string>();
    }
    if (j.contains("error_description") && j["error_description"].is_string()) {
      return j["error_description"].get<std::string>();
    }
    if (j.contains("code") && j["code"].is_string()) {
      std::string code = j["code"].get<std::string>();
      if (code == "resource_exhausted") return "Resource exhausted (rate limit or quota reached)";
      return code;
    }

    return {};
  };

  auto peel_message = [](const std::string& blob) -> std::string {
    const auto brace = blob.find('{');
    const auto end = blob.rfind('}');
    if (brace == std::string::npos || end == std::string::npos || end <= brace) {
      return {};
    }
    const std::string json = blob.substr(brace, end - brace + 1);
    static constexpr std::string_view kKeys[] = {"\"message\":\"",
                                                 "\"message\": \""};
    size_t best = std::string::npos;
    size_t key_len = 0;
    for (const auto key : kKeys) {
      const auto p = json.rfind(std::string(key));
      if (p != std::string::npos && (best == std::string::npos || p > best)) {
        best = p;
        key_len = key.size();
      }
    }
    if (best == std::string::npos) return {};
    std::string out;
    for (size_t i = best + key_len; i < json.size(); ++i) {
      if (json[i] == '\\' && i + 1 < json.size()) {
        const char esc = json[i + 1];
        if (esc == 'n' || esc == 'r' || esc == 't') {
          out.push_back(' ');
        } else {
          out.push_back(esc);
        }
        ++i;
        continue;
      }
      if (json[i] == '"') break;
      out.push_back(json[i]);
    }
    if (out == "Error" || out == "error") return {};
    return out;
  };

  if (auto peeled = peel_json_error(s); !peeled.empty()) {
    s = std::move(peeled);
  } else if (auto peeled_msg = peel_message(s); !peeled_msg.empty()) {
    s = std::move(peeled_msg);
  }

  static constexpr std::string_view kWrappers[] = {
      "Error from provider (Console): ",
      "Error from provider (console): ",
      "Upstream request failed: ",
  };
  for (bool changed = true; changed;) {
    changed = false;
    for (const auto prefix : kWrappers) {
      if (s.size() > prefix.size() &&
          s.compare(0, prefix.size(), prefix.data(), prefix.size()) == 0) {
        s.erase(0, prefix.size());
        changed = true;
      }
    }
  }

  std::string compact;
  compact.reserve(s.size());
  bool space = false;
  for (char c : s) {
    if (c == '\n' || c == '\r' || c == '\t') c = ' ';
    if (c == ' ') {
      if (!space && !compact.empty()) compact.push_back(' ');
      space = true;
    } else {
      compact.push_back(c);
      space = false;
    }
  }
  while (!compact.empty() && compact.back() == ' ') compact.pop_back();

  if (compact.empty() || compact == "Error" || compact == "error" || compact == "Error: Error") {
    compact = "Upstream request failed";
  }

  if (compact.size() > max_chars) {
    if (max_chars <= 3) {
      compact.resize(max_chars);
    } else {
      compact.resize(max_chars - 3);
      compact += "...";
    }
  }
  if (had_error_prefix && compact.rfind("Error:", 0) != 0 &&
      compact.rfind("Exception:", 0) != 0) {
    compact = "Error: " + compact;
  }
  while (compact.starts_with("Error: Error: ")) {
    compact.erase(0, 7);
  }
  if (compact == "Error: Error") {
    compact = "Error: Upstream request failed";
  }
  return compact;
}

/**
 * @brief Check if an HTTP status code indicates a retryable transient error.
 * @param status_code HTTP status code.
 * @return True if status code is retryable.
 */
[[nodiscard]] inline bool is_status_code_retryable(int status_code) {
  return status_code == 404 ||  // Not Found — transient for OpenAI-compatible (opencode parity)
         status_code == 408 ||  // Request Timeout
         status_code == 409 ||  // Conflict
         status_code == 425 ||  // Too Early
         status_code == 429 ||  // Too Many Requests
         status_code >= 500;    // Server errors (5xx)
}

// Antigravity (and some Vertex) 404s mean the model/project SKU does not
// exist. Retrying six times just burns a minute and surfaces "retry limit".
[[nodiscard]] inline bool is_permanent_not_found(int status_code,
                                                 std::string_view body) {
  if (status_code != 404) return false;
  return body.find("Requested entity was not found") != std::string_view::npos ||
         body.find("\"status\": \"NOT_FOUND\"") != std::string_view::npos ||
         body.find("\"status\":\"NOT_FOUND\"") != std::string_view::npos;
}

/**
 * @brief Check if an error message body contains transient/rate-limit error indicators.
 * @param msg Error message to inspect.
 * @return True if the error message matches known transient failure patterns.
 */
[[nodiscard]] inline bool is_error_message_retryable(std::string_view msg) {
  if (msg.empty()) return false;
  std::string lower;
  lower.reserve(msg.size());
  for (char c : msg) lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));

  // Rate limiting / capacity
  const bool rate_or_capacity =
      lower.find("rate increased too quickly") != std::string::npos ||
      lower.find("rate limit") != std::string::npos ||
      lower.find("rate-limit") != std::string::npos ||
      lower.find("rate_limit") != std::string::npos ||
      lower.find("too many requests") != std::string::npos ||
      lower.find("overloaded") != std::string::npos ||
      lower.find("service unavailable") != std::string::npos ||
      lower.find("service_unavailable") != std::string::npos ||
      lower.find("internal error") != std::string::npos ||
      lower.find("internal_error") != std::string::npos ||
      lower.find("internal server error") != std::string::npos ||
      lower.find("server_error") != std::string::npos ||
      lower.find("provider returned error") != std::string::npos ||
      lower.find("provider_returned_error") != std::string::npos;
  if (rate_or_capacity) return true;

  // Network / transport failures
  const bool network =
      lower.find("terminated") != std::string::npos ||
      lower.find("fetch failed") != std::string::npos ||
      lower.find("failed to fetch") != std::string::npos ||
      lower.find("network error") != std::string::npos ||
      lower.find("upstream connect") != std::string::npos ||
      lower.find("connection error") != std::string::npos ||
      lower.find("connection refused") != std::string::npos ||
      lower.find("connection lost") != std::string::npos ||
      lower.find("socket connection was closed") != std::string::npos ||
      lower.find("socket hang up") != std::string::npos ||
      lower.find("reset before headers") != std::string::npos ||
      lower.find("getaddrinfo") != std::string::npos ||
      lower.find("enotfound") != std::string::npos ||
      lower.find("eai_again") != std::string::npos ||
      lower.find("econnrefused") != std::string::npos ||
      lower.find("econnreset") != std::string::npos ||
      lower.find("etimedout") != std::string::npos;
  if (network) return true;

  // Timeouts ("timeout", "request timeout", "connection timed out", ...)
  {
    static constexpr std::string_view kSubjects[] = {
        "request", "response", "connection", "network", "stream", "read"};
    for (const auto subject : kSubjects) {
      for (const auto verb : {"timeout", "timed out", "time out"}) {
        std::string needle = std::string(subject) + " " + verb;
        if (lower.find(needle) != std::string::npos) return true;
      }
    }
    // Bare "<word> timeout" style messages (e.g. "... request timeout").
    if (lower == "timeout") return true;
    if (lower.find("timeout") != std::string::npos &&
        (lower.find("request") != std::string::npos ||
         lower.find("stream") != std::string::npos)) {
      return true;
    }
  }

  // Retry invitations / exhaustion
  return lower.find("try your request again") != std::string::npos ||
         lower.find("retry your request") != std::string::npos ||
         lower.find("resource exhausted") != std::string::npos ||
         lower.find("resource_exhausted") != std::string::npos ||
         lower.find("try again later") != std::string::npos ||
         lower.find("try again in") != std::string::npos ||
         lower.find("currently at capacity") != std::string::npos ||
         lower.find("temporarily at capacity") != std::string::npos ||
         lower.find("temporarily unavailable") != std::string::npos ||
         lower.find("freeusagelimiterror") != std::string::npos;
}

/**
 * @brief Check if a provider error indicates context-window overflow (requires compaction, not retry).
 * @param status_code HTTP status code.
 * @param msg Error message to inspect.
 * @return True if error represents context overflow.
 */
[[nodiscard]] inline bool is_context_overflow_error(int status_code, std::string_view msg) {
  if (status_code == 413) return true;  // Payload Too Large always overflow
  if (msg.empty()) return false;
  std::string lower;
  lower.reserve(msg.size());
  for (char c : msg) lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  // Exclude transient throttling that also mentions tokens but is retryable
  if (lower.find("throttl") != std::string::npos ||
      lower.find("service unavailable") != std::string::npos ||
      lower.find("service_unavailable") != std::string::npos ||
      lower.find("rate limit") != std::string::npos ||
      lower.find("rate-limit") != std::string::npos ||
      lower.find("too many requests") != std::string::npos) {
    return false;
  }
  // Core overflow signals (32 patterns in upstream; cover the common 10 here)
  return lower.find("context_length_exceeded") != std::string::npos ||
         lower.find("context window") != std::string::npos ||
         lower.find("maximum context") != std::string::npos ||
         lower.find("max context") != std::string::npos ||
         lower.find("input is too long") != std::string::npos ||
         lower.find("prompt is too long") != std::string::npos ||
         lower.find("too many tokens") != std::string::npos ||
         lower.find("token limit") != std::string::npos ||
         lower.find("context limit") != std::string::npos ||
         lower.find("exceeds the maximum") != std::string::npos ||
         lower.find("request too large") != std::string::npos ||
         lower.find("payload too large") != std::string::npos ||
         (status_code == 400 && lower.find("maximum") != std::string::npos) ||
         (status_code == 400 && lower.find("too large") != std::string::npos);
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

  [[nodiscard]] int status_code() const noexcept { return status_code_; }

  /// Check if the error is retryable based on status code
  [[nodiscard]] bool is_retryable() const {
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