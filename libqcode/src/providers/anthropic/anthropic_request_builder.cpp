#include "anthropic_request_builder.h"

#include <qcode/core/logger.h>
#include <qcode/transform/provider_transform.h>
#include "core/message_utils.h"

#include <unordered_set>

namespace qcode {
namespace anthropic {

nlohmann::json AnthropicRequestBuilder::build_request_json(
    const GenerateOptions& options) {
  nlohmann::json request;
  request["model"] = options.model;

  // Prompt-cache breakpoints (dsh/opencode "auto" policy: tools + system +
  // latest message) — but only for Claude/Anthropic model ids, so generic
  // Anthropic-compatible endpoints serving other models never see the field.
  const bool claude_route =
      options.model.find("claude") != std::string::npos ||
      options.model.find("anthropic") != std::string::npos;
  int max_tokens = options.max_tokens.value_or(4096);
  std::optional<int> thinking_budget = options.budget_tokens;
  if (!thinking_budget && options.reasoning_effort &&
      *options.reasoning_effort != "off" &&
      !options.reasoning_effort->empty()) {
    const auto& effort = *options.reasoning_effort;
    thinking_budget = effort == "low"      ? 2000
                      : effort == "medium" ? 8000
                      : effort == "max"    ? 24000
                                           : 16000;
  }
  if (thinking_budget.has_value()) {
    // Anthropic requires max_tokens > budget_tokens.
    max_tokens = std::max(max_tokens, *thinking_budget + 1024);
    request["thinking"] = {{"type", "enabled"},
                           {"budget_tokens", *thinking_budget}};
  }
  request["max_tokens"] = max_tokens;
  request["messages"] = nlohmann::json::array();

  // Automatic prompt caching: breakpoint advances with the conversation.
  request["cache_control"] = {{"type", "ephemeral"}};

  // Handle system message — content-block form so we can mark it cacheable.
  if (!options.system.empty()) {
    request["system"] = nlohmann::json::array(
        {{{"type", "text"},
          {"text", options.system},
          {"cache_control", {{"type", "ephemeral"}}}}});
  }

  // Build messages array
  if (!options.messages.empty()) {
    // Drop tool_results whose tool_use_id was never seen — Claude 400s on
    // orphans left behind by compaction / incomplete history reloads.
    std::unordered_set<std::string> seen_tool_use_ids;

    // Use provided messages
    for (const auto& msg : options.messages) {
      nlohmann::json message;

      // Handle different content types
      if (msg.has_tool_results()) {
        // Anthropic expects tool results as content arrays in user messages
        message["role"] = "user";
        message["content"] = nlohmann::json::array();

        for (const auto& result : msg.get_tool_results()) {
          if (!result.tool_call_id.empty() &&
              !seen_tool_use_ids.contains(result.tool_call_id)) {
            LOG_WARN(
                "anthropic_request_builder: dropping orphaned tool_result "
                "tool_use_id={}",
                result.tool_call_id);
            continue;
          }
          nlohmann::json tool_result_content;
          tool_result_content["type"] = "tool_result";
          tool_result_content["tool_use_id"] = result.tool_call_id;

          if (!result.is_error && result.result.is_object() &&
              result.result.contains("data") && result.result.contains("mime_type")) {
            nlohmann::json content_arr = nlohmann::json::array();
            std::string desc = "Image loaded: " + result.result.value("path", "image");
            if (result.result.contains("description") &&
                !result.result["description"].get<std::string>().empty()) {
              desc += " (" + result.result["description"].get<std::string>() + ")";
            }
            content_arr.push_back({{"type", "text"}, {"text", desc}});
            content_arr.push_back({
                {"type", "image"},
                {"source", {
                    {"type", "base64"},
                    {"media_type", result.result["mime_type"].get<std::string>()},
                    {"data", result.result["data"].get<std::string>()}
                }}
            });
            tool_result_content["content"] = std::move(content_arr);
          } else if (!result.is_error) {
            tool_result_content["content"] = result.result.dump();
          } else {
            tool_result_content["content"] = result.result.dump();
            tool_result_content["is_error"] = true;
          }

          message["content"].push_back(tool_result_content);
        }
        if (message["content"].empty()) continue;
      } else {
        // Handle messages with text and/or tool calls
        message["role"] = utils::message_role_to_string(msg.role);

        // Get text content and tool calls
        std::string text_content = msg.get_text();
        auto tool_calls = msg.get_tool_calls();
        for (const auto& tool_call : tool_calls) {
          if (!tool_call.id.empty()) seen_tool_use_ids.insert(tool_call.id);
        }

        // Anthropic expects content as array for mixed content or tool calls
        if (!tool_calls.empty() ||
            (msg.role == kMessageRoleAssistant &&
             (!text_content.empty() || msg.has_reasoning()))) {
          message["content"] = nlohmann::json::array();

          // Echo thinking blocks (required when extended thinking is enabled).
          // Must appear before the text block and carry the original signature.
          if (thinking_budget.has_value() && msg.has_reasoning()) {
            for (const auto& part : msg.content) {
              if (const auto* rp =
                      std::get_if<qcode::ReasoningContentPart>(&part)) {
                if (!rp->text.empty()) {
                  nlohmann::json thinking;
                  thinking["type"] = "thinking";
                  thinking["thinking"] = rp->text;
                  thinking["signature"] =
                      rp->signature.empty() ? "" : rp->signature;
                  message["content"].push_back(thinking);
                }
              }
            }
          }

          // Add text content if present
          if (!text_content.empty()) {
            message["content"].push_back(
                {{"type", "text"}, {"text", text_content}});
          }

          // Add tool use content
          for (const auto& tool_call : tool_calls) {
            message["content"].push_back({{"type", "tool_use"},
                                          {"id", tool_call.id},
                                          {"name", tool_call.tool_name},
                                          {"input", tool_call.arguments}});
          }
        } else if (!msg.get_images().empty()) {
          // Attachment message: text block + base64 image blocks.
          nlohmann::json arr = nlohmann::json::array();
          if (!text_content.empty()) {
            arr.push_back({{"type", "text"}, {"text", text_content}});
          }
          for (const auto& img : msg.get_images()) {
            arr.push_back({{"type", "image"},
                           {"source", {{"type", "base64"},
                                       {"media_type", img.mime_type},
                                       {"data", img.data}}}});
          }
          message["content"] = std::move(arr);
        } else if (!text_content.empty()) {
          // Simple text message (non-assistant or assistant with text only)
          message["content"] = text_content;
        } else {
          // Empty message, skip
          continue;
        }
      }

      // Anthropic requires alternating roles; consecutive same-role messages
      // (e.g. a directive appended after a trailing tool-result user message)
      // would 400. Merge them into one message instead.
      if (!request["messages"].empty() &&
          request["messages"].back().value("role", "") ==
              message.value("role", "")) {
        auto merge_block = [](nlohmann::json& dst_content,
                              const nlohmann::json& src_content) {
          nlohmann::json arr = nlohmann::json::array();
          auto append = [&arr](const nlohmann::json& c) {
            if (c.is_string()) {
              const auto& s = c.get_ref<const std::string&>();
              if (!s.empty()) {
                arr.push_back({{"type", "text"}, {"text", s}});
              }
            } else if (c.is_array()) {
              for (const auto& b : c) arr.push_back(b);
            }
          };
          if (dst_content.is_array()) {
            for (const auto& b : dst_content) arr.push_back(b);
          } else if (dst_content.is_string()) {
            append(dst_content);
          }
          append(src_content);
          dst_content = std::move(arr);
        };
        auto& prev = request["messages"].back();
        if (!prev.contains("content")) prev["content"] = nlohmann::json::array();
        if (!message.contains("content")) {
          message["content"] = nlohmann::json::array();
        }
        merge_block(prev["content"], message["content"]);
      } else {
        request["messages"].push_back(message);
      }
    }

    // Cache breakpoint on the latest message so the conversation prefix up
    // to it is written to the provider cache and reusable next request.
    if (claude_route && !request["messages"].empty()) {
      auto& last_msg = request["messages"].back();
      if (last_msg.contains("content")) {
        auto& content = last_msg["content"];
        if (content.is_string()) {
          const std::string text = content.get<std::string>();
          content = nlohmann::json::array(
              {{{"type", "text"},
                {"text", text},
                {"cache_control", {{"type", "ephemeral"}}}}});
        } else if (content.is_array() && !content.empty() &&
                   content.back().is_object()) {
          content.back()["cache_control"] = {{"type", "ephemeral"}};
        }
      }
    }
  } else {
    // Build from prompt
    if (!options.prompt.empty()) {
      nlohmann::json message;
      message["role"] = "user";
      message["content"] = options.prompt;
      request["messages"].push_back(message);
    }
  }

  // Add optional parameters
  if (options.temperature && !thinking_budget.has_value()) {
    request["temperature"] = *options.temperature;
  }

  if (options.top_p) {
    request["top_p"] = *options.top_p;
  }

  // Anthropic uses top_k instead of top_p for some control
  if (options.seed) {
    // Anthropic doesn't support seed directly, but we can add it as metadata
  }

  // Add tools if specified
  if (options.has_tools()) {
    LOG_DEBUG("Adding {} tools to Anthropic request",
                          options.tools.size());

    nlohmann::json tools_array = nlohmann::json::array();
    auto active_tool_names = options.get_active_tool_names();
    Model model_info(options.model, "anthropic");

    for (const auto& tool_name : active_tool_names) {
      auto it = options.tools.find(tool_name);
      if (it != options.tools.end()) {
        const auto& tool = it->second;
        auto normalized_schema =
            ProviderTransform::normalize_schema(tool.parameters_schema, model_info);

        nlohmann::json tool_def = {{"name", tool_name},
                                   {"description", tool.description},
                                   {"input_schema", normalized_schema}};

        tools_array.push_back(tool_def);
      }
    }

    if (!tools_array.empty()) {
      // Cache breakpoint on the last tool definition: tool schemas precede
      // the system prompt in the cacheable prefix.
      if (claude_route) {
        tools_array.back()["cache_control"] = {{"type", "ephemeral"}};
      }
      request["tools"] = tools_array;

      // Add tool choice if specified
      switch (options.tool_choice.type) {
        case ToolChoiceType::kAuto:
          request["tool_choice"] = {{"type", "auto"}};
          break;
        case ToolChoiceType::kRequired:
          request["tool_choice"] = {{"type", "any"}};
          break;
        case ToolChoiceType::kSpecific:
          if (options.tool_choice.tool_name) {
            request["tool_choice"] = {{"type", "tool"},
                                      {"name", *options.tool_choice.tool_name}};
          }
          break;
        case ToolChoiceType::kNone:
          // Anthropic doesn't have explicit "none" - just don't send tools
          break;
      }

      LOG_DEBUG("Added {} tools with choice: {}",
                            tools_array.size(),
                            options.tool_choice.to_string());
    }
  }

  return request;
}

nlohmann::json AnthropicRequestBuilder::build_request_json(
    const EmbeddingOptions& options) {
  // Note: Anthropic does not currently offer embeddings API
  // This is a placeholder for future compatibility or custom endpoints
  nlohmann::json request{{"model", options.model}, {"input", options.input}};
  return request;
}

httplib::Headers AnthropicRequestBuilder::build_headers(
    const providers::ProviderConfig& config) {
  const auto key =
      (!config.api_key.empty() && config.api_key != "__EMPTY__")
          ? config.api_key
          : (config.api_key == "__EMPTY__" ||
                     config.base_url.find("opencode.ai") != std::string::npos
                 ? "public"
                 : config.api_key);
  httplib::Headers headers = {
      {config.auth_header_name, config.auth_header_prefix + key}};

  // Add any extra headers
  for (const auto& [key, value] : config.extra_headers) {
    headers.emplace(key, value);
  }

  // Note: Content-Type is passed separately to httplib::Post() as content_type
  // parameter
  return headers;
}

}  // namespace anthropic
}  // namespace qcode
