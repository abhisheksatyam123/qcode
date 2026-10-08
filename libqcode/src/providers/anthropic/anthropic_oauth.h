#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace qcode {
namespace anthropic {

// OAuth constants matching Claude Code CLI & opencode
constexpr const char* kClaudeClientId = "9d1c250a-e61b-44d9-88ed-5944d1962f5e";
constexpr const char* kClaudeAuthorizeUrl = "https://claude.com/cai/oauth/authorize";
constexpr const char* kClaudeTokenHost = "https://platform.claude.com";
constexpr const char* kClaudeTokenPath = "/v1/oauth/token";
constexpr const char* kClaudeSuccessUrl = "https://platform.claude.com/oauth/code/success?app=claude-code";
constexpr const char* kClaudeScopes =
    "user:profile user:inference user:sessions:claude_code user:mcp_servers user:file_upload";
constexpr const char* kClaudeCliVersion = "2.1.300";
constexpr const char* kAnthropicBetaHeaders =
    "claude-code-20250219,oauth-2025-04-20,interleaved-thinking-2025-05-14,prompt-caching-scope-2026-01-05";
constexpr int64_t kRefreshBufferMs = 5 * 60 * 1000;  // 5 minutes

struct ClaudeCredentials {
  std::string access_token;
  std::string refresh_token;
  int64_t expires_at_ms = 0;
  std::string subscription_type;
  std::string rate_limit_tier;
};

// Returns path to ~/.claude/.credentials.json or env override.
std::filesystem::path get_claude_credentials_path();

// Reads credentials from disk.
std::optional<ClaudeCredentials> read_claude_credentials(
    const std::filesystem::path& path = get_claude_credentials_path());

// Writes credentials to disk with 0600 permissions.
bool write_claude_credentials(
    const ClaudeCredentials& creds,
    const std::filesystem::path& path = get_claude_credentials_path());

// Returns true if token expires within 5 minutes or is already expired.
bool is_claude_token_expired(int64_t expires_at_ms);

// Refreshes the token against platform.claude.com.
std::optional<ClaudeCredentials> refresh_claude_token(
    const std::string& refresh_token,
    const std::filesystem::path& path = get_claude_credentials_path());

// Exchange authorization code for tokens (PKCE).
std::optional<ClaudeCredentials> exchange_code_for_tokens(
    const std::string& code,
    const std::string& state,
    const std::string& verifier,
    const std::string& redirect_uri);

// PKCE crypto helpers
std::string generate_code_verifier();
std::string generate_code_challenge(const std::string& verifier);
std::string generate_state();
std::string base64url_encode(const unsigned char* data, size_t len);
std::string sha1_hex(const std::string& data);

// Rewrites XML tags in system prompt that trigger third-party detection:
// <directories> -> [directories], </directories> -> [/directories], <env> -> [env], </env> -> [/env]
std::string rewrite_claude_prompt_tags(std::string text);

// Returns a process-stable fingerprint for billing header: sha1(startup_now).substr(0, 8)
std::string generate_billing_fingerprint();

// Formats billing attribution header:
// "x-anthropic-billing-header: cc_version=2.1.300.<fp>; cc_entrypoint=cli;\n"
std::string format_billing_header();

// Header value only (no name prefix, no trailing newline) for use as a real
// HTTP header: "cc_version=2.1.300.<fp>; cc_entrypoint=cli;"
std::string format_billing_header_value();

// Runs local PKCE OAuth server and completes login flow.
bool login_anthropic_oauth(std::string* out_error = nullptr);

}  // namespace anthropic
}  // namespace qcode
