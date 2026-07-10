#pragma once

#include <nlohmann/json.hpp>
#include <ai/tui/bus/port.h>
#include <ai/tui/contract/event.h>
#include <string>
#include <optional>

namespace ai {
namespace server {

/**
 * Serialize a typed bus event payload to JSON.
 * Returns nullopt for unknown/unhandled event types.
 */
std::optional<nlohmann::json> serialize_event(
    const std::string& event_type,
    const std::any& payload,
    std::type_index payload_type);

/**
 * Deserialize a JSON message into a bus event.
 * Publishes the deserialized event onto the given bus.
 * Returns true if the event was recognized and published.
 */
bool dispatch_json(const nlohmann::json& msg, ai::tui::bus::BusPort& bus);

/**
 * Convert bus events to/from JSON for WebSocket streaming.
 */
nlohmann::json message_delta_to_json(const ai::tui::contract::MessageDelta::Payload& p);
nlohmann::json tool_call_started_to_json(const ai::tui::contract::ToolCallStarted::Payload& p);
nlohmann::json tool_call_completed_to_json(const ai::tui::contract::ToolCallCompleted::Payload& p);
nlohmann::json session_status_to_json(const ai::tui::contract::SessionStatusChanged::Payload& p);
nlohmann::json error_occurred_to_json(const ai::tui::contract::ErrorOccurred::Payload& p);
nlohmann::json reasoning_delta_to_json(const ai::tui::contract::ReasoningDelta::Payload& p);

} // namespace server
} // namespace ai
