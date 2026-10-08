#pragma once

#include <string>
#include <vector>

#include <qcode/config/provider_info.h>
#include <qcode/core/concepts.h>

namespace qcode {

// Retrieve antigravity API token from env var or keyring.
// When force_refresh is true, always attempt an OAuth refresh (used after 401).
[[nodiscard]] std::string get_antigravity_token(bool force_refresh = false);

// True when the on-disk Antigravity access token is missing or past expiry.
[[nodiscard]] bool antigravity_token_needs_refresh();

// Retrieve Cursor access token from env var or ~/.config/cursor/auth.json.
[[nodiscard]] std::string get_cursor_access_token();

// Retrieve Anthropic API token from env var or Claude Code credentials (~/.claude/.credentials.json).
// When force_refresh is true, always attempt an OAuth refresh (used after 401).
[[nodiscard]] std::string get_anthropic_token(bool force_refresh = false);

// True when Claude Code OAuth credentials are in use.
[[nodiscard]] bool is_anthropic_oauth();

// True when the on-disk Claude Code access token is missing or past expiry.
[[nodiscard]] bool anthropic_token_needs_refresh();

// Run interactive or loopback PKCE OAuth login against claude.com and save tokens to ~/.claude/.credentials.json.
bool login_anthropic_oauth(std::string* out_error = nullptr);

// The single config file: $OPENCODE_CONFIG, else
// $XDG_CONFIG_HOME/opencode/opencode.json, else ~/.config/opencode/opencode.json.
[[nodiscard]] std::string config_path();

// Resolve the notes root directory (OPENCODE_NOTES_ROOT / HOME env, else default).
[[nodiscard]] std::string get_notes_root();

// Load providers (and per-model context/cost/capabilities) from opencode.json.
[[nodiscard]] std::vector<ProviderInfo> load_providers_from_config();

} // namespace qcode
