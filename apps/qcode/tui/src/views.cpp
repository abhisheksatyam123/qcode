#include <qcode/ui/message_render.h>
#include <views.h>
#include <prompt_wrap.h>
#include "scroll_helpers.h"
#include "file_diff_preview.h"
#include "pickers.h"
#include <qcode/config/provider_info.h>
#include <qcode/core/logger.h>
#include <qcode/tools/task_tool.h>
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
#include <string_view>
#include <unordered_map>
#include <unordered_set>
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

int prompt_input_height(const std::string& prompt_input, int max_lines,
                        int width) {
    // Exact wrapped-row count (same wrap the renderer uses) — a ceil(w/avail)
    // estimate under-counts rows for word wrapping and over-counts for CJK.
    return wrapped_line_count(prompt_input, width, max_lines);
}

// Move every box to an empty sentinel (x_min > x_max never contains a
// point); nodes laid out this frame write real coordinates back in SetBox.
template <typename Map>
void park_hit_boxes(Map* boxes) {
    if (!boxes) return;
    for (auto& entry : *boxes) entry.second = HitBox{0, -1, 0, -1};
}

// Tool calls and their results are separate history rows, and a parallel
// batch lands as [callA][callB][resA][resB], so results pair with their call
// by id rather than by adjacency.
struct ToolPairing {
    // Call row -> later rows holding results for its calls.
    std::unordered_map<size_t, std::vector<size_t>> result_rows;
    std::vector<char> consumed;  // result-only row drawn by its call rows
    std::vector<char> pending;   // call row with a call still awaiting a result
};

// Pairs every tool result with its call row.
void build_tool_pairing(const qcode::Messages& history, ToolPairing& pairing) {
    pairing.result_rows.clear();
    pairing.consumed.assign(history.size(), 0);
    pairing.pending.assign(history.size(), 0);

    std::unordered_map<std::string_view, size_t> call_row;
    std::unordered_set<std::string_view> answered;
    for (size_t i = 0; i < history.size(); ++i) {
        bool any_result = false;
        bool all_paired = true;
        for (const auto& part : history[i].content) {
            if (const auto* tp = std::get_if<qcode::ToolCallContentPart>(&part)) {
                call_row.emplace(tp->id, i);
            } else if (const auto* rp =
                           std::get_if<qcode::ToolResultContentPart>(&part)) {
                any_result = true;
                answered.insert(rp->tool_call_id);
                auto c = call_row.find(rp->tool_call_id);
                if (c == call_row.end() || c->second == i) {
                    all_paired = false;  // orphan, or drawn by its own row
                    continue;
                }
                auto& later = pairing.result_rows[c->second];
                if (later.empty() || later.back() != i) later.push_back(i);
            }
        }
        if (any_result && all_paired) pairing.consumed[i] = 1;
    }
    for (size_t i = 0; i < history.size(); ++i) {
        for (const auto& part : history[i].content) {
            const auto* tp = std::get_if<qcode::ToolCallContentPart>(&part);
            if (tp != nullptr && answered.count(tp->id) == 0) pairing.pending[i] = 1;
        }
    }
}

}  // namespace

int prompt_box_inner_width(const ChatState& state) {
    const int term_w = std::max(20, stable_terminal_size().dimx);
    int box_w = term_w;
    if (state.tab_selected == 0 &&
        (!state.messages_history || state.messages_history->empty())) {
        // Empty chat: the prompt box is clamped (see render_view) and
        // centered; FTXUI additionally never exceeds the terminal width.
        box_w = std::min(std::clamp(term_w - 8, 48, 84), term_w);
    }
    // Minus borderRounded borders (2) and the " ❯ " prefix (3).
    return std::max(20, box_w - 5);
}

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
// Session cost for the header and the Stats tab: the sum of every call priced
// at its own model's opencode.json rates when it ran (cost.input/output/
// cache_read/cache_write), so switching models never reprices earlier calls.
// Sessions recorded before per-call pricing are estimated at the current
// model's prices (flagged estimated). Unpriced models add nothing.
static session::SessionCost session_cost_view(const ChatState& state, const ModelInfo* model) {
    return session::session_cost(*state.usage, model, *state.total_prompt_tokens,
                                 *state.total_completion_tokens);
}

// Share of the context window: one decimal under 10% (a 1M window shows
// "0.3%", not "0%"), whole percents above - like the WebUI pctLabel.
static std::string format_context_percent(long long used, int window_size) {
  if (window_size <= 0) return {};
  const double pct = static_cast<double>(used) * 100.0 / window_size;
  std::ostringstream os;
  os << std::fixed << std::setprecision(pct > 0.0 && pct < 10.0 ? 1 : 0)
     << (pct > 0.0 && pct < 10.0 ? pct : static_cast<double>(static_cast<long long>(pct)))
     << "%";
  return os.str();
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
  if (window_size > 0) text += " (" + format_context_percent(total, window_size) + ")";
  if (have_cost && cost > 0) {
    std::ostringstream os;
    os << "$" << std::fixed << std::setprecision(4) << cost;
    text += " · " + os.str();
  }
  return text;
}

// ── Stats tab ─────────────────────────────────────────────────────────────
namespace {
std::string fmt_ms(double ms) {
    char buf[32];
    if (ms >= 10000.0) {
        std::snprintf(buf, sizeof(buf), "%.1f s", ms / 1000.0);
    } else {
        std::snprintf(buf, sizeof(buf), "%lld ms", static_cast<long long>(ms));
    }
    return buf;
}
std::string fmt_usd(double usd) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), usd >= 100.0 ? "$%.2f" : "$%.4f", usd);
    return buf;
}
std::string fmt_rate(double per_mtok) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), per_mtok < 1.0 ? "$%.3f" : "$%.2f", per_mtok);
    return buf;
}
std::string fmt_pct(double part, double whole) {
    if (whole <= 0.0) return "-";
    return std::to_string(static_cast<int>(part * 100.0 / whole + 0.5)) + "%";
}
}  // namespace

static Element build_stats_tab(const ChatState& state,
                               const std::vector<ProviderInfo>& providers_list,
                               int selected_provider, int selected_model,
                               const std::string& theme) {
    const auto row = [](const std::string& label, Element value) {
        return hbox({text(label) | dim | size(WIDTH, EQUAL, 20), std::move(value)});
    };
    const auto row_text = [&](const std::string& label, const std::string& value) {
        return row(label, text(value));
    };
    const auto section = [&](const std::string& title) {
        return text(title) | bold | color(accent2(theme));
    };

    const ProviderInfo* prov = nullptr;
    const ModelInfo* model = nullptr;
    if (selected_provider >= 0 && selected_provider < static_cast<int>(providers_list.size())) {
        prov = &providers_list[selected_provider];
        if (selected_model >= 0 && selected_model < static_cast<int>(prov->models.size())) {
            model = &prov->models[selected_model];
        }
    }
    const ModelInfo empty_model;
    const ModelInfo& m = model ? *model : empty_model;
    const auto& usage = *state.usage;
    Elements rows;

    // ── Model & variant (what the next request sends) ──
    rows.push_back(section("⎔ MODEL"));
    rows.push_back(row("Provider / Model",
                       text((prov ? prov->name : std::string("Unknown")) + " / " +
                            (model ? m.name : std::string("Unknown"))) | bold));
    {
        const std::string rm = state.reasoning_mode ? *state.reasoning_mode : std::string{};
        const std::string variant = model ? ProviderTransform::resolve_session_variant(m, rm)
                                          : (rm.empty() ? std::string("off") : rm);
        std::string line = variant;
        if (model && variant != "off") {
            const std::string wire = ProviderTransform::variant_wire_effort(m, variant);
            if (!wire.empty() && wire != variant) line += "  → effort " + wire;
            if (const auto* spec = ProviderTransform::find_variant(m, variant)) {
                if (spec->max_tokens > 0) {
                    const int cap = m.output_limit > 0 ? std::min(spec->max_tokens, m.output_limit)
                                                       : spec->max_tokens;
                    line += " · max_tokens " + std::to_string(cap);
                }
                if (spec->budget_tokens > 0) {
                    line += " · budget " + std::to_string(spec->budget_tokens);
                }
                if (!spec->prompt.empty()) line += " · +prompt";
            }
        }
        rows.push_back(row("Variant", text(line) | color(accent(theme))));
        if (model) {
            // What the next request asks for: opencode.json max_tokens capped
            // by limit.output, raised by the variant (see apply_variant_options).
            int budget = ProviderTransform::max_output_tokens(m).value_or(0);
            if (const auto* spec = ProviderTransform::find_variant(m, variant);
                spec != nullptr && spec->max_tokens > 0 && variant != "off") {
                const int cap = m.output_limit > 0 ? std::min(spec->max_tokens, m.output_limit)
                                                   : spec->max_tokens;
                budget = std::max(budget, cap);
            }
            std::string out = budget > 0 ? format_grouped(budget) + " tokens"
                                         : std::string("provider default");
            out += m.output_limit > 0 ? "  (limit.output " + format_grouped(m.output_limit) + ")"
                                      : std::string("  (limit.output not set)");
            rows.push_back(row_text("Output Budget", out));
        }
        if (!m.thinking_type.empty()) {
            std::string thinking = m.thinking_type;
            if (!m.thinking_display.empty()) thinking += " (" + m.thinking_display + ")";
            if (!m.thinking_allow_off) thinking += " · always on";
            rows.push_back(row_text("Thinking", thinking));
        }
        const bool free_model = m.cost_configured && m.input_cost <= 0.0 &&
                                m.output_cost <= 0.0 && m.cache_read_cost <= 0.0 &&
                                m.cache_write_cost <= 0.0;
        if (model && free_model) {
            rows.push_back(row_text("Price / 1M tok", "free (opencode.json cost 0)"));
        } else if (model && (m.cost_configured || m.input_cost > 0.0 || m.output_cost > 0.0)) {
            std::string prices =
                fmt_rate(m.input_cost) + " in · " + fmt_rate(m.output_cost) + " out";
            if (m.cache_read_cost > 0.0) prices += " · " + fmt_rate(m.cache_read_cost) + " cache read";
            if (m.cache_write_cost > 0.0) prices += " · " + fmt_rate(m.cache_write_cost) + " cache write";
            rows.push_back(row_text("Price / 1M tok", prices));
        } else {
            rows.push_back(row("Price / 1M tok", text("not set (opencode.json cost)") | dim));
        }
    }

    // ── Context window ──
    rows.push_back(separatorLight() | color(accent(theme)));
    rows.push_back(section("⎔ CONTEXT WINDOW"));
    {
        const int window = m.context_window;  // opencode.json limit.context only
        // Live estimate, else the last prompt this process saw, else the
        // persisted prompt of the session's latest call (resumed sessions).
        const int used = *state.current_context_tokens > 0     ? *state.current_context_tokens
                         : *state.last_actual_prompt_tokens > 0 ? *state.last_actual_prompt_tokens
                                                                : std::max(0, usage.last_input_tokens);
        if (window > 0) {
            const double pct =
                std::clamp(static_cast<double>(used) * 100.0 / window, 0.0, 100.0);
            rows.push_back(row_text("Usage", format_grouped(used) + " / " +
                                                 format_grouped(window) + " tokens (" +
                                                 std::to_string(static_cast<int>(pct)) + "%)"));
            constexpr int kBarWidth = 40;
            const int filled = static_cast<int>(pct / 100.0 * kBarWidth);
            std::string filled_bar;
            std::string empty_bar;
            for (int i = 0; i < filled; ++i) filled_bar += "█";
            for (int i = filled; i < kBarWidth; ++i) empty_bar += "░";
            rows.push_back(hbox({text("[") | dim,
                                 text(filled_bar) | color(pct >= 90   ? Color::Red
                                                          : pct >= 70 ? Color::Yellow
                                                                      : accent2(theme)),
                                 text(empty_bar) | dim, text("]") | dim}));
        } else {
            rows.push_back(row_text("Usage", format_grouped(used) +
                                                 " tokens (window unknown: set limit.context)"));
        }
    }

    // ── Tokens ──
    rows.push_back(separatorLight() | color(accent(theme)));
    rows.push_back(section("⎔ TOKENS (this session)"));
    if (!usage.empty()) {
        rows.push_back(row_text("Input (billed)", format_grouped(usage.input_tokens)));
        rows.push_back(row_text(
            "  cache read",
            format_grouped(usage.cache_read_tokens) + "  (" +
                fmt_pct(static_cast<double>(usage.cache_read_tokens),
                        static_cast<double>(usage.input_tokens)) +
                " hit)"));
        rows.push_back(row_text("  cache write", format_grouped(usage.cache_write_tokens)));
        rows.push_back(row_text("Output", format_grouped(usage.output_tokens)));
        rows.push_back(row_text("  thinking", format_grouped(usage.reasoning_tokens)));
    } else {
        rows.push_back(row_text("Prompt", format_grouped(*state.total_prompt_tokens)));
        rows.push_back(row_text("Completion", format_grouped(*state.total_completion_tokens)));
        rows.push_back(text("  per-call input/cache figures start with the next turn") | dim);
    }
    rows.push_back(row_text("Tool Calls", std::to_string(*state.tool_call_count) + "  (" +
                                              fmt_ms(*state.total_tool_time_ms) + ")"));

    // ── Cost ──
    rows.push_back(separatorLight() | color(accent(theme)));
    rows.push_back(section("⎔ COST (each call at its model's list price)"));
    {
        const session::SessionCost cost = session_cost_view(state, model);
        if (cost.available) {
            rows.push_back(row("Total", text(fmt_usd(cost.total)) | color(Color::Green) | bold));
            rows.push_back(row_text("  input", fmt_usd(cost.parts.input)));
            if (!cost.estimated || usage.cache_read_tokens + usage.cache_write_tokens > 0) {
                rows.push_back(row_text("  cache read", fmt_usd(cost.parts.cache_read)));
                rows.push_back(row_text("  cache write", fmt_usd(cost.parts.cache_write)));
            }
            rows.push_back(row_text("  output", fmt_usd(cost.parts.output)));
            if (cost.estimated) {
                rows.push_back(text("  estimate at the current model's price (recorded "
                                    "before per-call pricing)") | dim);
            }
        } else if (usage.empty() && *state.total_prompt_tokens + *state.total_completion_tokens == 0) {
            rows.push_back(row("Total", text(fmt_usd(0.0)) | color(Color::Green)));
        } else {
            rows.push_back(text("  No price: add cost.input/output/cache_read/"
                                "cache_write to opencode.json") | dim);
        }
        if (cost.available && cost.unpriced_calls > 0) {
            rows.push_back(text("  " + std::to_string(cost.unpriced_calls) +
                                " call(s) on models without a price are not included") | dim);
        }
        // Delegated work is billed in the child sessions (task tool).
        if (state.subagent_usage && state.subagent_usage->sessions > 0) {
            const auto& sub = *state.subagent_usage;
            std::string line = std::to_string(sub.sessions) + " session" +
                               (sub.sessions == 1 ? "" : "s") + " · " +
                               std::to_string(sub.usage.model_calls) + " calls · ";
            line += sub.usage.priced_calls > 0 ? fmt_usd(sub.usage.cost.total())
                                               : std::string("no price");
            rows.push_back(row_text("Subagents", line));
            if (cost.available && sub.usage.priced_calls > 0) {
                rows.push_back(row(
                    "Total incl. subagents",
                    text(fmt_usd(cost.total + sub.usage.cost.total())) | color(Color::Green)));
            }
        }
        if (cost.legacy_calls > 0 && !cost.estimated) {
            rows.push_back(text("  " + std::to_string(cost.legacy_calls) +
                                " earlier call(s) predate per-call pricing; not included") | dim);
        }
        // Per-model share: makes model switches inside a session visible.
        if (!usage.by_model.empty()) {
            rows.push_back(text("  by model") | dim);
            for (const auto& [key, share] : usage.by_model) {
                std::string line = std::to_string(share.calls) + " call" +
                                   (share.calls == 1 ? "" : "s") + " · " +
                                   format_grouped(share.input_tokens) + " in · " +
                                   format_grouped(share.output_tokens) + " out · ";
                line += share.cost.priced ? fmt_usd(share.cost.total()) : std::string("no price");
                if (share.cost.priced && share.unpriced_calls > 0) {
                    line += " (+" + std::to_string(share.unpriced_calls) + " unpriced)";
                }
                rows.push_back(hbox({text("  " + key) | color(accent(theme)),
                                     text("  " + line) | dim}));
            }
        }
    }

    // ── Latency ──
    rows.push_back(separatorLight() | color(accent(theme)));
    rows.push_back(section("⎔ LATENCY"));
    if (usage.empty()) {
        rows.push_back(row_text("Model Calls", "0"));
    } else {
        rows.push_back(row_text("Model Calls", std::to_string(usage.model_calls)));
        rows.push_back(row_text("Avg Call", fmt_ms(usage.model_ms_total / usage.model_calls)));
        rows.push_back(row_text("Avg First Token",
                                usage.ttft_count > 0
                                    ? fmt_ms(usage.ttft_ms_total / usage.ttft_count)
                                    : std::string("-")));
        rows.push_back(row_text(
            "Last Call",
            fmt_ms(usage.model_ms_last) +
                (usage.ttft_ms_last >= 0.0
                     ? "  (first token " + fmt_ms(usage.ttft_ms_last) + ")"
                     : std::string())));
        rows.push_back(row("Slowest Call",
                           text(fmt_ms(usage.model_ms_max)) |
                               color(usage.model_ms_max >= 60000.0   ? Color(Color::Red)
                                     : usage.model_ms_max >= 20000.0 ? Color(Color::Yellow)
                                                                     : Color(Color::Default))));
        {
            const double secs = usage.model_ms_total / 1000.0;
            char speed[48];
            std::snprintf(speed, sizeof(speed), "%.1f tok/s",
                          secs > 0.0 ? static_cast<double>(usage.output_tokens) / secs : 0.0);
            rows.push_back(row_text("Output Speed", speed));
        }
        rows.push_back(row_text("Total Model Time", fmt_ms(usage.model_ms_total)));
        std::string last = usage.last_effort.empty() ? std::string("off") : usage.last_effort;
        if (!usage.last_variant.empty() && usage.last_variant != usage.last_effort) {
            last = usage.last_variant + " → effort " + last;
        }
        rows.push_back(row_text("Last Effort Sent", last));
    }

    // Scrollable: the mouse wheel moves scroll_line; clamp to the content.
    auto content = vbox(std::move(rows));
    content->ComputeRequirement();
    const int content_height = std::max(0, content->requirement().min_y);
    const int viewport_height = std::max(1, stable_terminal_size().dimy - 7);
    const int max_scroll = qcode::tui::compute_max_scroll(content_height, viewport_height);
    *state.scroll_line = std::clamp(*state.scroll_line, 0, max_scroll);
    const int focus_y = qcode::tui::compute_focus_y(*state.scroll_line, viewport_height);
    return vbox({
               text(""),
               hbox({text("  "),
                     content | focusPosition(0, focus_y) | vscroll_indicator | yframe | flex,
                     text("  ")}) |
                   flex,
           }) |
           borderRounded | color(accent(theme)) | size(WIDTH, LESS_THAN, 90) | hcenter | flex;
}

// Soft amber used for pending / queued chrome (readable on dark terminals).
static Color queue_amber() { return Color::RGB(0xE8, 0xB8, 0x4A); }

// Compact Opencode-style queued block: amber left bar, clean indentation,
// without bulky borders, duplicate headers, or empty lines.
static Element render_queued_block(const std::vector<std::string>& queue,
                                   const std::string& /*theme*/) {
    if (queue.empty()) return text("");

    Elements rows;
    rows.push_back(
        hbox({
            text("┃ ") | bold | color(queue_amber()),
            text("⏳ Queued") | bold | color(queue_amber()),
            text(" · /clear-queue to cancel") | dim | color(theme_text_muted()),
        }));

    for (size_t qi = 0; qi < queue.size(); ++qi) {
        if (qi > 0) {
            rows.push_back(
                hbox({
                    text("┃ ") | color(queue_amber()),
                    text("  ---") | dim | color(Color::GrayDark),
                }));
        }
        std::istringstream iss(queue[qi]);
        std::string line;
        bool any = false;
        while (std::getline(iss, line)) {
            any = true;
            rows.push_back(
                hbox({
                    text("┃ ") | color(queue_amber()),
                    text("  "),
                    paragraph(line) | color(Color::GrayLight) | flex,
                }));
        }
        if (!any) {
            rows.push_back(
                hbox({
                    text("┃ ") | color(queue_amber()),
                    text("  (empty prompt)") | dim | color(Color::GrayDark),
                }));
        }
    }

    return vbox(std::move(rows));
}

ftxui::Element render_logo() {
    // Two-tone 4-row block logo spelling "q-code", using the same
    // character-cell technique as OpenCode (muted mark + bold wordmark).
    const auto muted = theme_text_muted();
    const auto fg = theme_text();
    return vbox({
        hbox({
            text("█▀▀▀█      ") | color(muted), text("█▀▀▀ █▀▀█ █▀▀▄ █▀▀▀") | color(fg) | bold,
        }),
        hbox({
            text("█   █  ▀   ") | color(muted), text("█    █  █ █  █ █▀▀▀") | color(fg) | bold,
        }),
        hbox({
            text("▀▀▀▀▀▀     ") | color(muted),text("▀▀▀▀ ▀▀▀▀ ▀▀▀▀ ▀▀▀▀") | color(fg) | bold,
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
                                  : std::max(0, state.usage->last_input_tokens));
    // opencode.json limit.context is the only source (0 = unknown: no %).
    const int window_size = hdr_model_info.context_window;
    const int last_reasoning = state.last_reasoning_tokens ? *state.last_reasoning_tokens : 0;
    const long long usage_total =
        (long long)ctx_snapshot + (long long)std::max(0, last_reasoning);
    // Session cost (same figure as the Stats tab).
    const session::SessionCost hdr_cost = session_cost_view(state, &hdr_model_info);
    const double use_cost = hdr_cost.total;
    const bool use_have_cost = hdr_cost.available;
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
    // matches opencode's per-session directory. Falls back to cwd. Resolved
    // once per session id: the lookup is a SQLite query under the DB mutex.
    static std::optional<std::string> folder_session;
    static std::string folder_cached;
    const std::string cur_session =
        state.session_id ? *state.session_id : std::string{};
    if (folder_session != cur_session) {
        folder_session = cur_session;
        std::string& hdr_folder = folder_cached;
        std::string ws;
        if (!cur_session.empty())
            ws = session::get_session_workspace(cur_session);
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
    const std::string& hdr_folder = folder_cached;

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
        ((state.agent_mode && *state.agent_mode == "subagent")
             ? text(" Subagent") | bold | color(Color::CyanLight)
             : text(" Orchestrator") | bold | color(accent2(theme))),
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
        if (state.retry_available && *state.retry_available) {
            footer_bits.push_back(text("r") | bold | color(queue_amber()));
            footer_bits.push_back(text(" retry  ") | dim | color(theme_text_muted(theme)));
        }
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
        if (state.agent_mode && *state.agent_mode == "subagent") {
            left.push_back(text(" [subagent]") | bold | color(Color::CyanLight));
        }
        if (state.return_session_id && !state.return_session_id->empty() &&
            state.session_back_box) {
            Element back = text(" ← parent (b)") | bold | color(accent2(theme));
            back = std::move(back) | reflect_box(*state.session_back_box);
            left.push_back(std::move(back));
        }
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

    // Chat hit boxes outlive frames (cached message trees reference them);
    // only boxes laid out this frame — on the chat tab — are clickable.
    park_hit_boxes(state.tool_arrow_boxes.get());
    park_hit_boxes(state.tool_task_boxes.get());
    park_hit_boxes(state.thinking_header_boxes.get());

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
            const int input_height =
                prompt_input_height(prompt_input, 6, prompt_box_inner_width(state));
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
            // Up to 500 messages, render all history for continuous, fluid scrolling.
            // Beyond 500 messages, keep a generous sliding window of 300 messages.
            size_t first = 0;
            size_t last = history_size;
            constexpr size_t kMaxFullRender = 500;
            if (history_size > kMaxFullRender) {
                constexpr size_t kWindowSlice = 300;
                if (*state.auto_scroll) {
                    first = history_size - kWindowSlice;
                    last = history_size;
                } else {
                    first = std::min(
                        state.history_window_start ? *state.history_window_start : static_cast<size_t>(0),
                        history_size - kWindowSlice);
                    last = first + kWindowSlice;
                }
            }
            if (state.history_window_start) {
                *state.history_window_start = first;
            }

            // Every message but the last (still streaming, or awaiting its
            // tool result) is cached as a rendered FTXUI tree. Everything a
            // cached row's rendering depends on must invalidate it below.
            // Owner is held (not a raw pointer) so a new history can never
            // reuse the old one's address.
            static std::shared_ptr<const qcode::Messages> cache_owner;
            static size_t cached_history_size = 0;
            static int cached_width = 0;
            static int cached_provider = -1;
            static int cached_model = -1;
            static std::string cached_theme;
            static std::string cached_session;
            static bool cached_thinking = false;
            static std::unordered_map<std::string, bool> cached_collapse;
            static std::unordered_map<unsigned long, bool> cached_expand;
            static std::unordered_map<size_t, Element> message_cache;
            static ToolPairing tool_pairing;

            // Cached trees hold HitBox& into these maps and fill
            // tool_task_sessions only when rendered, so the maps are dropped
            // only together with the trees (boxes are parked per frame).
            auto reset_message_cache = [&] {
                message_cache.clear();
                if (state.tool_arrow_boxes) state.tool_arrow_boxes->clear();
                if (state.tool_task_boxes) state.tool_task_boxes->clear();
                if (state.tool_task_sessions) state.tool_task_sessions->clear();
                if (state.thinking_header_boxes)
                    state.thinking_header_boxes->clear();
            };

            const auto terminal_width = stable_terminal_size().dimx;
            const bool history_replaced =
                cache_owner != state.messages_history ||
                history_size < cached_history_size;
            const bool history_changed =
                history_replaced || history_size != cached_history_size;

            if (history_replaced ||
                terminal_width != cached_width ||
                cached_provider != selected_provider ||
                cached_model != selected_model ||
                cached_theme != *state.theme ||
                cached_session != *state.session_id ||
                cached_thinking != *state.show_thinking ||
                (state.tool_collapse_state &&
                 cached_collapse != *state.tool_collapse_state) ||
                (state.thinking_expand_state &&
                 cached_expand != *state.thinking_expand_state)) {
                reset_message_cache();
                cached_width = terminal_width;
                cached_provider = selected_provider;
                cached_model = selected_model;
                cached_theme = *state.theme;
                cached_session = *state.session_id;
                cached_thinking = *state.show_thinking;
                if (state.tool_collapse_state)
                    cached_collapse = *state.tool_collapse_state;
                if (state.thinking_expand_state)
                    cached_expand = *state.thinking_expand_state;
            }
            cache_owner = state.messages_history;
            cached_history_size = history_size;

            if (message_cache.size() > 1000) {
                reset_message_cache();
            }

            if (history_changed) {
                build_tool_pairing(*state.messages_history, tool_pairing);
            }

            if (first > 0) {
                msgs.push_back(
                    text(" " + std::to_string(first) +
                         " earlier messages · scroll up to view ") |
                    dim | hcenter);
                msgs.push_back(separatorLight() | color(dim_gray()));
            }
            std::vector<const qcode::Message*> paired_results;
            for (size_t i = first; i < last; ++i) {
                // Result rows are drawn inside their call's tool block.
                if (tool_pairing.consumed[i]) continue;
                const auto& msg = (*state.messages_history)[i];
                paired_results.clear();
                if (auto rows = tool_pairing.result_rows.find(i);
                    rows != tool_pairing.result_rows.end()) {
                    for (size_t r : rows->second) {
                        paired_results.push_back(&(*state.messages_history)[r]);
                    }
                }
                // A call still running re-renders until its result lands.
                const bool cacheable =
                    i + 1 < history_size && !tool_pairing.pending[i];
                auto cached = cacheable ? message_cache.find(i)
                                        : message_cache.end();
                if (cached != message_cache.end()) {
                    msgs.push_back(cached->second);
                } else {
                    const bool in_flight =
                        !cacheable && state.is_generating->load();
                    auto rendered = render_message(
                        msg, state, providers_list, selected_provider,
                        selected_model, *state.theme, nullptr,
                        static_cast<int>(i), terminal_width, in_flight,
                        paired_results);
                    if (cacheable) message_cache[i] = rendered;
                    msgs.push_back(std::move(rendered));
                }
                // No separator line between messages: the role header
                // ("❯ You" / "❯ Assistant") is delimiter enough.
            }
            if (last < history_size) {
                msgs.push_back(
                    text(" " + std::to_string(history_size - last) +
                         " later messages · scroll down to view ") |
                    dim | hcenter);
            }

            // Queued prompts appear at the end of the message list (compact amber block)
            const auto* queue =
                state.queued_prompt_texts ? state.queued_prompt_texts.get()
                                          : nullptr;
            if (queue && !queue->empty() && last >= history_size) {
                msgs.push_back(render_queued_block(*queue, theme));
            }

            const int input_height =
                prompt_input_height(prompt_input, 8, prompt_box_inner_width(state));
            Element prompt_box = make_prompt_box(input_height);

            auto suggestions = build_suggestions_panel();
            if (suggestions) {
                prompt_box = vbox({
                    *suggestions,
                    prompt_box
                });
            }

            // Measure chat content height (in rendered lines) and compute exact
            // scroll position without dead zone.
            Element chat_scroll = vbox(std::move(msgs));
            chat_scroll->ComputeRequirement();
            prompt_box->ComputeRequirement();

            const int content_height = std::max(0, chat_scroll->requirement().min_y);
            const int prompt_box_h = prompt_box->requirement().min_y;
            const int chrome_height = 2 + prompt_box_h;
            const int term_h = stable_terminal_size().dimy;
            const int viewport_height = std::max(1, term_h - chrome_height);
            const int max_scroll = qcode::tui::compute_max_scroll(content_height, viewport_height);

            if (*state.auto_scroll) {
                *state.scroll_line = max_scroll;
            } else {
                *state.scroll_line =
                    std::clamp(*state.scroll_line, 0, max_scroll);
                // Re-engage auto-scroll when user scrolls back to the bottom
                if (*state.scroll_line >= max_scroll) {
                    *state.auto_scroll = true;
                }
            }

            const int focus_y = qcode::tui::compute_focus_y(*state.scroll_line, viewport_height);

            body = vbox({
                chat_scroll | vscroll_indicator | focusPosition(0, focus_y) | yframe | flex,
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
                text("click a file to open its diff") | dim,
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
                const int viewport_height = std::max(1, state.terminal_height - 4);
                // Keep the selected row roughly in view.
                const int target = std::min(
                    content_height - 1,
                    std::max(0, state.selected_file + 2));
                const int focus_y = qcode::tui::compute_focus_y(target, viewport_height);
                *state.scroll_line = target;
                *state.auto_scroll = false;
                body = vbox({
                    file_scroll | vscroll_indicator |
                        focusPosition(0, focus_y) | yframe | flex,
                }) | flex;
            }
        } else {
            // Detail view: unified diff for the selected file.
            const auto selected = std::clamp(
                state.selected_file, 0,
                static_cast<int>(changes->size()) - 1);
            const auto& entry = (*changes)[selected];
            const bool untracked_dir =
                entry.untracked && !entry.path.empty() &&
                entry.path.back() == '/';
            // git runs on a background worker, memoized per
            // (files_revision, path); a placeholder shows until it lands.
            const auto preview = get_file_preview(
                entry.path, *state.files_revision,
                untracked_dir ? FilePreviewKind::kUntrackedDirSample
                              : FilePreviewKind::kDiff);
            const std::string& content = *preview;
            // Tokenizing a diff (up to 2 MiB) into per-line nodes every
            // frame is costly; preview text is immutable per shared_ptr.
            static std::shared_ptr<const std::string> diff_source;
            static Element diff_element;
            if (preview != diff_source) {
                diff_source = preview;
                diff_element = render_diff_content(content);
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
                    : hbox({text("  "), diff_element | flex}),
                text(""),
            }) | borderLight | color(accent(theme)));

            Element file_scroll = vbox(std::move(file_blocks));
            file_scroll->ComputeRequirement();
            {
                const int content_height =
                    std::max(0, file_scroll->requirement().min_y);
                const int chrome_height = 5;
                const int term_h = stable_terminal_size().dimy;
            const int viewport_height = std::max(1, term_h - chrome_height);
                const int max_scroll = qcode::tui::compute_max_scroll(content_height, viewport_height);
                if (*state.auto_scroll) {
                    *state.scroll_line = 0;
                    *state.auto_scroll = false;
                } else {
                    *state.scroll_line = std::clamp(
                        *state.scroll_line, 0,
                        max_scroll);
                }
                const int focus_y = qcode::tui::compute_focus_y(*state.scroll_line, viewport_height);
                body = vbox({
                    text(" FILE DIFF ") | bold | color(accent2(theme)) | hcenter,
                    text(""),
                    file_scroll | vscroll_indicator |
                        focusPosition(0, focus_y) | yframe | flex,
                }) | flex;
            }
        }
    }
    // ── Tab 2: Stats ──
    else if (state.tab_selected == 2) {
        body = build_stats_tab(state, providers_list, selected_provider, selected_model, theme);
    }
    // ── Tab 3: Subagents for current session only ──
    else {
        const auto& tasks = state.subagent_entries ? *state.subagent_entries
                                                   : std::vector<SubagentEntry>{};
        if (state.session_row_boxes) state.session_row_boxes->clear();
        if (state.subagent_row_boxes) {
            state.subagent_row_boxes->resize(tasks.size());
        }

        Elements content_rows;

        content_rows.push_back(hbox(
            text(" SUBAGENTS ") | bold | color(accent2(theme)),
            filler(),
            text("click a task to open it in chat") | dim
        ));
        content_rows.push_back(separatorLight() | color(accent(theme)));

        if (tasks.empty()) {
            content_rows.push_back(text("  No subagents run for this session.") | dim);
            content_rows.push_back(
                text("  Spawn subagents using the task tool. They will appear here for the active session.") | dim);
        } else {
            content_rows.push_back(hbox(
                text("▶ SUBAGENTS (" + std::to_string(tasks.size()) + ")") | bold | color(Color::CyanLight)
            ));
            for (size_t row_i = 0; row_i < tasks.size(); ++row_i) {
                const auto& t = tasks[row_i];
                const std::string& status = t.status;
                const std::string& model_str = t.model;
                const std::string& desc = t.description;
                const std::string& sid = t.task_id;
                const bool is_current = (state.session_id && sid == *state.session_id);
                const bool is_active_cursor = (static_cast<int>(row_i) == state.selected_session_item);

                Color status_col = Color::Default;
                if (status == "running") status_col = Color::Yellow;
                else if (status == "done") status_col = Color::Green;
                else if (status == "error" || status == "interrupted") status_col = Color::Red;

                std::string marker = is_active_cursor ? "▶ " : (is_current ? "● " : "  ");
                Element row = hbox(
                    text(marker) | color(is_active_cursor ? accent2(theme)
                                         : (is_current ? accent(theme) : Color::Default)),
                    text("[" + status + "] ") | bold | color(status_col),
                    text(!model_str.empty() ? ("{" + model_str + "} ") : "") | dim,
                    text(desc) | bold,
                    filler(),
                    text(is_current ? "[open] " : "  ") | color(Color::Green) | bold
                );
                if (is_active_cursor) {
                    row = std::move(row) | bgcolor(bg_popup());
                }
                if (state.subagent_row_boxes) {
                    row = std::move(row) |
                          reflect_box((*state.subagent_row_boxes)[row_i]);
                }
                content_rows.push_back(std::move(row));
            }
        }

        Element session_scroll = vbox(std::move(content_rows));
        session_scroll->ComputeRequirement();
        {
            const int content_height = std::max(0, session_scroll->requirement().min_y);
            const int viewport_height = std::max(1, state.terminal_height - 6);
            const int target = std::min(
                content_height - 1,
                std::max(0, state.selected_session_item + 2));
            const int focus_y = qcode::tui::compute_focus_y(target, viewport_height);
            *state.scroll_line = target;
            *state.auto_scroll = false;
            body = vbox({
                session_scroll | vscroll_indicator |
                    focusPosition(0, focus_y) | yframe | flex,
            }) | flex;
        }
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
