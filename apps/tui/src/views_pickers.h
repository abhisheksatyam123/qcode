#pragma once

#include <qcode/ui/commands.h>
#include <qcode/session/session_store.h>
#include <qcode/ui/themes.h>
#include <qcode/config/provider_info.h>
#include <qcode/ui/chat_state.h>
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

std::string format_relative_time(long long ts);

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

ftxui::Element build_variant_popup(
    const std::vector<VariantEntry>& entries,
    int select_idx,
    const std::string& active_variant,
    const std::string& query,
    const std::string& theme);

ftxui::Element build_palette_popup(
    const std::vector<PaletteCommand>& entries,
    int select_idx,
    const std::string& query,
    const std::string& theme);

ftxui::Element build_help_popup(const std::string& theme);

}  // namespace tui
}  // namespace qcode
