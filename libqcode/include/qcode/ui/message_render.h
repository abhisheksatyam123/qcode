#pragma once
#include <ftxui/dom/elements.hpp>
#include <qcode/core/message.h>
#include <qcode/config/provider_info.h>
#include <qcode/ui/chat_state.h>

#include <span>

namespace qcode {

// OpenCode-inspired shell session block:
//   ▸ # description                              ✓ 4ms
//     $ find . -maxdepth 3
//     ./src
//     ./include
ftxui::Element ToolBlock(const std::string& icon,
                          const std::string& title,
                          const std::string& description,
                          ftxui::Element content,
                          bool is_running,
                          const std::string& status,
                          ftxui::Color accent_color,
                          double duration_ms = 0.0,
                          bool collapsed = false,
                          bool collapsible = true,
                          bool focused = false,
                          const std::string& shell_command = "",
                          const std::string& theme = "opencode",
                          ChatState* state = nullptr,
                          const std::string& tool_call_id = "",
                          const std::string& open_session_id = "");

// ── Legacy BlockTool (compatibility) ──
ftxui::Element BlockTool(const std::string& title, ftxui::Element content,
                          bool is_running = false,
                          const std::string& status = "",
                          ftxui::Color border_color = ftxui::Color::GrayDark);

// ── Render a complete message (user/assistant/system) ──
// message_index is the stable row in messages_history (views loop `i`); it
// keys the per-message Thought expand state so vector reallocs can't orphan
// the toggle. Defaults to -1 (address fallback) for unit tests.
// terminal_width is the width the caller caches renders under (<= 0: query
// the terminal); in_flight marks a message still streaming, whose markdown
// is not inserted into the markdown cache.
// A tool block is focused when its id is tool_block_order[focused_tool_index];
// the view owns building tool_block_order.
// paired_tool_results are later history rows holding results for this
// message's tool calls (paired by id; a parallel batch is not adjacent).
ftxui::Element render_message(const qcode::Message& msg,
                               const ChatState& state,
                               const std::vector<ProviderInfo>& providers_list,
                               int selected_provider, int selected_model,
                               const std::string& theme,
                               const qcode::Message* adjacent_tool_results = nullptr,
                               int message_index = -1,
                               int terminal_width = 0,
                               bool in_flight = false,
                               std::span<const qcode::Message* const>
                                   paired_tool_results = {});

// Colored, truncated shell-style stdout/stderr.
ftxui::Element render_truncated_output(const std::string& output,
                                        int max_lines = 20,
                                        const std::string& theme = "opencode",
                                        bool is_error = false);

} // namespace qcode
