#pragma once

namespace ai {
namespace providers {

// Register core providers plus auth-backed ones (Antigravity, Cursor)
// that need local token helpers. Used by TUI and server. Idempotent.
void register_authenticated_providers();

}  // namespace providers
}  // namespace ai
