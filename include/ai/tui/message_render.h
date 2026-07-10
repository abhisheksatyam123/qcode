#pragma once
#include <ftxui/dom/elements.hpp>
#include <ai/types/message.h>
#include <ai/tui/state.h>

namespace ai {
namespace tui {

// ── ToolBlock: OpenCode-style tool rendering block ──
// Renders a tool call/result as a styled block with:
//   - Icon + tool name + description + duration in header
//   - Accent-colored left border
//   - Status badge (running/success/failed)
//   - Content area with optional truncation
ftxui::Element ToolBlock(const std::string& icon,
                          const std::string& title,
                          const std::string& description,
                          ftxui::Element content,
                          bool is_running,
                          const std::string& status,
                          ftxui::Color accent_color,
                          double duration_ms = 0.0);

// ── Legacy BlockTool (compatibility) ──
ftxui::Element BlockTool(const std::string& title, ftxui::Element content,
                          bool is_running = false,
                          const std::string& status = "",
                          ftxui::Color border_color = ftxui::Color::GrayDark);

// ── Render a complete message (user/assistant/system) ──
ftxui::Element render_message(const ai::Message& msg,
                               const ChatState& state,
                               const std::vector<ProviderInfo>& providers_list,
                               int selected_provider, int selected_model,
                               const std::string& theme);

// ── Output truncation helper ──
// Splits output into lines, renders up to max_lines, adds "[+N more]" hint
ftxui::Element render_truncated_output(const std::string& output,
                                        int max_lines = 20,
                                        const std::string& theme = "orange");

} // namespace tui
} // namespace ai
