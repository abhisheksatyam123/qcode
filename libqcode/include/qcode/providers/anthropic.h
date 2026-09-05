#pragma once

#ifndef QCODE_HAS_ANTHROPIC
#error \
    "Anthropic component not available. Link qcode::engine (defines QCODE_HAS_ANTHROPIC)."
#endif

#include <qcode/core/client.h>
#include <qcode/core/retry_policy.h>

#include <map>
#include <optional>
#include <string>

namespace qcode {
namespace anthropic {

namespace models {
/// Latest Anthropic model identifiers
constexpr const char* kClaudeOpus47 = "claude-opus-4-7";
constexpr const char* kClaudeSonnet46 = "claude-sonnet-4-6";
constexpr const char* kClaudeHaiku45 =
    "claude-haiku-4-5";  // claude-haiku-4-5-20251001

/// Legacy model identifiers (still available; consider migrating)
constexpr const char* kClaudeOpus46 = "claude-opus-4-6";
constexpr const char* kClaudeOpus45 =
    "claude-opus-4-5";  // claude-opus-4-5-20251101
constexpr const char* kClaudeSonnet45 =
    "claude-sonnet-4-5";  // claude-sonnet-4-5-20250929
constexpr const char* kClaudeOpus41 =
    "claude-opus-4-1";  // claude-opus-4-1-20250805

/// Deprecated identifiers - scheduled for retirement on 2026-06-15.
/// Migrate to kClaudeSonnet46 / kClaudeOpus47 respectively.
constexpr const char* kClaudeSonnet4 =
    "claude-sonnet-4-0";  // claude-sonnet-4-20250514 (DEPRECATED)
constexpr const char* kClaudeOpus4 =
    "claude-opus-4-0";  // claude-opus-4-20250514 (DEPRECATED)

/// Default model used when none is specified
constexpr const char* kDefaultModel = kClaudeSonnet46;
}  // namespace models

/// Native Anthropic uses x-api-key. OpenCode Zen `/messages` uses Bearer
/// like the rest of zen/v1.
struct CompatibleOptions {
  std::string base_url = "https://api.anthropic.com";
  std::string completions_path;
  std::map<std::string, std::string> headers;
  bool bearer_auth = false;
  std::optional<retry::RetryConfig> retry_config;
};

/// Create an Anthropic client with default configuration
/// Reads API key from ANTHROPIC_API_KEY environment variable
/// @return Configured Anthropic client
Client create_client();

/// Create an Anthropic client with explicit API key
/// @param api_key Anthropic API key
/// @return Configured Anthropic client
Client create_client(const std::string& api_key);

/// Create an Anthropic client with custom configuration
/// @param api_key Anthropic API key
/// @param base_url Custom base URL (for Anthropic-compatible APIs)
/// @return Configured Anthropic client
Client create_client(const std::string& api_key, const std::string& base_url);

/// Create an Anthropic client with custom configuration and retry settings
/// @param api_key Anthropic API key
/// @param base_url Custom base URL (for Anthropic-compatible APIs)
/// @param retry_config Custom retry configuration
/// @return Configured Anthropic client
Client create_client(const std::string& api_key,
                     const std::string& base_url,
                     const retry::RetryConfig& retry_config);

Client create_client(const std::string& api_key,
                     const CompatibleOptions& options);

/// Try to create an Anthropic client using environment variables
/// Reads API key from ANTHROPIC_API_KEY environment variable
/// @return Optional client - has value if environment variable is set, empty
/// otherwise
/// @note This is useful for chaining creation attempts with other providers
std::optional<Client> try_create_client();

}  // namespace anthropic
}  // namespace qcode
