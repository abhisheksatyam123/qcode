#pragma once
#include <string>

namespace qcode {

// ── Tool icon lookup ──
// Returns a Unicode icon for the given tool name
std::string tool_icon(const std::string& tool_name);

// ── Tool display name ──
// Returns a human-friendly display name
std::string tool_display_name(const std::string& tool_name);

} // namespace qcode
