#pragma once

#include <string>
#include <vector>

#include <ai/tui/state.h>

namespace ai {
namespace tui {

// Retrieve antigravity API token from env var or keyring.
std::string get_antigravity_token();

// Resolve the opencode.json config path (OPENCODE_CONFIG env, else default).
std::string config_path();

// Resolve the notes root directory (OPENCODE_NOTES_ROOT / HOME env, else default).
std::string get_notes_root();

// Load providers (and per-model context/cost/capabilities) from opencode.json.
std::vector<ProviderInfo> load_providers_from_config();

} // namespace tui
} // namespace ai
