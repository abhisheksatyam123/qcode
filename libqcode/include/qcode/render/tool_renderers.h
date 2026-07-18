#pragma once
#include <ftxui/dom/elements.hpp>
#include <nlohmann/json.hpp>
#include <string>

namespace qcode {
namespace tui {

// ── Tool icon lookup ──
// Returns a Unicode icon for the given tool name
std::string tool_icon(const std::string& tool_name);

// ── Tool display name ──
// Returns a human-friendly display name
std::string tool_display_name(const std::string& tool_name);

// ── Per-tool result renderers ──
// Each returns the inner content element for a tool result block.
// The caller wraps it in ToolBlock().

ftxui::Element RenderBashResult(const nlohmann::json& args,
                                 const nlohmann::json& result,
                                 bool is_error, double duration_ms,
                                 const std::string& theme);

ftxui::Element RenderTaskResult(const nlohmann::json& args,
                                 const nlohmann::json& result,
                                 bool is_error, double duration_ms,
                                 const std::string& theme);

ftxui::Element RenderFileResult(const std::string& tool_name,
                                 const nlohmann::json& args,
                                 const nlohmann::json& result,
                                 bool is_error, double duration_ms,
                                 const std::string& theme);

ftxui::Element RenderSearchResult(const nlohmann::json& args,
                                   const nlohmann::json& result,
                                   bool is_error, double duration_ms,
                                   const std::string& theme);

ftxui::Element RenderGenericResult(const std::string& tool_name,
                                    const nlohmann::json& args,
                                    const nlohmann::json& result,
                                    bool is_error, double duration_ms,
                                    const std::string& theme);

// ── Legacy API (kept for compatibility) ──
ftxui::Element BashToolRender(const std::string& command,
                               const std::string& output, int exit_code,
                               bool is_running,
                               const std::string& workdir = "");

} // namespace tui
} // namespace qcode
