#pragma once

#include <atomic>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <qcode/types/message.h>

namespace qcode {

struct HitBox {
    int x_min = 0;
    int x_max = 0;
    int y_min = 0;
    int y_max = 0;
    bool Contain(int x, int y) const {
        return x >= x_min && x <= x_max && y >= y_min && y <= y_max;
    }
};

// ── Provider/model types ──

struct ModelInfo {
    std::string name;
    std::string id;
    int context_window = 0;   // tokens; 0 => unknown (fallback)
    double input_cost = 0.0;   // USD per 1M input tokens
    double output_cost = 0.0;  // USD per 1M output tokens
    bool reasoning = false;
    bool tool_call = false;
    int output_limit = 0;
    std::string protocol;
};

struct ProviderInfo {
    std::string name;
    std::string id;
    std::string api_url;
    std::string api_key;
    std::map<std::string, std::string> headers;
    std::string protocol = "chat_completions";
    std::string project_id;
    std::vector<ModelInfo> models;
};

// One row in the Files tab list (from `git diff HEAD --numstat` + untracked).
struct FileChangeEntry {
    std::string path;
    int additions = 0;
    int deletions = 0;
    bool untracked = false;
    bool binary = false;
};

// ── Shared chat state ──

struct ChatState {
    std::shared_ptr<std::atomic<bool>> is_generating =
        std::make_shared<std::atomic<bool>>(false);
    std::shared_ptr<qcode::Messages> messages_history = std::make_shared<qcode::Messages>();
    std::shared_ptr<int> total_prompt_tokens = std::make_shared<int>(0);
    std::shared_ptr<int> total_completion_tokens = std::make_shared<int>(0);
    std::shared_ptr<int> total_tokens = std::make_shared<int>(0);
    std::shared_ptr<int> current_context_tokens = std::make_shared<int>(0);
    std::shared_ptr<std::vector<FileChangeEntry>> file_changes =
        std::make_shared<std::vector<FileChangeEntry>>();
    std::shared_ptr<size_t> files_revision = std::make_shared<size_t>(0);
    // Files tab: list of changed files, or the selected file's unified diff.
    bool files_detail_open = false;
    // Hit boxes for clickable file rows in the list view (index == file index).
    std::shared_ptr<std::vector<HitBox>> file_row_boxes =
        std::make_shared<std::vector<HitBox>>();
    std::shared_ptr<int> scroll_line = std::make_shared<int>(INT_MAX);
    std::shared_ptr<bool> auto_scroll = std::make_shared<bool>(true);
    // First message in the bounded render window. The complete history remains
    // in messages_history, but only a viewport-sized slice is turned into
    // FTXUI nodes on each frame.
    std::shared_ptr<size_t> history_window_start = std::make_shared<size_t>(0);
    std::shared_ptr<bool> show_thinking = std::make_shared<bool>(true);
    // Reasoning/thinking level: "off" | "low" | "medium" | "high" (opt-in)
    std::shared_ptr<std::string> reasoning_mode = std::make_shared<std::string>("off");
    std::shared_ptr<std::atomic<int>> generation_frame = std::make_shared<std::atomic<int>>(0);

    // Tab navigation
    int tab_selected = 0; // 0 = Chat, 1 = Files, 2 = Stats
    int selected_file = 0; // Selected index in file_changes

    int terminal_height = 40; // Approximate terminal height, updated during render

    // Color Theme Option (orange, green, blue, purple, monochrome)
    std::shared_ptr<std::string> theme = std::make_shared<std::string>("orange");

    // Active persistent session ID (UUID) + display title
    std::shared_ptr<std::string> session_id = std::make_shared<std::string>();
    std::shared_ptr<std::string> session_title = std::make_shared<std::string>();

    // Last user prompt for one-key retry after a failed/stopped turn.
    std::shared_ptr<std::string> last_user_prompt = std::make_shared<std::string>();
    std::shared_ptr<bool> retry_available = std::make_shared<bool>(false);

    // Slash command autocomplete suggestions state
    bool slash_suggestion_mode = false;
    int slash_suggestion_idx = 0;

    // Tool observability stats
    std::shared_ptr<int> tool_call_count = std::make_shared<int>(0);
    std::shared_ptr<double> total_tool_time_ms = std::make_shared<double>(0.0);

    // Prompt queue + status mirrors (consumed by the view).
    // queued_prompt_texts is the live queue body so the chat list can show
    // pending prompts (Grok-style) without reading AppStore internals.
    std::shared_ptr<int> queued_prompts = std::make_shared<int>(0);
    std::shared_ptr<std::vector<std::string>> queued_prompt_texts =
        std::make_shared<std::vector<std::string>>();
    std::shared_ptr<std::string> status = std::make_shared<std::string>("idle");
    std::shared_ptr<std::string> last_error = std::make_shared<std::string>();

    // Tool collapse/expand state (keyed by tool_call_id)
    std::shared_ptr<std::unordered_map<std::string, bool>> tool_collapse_state =
        std::make_shared<std::unordered_map<std::string, bool>>();

    // Context estimation calibration: our heuristic estimate vs the API's
    // actual prompt_tokens from the last generation. Used to correct the
    // chars÷4 heuristic (which is wrong for JSON-heavy tool results).
    std::shared_ptr<int> last_actual_prompt_tokens = std::make_shared<int>(0);
    std::shared_ptr<int> last_estimated_tokens = std::make_shared<int>(0);

    // Abort/pause flag for the current generation. Set by Escape; checked by
    // the backend generation loop. Resets to false on each new spawn.
    std::shared_ptr<std::atomic<bool>> abort_flag =
        std::make_shared<std::atomic<bool>>(false);

    // Context management: how many consecutive generations needed pruning.
    // Drives auto-compaction when the conversation is persistently too long.
    std::shared_ptr<int> consecutive_prunes = std::make_shared<int>(0);

    // Copy mode: when true, mouse tracking is disabled so the terminal
    // emulator can do native text selection (clean copy/paste).
    std::shared_ptr<bool> copy_mode = std::make_shared<bool>(false);

    // Keyboard navigation: ordered list of tool_call_ids in render order,
    // plus the currently focused index. -1 means no tool block focused.
    std::shared_ptr<std::vector<std::string>> tool_block_order =
        std::make_shared<std::vector<std::string>>();
    std::shared_ptr<int> focused_tool_index = std::make_shared<int>(-1);
    std::shared_ptr<std::unordered_map<std::string, HitBox>> tool_arrow_boxes =
        std::make_shared<std::unordered_map<std::string, HitBox>>();
};

} // namespace qcode
