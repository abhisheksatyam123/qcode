#pragma once

#include <ai/tui/db.h>
#include <ai/tui/markdown.h>  // render_markdown
#include <ai/tui/themes.h>
#include <ftxui/component/component.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>
#include <string>
#include <vector>
#include <utility>
#include <ai/tui/commands.h>
#include <ai/tui/state.h>
#include <ai/tui/store.h>

namespace ai {
namespace tui {

ftxui::Element render_logo();

ftxui::Element render_view(
    const ChatState& state,
    const std::vector<ProviderInfo>& providers_list,
    int selected_provider,
    int selected_model,
    bool enable_tools,
    const std::string& prompt_input,
    bool show_slash,
    int slash_idx,
    const std::vector<SlashCommand>& slash_commands,
    bool show_model_select,
    int model_select_idx,
    const std::vector<ModelEntry>& model_entries,
    const std::string& model_query,
    bool show_session_select,
    int session_select_idx,
    const std::vector<db::SessionInfo>& session_entries,
    const std::string& session_query,
    bool show_theme_select,
    int theme_select_idx,
    const std::vector<ThemeEntry>& theme_entries,
    const std::string& theme_query,
    const ftxui::Component& tab_toggle,
    const std::shared_ptr<int>& scroll_line,
    const ftxui::Component& input
);

// Toast overlay
ftxui::Element render_toast_overlay(
    const std::vector<Toast>& toasts,
    const std::string& theme
);

// Dynamic footer with model, token, session info
ftxui::Element render_dynamic_footer(
    const ChatState& state,
    const std::vector<ProviderInfo>& providers_list,
    int selected_provider,
    int selected_model,
    const std::string& status
);

}  // namespace tui
}  // namespace ai
