#pragma once

#include <string>
#include <vector>

#include <ai/tui/state.h>

namespace ai {
namespace tui {

// Retrieve antigravity API token from env var or keyring.
std::string get_antigravity_token();

// Load providers from opencode.json config file.
std::vector<ProviderInfo> load_providers_from_config();

} // namespace tui
} // namespace ai
