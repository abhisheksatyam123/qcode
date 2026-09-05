#include "tui_overlays.h"
#include "picker_helpers.h"

#include <algorithm>

namespace qcode {
namespace tui {

bool TuiOverlayState::any(const ChatState& state) const {
    return show_model_select || show_session_select || show_theme_select ||
           show_variant_select || show_palette || show_help ||
           state.slash_suggestion_mode;
}

void TuiOverlayState::close_all(ChatState& state) {
    show_model_select = false;
    model_query.clear();
    show_session_select = false;
    session_query.clear();
    show_theme_select = false;
    theme_query.clear();
    show_variant_select = false;
    variant_query.clear();
    show_palette = false;
    palette_query.clear();
    show_help = false;
    state.slash_suggestion_mode = false;
    state.slash_suggestion_idx = 0;
}

std::vector<qcode::ModelEntry> filter_models(
    const std::vector<qcode::ModelEntry>& entries, const std::string& query) {
    std::vector<qcode::ModelEntry> out;
    for (const auto& e : entries) {
        if (matches_query(e.model_name, query) ||
            matches_query(e.model_id, query) ||
            matches_query(e.category, query)) {
            out.push_back(e);
        }
    }
    return out;
}

std::vector<qcode::session::SessionInfo> filter_sessions(
    const std::vector<qcode::session::SessionInfo>& entries, const std::string& query) {
    std::vector<qcode::session::SessionInfo> out;
    for (const auto& e : entries) {
        if (matches_query(e.title, query) ||
            matches_query(e.id, query) ||
            matches_query(e.workspace, query)) {
            out.push_back(e);
        }
    }
    return out;
}

std::vector<qcode::ThemeEntry> filter_themes(
    const std::vector<qcode::ThemeEntry>& entries, const std::string& query) {
    std::vector<qcode::ThemeEntry> out;
    for (const auto& e : entries) {
        if (matches_query(e.name, query) ||
            matches_query(e.description, query)) {
            out.push_back(e);
        }
    }
    return out;
}

std::vector<qcode::VariantEntry> filter_variants(
    const std::vector<qcode::VariantEntry>& entries, const std::string& query) {
    std::vector<qcode::VariantEntry> out;
    for (const auto& e : entries) {
        if (matches_query(e.id, query) ||
            matches_query(e.title, query) ||
            matches_query(e.description, query)) {
            out.push_back(e);
        }
    }
    return out;
}

std::vector<qcode::PaletteCommand> filter_palette(
    const std::vector<qcode::PaletteCommand>& entries, const std::string& query) {
    std::vector<qcode::PaletteCommand> out;
    for (const auto& e : entries) {
        if (matches_query(e.title, query) ||
            matches_query(e.id, query) ||
            matches_query(e.description, query) ||
            matches_query(e.category, query) ||
            matches_query(e.shortcut, query)) {
            out.push_back(e);
        }
    }
    return out;
}

bool handle_model_select_keys(
    const ftxui::Event& e,
    TuiOverlayState& overlays,
    std::function<void(const qcode::ModelEntry&)> on_select) {
    if (!overlays.show_model_select) return false;

    if (e == ftxui::Event::Escape) {
        overlays.show_model_select = false;
        overlays.model_query.clear();
        return true;
    }

    auto filtered = filter_models(overlays.model_entries, overlays.model_query);
    if (!filtered.empty()) {
        overlays.model_select_idx = std::clamp(
            overlays.model_select_idx, 0, static_cast<int>(filtered.size()) - 1);
    } else {
        overlays.model_select_idx = 0;
    }

    if (e == ftxui::Event::ArrowDown) {
        if (!filtered.empty()) {
            overlays.model_select_idx = std::min(
                overlays.model_select_idx + 1, static_cast<int>(filtered.size()) - 1);
        }
        return true;
    }
    if (e == ftxui::Event::ArrowUp) {
        if (!filtered.empty()) {
            overlays.model_select_idx = std::max(overlays.model_select_idx - 1, 0);
        }
        return true;
    }
    if (e == ftxui::Event::Return) {
        if (!filtered.empty() && overlays.model_select_idx >= 0 &&
            overlays.model_select_idx < static_cast<int>(filtered.size())) {
            on_select(filtered[overlays.model_select_idx]);
        }
        overlays.show_model_select = false;
        overlays.model_query.clear();
        return true;
    }
    if (e == ftxui::Event::Backspace || e == ftxui::Event::Special("\x7f")) {
        if (!overlays.model_query.empty()) {
            overlays.model_query.pop_back();
            overlays.model_select_idx = 0;
        }
        return true;
    }
    if (e.is_character()) {
        overlays.model_query += e.character();
        overlays.model_select_idx = 0;
        return true;
    }
    return true;
}

bool handle_session_select_keys(
    const ftxui::Event& e,
    TuiOverlayState& overlays,
    std::function<void(const qcode::session::SessionInfo&)> on_select,
    std::function<void(const std::string& session_id)> on_delete) {
    if (!overlays.show_session_select) return false;

    if (e == ftxui::Event::Escape) {
        overlays.show_session_select = false;
        overlays.session_query.clear();
        return true;
    }

    auto filtered = filter_sessions(overlays.session_entries, overlays.session_query);
    if (!filtered.empty()) {
        overlays.session_select_idx = std::clamp(
            overlays.session_select_idx, 0, static_cast<int>(filtered.size()) - 1);
    } else {
        overlays.session_select_idx = 0;
    }

    if (e == ftxui::Event::ArrowDown) {
        if (!filtered.empty()) {
            overlays.session_select_idx = std::min(
                overlays.session_select_idx + 1, static_cast<int>(filtered.size()) - 1);
        }
        return true;
    }
    if (e == ftxui::Event::ArrowUp) {
        if (!filtered.empty()) {
            overlays.session_select_idx = std::max(overlays.session_select_idx - 1, 0);
        }
        return true;
    }
    if (e == ftxui::Event::Return) {
        if (!filtered.empty() && overlays.session_select_idx >= 0 &&
            overlays.session_select_idx < static_cast<int>(filtered.size())) {
            on_select(filtered[overlays.session_select_idx]);
        }
        overlays.show_session_select = false;
        overlays.session_query.clear();
        return true;
    }
    if (e == ftxui::Event::Special(std::string(1, '\x04'))) {  // Ctrl-D
        if (!filtered.empty() && overlays.session_select_idx >= 0 &&
            overlays.session_select_idx < static_cast<int>(filtered.size())) {
            std::string sid = filtered[overlays.session_select_idx].id;
            on_delete(sid);
            overlays.session_entries = qcode::session::list_sessions_full();
            filtered = filter_sessions(overlays.session_entries, overlays.session_query);
            if (!filtered.empty()) {
                overlays.session_select_idx = std::clamp(
                    overlays.session_select_idx, 0, static_cast<int>(filtered.size()) - 1);
            } else {
                overlays.session_select_idx = 0;
            }
        }
        return true;
    }
    if (e == ftxui::Event::Backspace || e == ftxui::Event::Special("\x7f")) {
        if (!overlays.session_query.empty()) {
            overlays.session_query.pop_back();
            overlays.session_select_idx = 0;
        }
        return true;
    }
    if (e.is_character()) {
        overlays.session_query += e.character();
        overlays.session_select_idx = 0;
        return true;
    }
    return true;
}

bool handle_theme_select_keys(
    const ftxui::Event& e,
    TuiOverlayState& overlays,
    std::function<void(const std::string& theme_name)> on_select) {
    if (!overlays.show_theme_select) return false;

    if (e == ftxui::Event::Escape) {
        overlays.show_theme_select = false;
        overlays.theme_query.clear();
        return true;
    }

    auto filtered = filter_themes(overlays.theme_entries, overlays.theme_query);
    if (!filtered.empty()) {
        overlays.theme_select_idx = std::clamp(
            overlays.theme_select_idx, 0, static_cast<int>(filtered.size()) - 1);
    } else {
        overlays.theme_select_idx = 0;
    }

    if (e == ftxui::Event::ArrowDown) {
        if (!filtered.empty()) {
            overlays.theme_select_idx = std::min(
                overlays.theme_select_idx + 1, static_cast<int>(filtered.size()) - 1);
        }
        return true;
    }
    if (e == ftxui::Event::ArrowUp) {
        if (!filtered.empty()) {
            overlays.theme_select_idx = std::max(overlays.theme_select_idx - 1, 0);
        }
        return true;
    }
    if (e == ftxui::Event::Return) {
        if (!filtered.empty() && overlays.theme_select_idx >= 0 &&
            overlays.theme_select_idx < static_cast<int>(filtered.size())) {
            on_select(filtered[overlays.theme_select_idx].name);
        }
        overlays.show_theme_select = false;
        overlays.theme_query.clear();
        return true;
    }
    if (e == ftxui::Event::Backspace || e == ftxui::Event::Special("\x7f")) {
        if (!overlays.theme_query.empty()) {
            overlays.theme_query.pop_back();
            overlays.theme_select_idx = 0;
        }
        return true;
    }
    if (e.is_character()) {
        overlays.theme_query += e.character();
        overlays.theme_select_idx = 0;
        return true;
    }
    return true;
}

bool handle_variant_select_keys(
    const ftxui::Event& e,
    TuiOverlayState& overlays,
    std::function<void(const std::string& variant_id)> on_select) {
    if (!overlays.show_variant_select) return false;

    auto filtered = filter_variants(overlays.variant_entries, overlays.variant_query);
    if (!filtered.empty()) {
        overlays.variant_select_idx = std::clamp(
            overlays.variant_select_idx, 0, static_cast<int>(filtered.size()) - 1);
    } else {
        overlays.variant_select_idx = 0;
    }

    const bool down = e == ftxui::Event::ArrowDown || e == ftxui::Event::Character('j');
    const bool up = e == ftxui::Event::ArrowUp || e == ftxui::Event::Character('k');
    if (down && !filtered.empty()) {
        overlays.variant_select_idx = std::min(
            overlays.variant_select_idx + 1, static_cast<int>(filtered.size()) - 1);
        return true;
    }
    if (up && !filtered.empty()) {
        overlays.variant_select_idx = std::max(overlays.variant_select_idx - 1, 0);
        return true;
    }
    if (e == ftxui::Event::Return) {
        if (!filtered.empty() && overlays.variant_select_idx >= 0 &&
            overlays.variant_select_idx < static_cast<int>(filtered.size())) {
            on_select(filtered[overlays.variant_select_idx].id);
        }
        overlays.show_variant_select = false;
        overlays.variant_query.clear();
        return true;
    }
    if (e == ftxui::Event::Escape) {
        overlays.show_variant_select = false;
        overlays.variant_query.clear();
        return true;
    }
    if (e == ftxui::Event::Backspace || e == ftxui::Event::Special("\x7f")) {
        if (!overlays.variant_query.empty()) {
            overlays.variant_query.pop_back();
            overlays.variant_select_idx = 0;
        }
        return true;
    }
    if (e.is_character() && e != ftxui::Event::Character('j') &&
        e != ftxui::Event::Character('k')) {
        overlays.variant_query += e.character();
        overlays.variant_select_idx = 0;
        return true;
    }
    return true;
}

bool handle_palette_keys(
    const ftxui::Event& e,
    TuiOverlayState& overlays,
    std::function<void(const qcode::PaletteCommand&)> on_select) {
    if (!overlays.show_palette) return false;

    if (e == ftxui::Event::Escape) {
        overlays.show_palette = false;
        overlays.palette_query.clear();
        return true;
    }

    auto filtered = filter_palette(overlays.palette_commands, overlays.palette_query);
    if (!filtered.empty()) {
        overlays.palette_select_idx = std::clamp(
            overlays.palette_select_idx, 0, static_cast<int>(filtered.size()) - 1);
    } else {
        overlays.palette_select_idx = 0;
    }

    if (e == ftxui::Event::ArrowDown) {
        if (!filtered.empty()) {
            overlays.palette_select_idx = std::min(
                overlays.palette_select_idx + 1, static_cast<int>(filtered.size()) - 1);
        }
        return true;
    }
    if (e == ftxui::Event::ArrowUp) {
        if (!filtered.empty()) {
            overlays.palette_select_idx = std::max(overlays.palette_select_idx - 1, 0);
        }
        return true;
    }
    if (e == ftxui::Event::Return) {
        if (!filtered.empty() && overlays.palette_select_idx >= 0 &&
            overlays.palette_select_idx < static_cast<int>(filtered.size())) {
            auto cmd = filtered[overlays.palette_select_idx];
            overlays.show_palette = false;
            overlays.palette_query.clear();
            on_select(cmd);
        } else {
            overlays.show_palette = false;
            overlays.palette_query.clear();
        }
        return true;
    }
    if (e == ftxui::Event::Backspace || e == ftxui::Event::Special("\x7f")) {
        if (!overlays.palette_query.empty()) {
            overlays.palette_query.pop_back();
            overlays.palette_select_idx = 0;
        }
        return true;
    }
    if (e.is_character()) {
        overlays.palette_query += e.character();
        overlays.palette_select_idx = 0;
        return true;
    }
    return true;
}

}  // namespace tui
}  // namespace qcode
