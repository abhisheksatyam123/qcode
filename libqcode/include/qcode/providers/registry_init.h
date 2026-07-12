#pragma once

namespace ai {
namespace providers {

// Register all providers the TUI can use, including the Antigravity resolver
// that needs the TUI-local token fetcher. Idempotent; safe to call repeatedly.
void register_tui_providers();

}  // namespace providers
}  // namespace ai
