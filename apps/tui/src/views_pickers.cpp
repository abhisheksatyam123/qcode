#include "views_pickers.h"
#include <algorithm>

namespace qcode {
namespace tui {

using namespace ftxui;

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

    // Search bar
    lines.push_back(hbox({
        text(" Find: ") | bold | color(accent(theme)),
        text(query) | color(Color::White)
    }));
    lines.push_back(separatorLight());

    if (entries.empty()) {
        lines.push_back(text("  (no matches)") | dim);
    } else {
        const int total = static_cast<int>(entries.size());
        select_idx = std::clamp(select_idx, 0, total - 1);

        constexpr int MAX_VISIBLE = 12;
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
           size(WIDTH, EQUAL, 72) | hcenter;
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

    // Search bar
    left_lines.push_back(hbox({
        text(" Find: ") | bold | color(accent(theme)),
        text(query) | color(Color::White)
    }));
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

        constexpr int MAX_VISIBLE = 12;
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

    auto left_box = vbox(std::move(left_lines)) | size(WIDTH, EQUAL, 42);
    auto right_box = vbox(std::move(right_lines)) | size(WIDTH, EQUAL, 46);

    return hbox({ left_box, separatorLight(), right_box }) | borderRounded | bgcolor(bg_popup()) | color(accent(theme)) | hcenter;
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

    // Search bar
    lines.push_back(hbox({
        text(" Find: ") | bold | color(accent(theme)),
        text(query) | color(Color::White)
    }));
    lines.push_back(separatorLight());

    if (entries.empty()) {
        lines.push_back(text("  (no matches)") | dim);
    } else {
        const int total = static_cast<int>(entries.size());
        select_idx = std::clamp(select_idx, 0, total - 1);

        constexpr int MAX_VISIBLE = 12;
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
            std::string line_text = marker + e.title;
            if (e.id != e.title)
                line_text += "  (" + e.id.substr(0, 8) + "...)";
            if (!e.workspace.empty())
                line_text += "  [" + e.workspace + "]";

            auto line = text(line_text);
            if (i == select_idx)
                line = line | bgcolor(bg_popup()) | bold;
            else if (active)
                line = line | color(accent2(theme));
            lines.push_back(line);
        }

        if (window_end < total) {
            lines.push_back(text("  ▼ " + std::to_string(total - window_end) + " more sessions below") | dim | color(accent(theme)));
        }
    }

    lines.push_back(separatorLight());
    lines.push_back(text(" ↑↓ navigate  Enter select  Esc cancel") | dim);

    return vbox(std::move(lines)) | borderRounded | bgcolor(bg_popup()) | color(accent(theme)) |
           size(WIDTH, EQUAL, 72) | hcenter;
}

}  // namespace tui
}  // namespace qcode
