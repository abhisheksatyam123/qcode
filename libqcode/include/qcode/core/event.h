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
    bus.register_event<SessionStatusChanged>();
    bus.register_event<ErrorOccurred>();
    bus.register_event<TokenUsageUpdated>();
    bus.register_event<ContextSizeUpdated>();
    bus.register_event<ReasoningDelta>();
    bus.register_event<CompactionResult>();
    bus.register_event<ToastRequested>();
}

} // namespace contract
} // namespace qcode
