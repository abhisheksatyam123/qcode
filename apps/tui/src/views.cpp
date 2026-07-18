#include <qcode/render/message_render.h>
#include <views.h>
#include <qcode/session/session_store.h>
#include <qcode/logger/logger.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <array>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <climits>
#include <unordered_map>

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

}  // namespace

// Compact token-count formatter: 1234 -> "1234", 128000 -> "128k",
// 2'000'000 -> "2M". Used for the always-visible context usage / window.
static std::string format_tokens(int n) {
  if (n >= 1'000'000) return std::to_string(n / 1'000'000) + "M";
  if (n >= 10'000) return std::to_string(n / 1'000) + "k";
  return std::to_string(n);
}

// Soft amber used for pending / queued chrome (readable on dark terminals).
static Color queue_amber() { return Color::RGB(0xE8, 0xB8, 0x4A); }

// Grok-style queued prompt card: looks like a user turn, dimmed, with a
// position badge so the queue is visible in the message list.
static Element render_queued_prompt(const std::string& prompt_body,
                                    size_t index, size_t total,
                                    const std::string& /*theme*/) {
    Elements body_lines;
    std::istringstream iss(prompt_body);
    std::string line;
    bool any = false;
    while (std::getline(iss, line)) {
        any = true;
        body_lines.push_back(
            hbox({text("  "), text(line) | dim | color(Color::GrayLight)}));
    }
    if (!any) {
        body_lines.push_back(hbox({text("  "), text("(empty)") | dim}));
    }

    auto header = hbox({
        text("❯ ") | bold | color(queue_amber()),
        text("You") | bold | color(queue_amber()),
        text("  ·  ") | dim,
        text("queued") | dim | color(queue_amber()),
        text("  #" + std::to_string(index + 1) + "/" +
             std::to_string(total)) |
            dim | color(Color::GrayDark),
    });

    return vbox({
        header,
        vbox(std::move(body_lines)),
        text(""),
    });
}

// Raw helper to get git diff of the selected file
// Quote a path as a single-quoted POSIX shell literal (escapes embedded
// single quotes) so a file path containing shell metacharacters cannot inject
// commands when we run git via popen.
static std::string shell_quote(const std::string& s) {
  std::string out = "'";
  for (char c : s) {
    if (c == '\'') {
      out += '\'';
      out += '\\';
      out += '\'';
      out += '\'';
    } else {
      out += c;
    }
  }
  out += "'";
  return out;
}

static std::string get_file_diff_raw(const std::string& path) {
    if (path.empty()) return "";

    static constexpr size_t kMaxPreviewBytes = 2 * 1024 * 1024;
    std::string diff_output;
    diff_output.reserve(64 * 1024);
    bool truncated = false;
    std::array<char, 512> buffer;
    
    // Check if the file is tracked
    std::string check_cmd = "git ls-files --error-unmatch " + shell_quote(path) + " 2>/dev/null";
    FILE* check_pipe = popen(check_cmd.c_str(), "r");
    bool is_tracked = false;
    if (check_pipe) {
        is_tracked = (pclose(check_pipe) == 0);
    }

    std::string cmd;
    if (is_tracked) {
        // Tracked file: show diff against HEAD (unstaged + staged changes)
        cmd = "git diff HEAD -- " + shell_quote(path) + " 2>/dev/null";
    } else {
        // Untracked file: show diff as a new file (all lines added)
        cmd = "git diff --no-index /dev/null " + shell_quote(path) + " 2>/dev/null";
    }

    FILE* pipe = popen(cmd.c_str(), "r");
    if (pipe) {
        while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
            const auto remaining = kMaxPreviewBytes - diff_output.size();
            if (remaining == 0) {
                truncated = true;
                continue;
            }
            diff_output.append(
                buffer.data(), std::min(remaining, std::strlen(buffer.data())));
            truncated = truncated ||
                        std::strlen(buffer.data()) > remaining;
        }
        pclose(pipe);
    }

    // Fallback if diff is empty (e.g., untracked new file outside git index entirely)
    if (diff_output.empty() && std::filesystem::exists(path)) {
        std::ifstream file(path);
        if (file.is_open()) {
            std::string line;
            while (std::getline(file, line)) {
                if (diff_output.size() + line.size() + 2 >
                    kMaxPreviewBytes) {
                    truncated = true;
                    break;
                }
                diff_output += "+" + line + "\n";
            }
        }
    }
    if (truncated) {
        diff_output += "\n[Diff preview truncated at 2 MiB]\n";
    }

    return diff_output;
}

// Keep previews bounded: diffs can be multi-megabyte and the old cache retained
// every file ever viewed for the lifetime of the process.
static const std::string& get_file_diff(const std::string& path,
                                        size_t files_revision) {
    struct CacheEntry {
        std::string content;
        std::filesystem::file_time_type modified_at{};
        size_t last_used = 0;
    };
    static std::unordered_map<std::string, CacheEntry> cache;
    static size_t cache_bytes = 0;
    static size_t use_counter = 0;
    static size_t cached_revision = 0;
    static constexpr size_t kMaxCacheEntries = 16;
    static constexpr size_t kMaxCacheBytes = 4 * 1024 * 1024;
    static const std::string kEmpty;

    if (path.empty()) return kEmpty;
    if (cached_revision != files_revision) {
        cache.clear();
        cache_bytes = 0;
        cached_revision = files_revision;
    }

    auto& entry = cache[path];
    entry.last_used = ++use_counter;
    auto needs_update = entry.content.empty();
    try {
        if (std::filesystem::exists(path)) {
            const auto current_time = std::filesystem::last_write_time(path);
            needs_update = needs_update || entry.modified_at != current_time;
            entry.modified_at = current_time;
        }
    } catch (...) {
        needs_update = true;
    }

    if (needs_update) {
        cache_bytes -= entry.content.size();
        entry.content = get_file_diff_raw(path);
        cache_bytes += entry.content.size();
    }

    while (cache.size() > kMaxCacheEntries || cache_bytes > kMaxCacheBytes) {
        auto oldest = cache.end();
        for (auto it = cache.begin(); it != cache.end(); ++it) {
            if (it->first == path) continue;
            if (oldest == cache.end() ||
                it->second.last_used < oldest->second.last_used) {
                oldest = it;
            }
        }
        if (oldest == cache.end()) break;
        cache_bytes -= oldest->second.content.size();
        cache.erase(oldest);
    }
    return entry.content;
}

ftxui::Element render_logo() {
    auto cyan  = Color::RGB(22, 184, 243);
    auto blue  = Color::RGB(72, 124, 255);
    return vbox({
        hbox({ text("        ") | color(cyan), text("                         ") | color(blue) }),
        hbox({ text("█▀▀█    ") | color(cyan), text("  ▀▀▀▀ █▀▀█ █▀▀▄ █▀▀ ") | color(blue) | bold }),
        hbox({ text("█  █ ▀▀ ") | color(cyan), text("  █    █  █ █  █ ▀▀ ") | color(blue) | bold }),
        hbox({ text("▀▀▀█▀   ") | color(cyan), text("  ▀▀▀▀ ▀▀▀▀ ▀▀▀  ▀▀▀ ") | color(blue) | bold }),
    }) | hcenter;
}

// Helper to render code/diff content line by line to preserve formatting and highlight changes
static ftxui::Element render_diff_content(const std::string& content) {
    using namespace ftxui;
    Elements lines;
    std::stringstream ss(content);
    std::string line;
    while (std::getline(ss, line)) {
        Color c = Color::Default;
        if (!line.empty()) {
            if (line[0] == '+') c = Color::Green;
            else if (line[0] == '-') c = Color::Red;
            else if (line[0] == '@') c = Color::Cyan;
        }
        lines.push_back(text(line) | color(c));
    }
    return vbox(std::move(lines));
}

// Forward declarations of popups with theme support
ftxui::Element build_model_popup(
    const std::vector<ModelEntry>& entries,
    int select_idx,
    int selected_provider,
    int selected_model,
    const std::string& query,
    const std::string& theme
);
ftxui::Element build_session_popup(
    const std::vector<session::SessionInfo>& entries,
    int select_idx,
    const std::string& active_session_id,
    const std::string& query,
    const std::string& theme
);
ftxui::Element build_theme_popup(
    const std::vector<ThemeEntry>& entries,
    int select_idx,
    const std::string& active_theme,
    const std::string& query,
    const std::string& theme
);




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
        for (int i = 0; i < static_cast<int>(entries.size()); i++) {
            const auto& e = entries[i];
            bool active = (e.name == active_theme);
            std::string marker = (i == select_idx) ? " ▶ " : (active ? " ● " : "   ");
            std::string line_text = marker + e.name + " (" + e.description + ")";

            auto line = text(line_text);
            if (i == select_idx)
                line = line | bgcolor(bg_popup()) | bold;
            else if (active)
                line = line | color(accent2(theme));
            lines.push_back(line);
        }
    }

    lines.push_back(separatorLight());
    lines.push_back(text(" ↑↓ navigate  Enter select  Esc cancel") | dim);

    return vbox(std::move(lines)) | border | bgcolor(bg_popup()) |
           size(WIDTH, EQUAL, 72) | hcenter;
}

// ── Basic markdown rendering (code blocks, bold, inline code, lists) ──
static std::vector<std::string> split_lines(const std::string& s) {
    std::vector<std::string> lines;
    std::istringstream ss(s);
    std::string line;
    while (std::getline(ss, line)) {
        lines.push_back(line);
    }
    return lines;
}

// ── Basic markdown rendering (code blocks, inline code, bold, lists) ──

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
) {
    std::string theme = state.theme ? *state.theme : "orange";

    // ── Header strip: tabs + model badge + token count + status ──
    std::string hdr_model = providers_list[selected_provider].models[selected_model].name;
    const auto& hdr_model_info = providers_list[selected_provider].models[selected_model];
    // Show the CURRENT context sent for this turn against the model window.
    // (state.current_context_tokens is the per-request snapshot; state.total_tokens
    // is the session lifetime total and is shown in the Stats tab instead.)
    std::string hdr_tokens;
    int ctx_used = *state.current_context_tokens;
    int ctx_pct = 0;
    Color ctx_color = Color::Default;
    if (hdr_model_info.context_window > 0) {
      if (ctx_used > 0) {
        ctx_pct = static_cast<int>(ctx_used * 100 /
                                   hdr_model_info.context_window);
        hdr_tokens = format_tokens(ctx_used) + " / " +
                     format_tokens(hdr_model_info.context_window) + " (" +
                     std::to_string(ctx_pct) + "%)";
        ctx_color = ctx_pct >= 85   ? Color::Red
                    : ctx_pct >= 70 ? Color::Yellow
                                    : accent2(theme);
      } else {
        hdr_tokens = format_tokens(hdr_model_info.context_window) + " tok";
      }
    } else {
      hdr_tokens = format_tokens(ctx_used > 0 ? ctx_used : *state.total_tokens) + " tok";
    }
    
    // Status (compact, no spinner — spinner is rendered in the header below)
    Color status_color = accent2(theme);
    std::string hdr_status;
    const bool agent_busy =
        *state.is_generating ||
        (state.status && (*state.status == "generating" ||
                          *state.status == "agent"));
    if (agent_busy) {
        static const std::array<const char*, 10> sp = {
            "\u280b", "\u2819", "\u2839", "\u2838", "\u283c",
            "\u2834", "\u2836", "\u2837", "\u280f", "\u280b"
        };
        int frame = *state.generation_frame % sp.size();
        const bool is_agent =
            state.status && *state.status == "agent";
        hdr_status = std::string(sp[frame]) +
                     (is_agent ? " agent..." : " gen...");
    } else if (state.status && *state.status == "error") {
        hdr_status = "\u26a0 error";
        status_color = Color::RGB(0xCC, 0x33, 0x33);
    } else if (state.status && *state.status == "warn") {
        // "warn" reflects an empty model response (see chat_bus.cpp)
        hdr_status = "\u26a0 empty";
        status_color = Color::Yellow;
    } else if (state.retry_available && *state.retry_available) {
        hdr_status = "r retry";
        status_color = Color::Yellow;
    }

    // Prompt queue indicator (hourglass + count)
    std::string hdr_queue;
    if (state.queued_prompts && *state.queued_prompts > 0) {
        hdr_queue = "\u29d6 " + std::to_string(*state.queued_prompts) + " queued";
    }

    
    std::string hdr_session;
    if (state.session_title && !state.session_title->empty()) {
        hdr_session = *state.session_title;
        if (hdr_session.size() > 28) {
            hdr_session = hdr_session.substr(0, 27) + "…";
        }
    } else if (state.session_id && state.session_id->size() >= 8) {
        hdr_session = state.session_id->substr(0, 8);
    }

    auto header = hbox({
        text(" QCODE ") | bold | bgcolor(accent(theme)) | color(Color::White),
        text("  "),
        tab_toggle->Render(),
        filler(),
        // Right side: session, model, tokens, status
        (hdr_session.empty()
             ? emptyElement()
             : hbox({text(" " + hdr_session + " ") | dim, separatorLight()})),
        text(" " + hdr_model + " ") | color(accent2(theme)) | bold,
        separatorLight(),
        text(" " + hdr_tokens + " ") |
            (ctx_pct > 0 ? color(ctx_color) : dim),
        (ctx_pct >= 70
             ? hbox({separatorLight(),
                     text(" /compact? ") | color(Color::Yellow) | dim})
             : emptyElement()),
        (hdr_queue.empty() ? emptyElement() : hbox({
            separatorLight(),
            text(" " + hdr_queue + " ") | color(queue_amber()) | bold,
        })),
        (hdr_status.empty() ? emptyElement() : hbox({
            separatorLight(),
            text(" " + hdr_status + " ") | color(status_color) | bold,
        })),
    }) | borderLight | color(accent(theme));

    Element body;

    // ── Tab 0: Chat ──
    if (state.tab_selected == 0) {
        bool empty = state.messages_history->empty();

        if (empty) {
            // Home screen
            auto prompt_bar = hbox({
                text(" ❯ ") | color(accent2(theme)) | bold,
                input->Render() | yframe | size(HEIGHT, LESS_THAN, 6) | flex,
            }) | border | color(accent(theme));

            body = vbox({
                filler() | flex,
                render_logo(),
                text("") | size(HEIGHT, EQUAL, 1),
                text("What can I help you build today?") | dim | hcenter,
                prompt_bar | size(WIDTH, EQUAL, 80) | hcenter,
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
            const auto terminal_width = Terminal::Size().dimx;
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
                msgs.push_back(
                separatorLight() | color(
                    msg.role == kMessageRoleAssistant ? accent(theme) :
                    msg.role == kMessageRoleUser ? user_green() :
                    dim_gray()));
                // Skip the paired result row when it is also inside this window.
                if (adjacent_tool_results != nullptr && i + 1 < last) ++i;
            }
            if (last < history_size) {
                msgs.push_back(
                    text(" " + std::to_string(history_size - last) +
                         " later messages · PageDown/End to view ") |
                    dim | hcenter);
            }

            // Queued prompts appear at the end of the message list (Grok-style)
            // so pending turns are first-class chat rows, not only a header badge.
            const auto* queue =
                state.queued_prompt_texts ? state.queued_prompt_texts.get()
                                          : nullptr;
            if (queue && !queue->empty() && last >= history_size) {
                msgs.push_back(hbox({
                    text(" ─── ") | dim | color(queue_amber()),
                    text("queue") | bold | color(queue_amber()),
                    text(" · " + std::to_string(queue->size()) +
                         " waiting · Esc clears queue ") |
                        dim | color(Color::GrayDark),
                    text("───") | dim | color(queue_amber()),
                    filler(),
                }));
                for (size_t qi = 0; qi < queue->size(); ++qi) {
                    msgs.push_back(
                        render_queued_prompt((*queue)[qi], qi, queue->size(),
                                             theme));
                    msgs.push_back(separatorLight() | color(queue_amber()) |
                                   dim);
                }
            }

            // Clean input bar: prompt prefix + input; subtle queue hint when busy
            Elements prompt_row = {
                text(" ❯ ") | color(accent2(theme)) | bold,
                // Wrap input render in a vertical frame constrained to max 6 lines for scrolling
                input->Render() | yframe | size(HEIGHT, LESS_THAN, 6) | flex,
            };
            auto prompt_inner = hbox(std::move(prompt_row));
            Element prompt_box = prompt_inner | border | color(accent(theme));
            if (queue && !queue->empty()) {
                prompt_box = vbox({
                    prompt_box,
                    hbox({
                        text("  "),
                        text("\u29d6 " + std::to_string(queue->size()) +
                             " queued") |
                            dim | color(queue_amber()),
                        text("  ·  next runs when this turn finishes") | dim,
                        filler(),
                        text("Esc cancels queue") | dim | color(Color::GrayDark),
                        text("  "),
                    }),
                });
            }

            // Dynamic inline slash command / session autocomplete matching opencode
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
                    rows.push_back(text(" Select Session to Load:") | bold | color(accent2(theme)));
                    rows.push_back(separatorLight() | color(accent(theme)));
                    for (int i = 0; i < static_cast<int>(matches.size()); ++i) {
                        bool active = (state.slash_suggestion_mode && state.slash_suggestion_idx == i);
                        std::string marker = active ? " ▶ " : "   ";
                        auto row = hbox({
                            text(marker + matches[i].first) | color(active ? accent2(theme) : Color::Default) | bold,
                            text("  (" + matches[i].second + ")") | dim
                        });
                        if (active) {
                            row = row | bgcolor(bg_popup()) | bold;
                        }
                        rows.push_back(row);
                    }
                    auto suggestions_panel = vbox(std::move(rows)) | border | color(accent(theme));
                    prompt_box = vbox({
                        suggestions_panel,
                        prompt_box
                    });
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
                    rows.push_back(text(" Autocomplete Commands:") | bold | color(accent2(theme)));
                    rows.push_back(separatorLight() | color(accent(theme)));
                    for (int i = 0; i < static_cast<int>(matches.size()); ++i) {
                        bool active = (state.slash_suggestion_mode && state.slash_suggestion_idx == i);
                        std::string marker = active ? " ▶ " : "   ";
                        auto row = hbox({
                            text(marker + "/" + matches[i].name) | color(active ? accent2(theme) : Color::Default) | bold,
                            text("  " + matches[i].description) | dim
                        });
                        if (active) {
                            row = row | bgcolor(bg_popup()) | bold;
                        }
                        rows.push_back(row);
                    }
                    auto suggestions_panel = vbox(std::move(rows)) | border | color(accent(theme));
                    prompt_box = vbox({
                        suggestions_panel,
                        prompt_box
                    });
                }
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
                // status removed — shown in header strip instead
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

            auto file_header = hbox({
                text(" ← Esc ") | dim,
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
        int hard_limit = 200000;  // fallback; overridden below from model config
        int used = *state.current_context_tokens > 0 ? *state.current_context_tokens : *state.total_tokens;
        double used_pct = (double)used / hard_limit * 100.0;
        if (used_pct > 100.0) used_pct = 100.0;

        int bar_width = 40;
        int filled = (int)(used_pct / 100.0 * bar_width);
        std::string filled_bar = "";
        for (int i = 0; i < filled; ++i) filled_bar += "█";
        std::string empty_bar = "";
        for (int i = 0; i < bar_width - filled; ++i) empty_bar += "░";

        double cost = 0.0;
        std::string prov_id = providers_list[selected_provider].id;
        const auto& m = providers_list[selected_provider].models[selected_model];
        if (m.context_window > 0) hard_limit = m.context_window;
        double in_rate = m.input_cost > 0 ? m.input_cost : (prov_id == "openrouter" ? 2.50 : 3.00);
        double out_rate = m.output_cost > 0 ? m.output_cost : (prov_id == "openrouter" ? 10.00 : 15.00);
        cost = (*state.total_prompt_tokens * in_rate + *state.total_completion_tokens * out_rate) / 1000000.0;

        body = vbox({
            text(""),
            hbox({
                text("  "),
                vbox({
                    text("⎔ CONTEXT WINDOW") | bold | color(accent2(theme)),
                    hbox({
                        text("Model/Provider: ") | dim,
                        text(providers_list[selected_provider].name + " / " + providers_list[selected_provider].models[selected_model].name) | bold
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
                    hbox({ text("Estimated Cost: ") | dim, text("$" + std::to_string(cost)) | color(Color::Green) | bold }),
                }) | flex,
                text("  ")
            }),
            text("")
        }) | border | color(accent(theme)) | size(WIDTH, LESS_THAN, 80) | hcenter | flex;
    }

    auto main_layout = vbox({
        header,
        body,
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

    return main_layout;
}

// ── Model selector popup ──
ftxui::Element build_model_popup(
    const std::vector<ModelEntry>& entries,
    int select_idx,
    int selected_provider,
    int selected_model,
    const std::string& query,
    const std::string& theme
) {
    Elements lines;
    lines.push_back(text(" Select Model") | bold | color(accent2(theme)));
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
        std::string last_cat;
        for (int i = 0; i < static_cast<int>(entries.size()); i++) {
            auto& e = entries[i];
            if (e.category != last_cat) {
                if (!last_cat.empty()) lines.push_back(text(""));
                lines.push_back(text(" " + e.category) | bold | color(accent2(theme)));
                last_cat = e.category;
            }

            bool active = (e.provider_idx == selected_provider && e.model_idx == selected_model);
            std::string marker = (i == select_idx) ? " ▶ " : (active ? " ● " : "   ");
            std::string line_text = marker + e.model_name;
            if (e.model_id != e.model_name)
                line_text += "  " + e.model_id;

            auto line = text(line_text);
            if (i == select_idx)
                line = line | bgcolor(bg_popup()) | bold;
            else if (active)
                line = line | color(accent2(theme));
            lines.push_back(line);
        }
    }

    lines.push_back(separatorLight());
    lines.push_back(text(" ↑↓ navigate  Enter select  Esc cancel") | dim);

    return vbox(std::move(lines)) | border | bgcolor(bg_popup()) |
           size(WIDTH, EQUAL, 72) | hcenter;
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

    for (int i = 0; i < static_cast<int>(entries.size()); i++) {
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

    lines.push_back(separatorLight());
    lines.push_back(text(" ↑↓ navigate  Enter select  Esc cancel") | dim);

    return vbox(std::move(lines)) | border | bgcolor(bg_popup()) |
           size(WIDTH, EQUAL, 72) | hcenter;
}




// ── Toast overlay ─────────────────────────────────────────────────────────────
ftxui::Element render_toast_overlay(
    const std::vector<Toast>& toasts,
    const std::string& theme)
{
    using namespace ftxui;
    if (toasts.empty()) return emptyElement();
    
    Elements toast_elems;
    for (const auto& t : toasts) {
        Color bg;
        Color fg = Color::White;
        std::string icon;
        if (t.variant == "error") {
            bg = Color::RGB(0xCC, 0x33, 0x33);
            icon = " ✗ ";
        } else if (t.variant == "success") {
            bg = Color::RGB(0x22, 0xBB, 0x88);
            icon = " ✓ ";
        } else if (t.variant == "warning") {
            bg = Color::RGB(0xEE, 0x99, 0x22);
            icon = " ⚠ ";
        } else {
            bg = Color::RGB(0x33, 0x66, 0xCC);
            icon = " ℹ ";
        }
        Elements toast_row;
        toast_row.push_back(text(icon) | bold);
        toast_row.push_back(text(t.message));
        toast_elems.push_back(
            hbox(std::move(toast_row)) | bgcolor(bg) | color(fg)
        );
    }
    
    return vbox(std::move(toast_elems)) | size(WIDTH, LESS_THAN, 72) | hcenter;
}

} // namespace tui
} // namespace qcode

