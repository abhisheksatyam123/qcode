#include "views_pickers.h"
#include <algorithm>
#include <chrono>
#include <ctime>

namespace qcode {
namespace tui {

using namespace ftxui;

ftxui::Element search_bar(const std::string& query, const std::string& theme) {
    return hbox({
        text(" > ") | bold | color(accent(theme)),
        query.empty()
            ? (text("Search...") | dim | color(theme_text_muted(theme)))
            : (text(query) | color(theme_text(theme))),
        filler(),
    });
}

// ── Theme selector popup ──
ftxui::Element build_theme_popup(
    const std::vector<ThemeEntry>& entries,
    int select_idx,
    const std::string& active_theme,
    const std::string& query,
    const std::string& theme
) {
    Elements lines;
    lines.push_back(text(" Select Theme") | bold | color(accent2(theme)));
    lines.push_back(separatorLight());
    lines.push_back(search_bar(query, theme));
    lines.push_back(separatorLight());

    if (entries.empty()) {
        lines.push_back(text("  (no matches)") | dim);
    } else {
        const int total = static_cast<int>(entries.size());
        select_idx = std::clamp(select_idx, 0, total - 1);

        constexpr int MAX_VISIBLE = 8;
        int window_start = 0;
        if (select_idx >= MAX_VISIBLE) {
            window_start = select_idx - MAX_VISIBLE + 1;
        }
        int window_end = std::min(total, window_start + MAX_VISIBLE);

        if (window_start > 0) {
            lines.push_back(text("  ▲ " + std::to_string(window_start) + " more themes above") | dim | color(accent(theme)));
        }

        for (int i = window_start; i < window_end; i++) {
            const auto& e = entries[i];
            bool active = (e.name == active_theme);
            std::string marker = (i == select_idx) ? " ▶ " : (active ? " ● " : "   ");

            auto swatch = hbox({
                text(" ■") | color(accent(e.name)),
                text("■ ") | color(accent2(e.name)),
            });

            auto row = hbox({
                text(marker) | color(i == select_idx ? accent2(theme) : (active ? accent(theme) : Color::Default)),
                swatch,
                text(e.name) | (i == select_idx ? bold : nothing) | color(i == select_idx ? Color::White : (active ? accent2(theme) : Color::GrayLight)),
                text("  (" + e.description + ")") | dim,
            });

            if (i == select_idx)
                row = row | bgcolor(bg_popup()) | bold;
            else if (active)
                row = row | color(accent2(theme));
            lines.push_back(row);
        }

        if (window_end < total) {
            lines.push_back(text("  ▼ " + std::to_string(total - window_end) + " more themes below") | dim | color(accent(theme)));
        }
    }

    lines.push_back(separatorLight());
    lines.push_back(text(" ↑↓ navigate  Enter select  Esc cancel") | dim);

    return vbox(std::move(lines)) | borderRounded | bgcolor(bg_popup()) | color(accent(theme)) |
           size(WIDTH, EQUAL, 56) | hcenter;
}

// ── Model selector popup with Capability & Telemetry Inspector ──
ftxui::Element build_model_popup(
    const std::vector<ModelEntry>& entries,
    int select_idx,
    int selected_provider,
    int selected_model,
    const std::string& query,
    const std::string& theme
) {
    Elements left_lines;
    left_lines.push_back(text(" Select Model") | bold | color(accent2(theme)));
    left_lines.push_back(separatorLight());

    left_lines.push_back(search_bar(query, theme));
    left_lines.push_back(separatorLight());

    Elements right_lines;
    right_lines.push_back(text(" Capability & Runtime Inspector") | bold | color(accent2(theme)));
    right_lines.push_back(separatorLight());

    if (entries.empty()) {
        left_lines.push_back(text("  (no matches)") | dim);
        right_lines.push_back(text("  No model selected") | dim);
    } else {
        const int total = static_cast<int>(entries.size());
        select_idx = std::clamp(select_idx, 0, total - 1);

        constexpr int MAX_VISIBLE = 8;
        int window_start = 0;
        if (select_idx >= MAX_VISIBLE) {
            window_start = select_idx - MAX_VISIBLE + 1;
        }
        int window_end = std::min(total, window_start + MAX_VISIBLE);

        if (window_start > 0) {
            left_lines.push_back(text("  ▲ " + std::to_string(window_start) + " more models above") | dim | color(accent(theme)));
        }

        std::string last_cat;
        for (int i = window_start; i < window_end; i++) {
            const auto& e = entries[i];
            if (e.category != last_cat) {
                if (!last_cat.empty() || i > window_start) left_lines.push_back(text(""));
                left_lines.push_back(text(" " + e.category) | bold | color(accent2(theme)));
                last_cat = e.category;
            }

            bool active = (e.provider_idx == selected_provider && e.model_idx == selected_model);
            std::string marker = (i == select_idx) ? " ▶ " : (active ? " ● " : "   ");
            std::string line_text = marker + e.model_name;

            auto line = text(line_text);
            if (i == select_idx)
                line = line | bgcolor(bg_popup()) | bold;
            else if (active)
                line = line | color(accent2(theme));
            left_lines.push_back(line);
        }

        if (window_end < total) {
            left_lines.push_back(text("  ▼ " + std::to_string(total - window_end) + " more models below") | dim | color(accent(theme)));
        }

        // ── Populate Right Inspector Panel for selected item ──
        const auto& sel_entry = entries[select_idx];
        auto perf = session::get_model_performance_summary(sel_entry.model_id, sel_entry.category);

        right_lines.push_back(hbox({ text(" Model: ") | bold | color(accent(theme)), text(sel_entry.model_name) | bold }));
        right_lines.push_back(hbox({ text(" Provider: ") | dim, text(sel_entry.category) }));
        if (!sel_entry.model_id.empty() && sel_entry.model_id != sel_entry.model_name) {
            right_lines.push_back(hbox({ text(" ID: ") | dim, text(sel_entry.model_id) | dim }));
        }
        if (!perf.architecture_info.empty()) {
            right_lines.push_back(hbox({ text(" Arch: ") | dim, text(perf.architecture_info) }));
        }

        right_lines.push_back(separatorLight());
        right_lines.push_back(text(" SPECIFICATIONS & BENCHMARKS") | bold | color(accent(theme)));
        
        std::string ctx_str = perf.context_window > 0 ? (std::to_string(perf.context_window / 1000) + "K tokens") : "Default";
        if (perf.context_window >= 1000000) ctx_str = std::to_string(perf.context_window / 1000000) + "M tokens";
        right_lines.push_back(hbox({ text(" Context Window: ") | dim, text(ctx_str) | color(Color::Green) }));

        std::string mt_str = perf.multi_turn_reliable ? "Reliable (Multi-turn)" : "Unstable (Single-turn)";
        right_lines.push_back(hbox({ text(" Tool Reliability: ") | dim, text(mt_str) | color(perf.multi_turn_reliable ? Color::Green : Color::Yellow) }));

        if (!perf.verified_benchmark.empty()) {
            right_lines.push_back(hbox({ text(" Verified Bench: ") | dim, text(perf.verified_benchmark) | bold }));
        }
        if (!perf.recommended_for.empty()) {
            right_lines.push_back(hbox({ text(" Recommended: ") | dim, text(perf.recommended_for) }));
        }

        right_lines.push_back(separatorLight());
        right_lines.push_back(text(" LOCAL RUNTIME TELEMETRY") | bold | color(accent(theme)));

        char rate_buf[32];
        snprintf(rate_buf, sizeof(rate_buf), "%.1f%%", perf.success_rate());
        std::string succ_str = std::string(rate_buf) + " (" + std::to_string(perf.successful_turns) + "/" + std::to_string(perf.total_turns) + " turns)";
        right_lines.push_back(hbox({ text(" Success Rate: ") | dim, text(succ_str) | bold | color(perf.success_rate() > 80.0 ? Color::Green : Color::Red) }));

        if (perf.tool_loop_failures > 0) {
            right_lines.push_back(hbox({ text(" Loop Failures: ") | dim, text(std::to_string(perf.tool_loop_failures)) | color(Color::Red) }));
        }
        if (perf.avg_latency_ms > 0.0) {
            char lat_buf[32];
            snprintf(lat_buf, sizeof(lat_buf), "%.0f ms", perf.avg_latency_ms);
            right_lines.push_back(hbox({ text(" Avg Latency: ") | dim, text(lat_buf) }));
        }
        if (perf.positive_feedback_count > 0 || perf.negative_feedback_count > 0) {
            std::string fb_str = "👍 " + std::to_string(perf.positive_feedback_count) + "   👎 " + std::to_string(perf.negative_feedback_count);
            right_lines.push_back(hbox({ text(" User Ratings: ") | dim, text(fb_str) }));
        }

        if (perf.total_turns >= 3 && perf.success_rate() < 70.0) {
            right_lines.push_back(separatorLight());
            right_lines.push_back(text(" ⚠️ High failure rate recorded locally!") | color(Color::Red) | bold);
        }
    }

    left_lines.push_back(separatorLight());
    left_lines.push_back(text(" ↑↓ navigate  Enter select  Esc cancel") | dim);

    auto left_box = vbox(std::move(left_lines)) | size(WIDTH, EQUAL, 34);
    auto right_box = vbox(std::move(right_lines)) | size(WIDTH, EQUAL, 40);

    return hbox({ left_box, separatorLight(), right_box }) | borderRounded | bgcolor(bg_popup()) | color(accent(theme)) | hcenter;
}

static std::string format_relative_time(long long ts) {
    if (ts <= 0) return "";
    auto now = std::chrono::system_clock::now();
    long long now_sec = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
    long long diff = now_sec - ts;
    if (diff < 0) diff = 0;
    if (diff < 60) return "just now";
    if (diff < 3600) return std::to_string(diff / 60) + "m ago";
    if (diff < 86400) return std::to_string(diff / 3600) + "h ago";
    if (diff < 86400 * 7) return std::to_string(diff / 86400) + "d ago";
    std::time_t t = static_cast<std::time_t>(ts);
    std::tm tm{};
    localtime_r(&t, &tm);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%b %d", &tm);
    return buf;
}

// ── Session selector popup ──
ftxui::Element build_session_popup(
    const std::vector<session::SessionInfo>& entries,
    int select_idx,
    const std::string& active_session_id,
    const std::string& query,
    const std::string& theme
) {
    Elements lines;
    lines.push_back(text(" Select Session") | bold | color(accent2(theme)));
    lines.push_back(separatorLight());
    lines.push_back(search_bar(query, theme));
    lines.push_back(separatorLight());

    if (entries.empty()) {
        lines.push_back(text("  (no matches)") | dim);
    } else {
        const int total = static_cast<int>(entries.size());
        select_idx = std::clamp(select_idx, 0, total - 1);

        constexpr int MAX_VISIBLE = 8;
        int window_start = 0;
        if (select_idx >= MAX_VISIBLE) {
            window_start = select_idx - MAX_VISIBLE + 1;
        }
        int window_end = std::min(total, window_start + MAX_VISIBLE);

        if (window_start > 0) {
            lines.push_back(text("  ▲ " + std::to_string(window_start) + " more sessions above") | dim | color(accent(theme)));
        }

        for (int i = window_start; i < window_end; i++) {
            const auto& e = entries[i];
            bool active = (e.id == active_session_id);
            std::string marker = (i == select_idx) ? " ▶ " : (active ? " ● " : "   ");
            
            std::string time_str = format_relative_time(e.last_active_at);
            std::string count_str = e.message_count > 0 ? (std::to_string(e.message_count) + " msgs") : "new";

            auto row = hbox({
                text(marker) | color(i == select_idx ? accent2(theme) : (active ? accent(theme) : Color::Default)),
                text(e.title) | (i == select_idx ? bold : nothing) | color(i == select_idx ? Color::White : (active ? accent2(theme) : Color::GrayLight)),
                text(!e.model.empty() ? (" [" + e.model + "]") : "") | dim | color(Color::CyanLight),
                text(!e.workspace.empty() ? (" " + e.workspace) : "") | dim | color(Color::GrayDark),
                filler(),
                text(" " + count_str + " ") | dim | color(Color::GrayLight),
                text(!time_str.empty() ? ("· " + time_str + " ") : " ") | dim | color(accent2(theme)),
            });

            if (i == select_idx)
                row = row | bgcolor(bg_popup()) | bold;
            else if (active)
                row = row | color(accent2(theme));
            lines.push_back(row);
        }

        if (window_end < total) {
            lines.push_back(text("  ▼ " + std::to_string(total - window_end) + " more sessions below") | dim | color(accent(theme)));
        }
    }

    lines.push_back(separatorLight());
    lines.push_back(text(" ↑↓ navigate  Enter select  Ctrl-D delete  Esc cancel") | dim);

    return vbox(std::move(lines)) | borderRounded | bgcolor(bg_popup()) | color(accent(theme)) |
           size(WIDTH, EQUAL, 60) | hcenter;
}

ftxui::Element build_variant_popup(
    const std::vector<VariantEntry>& entries,
    int select_idx,
    const std::string& active_variant,
    const std::string& query,
    const std::string& theme
) {
    Elements lines;
    lines.push_back(text(" Select Variant") | bold | color(accent2(theme)));
    lines.push_back(separatorLight());
    lines.push_back(search_bar(query, theme));
    lines.push_back(separatorLight());

    if (entries.empty()) {
        lines.push_back(text("  (no matches)") | dim);
    } else {
        const int total = static_cast<int>(entries.size());
        select_idx = std::clamp(select_idx, 0, total - 1);
        for (int i = 0; i < total; ++i) {
            const auto& e = entries[i];
            const bool active = (e.id == active_variant);
            const std::string marker =
                (i == select_idx) ? " ▶ " : (active ? " ● " : "   ");
            auto row = hbox({
                text(marker) | color(i == select_idx ? accent2(theme)
                                : (active ? accent(theme) : Color::Default)),
                text(e.title) |
                    (i == select_idx ? bold : nothing) |
                    color(i == select_idx ? Color::White
                          : (active ? accent2(theme) : Color::GrayLight)),
                text("  " + e.description) | dim,
            });
            if (i == select_idx)
                row = row | bgcolor(bg_popup()) | bold;
            else if (active)
                row = row | color(accent2(theme));
            lines.push_back(row);
        }
    }

    lines.push_back(separatorLight());
    lines.push_back(text(" ↑↓ navigate  Enter select  Esc cancel") | dim);
    return vbox(std::move(lines)) | borderRounded | bgcolor(bg_popup()) |
           color(accent(theme)) | size(WIDTH, EQUAL, 56) | hcenter;
}

ftxui::Element build_palette_popup(
    const std::vector<PaletteCommand>& entries,
    int select_idx,
    const std::string& query,
    const std::string& theme
) {
    Elements lines;
    lines.push_back(hbox({
        text(" Command Palette") | bold | color(accent2(theme)),
        filler(),
        text("Ctrl+P") | dim | color(theme_muted(theme)),
        text(" "),
    }));
    lines.push_back(separatorLight());

    // Search bar
    lines.push_back(hbox({
        text(" > ") | bold | color(accent(theme)),
        text(query.empty() ? "" : query) | color(Color::White),
        query.empty() ? (text("Type a command or shortcut...") | dim | color(Color::GrayDark)) : emptyElement(),
        filler(),
    }));
    lines.push_back(separatorLight());

    if (entries.empty()) {
        lines.push_back(text("  (no commands match)") | dim);
    } else {
        const int total = static_cast<int>(entries.size());
        select_idx = std::clamp(select_idx, 0, total - 1);

        constexpr int MAX_VISIBLE = 9;
        int window_start = 0;
        if (select_idx >= MAX_VISIBLE) {
            window_start = select_idx - MAX_VISIBLE + 1;
        }
        int window_end = std::min(total, window_start + MAX_VISIBLE);

        if (window_start > 0) {
            lines.push_back(text("  ▲ " + std::to_string(window_start) + " more commands above") | dim | color(accent(theme)));
        }

        std::string last_cat;
        for (int i = window_start; i < window_end; ++i) {
            const auto& e = entries[i];
            if (e.category != last_cat) {
                if (!last_cat.empty() || i > window_start) {
                    lines.push_back(text(""));
                }
                lines.push_back(text(" " + e.category) | bold |
                                color(accent2(theme)));
                last_cat = e.category;
            }
            const bool active = (i == select_idx);
            const std::string marker = active ? " ▶ " : "   ";

            auto row = hbox({
                text(marker) | color(active ? accent2(theme) : Color::Default),
                text(e.title) | (active ? bold : nothing) |
                    color(active ? Color::White : Color::GrayLight),
                text("  " + e.description) | dim,
                filler(),
                e.shortcut.empty() ? emptyElement() : (text(" " + e.shortcut + " ") | dim | color(accent2(theme))),
            });

            if (active) {
                row = row | bgcolor(bg_popup()) | bold;
            }
            lines.push_back(row);
        }

        if (window_end < total) {
            lines.push_back(text("  ▼ " + std::to_string(total - window_end) + " more commands below") | dim | color(accent(theme)));
        }
    }

    lines.push_back(separatorLight());
    lines.push_back(text(" ↑↓ navigate  Enter execute  Esc cancel") | dim);
    return vbox(std::move(lines)) | borderRounded | bgcolor(bg_popup()) |
           color(accent(theme)) | size(WIDTH, EQUAL, 64) | hcenter;
}

ftxui::Element build_help_popup(const std::string& theme) {
    auto key = [&](const std::string& k, const std::string& label) {
        return hbox({
            text("  " + k) | bold | color(theme_text(theme)) |
                size(WIDTH, EQUAL, 16),
            text(label) | dim | color(theme_text_muted(theme)),
        });
    };
    Elements lines;
    lines.push_back(hbox({
        text(" Help") | bold | color(theme_text(theme)),
        filler(),
        text("esc/enter") | dim | color(theme_text_muted(theme)),
        text(" "),
    }));
    lines.push_back(separatorLight());
    lines.push_back(text(" Press ctrl+p to see every command.") |
                    dim | color(theme_text_muted(theme)));
    lines.push_back(text(""));
    lines.push_back(text(" Session") | bold | color(accent2(theme)));
    lines.push_back(key("ctrl+n", "New session"));
    lines.push_back(key("/session", "Switch session"));
    lines.push_back(key("/compact", "Summarize context"));
    lines.push_back(text(""));
    lines.push_back(text(" Model") | bold | color(accent2(theme)));
    lines.push_back(key("/model", "Switch model"));
    lines.push_back(key("/variant", "Reasoning effort"));
    lines.push_back(key("tab", "Build / Plan agent"));
    lines.push_back(text(""));
    lines.push_back(text(" View") | bold | color(accent2(theme)));
    lines.push_back(key("esc", "Stop generation"));
    lines.push_back(key("r", "Retry last prompt"));
    lines.push_back(key("ctrl+t", "Toggle thinking"));
    lines.push_back(key("f3", "Copy mode"));
    lines.push_back(key("alt+2 / alt+3", "Files / Stats"));
    lines.push_back(separatorLight());
    lines.push_back(text(" enter close") | dim | color(theme_text_muted(theme)));
    return vbox(std::move(lines)) | borderRounded | bgcolor(bg_popup()) |
           color(theme_border(theme)) | size(WIDTH, EQUAL, 52) | hcenter;
}

}  // namespace tui
}  // namespace qcode
