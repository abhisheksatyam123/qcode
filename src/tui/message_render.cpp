#include <ai/tui/tool_renderers.h>
#include <ai/tui/message_render.h>
#include <ai/tui/views.h>
#include <nlohmann/json.hpp>
#include <ai/logger.h>

namespace ai {
namespace tui {

using namespace ftxui;

// ── BlockTool: generic collapsible-style tool block ──
Element BlockTool(const std::string& title, Element content,
                  bool is_running, const std::string& status,
                  Color border_color) {
  Elements header_parts;

  // Icon + title - only render if we have a title
  if (!title.empty()) {
    if (is_running) {
      header_parts.push_back(text(" ⠋ " + title) | dim);
    } else {
      header_parts.push_back(text(" " + title) | dim);
    }
  }

  // Status badge
  if (!status.empty()) {
    auto badge_color = border_color;
    header_parts.push_back(
        text(" " + status + " ") | color(badge_color) | bold);
  }

  return vbox({
      hbox(std::move(header_parts)),
      hbox({
          separatorLight() | color(border_color),
          vbox(std::move(content)) | flex,
      }),
  });
}

// ── Format a JSON value as a compact single-line string ──
static std::string format_arg_value(const nlohmann::json& val) {
  if (val.is_string()) return val.get<std::string>();
  if (val.is_number()) return val.dump();
  if (val.is_boolean()) return val.get<bool>() ? "true" : "false";
  if (val.is_null()) return "null";
  // For objects/arrays, keep compact
  auto s = val.dump();
  if (s.size() > 80) s = s.substr(0, 77) + "...";
  return s;
}

// ── Render a tool call part ──
static Element render_tool_call(const ai::ToolCallContentPart& part,
                                const std::string& theme) {
  Elements body;

  // Format arguments as key-value pairs
  if (part.arguments.is_object()) {
    for (auto it = part.arguments.begin(); it != part.arguments.end(); ++it) {
      body.push_back(hbox({
          text("  ") | dim,
          text(it.key() + ": ") | bold | color(accent(theme)),
          text(format_arg_value(it.value())) | color(accent2(theme)),
      }));
    }
  } else {
    // Non-object: show compact dump
    body.push_back(
        hbox({text("  ") | dim, text(part.arguments.dump()) | dim}));
  }

  return BlockTool(" " + part.tool_name, vbox(std::move(body)), false,
                   "calling...", Color::Yellow);
}

// ── Render a tool result part ──
static Element render_tool_result(const ai::ToolResultContentPart& part,
                                  const std::string& theme) {
  Elements body;

  std::string result_str;
  if (part.is_error) {
    result_str = part.result.value("error", part.result.dump());
  } else {
    // Try to extract meaningful output
    if (part.result.is_string()) {
      result_str = part.result.get<std::string>();
    } else if (part.result.is_object()) {
      // For bash tool: show stdout/stderr
      if (part.result.contains("output")) {
        result_str = part.result["output"].get<std::string>();
      } else if (part.result.contains("stdout")) {
        result_str = part.result["stdout"].get<std::string>();
      } else {
        result_str = part.result.dump(2);
      }
    } else {
      result_str = part.result.dump();
    }
  }

  // Truncate long output
  constexpr size_t MAX_OUTPUT = 2000;
  if (result_str.size() > MAX_OUTPUT) {
    result_str = result_str.substr(0, MAX_OUTPUT) + "\n... [truncated]";
  }

  if (!result_str.empty()) {
    body.push_back(hbox({text("  ") | dim, text(result_str) | dim}));
  }

  auto status = part.is_error ? "failed" : "done";
  auto border = part.is_error ? Color::Red : Color::Green;
  return BlockTool("", vbox(std::move(body)), false, status, border);
}

// ── Render a full message ──
Element render_message(const ai::Message& msg, const ChatState& state,
                       const std::vector<ProviderInfo>& providers_list,
                       int selected_provider, int selected_model,
                       const std::string& theme) {
  Elements parts;

  // Determine if this message is purely tool content
  bool has_text = false;
  bool has_tool_calls = false;
  bool has_tool_results = false;
  for (const auto& part : msg.content) {
    if (std::holds_alternative<ai::TextContentPart>(part)) has_text = true;
    if (std::holds_alternative<ai::ToolCallContentPart>(part))
      has_tool_calls = true;
    if (std::holds_alternative<ai::ToolResultContentPart>(part))
      has_tool_results = true;
  }

  // Only show role header for messages with actual text content.
  // Pure tool-call/result messages show their own headers via BlockTool.
  if (has_text) {
    if (msg.role == kMessageRoleAssistant) {
      parts.push_back(hbox({
          text("  Assistant") | bold | color(accent2(theme)),
          text(" · " +
               providers_list[selected_provider].models[selected_model].name)
              | dim | color(accent(theme)),
      }));
    } else if (msg.role == kMessageRoleUser && !has_tool_results) {
      parts.push_back(text("  You") | bold | color(user_green()));
    }
  }

  LOG_DEBUG("render_message: role={} text={} tool_calls={} tool_results={}", 
           static_cast<int>(msg.role), has_text, has_tool_calls, has_tool_results);

  // Render content parts
  for (const auto& part : msg.content) {
    if (const auto* text_part =
            std::get_if<ai::TextContentPart>(&part)) {
      if (!text_part->text.empty()) {
        parts.push_back(vbox(render_markdown(text_part->text)));
      }
    } else if (const auto* tool_part =
                   std::get_if<ai::ToolCallContentPart>(&part)) {
      parts.push_back(render_tool_call(*tool_part, theme));
    } else if (const auto* result_part =
                   std::get_if<ai::ToolResultContentPart>(&part)) {
      parts.push_back(render_tool_result(*result_part, theme));
    }
  }

  return vbox(std::move(parts));
}

}  // namespace tui
}  // namespace ai
