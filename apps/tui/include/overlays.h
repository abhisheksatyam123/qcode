#pragma once

#include <ftxui/component/event.hpp>
#include <qcode/config/provider_info.h>
#include <qcode/session/session_store.h>
#include <qcode/ui/chat_state.h>
#include <qcode/ui/commands.h>
#include <qcode/ui/themes.h>

#include <functional>
#include <string>
#include <vector>

namespace qcode {
namespace tui {

struct TuiOverlayState {
    bool show_model_select = false;
    int model_select_idx = 0;
    std::string model_query = "";
    std::vector<qcode::ModelEntry> model_entries;

    bool show_session_select = false;
    int session_select_idx = 0;
    std::string session_query = "";
    std::vector<qcode::session::SessionInfo> session_entries;

    bool show_theme_select = false;
    int theme_select_idx = 0;
    std::string theme_query = "";
    std::vector<qcode::ThemeEntry> theme_entries;

    bool show_variant_select = false;
    int variant_select_idx = 0;
    std::string variant_query = "";
    std::vector<qcode::VariantEntry> variant_entries;

    bool show_palette = false;
    int palette_select_idx = 0;
    std::string palette_query = "";
    std::vector<qcode::PaletteCommand> palette_commands;
    bool show_help = false;

    bool show_slash = false;
    int slash_idx = 0;
    std::vector<qcode::SlashCommand> slash_commands;

    bool any(const ChatState& state) const;
    void close_all(ChatState& state);
};

std::vector<qcode::ModelEntry> filter_models(
    const std::vector<qcode::ModelEntry>& entries, const std::string& query);

std::vector<qcode::session::SessionInfo> filter_sessions(
    const std::vector<qcode::session::SessionInfo>& entries, const std::string& query);

std::vector<qcode::ThemeEntry> filter_themes(
    const std::vector<qcode::ThemeEntry>& entries, const std::string& query);

std::vector<qcode::VariantEntry> filter_variants(
    const std::vector<qcode::VariantEntry>& entries, const std::string& query);

std::vector<qcode::PaletteCommand> filter_palette(
    const std::vector<qcode::PaletteCommand>& entries, const std::string& query);

bool handle_model_select_keys(
    const ftxui::Event& e,
    TuiOverlayState& overlays,
    std::function<void(const qcode::ModelEntry&)> on_select);

bool handle_session_select_keys(
    const ftxui::Event& e,
    TuiOverlayState& overlays,
    std::function<void(const qcode::session::SessionInfo&)> on_select,
    std::function<void(const std::string& session_id)> on_delete);

bool handle_theme_select_keys(
    const ftxui::Event& e,
    TuiOverlayState& overlays,
    std::function<void(const std::string& theme_name)> on_select);

bool handle_variant_select_keys(
    const ftxui::Event& e,
    TuiOverlayState& overlays,
    std::function<void(const std::string& variant_id)> on_select);

bool handle_palette_keys(
    const ftxui::Event& e,
    TuiOverlayState& overlays,
    std::function<void(const qcode::PaletteCommand&)> on_select);

}  // namespace tui
}  // namespace qcode
