#pragma once

#include <qcode/ui/commands.h>
#include <qcode/session/session_store.h>
#include <qcode/ui/themes.h>
#include <qcode/core/state.h>
#include <ftxui/dom/elements.hpp>
#include <string>
#include <vector>

namespace qcode {
namespace tui {

ftxui::Element build_model_popup(
    const std::vector<ModelEntry>& entries,
    int select_idx,
    int selected_provider,
    int selected_model,
    const std::string& query,
    const std::string& theme);

ftxui::Element build_session_popup(
    const std::vector<session::SessionInfo>& entries,
    int select_idx,
    const std::string& active_session_id,
    const std::string& query,
    const std::string& theme);

ftxui::Element build_theme_popup(
    const std::vector<ThemeEntry>& entries,
    int select_idx,
    const std::string& active_theme,
    const std::string& query,
    const std::string& theme);

}  // namespace tui
}  // namespace qcode
