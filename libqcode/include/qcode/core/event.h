#pragma once

#include <string>
#include <vector>
#include <optional>
#include <nlohmann/json.hpp>
#include <qcode/core/bus_port.h>

namespace qcode {
namespace contract {

// ─── TUI → Backend (User-initiated) ───────────────────────────────────────────

struct PromptSubmitted {
    static constexpr const char* type = "tui.prompt.submitted";
    struct Payload {
        std::string text;
        std::vector<std::string> attachment_paths;
    };
};

struct CommandExecuted {
    static constexpr const char* type = "tui.command.executed";
    struct Payload {
        std::string command;
        std::string args;
    };
};

struct SessionSelected {
    static constexpr const char* type = "tui.session.selected";
    struct Payload {
        std::string session_id;
    };
};

struct ModelSelected {
    static constexpr const char* type = "tui.model.selected";
    struct Payload {
        std::string provider_name;
        std::string model_id;
    };
};

struct SessionListRequested {
    static constexpr const char* type = "tui.session.list.requested";
    struct Payload {};
};

struct ScrollRequested {
    static constexpr const char* type = "tui.scroll.requested";
    struct Payload {
        int delta = 0;
        bool page = false;
    };
};

// ─── Backend → TUI (System-initiated) ─────────────────────────────────────────

struct MessageDelta {
    static constexpr const char* type = "backend.message.delta";
    struct Payload {
        std::string session_id;
        // Append-only text produced since the previous event. A final event may
        // carry an empty chunk when all text has already been flushed.
        std::string text;
        bool done = false;
    };
};

struct ToolCallStarted {
    static constexpr const char* type = "backend.tool.call.started";
    struct Payload {
        std::string session_id;
        std::string tool_call_id;
        std::string tool_name;
        nlohmann::json arguments;
    };
};

struct ToolCallCompleted {
    static constexpr const char* type = "backend.tool.call.completed";
    struct Payload {
        std::string session_id;
        std::string tool_call_id;
        std::string tool_name;
        nlohmann::json result;
        bool is_error = false;
        double duration_ms = 0.0;
    };
};

// A prompt the user queued while a turn was running, folded into that turn's
// next model request. Subscribers persist and render it as a user message.
struct UserMessageInjected {
    static constexpr const char* type = "backend.user.message.injected";
    struct Payload {
        std::string session_id;
        std::string text;
    };
};

// The tool loop compacted the conversation mid-turn (auto-compaction).
// `text` is the continuation user message that replaced the history (it
// holds compaction::kSummaryMarker); subscribers persist it as a User row so
// stored history is cut there, and show a notice.
struct ConversationCompacted {
    static constexpr const char* type = "backend.conversation.compacted";
    struct Payload {
        std::string session_id;
        std::string text;
        std::string todo_path;
        int tokens_before = 0;
        int tokens_after = 0;
        int threshold = 0;
        int messages_before = 0;
    };
};

// One-line notice for a ConversationCompacted event (chat + session row).
inline std::string compacted_note(const ConversationCompacted::Payload& p) {
    std::string note = "Context auto-compacted at " + std::to_string(p.tokens_before) +
                       " tokens (threshold " + std::to_string(p.threshold) + "): " +
                       std::to_string(p.messages_before) + " messages -> handoff packet, ~" +
                       std::to_string(p.tokens_after) + " tokens";
    if (!p.todo_path.empty()) note += "\nHandoff file: " + p.todo_path;
    return note;
}

struct SessionStatusChanged {
    static constexpr const char* type = "backend.session.status.changed";
    struct Payload {
        std::string session_id;
        std::string status; // "idle", "generating", "error"
    };
};

struct ErrorOccurred {
    static constexpr const char* type = "backend.error.occurred";
    struct Payload {
        std::string session_id;
        std::string message;
        std::string severity; // "info", "warning", "error"
    };
};

struct ReasoningDelta {
    static constexpr const char* type = "backend.reasoning.delta";
    struct Payload {
        std::string session_id;
        // Append-only reasoning produced since the previous event.
        std::string text;
        std::string signature; // provider signature (anthropic); may be empty
        bool done = false;
    };
};


struct StepLatency {
    static constexpr const char* type = "backend.step.latency";
    struct Payload {
        std::string session_id;
        int step = 0;
        bool streamed = false;
        double model_ms = 0.0;
        double ttft_ms = -1.0;
        int output_tokens = 0;
        int reasoning_tokens = 0;
        std::string effort;
        bool ok = false;
        // Billed input of this call (whole prompt incl. cache reads/writes)
        // and its cache split, for the Stats tab cost/cache-hit figures.
        int input_tokens = 0;
        int cache_read_tokens = 0;
        int cache_write_tokens = 0;
        std::string variant;  // picker variant ("ultra"); effort is the wire value
        // Who served the call and what it cost at that model's opencode.json
        // prices when it ran (priced=false: the model has no price).
        std::string provider;
        std::string model;
        bool priced = false;
        double cost_input = 0.0;
        double cost_cache_read = 0.0;
        double cost_cache_write = 0.0;
        double cost_output = 0.0;
    };
};

struct TokenUsageUpdated {
    static constexpr const char* type = "backend.token.usage.updated";
    struct Payload {
        int prompt_tokens = 0;
        int completion_tokens = 0;
        int total_tokens = 0;
        // Tokens served from the provider's prompt cache this turn (when the
        // provider reports them, e.g. cached_tokens on OpenCode Zen /
        // OpenRouter). 0 when unknown.
        int cached_prompt_tokens = 0;
        // Thinking/reasoning output tokens this turn
        // (completion_tokens_details.reasoning_tokens) when reported.
        int reasoning_tokens = 0;
        std::string session_id = "";
    };
};

struct ContextSizeUpdated {
    static constexpr const char* type = "backend.context.size.updated";
    struct Payload {
        // Estimated tokens in the context sent for the CURRENT turn
        // (system + messages). This is a per-request snapshot that grows as
        // tool calls append messages; it is NOT the session lifetime total.
        int context_tokens = 0;
    };
};

struct CompactionResult {
    static constexpr const char* type = "backend.compaction.result";
    struct Payload {
        std::string summary;
        std::string todo_path;
        bool wrote = false;
        int keep = 0;
        size_t original_size = 0;
        std::string error;
    };
};

struct ToastRequested {
    static constexpr const char* type = "tui.toast.requested";
    struct Payload {
        std::string message;
        std::string variant = "info";
        int duration_ms = 5000;
    };
};

// ── Registry helper ───────────────────────────────────────────────────────────
inline void register_all_events(bus::BusPort& bus) {
    bus.register_event<PromptSubmitted>();
    bus.register_event<CommandExecuted>();
    bus.register_event<SessionSelected>();
    bus.register_event<ModelSelected>();
    bus.register_event<SessionListRequested>();
    bus.register_event<ScrollRequested>();
    bus.register_event<MessageDelta>();
    bus.register_event<ToolCallStarted>();
    bus.register_event<ToolCallCompleted>();
    bus.register_event<UserMessageInjected>();
    bus.register_event<ConversationCompacted>();
    bus.register_event<SessionStatusChanged>();
    bus.register_event<ErrorOccurred>();
    bus.register_event<StepLatency>();
    bus.register_event<TokenUsageUpdated>();
    bus.register_event<ContextSizeUpdated>();
    bus.register_event<ReasoningDelta>();
    bus.register_event<CompactionResult>();
    bus.register_event<ToastRequested>();
}

} // namespace contract
} // namespace qcode
