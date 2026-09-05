#include "bus_json_codec.h"
#include <qcode/core/event.h>
#include <nlohmann/json.hpp>
#include <any>
#include <typeindex>

namespace qcode {
namespace server {

using namespace qcode::contract;

nlohmann::json message_delta_to_json(const MessageDelta::Payload& p) {
    return {
        {"type", MessageDelta::type},
        {"session_id", p.session_id},
        {"text", p.text},
        {"done", p.done}
    };
}

nlohmann::json tool_call_started_to_json(const ToolCallStarted::Payload& p) {
    return {
        {"type", ToolCallStarted::type},
        {"session_id", p.session_id},
        {"tool_call_id", p.tool_call_id},
        {"tool_name", p.tool_name},
        {"arguments", p.arguments}
    };
}

nlohmann::json tool_call_completed_to_json(const ToolCallCompleted::Payload& p) {
    return {
        {"type", ToolCallCompleted::type},
        {"session_id", p.session_id},
        {"tool_call_id", p.tool_call_id},
        {"tool_name", p.tool_name},
        {"result", p.result},
        {"is_error", p.is_error},
        {"duration_ms", p.duration_ms}
    };
}

nlohmann::json session_status_to_json(const SessionStatusChanged::Payload& p) {
    return {
        {"type", SessionStatusChanged::type},
        {"session_id", p.session_id},
        {"status", p.status}
    };
}

nlohmann::json error_occurred_to_json(const ErrorOccurred::Payload& p) {
    return {
        {"type", ErrorOccurred::type},
        {"session_id", p.session_id},
        {"message", p.message},
        {"severity", p.severity}
    };
}

nlohmann::json token_usage_to_json(const TokenUsageUpdated::Payload& p) {
    return {
        {"type", TokenUsageUpdated::type},
        {"prompt_tokens", p.prompt_tokens},
        {"completion_tokens", p.completion_tokens},
        {"total_tokens", p.total_tokens},
        {"cached_prompt_tokens", p.cached_prompt_tokens}
    };
}

nlohmann::json reasoning_delta_to_json(const ReasoningDelta::Payload& p) {
    return {
        {"type", ReasoningDelta::type},
        {"session_id", p.session_id},
        {"text", p.text},
        {"signature", p.signature},
        {"done", p.done}
    };
}

std::optional<nlohmann::json> serialize_event(
    const std::string& event_type,
    const std::any& payload,
    std::type_index payload_type)
{
    if (event_type == MessageDelta::type) {
        return message_delta_to_json(std::any_cast<const MessageDelta::Payload&>(payload));
    }
    if (event_type == ToolCallStarted::type) {
        return tool_call_started_to_json(std::any_cast<const ToolCallStarted::Payload&>(payload));
    }
    if (event_type == ToolCallCompleted::type) {
        return tool_call_completed_to_json(std::any_cast<const ToolCallCompleted::Payload&>(payload));
    }
    if (event_type == SessionStatusChanged::type) {
        return session_status_to_json(std::any_cast<const SessionStatusChanged::Payload&>(payload));
    }
    if (event_type == ErrorOccurred::type) {
        return error_occurred_to_json(std::any_cast<const ErrorOccurred::Payload&>(payload));
    }
    if (event_type == TokenUsageUpdated::type) {
        return token_usage_to_json(std::any_cast<const TokenUsageUpdated::Payload&>(payload));
    }
    if (event_type == ReasoningDelta::type) {
        return reasoning_delta_to_json(std::any_cast<const ReasoningDelta::Payload&>(payload));
    }
    return std::nullopt;
}

bool dispatch_json(const nlohmann::json& msg, qcode::bus::BusPort& bus) {
    if (!msg.contains("type")) return false;
    std::string type = msg["type"];

    if (type == PromptSubmitted::type) {
        PromptSubmitted::Payload p{msg.value("text", ""), {}};
        if (msg.contains("attachment_paths")) {
            for (const auto& a : msg["attachment_paths"])
                p.attachment_paths.push_back(a.get<std::string>());
        }
        bus.publish<PromptSubmitted>(p);
        return true;
    }
    if (type == CommandExecuted::type) {
        CommandExecuted::Payload p{msg.value("command", ""), msg.value("args", "")};
        bus.publish<CommandExecuted>(p);
        return true;
    }
    if (type == ModelSelected::type) {
        ModelSelected::Payload p{msg.value("provider_name", ""), msg.value("model_id", "")};
        bus.publish<ModelSelected>(p);
        return true;
    }
    if (type == SessionSelected::type) {
        SessionSelected::Payload p{msg.value("session_id", "")};
        bus.publish<SessionSelected>(p);
        return true;
    }

    return false;
}

} // namespace server
} // namespace qcode
