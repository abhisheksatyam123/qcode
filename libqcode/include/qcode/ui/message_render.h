#pragma once
#include <ftxui/dom/elements.hpp>
#include <qcode/core/message.h>
#include <qcode/core/state.h>

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
                          const std::string& theme = "orange",
                          ChatState* state = nullptr,
                          const std::string& tool_call_id = "");

// ── Legacy BlockTool (compatibility) ──
ftxui::Element BlockTool(const std::string& title, ftxui::Element content,
                          bool is_running = false,
                          const std::string& status = "",
                          ftxui::Color border_color = ftxui::Color::GrayDark);

// ── Render a complete message (user/assistant/system) ──
ftxui::Element render_message(const qcode::Message& msg,
                               const ChatState& state,
                               const std::vector<ProviderInfo>& providers_list,
                               int selected_provider, int selected_model,
                               const std::string& theme,
                               const qcode::Message* adjacent_tool_results = nullptr);

// Colored, truncated shell-style stdout/stderr.
ftxui::Element render_truncated_output(const std::string& output,
                                        int max_lines = 20,
                                        const std::string& theme = "orange",
                                        bool is_error = false);

} // namespace qcode
