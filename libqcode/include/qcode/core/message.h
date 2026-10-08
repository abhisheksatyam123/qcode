#pragma once

#include <qcode/core/enums.h>

#include <algorithm>
#include <climits>
#include <cstddef>
#include <string>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>

namespace qcode {

using JsonValue = nlohmann::json;

// Base content part types
struct TextContentPart {
  std::string text;

  explicit TextContentPart(std::string t) : text(std::move(t)) {}
};

struct ToolCallContentPart {
  std::string id;
  std::string tool_name;
  JsonValue arguments;
  std::string thought_signature;

  ToolCallContentPart(std::string i, std::string n, JsonValue a,
                      std::string signature = "")
      : id(std::move(i)),
        tool_name(std::move(n)),
        arguments(std::move(a)),
        thought_signature(std::move(signature)) {}
};

struct ToolResultContentPart {
  std::string tool_call_id;
  JsonValue result;
  bool is_error = false;
  double duration_ms = 0.0;

  ToolResultContentPart(std::string id, JsonValue r, bool err = false,
                         double dur = 0.0)
      : tool_call_id(std::move(id)), result(std::move(r)), is_error(err),
        duration_ms(dur) {}
};

struct ReasoningContentPart {
  std::string text;        // thinking/reasoning text (display)
  std::string signature;   // provider signature (anthropic) for multi-turn echo-back

  ReasoningContentPart() = default;
  ReasoningContentPart(std::string t, std::string sig = "")
      : text(std::move(t)), signature(std::move(sig)) {}
};

// An image attached to a conversation turn (base64 payload, wire and storage).
// Emitted from server `attachments`, and per request from `image` tool
// results (ProviderTransform::lift_tool_result_images). Each provider's
// request builder converts it to its own format: OpenAI-compatible
// `image_url` data URL (or `input_image` on the Responses API), Anthropic
// base64 `image` block, Gemini/Antigravity `inlineData` part.
struct ImageContentPart {
  std::string data;       // base64-encoded image bytes
  std::string mime_type;  // e.g. "image/png"
  std::string description;

  ImageContentPart(std::string d, std::string m, std::string desc = "")
      : data(std::move(d)),
        mime_type(std::move(m)),
        description(std::move(desc)) {}
};

// Content part variant
using ContentPart =
    std::variant<TextContentPart, ToolCallContentPart, ToolResultContentPart,
                 ReasoningContentPart, ImageContentPart>;

// Message content is now a vector of content parts
using MessageContent = std::vector<ContentPart>;

struct Message {
  MessageRole role;
  MessageContent content;

  Message(MessageRole r, MessageContent c) : role(r), content(std::move(c)) {}

  // Factory methods for convenience
  static Message system(const std::string& text) {
    return Message(kMessageRoleSystem, {TextContentPart{text}});
  }

  static Message user(const std::string& text) {
    return Message(kMessageRoleUser, {TextContentPart{text}});
  }

  static Message user_with_images(const std::string& text,
                                  const std::vector<ImageContentPart>& images) {
    MessageContent parts;
    if (!text.empty()) {
      parts.emplace_back(TextContentPart{text});
    }
    for (const auto& img : images) {
      parts.emplace_back(img);
    }
    return Message(kMessageRoleUser, std::move(parts));
  }

  static Message assistant(const std::string& text) {
    return Message(kMessageRoleAssistant, {TextContentPart{text}});
  }

  static Message assistant_with_tools(
      const std::string& text,
      const std::vector<ToolCallContentPart>& tools) {
    MessageContent content_parts;

    // Add text content if not empty
    if (!text.empty()) {
      content_parts.emplace_back(TextContentPart{text});
    }

    // Add tool calls
    for (const auto& tool : tools) {
      content_parts.emplace_back(
          ToolCallContentPart{tool.id, tool.tool_name, tool.arguments,
                              tool.thought_signature});
    }

    return Message(kMessageRoleAssistant, std::move(content_parts));
  }

  static Message assistant_with_reasoning(
      const std::string& text, const std::string& reasoning,
      const std::string& signature = "") {
    MessageContent content_parts;
    if (!reasoning.empty()) {
      content_parts.emplace_back(
          ReasoningContentPart{reasoning, signature});
    }
    if (!text.empty()) {
      content_parts.emplace_back(TextContentPart{text});
    }
    return Message(kMessageRoleAssistant, std::move(content_parts));
  }

  static Message tool_results(
      const std::vector<ToolResultContentPart>& results) {
    MessageContent content_parts;
    for (const auto& result : results) {
      content_parts.emplace_back(ToolResultContentPart{
          result.tool_call_id, result.result, result.is_error});
    }
    return Message(kMessageRoleUser, std::move(content_parts));
  }

  // Helper methods
  bool has_text() const {
    return std::any_of(content.begin(), content.end(),
                       [](const ContentPart& part) {
                         return std::holds_alternative<TextContentPart>(part);
                       });
  }

  bool has_images() const {
    return std::any_of(content.begin(), content.end(),
                       [](const ContentPart& part) {
                         return std::holds_alternative<ImageContentPart>(part);
                       });
  }

  std::vector<ImageContentPart> get_images() const {
    std::vector<ImageContentPart> out;
    for (const auto& part : content) {
      if (const auto* ip = std::get_if<ImageContentPart>(&part)) {
        out.push_back(*ip);
      }
    }
    return out;
  }

  bool has_tool_calls() const {
    return std::any_of(
        content.begin(), content.end(), [](const ContentPart& part) {
          return std::holds_alternative<ToolCallContentPart>(part);
        });
  }

  bool has_tool_results() const {
    return std::any_of(
        content.begin(), content.end(), [](const ContentPart& part) {
          return std::holds_alternative<ToolResultContentPart>(part);
        });
  }

  bool has_reasoning() const {
    return std::any_of(
        content.begin(), content.end(), [](const ContentPart& part) {
          return std::holds_alternative<ReasoningContentPart>(part);
        });
  }

  std::string get_reasoning() const {
    std::string result;
    for (const auto& part : content) {
      if (const auto* rp = std::get_if<ReasoningContentPart>(&part)) {
        result += rp->text;
      }
    }
    return result;
  }

  std::string get_text() const {
    std::string result;
    for (const auto& part : content) {
      if (const auto* text_part = std::get_if<TextContentPart>(&part)) {
        result += text_part->text;
      }
    }
    return result;
  }

  std::vector<ToolCallContentPart> get_tool_calls() const {
    std::vector<ToolCallContentPart> result;
    for (const auto& part : content) {
      if (const auto* tool_part = std::get_if<ToolCallContentPart>(&part)) {
        result.emplace_back(tool_part->id, tool_part->tool_name,
                            tool_part->arguments,
                            tool_part->thought_signature);
      }
    }
    return result;
  }

  std::vector<ToolResultContentPart> get_tool_results() const {
    std::vector<ToolResultContentPart> result;
    for (const auto& part : content) {
      if (const auto* result_part = std::get_if<ToolResultContentPart>(&part)) {
        result.emplace_back(result_part->tool_call_id, result_part->result,
                            result_part->is_error);
      }
    }
    return result;
  }

  std::string roleToString() const {
    switch (role) {
      case kMessageRoleSystem:
        return "system";
      case kMessageRoleUser:
        return "user";
      case kMessageRoleAssistant:
        return "assistant";
      default:
        return "unknown";
    }
  }
};

using Messages = std::vector<Message>;

// Apply compaction cutoff: discard all messages before the latest compaction summary message.
// Takes the history by value: pass an rvalue to avoid copying it.
inline Messages apply_compaction_cutoff(Messages messages) {
  for (auto it = messages.rbegin(); it != messages.rend(); ++it) {
    if (it->role == kMessageRoleUser) {
      std::string text;
      for (const auto& part : it->content) {
        if (const auto* tp = std::get_if<TextContentPart>(&part)) {
          text += tp->text;
        }
      }
      if (text.find("This conversation was compacted into a handoff packet") != std::string::npos) {
        messages.erase(messages.begin(), (it + 1).base());
        return messages;
      }
    }
  }
  return messages;
}

namespace detail {
// Position of call `id` among `assistant`'s tool calls, -1 when absent.
inline int tool_call_index(const Message& assistant, const std::string& id) {
  int k = 0;
  for (const auto& part : assistant.content) {
    if (const auto* call = std::get_if<ToolCallContentPart>(&part)) {
      if (call->id == id) return k;
      ++k;
    }
  }
  return -1;
}

// Call id answered by a tool-result message (its first result).
inline const std::string* first_tool_result_id(const Message& message) {
  for (const auto& part : message.content) {
    if (const auto* result = std::get_if<ToolResultContentPart>(&part)) {
      return &result->tool_call_id;
    }
  }
  return nullptr;
}

inline bool is_tool_result_message(const Message& message) {
  return message.has_tool_results() && !message.has_tool_calls();
}
}  // namespace detail

// Where the result of `tool_call_id` goes in a live history so the trailing
// step's results follow its calls in call order - the order the tool loop
// sends them - whatever order parallel tools finish in. The end when the
// call is not part of the trailing step.
inline size_t tool_result_insert_position(const Messages& history,
                                          const std::string& tool_call_id) {
  size_t first_result = history.size();
  while (first_result > 0 &&
         detail::is_tool_result_message(history[first_result - 1])) {
    --first_result;
  }
  if (first_result == 0) return history.size();
  const Message& assistant = history[first_result - 1];
  if (assistant.role != kMessageRoleAssistant) return history.size();
  const int k = detail::tool_call_index(assistant, tool_call_id);
  if (k < 0) return history.size();
  size_t pos = first_result;
  while (pos < history.size()) {
    const std::string* id = detail::first_tool_result_id(history[pos]);
    const int other = id != nullptr ? detail::tool_call_index(assistant, *id) : -1;
    if (other < 0 || other > k) break;
    ++pos;
  }
  return pos;
}

// The same order for a whole history (session reload): the result messages
// right after each tool-calling assistant message sorted by call position.
inline void order_tool_results_by_call(Messages& history) {
  for (size_t i = 0; i < history.size(); ++i) {
    const Message& assistant = history[i];
    if (assistant.role != kMessageRoleAssistant || !assistant.has_tool_calls()) {
      continue;
    }
    size_t end = i + 1;
    while (end < history.size() && detail::is_tool_result_message(history[end])) {
      ++end;
    }
    if (end - i > 2) {
      auto rank = [&assistant](const Message& message) {
        const std::string* id = detail::first_tool_result_id(message);
        const int k = id != nullptr ? detail::tool_call_index(assistant, *id) : -1;
        return k < 0 ? INT_MAX : k;
      };
      std::stable_sort(history.begin() + static_cast<std::ptrdiff_t>(i + 1),
                       history.begin() + static_cast<std::ptrdiff_t>(end),
                       [&rank](const Message& a, const Message& b) {
                         return rank(a) < rank(b);
                       });
    }
    i = end - 1;
  }
}

}  // namespace qcode