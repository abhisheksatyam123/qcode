#pragma once

#include <string>
#include <vector>

#include <qcode/state/state.h>

namespace ai {
namespace tui {

// Retrieve antigravity API token from env var or keyring.
// When force_refresh is true, always attempt an OAuth refresh (used after 401).
std::string get_antigravity_token(bool force_refresh = false);

// True when the on-disk Antigravity access token is missing or past expiry.
bool antigravity_token_needs_refresh();

// Retrieve Cursor access token from env var or ~/.config/cursor/auth.json.
std::string get_cursor_access_token();

// Resolve the opencode.json config path (OPENCODE_CONFIG env, else default).
std::string config_path();

// Resolve the notes root directory (OPENCODE_NOTES_ROOT / HOME env, else default).
std::string get_notes_root();

// Load providers (and per-model context/cost/capabilities) from opencode.json.
std::vector<ProviderInfo> load_providers_from_config();

} // namespace tui
} // namespace ai
