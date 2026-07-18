#pragma once

#include <string>

namespace qcode {
namespace tui {
namespace contract {

// ── Module Identity ───────────────────────────────────────────────────────────
// Every module in the system declares who it is, what layer it occupies,
// and what contract version it speaks.

struct ModuleIdentity {
    std::string module;        // e.g. "tui"
    std::string layer;         // e.g. "interface"
    std::string tier;          // e.g. "L5" (following OpenCode layering)
    std::string contract_version; // semver
};

// TUI surface identity (L5 — topmost interface layer)
inline constexpr ModuleIdentity kTuiIdentity = {
    .module = "tui",
    .layer = "interface",
    .tier = "L5",
    .contract_version = "1.0.0"
};

// Backend identity (L3 — business logic layer)
inline constexpr ModuleIdentity kBackendIdentity = {
    .module = "backend",
    .layer = "business",
    .tier = "L3",
    .contract_version = "1.0.0"
};

} // namespace contract
} // namespace tui
} // namespace qcode
