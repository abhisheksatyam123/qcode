#pragma once

#ifndef QCODE_HAS_OPENAI
#error \
    "OpenAI component not available. Link qcode::engine (defines QCODE_HAS_OPENAI)."
#endif

#include <qcode/core/retry_policy.h>
#include <qcode/core/client.h>

#include <optional>
#include <map>
#include <string>

namespace qcode {
namespace openai {

struct CompatibleOptions {
  std::string base_url;
  std::string protocol = "chat_completions";
  std::string completions_path;
  std::map<std::string, std::string> headers;
  std::optional<retry::RetryConfig> retry_config;
};

namespace models {
/// Model ids used by the client's defaults and the integration tests. The
/// models qcode offers come from opencode.json, not from this list.
constexpr const char* kGpt54 = "gpt-5.4";
constexpr const char* kGpt4o = "gpt-4o";
constexpr const char* kGpt4oMini = "gpt-4o-mini";
constexpr const char* kGpt35Turbo = "gpt-3.5-turbo";

/// Default model used when none is specified
constexpr const char* kDefaultModel = kGpt54;

}  // namespace models

/// Create an OpenAI client with default configuration
/// Reads API key from OPENAI_API_KEY environment variable
/// @return Configured OpenAI client
Client create_client();

/// Create an OpenAI client with explicit API key
/// @param api_key OpenAI API key
/// @return Configured OpenAI client
Client create_client(const std::string& api_key);

/// Create an OpenAI client with custom configuration
/// @param api_key OpenAI API key
/// @param base_url Custom base URL (for OpenAI-compatible APIs)
/// @return Configured OpenAI client
Client create_client(const std::string& api_key, const std::string& base_url);

/// Create an OpenAI-compatible client with explicit transport and headers.
Client create_client(const std::string& api_key,
                     const CompatibleOptions& options);

/// Create an OpenAI client with custom configuration and retry settings
/// @param api_key OpenAI API key
/// @param base_url Custom base URL (for OpenAI-compatible APIs)
/// @param retry_config Custom retry configuration
/// @return Configured OpenAI client
Client create_client(const std::string& api_key,
                     const std::string& base_url,
                     const retry::RetryConfig& retry_config);

/// Try to create an OpenAI client using environment variables
/// Reads API key from OPENAI_API_KEY environment variable
/// @return Optional client - has value if environment variable is set, empty
/// otherwise
/// @note This is useful for chaining creation attempts with other providers
std::optional<Client> try_create_client();

}  // namespace openai
}  // namespace qcode
