#pragma once

#include <qcode/ui/commands.h>
#include <qcode/session/session_store.h>
#include <qcode/ui/markdown.h>  // render_markdown
#include <qcode/ui/themes.h>
#include <qcode/core/state.h>
#include <qcode/ui/app_store.h>
#include <ftxui/component/component.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>
#include <string>
#include <utility>
#include <vector>

namespace qcode {
namespace tui {

// Debounced terminal size. Transient PTY size fluctuations (common during
// heavy I/O such as long bash tool runs with streaming "working" messages)
// would otherwise flip FTXUI's resize detection and the chat render cache key
// every frame, forcing full-screen clear+repaint (visible flicker). This
// commits to a new size only after it is observed stable for a few frames.
ftxui::Dimensions stable_terminal_size();

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
    const std::vector<session::SessionInfo>& session_entries,
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

}  // namespace tui
}  // namespace qcode
