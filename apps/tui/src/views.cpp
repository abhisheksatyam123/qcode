#include <qcode/ui/message_render.h>
#include <views.h>
#include "file_diff_preview.h"
#include "views_pickers.h"
#include <qcode/config/provider_info.h>
#include <qcode/core/logger.h>
#include <qcode/session/session_store.h>
#include <qcode/transform/provider_transform.h>
#include <qcode/ui/chat_state.h>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <climits>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <ftxui/screen/string.hpp>

namespace qcode {
namespace tui {

using namespace ftxui;

namespace {

// Reflect layout box into ChatState hit-testing (file list rows, etc.).
class ReflectSimple : public ftxui::Node {
 public:
  ReflectSimple(ftxui::Element child, HitBox& box)
      : ftxui::Node(unpack(std::move(child))), reflected_box_(box) {}

  void ComputeRequirement() final {
    ftxui::Node::ComputeRequirement();
    requirement_ = children_[0]->requirement();
  }

  void SetBox(ftxui::Box box) final {
    reflected_box_.x_min = box.x_min;
    reflected_box_.x_max = box.x_max;
    reflected_box_.y_min = box.y_min;
    reflected_box_.y_max = box.y_max;
    ftxui::Node::SetBox(box);
    children_[0]->SetBox(box);
  }

 private:
  HitBox& reflected_box_;
};

ftxui::Decorator reflect_box(HitBox& box) {
  return [&box](ftxui::Element child) -> ftxui::Element {
    return std::make_shared<ReflectSimple>(std::move(child), box);
  };
}

std::string format_workspace_path() {
    std::error_code ec;
    auto cwd = std::filesystem::current_path(ec);
    if (ec) return ".";
    std::string path = cwd.string();
    const char* home = std::getenv("HOME");
    if (home != nullptr && home[0] != '\0') {
        const std::string home_s{home};
        if (path.rfind(home_s, 0) == 0) {
            path.replace(0, home_s.size(), "~");
        }
    }
    return path;
}

int prompt_input_height(const std::string& prompt_input, int max_lines) {
    int input_lines = 1;
    const int term_w = stable_terminal_size().dimx;
    const int avail = std::max(20, term_w - 7);
    size_t pos = 0;
    while (pos <= prompt_input.size()) {
        size_t nl = prompt_input.find('\n', pos);
        std::string seg = (nl == std::string::npos)
            ? prompt_input.substr(pos)
            : prompt_input.substr(pos, nl - pos);
        const int w = ftxui::string_width(seg);
        int segs = (w <= 0) ? 1 : (w + avail - 1) / avail;
        input_lines += segs - 1;
        if (nl == std::string::npos) break;
        pos = nl + 1;
    }
    if (input_lines < 1) input_lines = 1;
    return std::min(input_lines, max_lines);
}

}  // namespace

// Upstream opencode formatUsage (session-data.ts): locale-grouped total with
// optional percent + cost. Total folds input+output+reasoning+cache.
static std::string format_grouped(long long n) {
  bool neg = n < 0;
  if (neg) n = -n;
  std::string d = std::to_string(n);
  std::string out;
  int c = 0;
  for (int i = (int)d.size() - 1; i >= 0; --i) {
    out.push_back(d[i]);
    if (++c == 3 && i > 0) { out.push_back(','); c = 0; }
  }
  std::reverse(out.begin(), out.end());
  return neg ? "-" + out : out;
}
static std::string format_usage_upstream(long long total, int window_size,
                                         double cost, bool have_cost) {
  if (total <= 0) {
    if (have_cost && cost > 0) {
      std::ostringstream os;
      os << "$" << std::fixed << std::setprecision(4) << cost;
      return os.str();
    }
    return {};
  }
  std::string text = format_grouped(total);
  if (window_size > 0)
    text += " (" + std::to_string((int)(total * 100 / window_size)) + "%)";
  if (have_cost && cost > 0) {
    std::ostringstream os;
    os << "$" << std::fixed << std::setprecision(4) << cost;
    text += " · " + os.str();
  }
  return text;
}

// Soft amber used for pending / queued chrome (readable on dark terminals).
static Color queue_amber() { return Color::RGB(0xE8, 0xB8, 0x4A); }

// Grok-style queued prompt card: looks like a user turn, dimmed, with a
// position badge so the queue is visible in the message list.
static Element render_queued_prompt(const std::string& prompt_body,
                                    size_t index, size_t total,
                                    const std::string& theme) {
    Elements body_lines;
    std::istringstream iss(prompt_body);
    std::string line;
    bool any = false;
    while (std::getline(iss, line)) {
        any = true;
        body_lines.push_back(
            hbox({text("  "), text(line) | color(Color::GrayLight)}));
    }
    if (!any) {
        body_lines.push_back(hbox({text("  "), text("(empty prompt)") | dim}));
    }

    auto header = hbox({
        text(" #" + std::to_string(index + 1) + "/" + std::to_string(total) + " ") |
            bold | bgcolor(queue_amber()) | color(Color::Black),
        text("  You") | bold | color(accent2(theme)),
        text(" · queued") | dim | color(queue_amber()),
        filler(),
        text("pending ") | dim | color(Color::GrayDark),
    });

    return vbox({
        header,
        separatorLight() | color(Color::GrayDark),
        vbox(std::move(body_lines)),
    }) | borderRounded | color(queue_amber());
}

ftxui::Element render_logo() {
    // Two-tone 4-row block logo spelling "q-code", using the same
    // character-cell technique as OpenCode (muted mark + bold wordmark).
    const auto muted = theme_text_muted();
    const auto fg = theme_text();
    return vbox({
        hbox({
            text("         ") | color(muted),
            text(" ▄ ") | color(fg) | bold,
        }),
        hbox({
            text("█▀▀█     ") | color(muted),
            text("█▀▀▀ █▀▀█ █▀▀▄ █▀▀▀") | color(fg) | bold,
        }),
        hbox({
            text("█▄▄█  ▀  ") | color(muted),
            text("█    █  █ █  █ █▀▀▀") | color(fg) | bold,
        }),
        hbox({
            text("▀▀▀▀     ") | color(muted),
            text("▀▀▀▀ ▀▀▀▀ ▀▀▀▀ ▀▀▀▀") | color(fg) | bold,
        }),
    }) | hcenter;
}

ftxui::Element render_view(
    const ChatState& state,
    const std::vector<ProviderInfo>& providers_list,
    int selected_provider,
    int selected_model,
    bool /*enable_tools*/,
    const std::string& prompt_input,
    bool /*show_slash*/,
    int /*slash_idx*/,
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
    bool show_variant_select,
    int variant_select_idx,
    const std::vector<VariantEntry>& variant_entries,
    const std::string& variant_query,
    bool show_palette,
    int palette_select_idx,
    const std::vector<PaletteCommand>& palette_entries,
    const std::string& palette_query,
    bool show_help,
    const ftxui::Component& tab_toggle,
    const std::shared_ptr<int>& /*scroll_line*/,
    const ftxui::Component& input
) {
    std::string theme = state.theme ? *state.theme : "opencode";

    // ── Header: identity + model + context (transient status lives in footer) ──
    std::string hdr_model = providers_list[selected_provider].models[selected_model].name;
    const auto& hdr_model_info = providers_list[selected_provider].models[selected_model];
    // Upstream opencode usage: total = ctx snapshot + reasoning + cache
    // (session-data.ts formatUsage), rendered "12,345 (8%) · $0.0123".
    int ctx_snapshot = *state.current_context_tokens > 0
                           ? *state.current_context_tokens
                           : (*state.last_actual_prompt_tokens > 0
                                  ? *state.last_actual_prompt_tokens
                                  : 0);
    int window_size = hdr_model_info.context_window;
    if (window_size <= 0) {
        auto summary = session::get_model_performance_summary(hdr_model_info.id);
        if (summary.context_window > 0) {
            window_size = summary.context_window;
        } else {
            std::string lower = hdr_model_info.id;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            if (lower.find("2m") != std::string::npos || lower.find("gemini-3.1") != std::string::npos) {
                window_size = 2000000;
            } else if (lower.find("gemini") != std::string::npos || lower.find("deepseek") != std::string::npos || lower.find("nemotron") != std::string::npos) {
                window_size = 1000000;
            } else {
                window_size = 200000;
            }
        }
    }
    const int last_reasoning = state.last_reasoning_tokens ? *state.last_reasoning_tokens : 0;
    const int last_cache = state.last_cached_prompt_tokens ? *state.last_cached_prompt_tokens : 0;
    const long long usage_total =
        (long long)ctx_snapshot + (long long)std::max(0, last_reasoning) + (long long)std::max(0, last_cache);
    // Session cost estimate for the usage suffix (same rates as Stats tab).
    double use_in_rate = 3.00, use_out_rate = 15.00;
    {
        const std::string& pid =
            (selected_provider >= 0 && selected_provider < (int)providers_list.size())
                ? providers_list[selected_provider].id
                : std::string{};
        use_in_rate = hdr_model_info.input_cost > 0 ? hdr_model_info.input_cost
                      : (pid == "openrouter" ? 2.50 : 3.00);
        use_out_rate = hdr_model_info.output_cost > 0 ? hdr_model_info.output_cost
                       : (pid == "openrouter" ? 10.00 : 15.00);
    }
    const double use_cost =
        (*state.total_prompt_tokens * use_in_rate + *state.total_completion_tokens * use_out_rate) / 1000000.0;
    const bool use_have_cost = (*state.total_prompt_tokens + *state.total_completion_tokens) > 0;
    std::string hdr_tokens = format_usage_upstream(usage_total, window_size, use_cost, use_have_cost);
    int ctx_pct = 0;
    Color ctx_color = Color::Default;
    if (window_size > 0 && usage_total > 0) {
      ctx_pct = (int)(usage_total * 100 / window_size);
      if (ctx_pct > 100) ctx_pct = 100;
      ctx_color = ctx_pct >= 85   ? Color::Red
                  : ctx_pct >= 70 ? Color::Yellow
                                  : accent2(theme);
    }

    // Upstream statusline vocabulary (footer.view.tsx statusText + runtime
    // phases): busy => interrupt labels; else raw backend status verbatim.
    // No local badges (no "gen...", no warn/error/retry latches).
    Color status_color = accent2(theme);
    std::string hdr_status;
    const bool agent_busy =
        *state.is_generating ||
        (state.status && (*state.status == "generating" ||
                          *state.status == "agent"));
    const bool abort_armed = agent_busy && state.abort_flag && state.abort_flag->load();
    if (agent_busy) {
        hdr_status = abort_armed ? "again to interrupt" : "interrupt";
        status_color = abort_armed ? accent(theme) : theme_text(theme);
    } else if (state.status && !state.status->empty() && *state.status != "idle" &&
               *state.status != "generating" && *state.status != "agent" &&
               *state.status != "error" && *state.status != "warn") {
        hdr_status = *state.status;
        status_color = theme_text(theme);
    }

    // Upstream queue: plain count hint, shown only when N > 0.
    std::string hdr_queue;
    if (state.queued_prompts && *state.queued_prompts > 0) {
        hdr_queue = "queued " + std::to_string(*state.queued_prompts);
    }

    // Session segment: ALWAYS visible (never blank). Custom title wins;
    // default "Session - <model>" titles collapse to short id so the model
    // name doesn't duplicate the footer chip.
    std::string hdr_session;
    {
        std::string title =
            (state.session_title && !state.session_title->empty())
                ? *state.session_title : std::string{};
        const std::string sess_prefix = "Session - " + hdr_model;
        bool is_default = (title == sess_prefix) ||
            (title.rfind("Session - ", 0) == 0 && title.size() > 10 &&
             hdr_model.find(title.substr(10)) != std::string::npos);
        if (!title.empty() && !is_default) {
            hdr_session = title;
        } else if (state.session_id && state.session_id->size() >= 8) {
            hdr_session = state.session_id->substr(0, 8);
        }
        if (hdr_session.size() > 28) {
            hdr_session = hdr_session.substr(0, 27) + "…";
        }
        if (hdr_session.empty() && state.session_id && !state.session_id->empty())
            hdr_session = state.session_id->substr(0, 8);
    }
    // Folder for the header: ~/a/b/c shortened to b/c (last 2 segments,
    // footer is dropped so everything lives in one designed header).
    // Folder for the header: the SESSION workspace (not process cwd), so it
    // matches opencode's per-session directory. Falls back to cwd.
    std::string hdr_folder;
    {
        std::string ws;
        if (state.session_id && !state.session_id->empty())
            ws = session::get_session_workspace(*state.session_id);
        hdr_folder = ws.empty() ? format_workspace_path() : ws;
        const char* home = std::getenv("HOME");
        if (home && home[0] && hdr_folder.rfind(home, 0) == 0)
            hdr_folder.replace(0, std::string(home).size(), "~");
        std::vector<std::string> segs;
        std::string cur;
        for (char c : hdr_folder) {
            if (c == '/' || c == '\\') {
                if (!cur.empty()) { segs.push_back(cur); cur.clear(); }
            } else cur.push_back(c);
        }
        if (!cur.empty()) segs.push_back(cur);
        if (segs.size() >= 2)
            hdr_folder = segs[segs.size()-2] + "/" + segs.back();
        else if (!segs.empty())
            hdr_folder = segs.back();
        if (hdr_folder.size() > 32) hdr_folder = "…/" + hdr_folder.substr(hdr_folder.size()-31);
    }

    std::string variant_label;
    {
        std::string rm = state.reasoning_mode ? *state.reasoning_mode : std::string{};
        if (!rm.empty() && rm != "off") {
            variant_label = rm;
        } else if (rm.empty()) {
            // Auto-thinking (generation defaults empty -> default_variant);
            // surface it so the header matches what the backend requests.
            variant_label = qcode::ProviderTransform::default_variant(hdr_model_info);
            if (variant_label == "off") variant_label.clear();
        }
        // Explicit "off" stays empty (thinking disabled).
    }
    const bool plan_mode =
        state.agent_mode && *state.agent_mode == "plan";
    const bool is_home =
        state.tab_selected == 0 && state.messages_history &&
        state.messages_history->empty();
    const std::string hdr_provider =
        (selected_provider >= 0 &&
         selected_provider < static_cast<int>(providers_list.size()))
            ? providers_list[selected_provider].name
            : std::string{};

    const bool copy_mode = state.copy_mode && *state.copy_mode;
    // Upstream statusline order: [spinner] [esc] status-text … queue.
    // Spinner renders only while busy (blocks style upstream; braille here).
    // Upstream statusline order: [spinner] status-text … queue.
    // Spinner renders only while busy (single animated owner).
    static const std::array<const char*, 10> footer_sp = {
        "\u280b", "\u2819", "\u2839", "\u2838", "\u283c",
        "\u2834", "\u2836", "\u2837", "\u280f", "\u280b"
    };
    const std::string footer_spin =
        agent_busy ? footer_sp[*state.generation_frame % footer_sp.size()] : std::string{};
    // Upstream footer: [spinner] model variant … status queue.
    // Model + spinner live here only (header keeps folder/session/mode/tabs).
    Elements footer_bits = {
        (copy_mode ? text(" COPY ") | bold | color(theme_warning(theme))
                   : emptyElement()),
        (agent_busy ? text(" " + footer_spin) | color(accent(theme)) | bold
                    : emptyElement()),
        (plan_mode ? text(" Plan") | bold | color(queue_amber())
                   : text(" Build") | bold | color(accent2(theme))),
        text(" " + hdr_model) | color(accent(theme)),
    };
    if (!variant_label.empty())
        footer_bits.push_back(text(" " + variant_label) | bold | color(theme_warning(theme)));
    if (!hdr_status.empty())
        footer_bits.push_back(text((agent_busy ? " esc " : " ") + hdr_status) | color(status_color));
    if (!hdr_queue.empty())
        footer_bits.push_back(text("  " + hdr_queue) | dim | color(theme_text_muted(theme)));
    footer_bits.push_back(filler());
    // Key hints merged here (single-line footer): busy state already shows
    // "esc interrupt" via hdr_status above, so only add idle hints.
    if (!agent_busy) {
        footer_bits.push_back(text("tab") | bold | color(theme_text(theme)));
        footer_bits.push_back(text(" agents  ") | dim | color(theme_text_muted(theme)));
        footer_bits.push_back(text("ctrl+p") | bold | color(theme_text(theme)));
        footer_bits.push_back(text(" commands ") | dim | color(theme_text_muted(theme)));
    }
    if (is_home)
        footer_bits.push_back(text("0.1.0 ") | dim | color(theme_text_muted(theme)));
    auto footer = hbox(std::move(footer_bits));

    Element header = emptyElement();  // built below after prompt_status

    auto make_prompt_box = [&](int input_height) -> Element {
        // Single-line footer owns ALL status/hints (see footer below).
        // Prompt box is input only — no second hint line.
        return vbox({
            hbox({
                text(" ❯ ") | color(accent2(theme)) | bold,
                input->Render() | color(theme_text(theme)) | yframe |
                    size(HEIGHT, EQUAL, input_height) | flex,
            }),
        }) | borderRounded | color(theme_border(theme));
    };

    // -- Designed single header: 📁 folder · session · Build model (variant) · ctx · tabs --
    {
        Elements left;
        // folder + session always visible (session never blank, never hidden).
        left.push_back(text("⌂ " + hdr_folder) | bold | color(accent2(theme)));
        left.push_back(text(" · ") | dim | color(theme_text_muted(theme)));
        left.push_back(text(hdr_session.empty() ? "session" : hdr_session) |
                       color(theme_text(theme)));
        left.push_back(text("  ") | dim);
        // Mode lives in the footer (upstream statusline order); header keeps
        // identity + usage + tabs only.
        Elements ctx;
        if (!hdr_tokens.empty()) {
            ctx.push_back(
                text(" " + hdr_tokens) |
                (ctx_pct > 0 ? color(ctx_color) : color(theme_text_muted(theme))));
        }
        if (ctx_pct >= 70) {
            ctx.push_back(text("  /compact") | dim | color(Color::Yellow));
        }
        Elements row = {
            hbox(std::move(left)),
            filler(),
        };
        if (!ctx.empty()) {
            row.push_back(hbox(std::move(ctx)));
            row.push_back(text("  ") | dim);
        }
        row.push_back(tab_toggle->Render());
        row.push_back(text(" "));
        header = vbox({
            hbox(std::move(row)),
            separatorLight() | color(theme_border(theme)),
        });
    }

    Element body;

    // ── Dynamic inline slash command / session autocomplete matching opencode ──
    auto build_suggestions_panel = [&]() -> std::optional<Element> {
        if (prompt_input.size() >= 9 && prompt_input.substr(0, 9) == "/session ") {
            std::string filter_str = prompt_input.substr(9);
            std::vector<std::pair<std::string, std::string>> matches;
            for (const auto& session : session_entries) {
                if (session.id.find(filter_str) != std::string::npos ||
                    session.title.find(filter_str) != std::string::npos) {
                    matches.emplace_back(session.id, session.title);
                }
            }
            if (!matches.empty()) {
                Elements rows;
                rows.push_back(text(" Sessions") | bold | color(accent2(theme)));
                rows.push_back(separatorLight() | color(accent(theme)));
                for (int i = 0; i < static_cast<int>(matches.size()); ++i) {
                    bool active = (state.slash_suggestion_mode && state.slash_suggestion_idx == i);
                    std::string marker = active ? " ▶ " : "   ";
                    auto row = hbox({
                        text(marker + matches[i].first) | color(active ? accent2(theme) : Color::White) | bold,
                        text("  (" + matches[i].second + ")") | dim
                    });
                    if (active) {
                        row = row | bgcolor(bg_popup()) | bold;
                    }
                    rows.push_back(row);
                }
                return vbox(std::move(rows)) | borderRounded | color(accent(theme)) | bgcolor(bg_popup());
            }
        } else if (prompt_input.size() >= 9 && prompt_input.substr(0, 9) == "/variant ") {
            std::string filter_str = prompt_input.substr(9);
            const ModelInfo* model = nullptr;
            if (selected_provider >= 0 &&
                selected_provider < static_cast<int>(providers_list.size()) &&
                selected_model >= 0 &&
                selected_model < static_cast<int>(
                    providers_list[selected_provider].models.size())) {
                model = &providers_list[selected_provider].models[selected_model];
            }
            ModelInfo fallback;
            const auto variants = build_variant_entries(model ? *model : fallback);
            std::vector<VariantEntry> matches;
            for (const auto& v : variants) {
                if (filter_str.empty() ||
                    v.id.find(filter_str) != std::string::npos ||
                    v.title.find(filter_str) != std::string::npos) {
                    matches.push_back(v);
                }
            }
            if (!matches.empty()) {
                Elements rows;
                rows.push_back(text(" Variants") | bold | color(accent2(theme)));
                rows.push_back(separatorLight() | color(accent(theme)));
                for (int i = 0; i < static_cast<int>(matches.size()); ++i) {
                    bool active = (state.slash_suggestion_mode &&
                                   state.slash_suggestion_idx == i);
                    std::string marker = active ? " ▶ " : "   ";
                    auto row = hbox({
                        text(marker + matches[i].title) |
                            color(active ? accent2(theme) : Color::White) | bold,
                        text("  " + matches[i].description) | dim
                    });
                    if (active) row = row | bgcolor(bg_popup()) | bold;
                    rows.push_back(row);
                }
                return vbox(std::move(rows)) | borderRounded | color(accent(theme)) |
                       bgcolor(bg_popup());
            }
        } else if (prompt_input.size() > 0 && prompt_input[0] == '/' && prompt_input.find(' ') == std::string::npos) {
            std::string filter_str = prompt_input.substr(1);
            std::vector<SlashCommand> matches;
            for (const auto& cmd : slash_commands) {
                if (cmd.name.find(filter_str) != std::string::npos) {
                    matches.push_back(cmd);
                }
            }
            if (!matches.empty()) {
                Elements rows;
                rows.push_back(text(" Commands") | bold | color(accent2(theme)));
                rows.push_back(separatorLight() | color(accent(theme)));
                for (int i = 0; i < static_cast<int>(matches.size()); ++i) {
                    bool active = (state.slash_suggestion_mode && state.slash_suggestion_idx == i);
                    std::string marker = active ? " ▶ " : "   ";
                    auto row = hbox({
                        text(marker + "/" + matches[i].name) | color(active ? accent2(theme) : Color::White) | bold,
                        text("  " + matches[i].description) | dim
                    });
                    if (active) {
                        row = row | bgcolor(bg_popup()) | bold;
                    }
                    rows.push_back(row);
                }
                return vbox(std::move(rows)) | borderRounded | color(accent(theme)) | bgcolor(bg_popup());
            }
        }
        return std::nullopt;
    };

    // ── Tab 0: Chat ──
    if (state.tab_selected == 0) {
        bool empty = state.messages_history->empty();

        if (empty) {
            const int input_height = prompt_input_height(prompt_input, 6);
            const int term_w = stable_terminal_size().dimx;
            const int prompt_w = std::clamp(term_w - 8, 48, 84);
            Element prompt_box = make_prompt_box(input_height);
            auto suggestions = build_suggestions_panel();
            if (suggestions) {
                prompt_box = vbox({*suggestions, prompt_box});
            }

            body = vbox({
                filler() | flex,
                render_logo(),
                text("") | size(HEIGHT, EQUAL, 1),
                prompt_box | size(WIDTH, EQUAL, prompt_w) | hcenter,
                filler() | flex,
            }) | flex;
        } else {
            // Chat history viewport. Building an FTXUI tree for every historical
            // message on every token made render cost grow without bound.
            Elements msgs;
            const auto history_size = state.messages_history->size();
            // Keep the rendered window tight so stream frames stay cheap even
            // on long sessions (older messages stay in history/DB).
            const auto window_size = static_cast<size_t>(
                std::max(24, state.terminal_height + 8));
            const auto max_start =
                history_size > window_size ? history_size - window_size : 0;
            if (*state.auto_scroll) {
                *state.history_window_start = max_start;
            } else {
                *state.history_window_start =
                    std::min(*state.history_window_start, max_start);
            }
            const auto first = *state.history_window_start;
            const auto last = std::min(history_size, first + window_size);

            // Completed plain messages are immutable. Reuse their parsed
            // Markdown/FTXUI trees across frames and keep cache residency
            // bounded to the current window.
            static const qcode::Messages* cache_owner = nullptr;
            static size_t cached_history_size = 0;
            static int cached_width = 0;
            static int cached_provider = -1;
            static int cached_model = -1;
            static std::string cached_theme;
            static std::string cached_session;
            static bool cached_thinking = false;
            static std::unordered_map<size_t, Element> message_cache;
            const auto terminal_width = stable_terminal_size().dimx;
            if (cache_owner != state.messages_history.get() ||
                history_size < cached_history_size ||
                terminal_width != cached_width ||
                cached_provider != selected_provider ||
                cached_model != selected_model ||
                cached_theme != *state.theme ||
                cached_session != *state.session_id ||
                cached_thinking != *state.show_thinking) {
                message_cache.clear();
                cache_owner = state.messages_history.get();
                cached_width = terminal_width;
                cached_provider = selected_provider;
                cached_model = selected_model;
                cached_theme = *state.theme;
                cached_session = *state.session_id;
                cached_thinking = *state.show_thinking;
            }
            cached_history_size = history_size;
            for (auto it = message_cache.begin(); it != message_cache.end();) {
                if (it->first < first || it->first >= last) {
                    it = message_cache.erase(it);
                } else {
                    ++it;
                }
            }

            if (state.tool_block_order) state.tool_block_order->clear();
            if (state.tool_arrow_boxes) state.tool_arrow_boxes->clear();
            if (state.thinking_header_boxes)
                state.thinking_header_boxes->clear();
            if (first > 0) {
                msgs.push_back(
                    text(" " + std::to_string(first) +
                         " earlier messages · Home/PageUp to view ") |
                    dim | hcenter);
                msgs.push_back(separatorLight() | color(dim_gray()));
            }
            for (size_t i = first; i < last; ++i) {
                const auto& msg = (*state.messages_history)[i];
                const qcode::Message* adjacent_tool_results = nullptr;
                if (msg.has_tool_calls() && i + 1 < history_size &&
                    (*state.messages_history)[i + 1].has_tool_results()) {
                    adjacent_tool_results =
                        &(*state.messages_history)[i + 1];
                }
                const bool cacheable =
                    i + 1 < history_size && !msg.has_tool_calls() &&
                    !msg.has_tool_results() && !msg.has_reasoning();
                auto cached = message_cache.find(i);
                if (cacheable && cached != message_cache.end()) {
                    msgs.push_back(cached->second);
                } else {
                    auto rendered = render_message(
                        msg, state, providers_list, selected_provider,
                        selected_model, *state.theme,
                        adjacent_tool_results);
                    if (cacheable) message_cache[i] = rendered;
                    msgs.push_back(std::move(rendered));
                }
                // No separator line between messages: the role header
                // ("❯ You" / "❯ Assistant") is delimiter enough.
                // Skip the paired result row when it is also inside this window.
                if (adjacent_tool_results != nullptr && i + 1 < last) ++i;
            }
            if (last < history_size) {
                msgs.push_back(
                    text(" " + std::to_string(history_size - last) +
                         " later messages · PageDown/End to view ") |
                    dim | hcenter);
            }

            // Queued prompts appear at the end of the message list (Grok/Claude style cards)
            const auto* queue =
                state.queued_prompt_texts ? state.queued_prompt_texts.get()
                                          : nullptr;
            if (queue && !queue->empty() && last >= history_size) {
                Elements queue_cards;
                queue_cards.push_back(text(""));
                queue_cards.push_back(hbox({
                    text(" ⏳ QUEUED PROMPTS ") | bold | color(queue_amber()),
                    text(" · /clear-queue to cancel") | dim | color(Color::GrayDark),
                }));
                queue_cards.push_back(text(""));

                for (size_t qi = 0; qi < queue->size(); ++qi) {
                    queue_cards.push_back(render_queued_prompt((*queue)[qi], qi, queue->size(), theme));
                }
                
                msgs.push_back(vbox(std::move(queue_cards)));
            }

            const int input_height = prompt_input_height(prompt_input, 8);
            Element prompt_box = make_prompt_box(input_height);

            auto suggestions = build_suggestions_panel();
            if (suggestions) {
                prompt_box = vbox({
                    *suggestions,
                    prompt_box
                });
            }

            // Measure chat content height (in rendered lines) so scrolling uses a
            // real line index instead of INT_MAX. ftxui clamps focusPosition(y) to
            // [0, content-1], so INT_MAX (and INT_MAX-3) both pin to the bottom and
            // make wheel/key scrolling a no-op. Keeping a real index fixes that.
            Element chat_scroll = vbox(std::move(msgs));
            chat_scroll->ComputeRequirement();
            {
                const int content_height = std::max(0, chat_scroll->requirement().min_y);
                if (*state.auto_scroll) {
                    *state.scroll_line = std::max(0, content_height - 1);
                } else {
                    *state.scroll_line =
                        std::clamp(*state.scroll_line, 0, std::max(0, content_height - 1));
                    // Re-engage auto-scroll when user scrolls back to the bottom
                    if (*state.scroll_line >= content_height - 1) {
                        *state.auto_scroll = true;
                    }
                }
            }
            body = vbox({
                chat_scroll | vscroll_indicator | focusPosition(0, *state.scroll_line) | yframe | flex,
                // prompt box follows directly (status lives in footer)
                prompt_box,
            }) | flex;
        }
    }
    // ── Tab 1: Files — list of git changes, then per-file diff ──
    else if (state.tab_selected == 1) {
        const auto* changes =
            state.file_changes ? state.file_changes.get() : nullptr;
        if (state.file_row_boxes) state.file_row_boxes->clear();

        if (!changes || changes->empty()) {
            body = vbox({
                filler() | flex,
                text("Working tree clean — no changes in git diff.") | hcenter |
                    dim,
                text("Press r to refresh") | hcenter | dim,
                filler() | flex,
            }) | flex;
        } else if (!state.files_detail_open) {
            // List view: every changed file with +/- from numstat.
            Elements rows;
            rows.push_back(hbox({
                text(" CHANGED FILES ") | bold | color(accent2(theme)),
                text(" (" + std::to_string(changes->size()) + ") ") | dim,
                filler(),
                text("↑↓ select  Enter/click open  r refresh") | dim,
            }));
            rows.push_back(separatorLight() | color(accent(theme)));

            if (state.file_row_boxes) {
                state.file_row_boxes->resize(changes->size());
            }

            int total_add = 0;
            int total_del = 0;
            for (size_t i = 0; i < changes->size(); ++i) {
                const auto& entry = (*changes)[i];
                total_add += entry.additions;
                total_del += entry.deletions;
                const bool active =
                    static_cast<int>(i) == state.selected_file;
                const std::string marker = active ? "▶ " : "  ";

                Element stats;
                if (entry.binary) {
                    stats = text("binary") | dim | color(Color::Yellow);
                } else if (entry.untracked && !entry.path.empty() &&
                           entry.path.back() == '/') {
                    stats = text("untracked dir") | dim |
                            color(Color::Yellow);
                } else {
                    stats = hbox({
                        text("+" + std::to_string(entry.additions)) |
                            color(Color::Green) | (active ? bold : dim),
                        text("  "),
                        text("-" + std::to_string(entry.deletions)) |
                            color(Color::Red) | (active ? bold : dim),
                    });
                }

                Element path_el =
                    text(entry.path) |
                    color(active ? accent2(theme) : Color::Default);
                if (active) path_el = std::move(path_el) | bold;

                Element row = hbox({
                    text(marker) |
                        color(active ? accent2(theme) : Color::Default),
                    std::move(path_el),
                    text(entry.untracked ? "  (new)" : "") | dim |
                        color(Color::Yellow),
                    filler(),
                    stats,
                    text("  "),
                });
                if (active) {
                    row = std::move(row) | bgcolor(bg_popup());
                }
                if (state.file_row_boxes) {
                    row = std::move(row) |
                          reflect_box((*state.file_row_boxes)[i]);
                }
                rows.push_back(std::move(row));
            }

            rows.push_back(separatorLight() | color(dim_gray()));
            rows.push_back(hbox({
                text("  "),
                text(std::to_string(changes->size()) + " files") | dim,
                text("  "),
                text("+" + std::to_string(total_add)) | color(Color::Green) |
                    dim,
                text("  "),
                text("-" + std::to_string(total_del)) | color(Color::Red) |
                    dim,
            }));

            Element file_scroll = vbox(std::move(rows));
            file_scroll->ComputeRequirement();
            {
                const int content_height =
                    std::max(0, file_scroll->requirement().min_y);
                // Keep the selected row roughly in view.
                const int target = std::min(
                    content_height - 1,
                    std::max(0, state.selected_file + 2));
                *state.scroll_line =
                    std::clamp(target, 0, std::max(0, content_height - 1));
                *state.auto_scroll = false;
            }
            body = vbox({
                file_scroll | vscroll_indicator |
                    focusPosition(0, *state.scroll_line) | yframe | flex,
            }) | flex;
        } else {
            // Detail view: unified diff for the selected file.
            const auto selected = std::clamp(
                state.selected_file, 0,
                static_cast<int>(changes->size()) - 1);
            const auto& entry = (*changes)[selected];
            std::string content;
            const bool untracked_dir =
                entry.untracked && !entry.path.empty() &&
                entry.path.back() == '/';
            if (untracked_dir) {
                content = "Untracked directory — sample paths:\n\n";
                const std::string cmd =
                    "git -c core.quotepath=false ls-files --others "
                    "--exclude-standard -- " +
                    shell_quote(entry.path) + " 2>/dev/null | head -n 80";
                std::array<char, 512> buf{};
                FILE* pipe = popen(cmd.c_str(), "r");
                int n = 0;
                if (pipe) {
                    while (fgets(buf.data(), static_cast<int>(buf.size()),
                                pipe) != nullptr) {
                        content += "+";
                        content += buf.data();
                        ++n;
                    }
                    pclose(pipe);
                }
                if (n == 0) {
                    content += "(empty or ignored)\n";
                } else if (n >= 80) {
                    content += "\n… truncated\n";
                }
            } else {
                content = get_file_diff(entry.path, *state.files_revision);
            }

            Element back_btn = text(" ← Esc ") | dim;
            if (state.files_back_box) {
                back_btn = std::move(back_btn) | reflect_box(*state.files_back_box);
            }

            auto file_header = hbox({
                std::move(back_btn),
                text(entry.path) | bold | color(accent2(theme)),
                text(entry.untracked ? "  (untracked)" : "") | dim |
                    color(Color::Yellow),
                filler(),
                entry.binary
                    ? text("binary") | color(Color::Yellow)
                    : hbox({
                          text("+" + std::to_string(entry.additions)) |
                              color(Color::Green),
                          text(" "),
                          text("-" + std::to_string(entry.deletions)) |
                              color(Color::Red),
                      }),
            });

            Elements file_blocks;
            file_blocks.push_back(vbox({
                file_header,
                separatorLight() | color(accent(theme)),
                content.empty()
                    ? text("  (no diff output)") | dim
                    : hbox({text("  "), render_diff_content(content) | flex}),
                text(""),
            }) | borderLight | color(accent(theme)));

            Element file_scroll = vbox(std::move(file_blocks));
            file_scroll->ComputeRequirement();
            {
                const int content_height =
                    std::max(0, file_scroll->requirement().min_y);
                if (*state.auto_scroll) {
                    *state.scroll_line = 0;
                    *state.auto_scroll = false;
                } else {
                    *state.scroll_line = std::clamp(
                        *state.scroll_line, 0,
                        std::max(0, content_height - 1));
                }
            }
            body = vbox({
                text(" FILE DIFF ") | bold | color(accent2(theme)) | hcenter,
                text(""),
                file_scroll | vscroll_indicator |
                    focusPosition(0, *state.scroll_line) | yframe | flex,
            }) | flex;
        }
    }
    // ── Tab 2: Stats ──
    else {
        std::string prov_name = "Unknown";
        std::string mod_name = "Unknown";
        std::string prov_id = "";
        int hard_limit = 200000;
        double in_rate = 3.00;
        double out_rate = 15.00;

        if (selected_provider >= 0 && selected_provider < static_cast<int>(providers_list.size())) {
            const auto& prov = providers_list[selected_provider];
            prov_name = prov.name;
            prov_id = prov.id;
            if (selected_model >= 0 && selected_model < static_cast<int>(prov.models.size())) {
                const auto& m = prov.models[selected_model];
                mod_name = m.name;
                if (m.context_window > 0) {
                    hard_limit = m.context_window;
                } else {
                    auto summary = session::get_model_performance_summary(m.id);
                    if (summary.context_window > 0) {
                        hard_limit = summary.context_window;
                    } else {
                        std::string lower = m.id;
                        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
                        if (lower.find("2m") != std::string::npos || lower.find("gemini-3.1") != std::string::npos) {
                            hard_limit = 2000000;
                        } else if (lower.find("gemini") != std::string::npos || lower.find("deepseek") != std::string::npos || lower.find("nemotron") != std::string::npos) {
                            hard_limit = 1000000;
                        }
                    }
                }
                in_rate = m.input_cost > 0 ? m.input_cost : (prov_id == "openrouter" ? 2.50 : 3.00);
                out_rate = m.output_cost > 0 ? m.output_cost : (prov_id == "openrouter" ? 10.00 : 15.00);
            }
        }
        if (hard_limit <= 0) hard_limit = 200000;

        int used = *state.current_context_tokens > 0
                       ? *state.current_context_tokens
                       : (*state.last_actual_prompt_tokens > 0
                              ? *state.last_actual_prompt_tokens
                              : 0);
        double used_pct = (double)used / hard_limit * 100.0;
        if (used_pct > 100.0) used_pct = 100.0;
        if (used_pct < 0.0) used_pct = 0.0;

        int bar_width = 40;
        int filled = (int)(used_pct / 100.0 * bar_width);
        std::string filled_bar = "";
        for (int i = 0; i < filled; ++i) filled_bar += "█";
        std::string empty_bar = "";
        for (int i = 0; i < bar_width - filled; ++i) empty_bar += "░";

        double cost = (*state.total_prompt_tokens * in_rate + *state.total_completion_tokens * out_rate) / 1000000.0;
        std::ostringstream cost_stream;
        cost_stream << std::fixed << std::setprecision(4) << cost;
        std::string cost_str = cost_stream.str();

        body = vbox({
            text(""),
            hbox({
                text("  "),
                vbox({
                    text("⎔ CONTEXT WINDOW") | bold | color(accent2(theme)),
                    hbox({
                        text("Model/Provider: ") | dim,
                        text(prov_name + " / " + mod_name) | bold
                    }),
                    hbox({
                        text("Usage: ") | dim,
                        text(std::to_string(used) + " / " + std::to_string(hard_limit) + " tokens (" + std::to_string((int)used_pct) + "%)")
                    }),
                    hbox({
                        text("[") | dim,
                        text(filled_bar) | color(used_pct >= 90 ? Color::Red : (used_pct >= 70 ? Color::Yellow : accent2(theme))),
                        text(empty_bar) | dim,
                        text("]") | dim
                    }),
                    separatorLight() | color(accent(theme)),
                    text("⎔ SESSION STATS") | bold | color(accent2(theme)),
                    hbox({ text("Prompt Tokens: ") | dim, text(std::to_string(*state.total_prompt_tokens)) }),
                    hbox({ text("Completion Tokens: ") | dim, text(std::to_string(*state.total_completion_tokens)) }),
                    hbox({ text("Total Tokens: ") | dim, text(std::to_string(*state.total_tokens)) }),
                    hbox({ text("Tool Calls: ") | dim, text(std::to_string(*state.tool_call_count)) }),
                    hbox({ text("Tool Time: ") | dim, text(std::to_string(static_cast<int>(*state.total_tool_time_ms)) + " ms") }),
                    hbox({ text("Estimated Cost: ") | dim, text("$" + cost_str) | color(Color::Green) | bold }),
                }) | flex,
                text("  ")
            }),
            text("")
        }) | borderRounded | color(accent(theme)) | size(WIDTH, LESS_THAN, 80) | hcenter | flex;
    }

    auto main_layout = vbox({
        header,
        body | flex,
        footer,
    }) | flex;

    // Overlay popups using dbox (stacked overlay layout) to match fluidity of opencode
    if (show_model_select) {
        return dbox({
            main_layout,
            clear_under(build_model_popup(model_entries, model_select_idx, selected_provider, selected_model, model_query, theme)) | center
        });
    }
    if (show_session_select) {
        return dbox({
            main_layout,
            clear_under(build_session_popup(session_entries, session_select_idx, *state.session_id, session_query, theme)) | center
        });
    }
    if (show_theme_select) {
        return dbox({
            main_layout,
            clear_under(build_theme_popup(theme_entries, theme_select_idx, *state.theme, theme_query, theme)) | center
        });
    }
    if (show_variant_select) {
        std::string active = state.reasoning_mode ? *state.reasoning_mode : std::string{};
        if (active.empty()) {
            active = qcode::ProviderTransform::default_variant(
                providers_list[selected_provider].models[selected_model]);
            if (active == "off") active.clear();
        }
        return dbox({
            main_layout,
            clear_under(build_variant_popup(variant_entries, variant_select_idx,
                                            active, variant_query, theme)) |
                center
        });
    }
    if (show_palette) {
        return dbox({
            main_layout,
            clear_under(build_palette_popup(palette_entries, palette_select_idx,
                                            palette_query, theme)) |
                center
        });
    }
    if (show_help) {
        return dbox({
            main_layout,
            clear_under(build_help_popup(theme)) | center
        });
    }

    return main_layout;
}


ftxui::Dimensions stable_terminal_size() {
  static ftxui::Dimensions stable{0, 0};
  static ftxui::Dimensions candidate{0, 0};
  static int stable_frames = 0;
  const auto current = ftxui::Terminal::Size();
  if (current.dimx <= 0 || current.dimy <= 0) {
    // Ignore bogus reads (e.g. PTY temporarily reports 0).
    return stable;
  }
  if (current.dimx != candidate.dimx || current.dimy != candidate.dimy) {
    candidate = current;
    stable_frames = 0;
  } else if (++stable_frames >= 2) {
    stable = candidate;
  }
  if (stable.dimx == 0 && stable.dimy == 0) stable = candidate;
  return stable;
}

}  // namespace tui
}  // namespace qcode
