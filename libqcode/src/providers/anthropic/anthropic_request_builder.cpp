#include "anthropic_request_builder.h"
#include "anthropic_oauth.h"
#include "anthropic_thinking.h"

#include <qcode/core/logger.h>
#include <qcode/transform/provider_transform.h>
#include "core/message_utils.h"

#include <unordered_set>

namespace qcode {
namespace anthropic {

nlohmann::json AnthropicRequestBuilder::build_request_json(
    const GenerateOptions& options) {
  nlohmann::json request;
  std::string wire_model = options.model;
  if (wire_model.ends_with("-thinking")) {
    wire_model = wire_model.substr(0, wire_model.size() - 9);
  }
  request["model"] = wire_model;

  // Prompt-cache breakpoints (dsh/opencode "auto" policy: tools + system +
  // latest message) — but only for Claude/Anthropic model ids, so generic
  // Anthropic-compatible endpoints serving other models never see the field.
  const bool claude_route =
      options.model.find("claude") != std::string::npos ||
      options.model.find("anthropic") != std::string::npos;
  // Thinking form is model-dependent (see anthropic_thinking.h): Claude >=4.6
  // takes thinking{type:adaptive, display:summarized} + output_config{effort}
  // and ignores budget_tokens; older models keep the legacy budget form.
  const AnthropicThinkingPlan thinking =
      anthropic_plan_thinking(options.model, options.reasoning_effort,
                             options.budget_tokens, options.max_tokens);
  int max_tokens = thinking.max_tokens;
  if (thinking.thinking_on) {
    if (thinking.adaptive) {
      // display:summarized is what makes the reasoning text visible; without
      // it the API omits the thinking block. type:disabled is rejected (400)
      // by these models, so an effort-less turn simply sends no output_config.
      request["thinking"] = {{"type", "adaptive"},
                             {"display", "summarized"}};
      if (thinking.effort) {
        request["output_config"] = {{"effort", *thinking.effort}};
      }
    } else if (thinking.budget_tokens) {
      request["thinking"] = {{"type", "enabled"},
                             {"budget_tokens", *thinking.budget_tokens}};
    }
  }
  request["max_tokens"] = max_tokens;
  if (thinking.thinking_on) {
    if (thinking.adaptive) {
      LOG_INFO(
          "[anthropic] thinking mode=adaptive effort={} display=summarized "
          "max_tokens={} model={}",
          thinking.effort ? *thinking.effort : std::string("default"),
          max_tokens, wire_model);
    } else {
      LOG_INFO("[anthropic] thinking mode=budget budget={} max_tokens={} model={}",
               thinking.budget_tokens.value_or(0), max_tokens, wire_model);
    }
  } else {
    LOG_INFO("[anthropic] thinking mode=none max_tokens={} model={}",
             max_tokens, wire_model);
  }
  request["messages"] = nlohmann::json::array();

  // Automatic prompt caching: breakpoints live on system/messages/tools
  // content blocks only (top-level cache_control is not a valid API field).

  // Handle system message — content-block form so we can mark it cacheable.
  // NOTE: the Claude Code billing attribution (x-anthropic-billing-header)
  // travels as an HTTP header (see extra_headers in anthropic_client.cpp),
  // never as system-prompt text: any prefix byte change breaks the prompt
  // cache prefix match (proven live: billing-first system -> 0% hit rate).
  std::string system_text = options.system;
  if (is_oauth_) {
    system_text = rewrite_claude_prompt_tags(system_text);
  }

  if (is_oauth_) {
    nlohmann::json sys_arr = nlohmann::json::array();
    sys_arr.push_back({
        {"type", "text"},
        {"text", format_billing_header()}
    });
    if (!system_text.empty()) {
      sys_arr.push_back({
          {"type", "text"},
          {"text", system_text},
          {"cache_control", {{"type", "ephemeral"}}}
      });
    }
    request["system"] = std::move(sys_arr);
  } else if (!system_text.empty()) {
    request["system"] = nlohmann::json::array(
        {{{"type", "text"},
          {"text", system_text},
          {"cache_control", {{"type", "ephemeral"}}}}});
  }

  // Build messages array
  if (!options.messages.empty()) {
    // Drop tool_results whose tool_use_id was never seen — Claude 400s on
    // orphans left behind by compaction / incomplete history reloads.
    std::unordered_set<std::string> seen_tool_use_ids;

    // Image tool results arrive as text plus a user image message after the
    // tool results; the same-role merge below appends that image after the
    // tool_result blocks, which must lead the user turn. The history is
    // copied only when it holds an image to lift.
    const bool lift_images =
        ProviderTransform::has_tool_result_images(options.messages);
    const Messages lifted =
        lift_images ? ProviderTransform::lift_tool_result_images(options.messages)
                    : Messages{};
    const Messages& messages = lift_images ? lifted : options.messages;
    for (const auto& msg : messages) {
      nlohmann::json message;

      // Handle different content types
      if (msg.has_tool_results()) {
        // Anthropic expects tool results as content arrays in user messages
        message["role"] = "user";
        message["content"] = nlohmann::json::array();

        for (const auto& part : msg.content) {
          const auto* result = std::get_if<ToolResultContentPart>(&part);
          if (!result) continue;
          if (!result->tool_call_id.empty() &&
              !seen_tool_use_ids.contains(result->tool_call_id)) {
            LOG_WARN(
                "anthropic_request_builder: dropping orphaned tool_result "
                "tool_use_id={}",
                result->tool_call_id);
            continue;
          }
          nlohmann::json tool_result_content;
          tool_result_content["type"] = "tool_result";
          tool_result_content["tool_use_id"] = result->tool_call_id;

          if (!result->is_error) {
            tool_result_content["content"] = result->result.dump();
          } else {
            tool_result_content["content"] = result->result.dump();
            tool_result_content["is_error"] = true;
          }

          message["content"].push_back(std::move(tool_result_content));
        }
        if (message["content"].empty()) continue;
      } else {
        // Handle messages with text and/or tool calls.
        // The Anthropic Messages API accepts only user/assistant inside
        // `messages` - history system notes (e.g. the compaction handoff
        // written by /compact) must ride as user turns or the request 400s.
        message["role"] = msg.role == kMessageRoleSystem
                             ? "user"
                             : utils::message_role_to_string(msg.role);

        // Get text content; tool calls are read in place below.
        std::string text_content = msg.get_text();
        const bool has_tool_calls = msg.has_tool_calls();

        // Anthropic expects content as array for mixed content or tool calls
        if (has_tool_calls ||
            (msg.role == kMessageRoleAssistant &&
             (!text_content.empty() || msg.has_reasoning()))) {
          message["content"] = nlohmann::json::array();

          // Echo thinking blocks (required when extended thinking is enabled).
          // Must appear before the text block and carry the original signature.
          if (thinking.thinking_on && msg.has_reasoning()) {
            for (const auto& part : msg.content) {
              const auto* rp = std::get_if<qcode::ReasoningContentPart>(&part);
              if (!rp) continue;
              // Anthropic 400s on an unsigned thinking block, but an
              // empty thinking text is legal (and must ride along) as long as
              // the signature is present.
              if (rp->signature.empty()) continue;
              nlohmann::json thinking_block;
              thinking_block["type"] = "thinking";
              thinking_block["thinking"] = rp->text;
              thinking_block["signature"] = rp->signature;
              message["content"].push_back(std::move(thinking_block));
            }
          }

          // Add text content if present
          if (!text_content.empty()) {
            message["content"].push_back(
                {{"type", "text"}, {"text", std::move(text_content)}});
          }

          // Add tool use content
          for (const auto& part : msg.content) {
            const auto* tool_call = std::get_if<ToolCallContentPart>(&part);
            if (!tool_call) continue;
            if (!tool_call->id.empty()) seen_tool_use_ids.insert(tool_call->id);
            message["content"].push_back({{"type", "tool_use"},
                                          {"id", tool_call->id},
                                          {"name", tool_call->tool_name},
                                          {"input", tool_call->arguments}});
          }
        } else if (msg.has_images()) {
          // Attachment or image-tool message: text block + base64 images.
          nlohmann::json arr = nlohmann::json::array();
          if (!text_content.empty()) {
            arr.push_back({{"type", "text"}, {"text", std::move(text_content)}});
          }
          for (const auto& part : msg.content) {
            const auto* img = std::get_if<ImageContentPart>(&part);
            if (!img) continue;
            arr.push_back({{"type", "image"},
                           {"source", {{"type", "base64"},
                                       {"media_type", img->mime_type},
                                       {"data", img->data}}}});
          }
          message["content"] = std::move(arr);
        } else if (!text_content.empty()) {
          // Simple text message (non-assistant or assistant with text only)
          message["content"] = std::move(text_content);
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
        // Turn content into a block array in place: a string becomes one
        // text block (none if empty), anything else but an array is dropped.
        auto to_blocks = [](nlohmann::json& content) {
          if (content.is_array()) return;
          nlohmann::json arr = nlohmann::json::array();
          if (content.is_string() &&
              !content.get_ref<const std::string&>().empty()) {
            arr.push_back({{"type", "text"}, {"text", std::move(content)}});
          }
          content = std::move(arr);
        };
        auto& prev_content = request["messages"].back()["content"];
        auto& content = message["content"];
        to_blocks(prev_content);
        to_blocks(content);
        for (auto& block : content) prev_content.push_back(std::move(block));
      } else {
        request["messages"].push_back(std::move(message));
      }
    }

    // Fork-style rolling cache window (opencode applyCaching): mark the last
    // TWO user/tool turns, not just the latest message. Every server call
    // carries these breakpoints, so follow-up turns reuse the cached prefix
    // instead of paying full input again. Assistant-only tails are skipped:
    // ephemeral assistant chunks are rarely re-read, and marking them would
    // only pay write cost. Max 4 breakpoints total (Anthropic limit).
    if (claude_route && !request["messages"].empty()) {
      auto mark_breakpoint = [](nlohmann::json& msg) {
        if (!msg.contains("content")) return;
        auto& content = msg["content"];
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
      };
      int marked = 0;
      for (auto it = request["messages"].rbegin();
           it != request["messages"].rend() && marked < 2; ++it) {
        const std::string role = it->value("role", "");
        // tool_result turns ride as role=user; assistant text tails are skipped.
        if (role != "user") continue;
        mark_breakpoint(*it);
        ++marked;
      }
      // Fallback: conversation is assistant-only (e.g. single assistant stub
      // in tests) — mark the tail so at least one message breakpoint exists.
      if (marked == 0) mark_breakpoint(request["messages"].back());
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
  if (options.temperature && !thinking.thinking_on) {
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
                                   {"input_schema", std::move(normalized_schema)}};

        tools_array.push_back(std::move(tool_def));
      }
    }

    if (!tools_array.empty()) {
      // Cache breakpoint on the last tool definition: tool schemas precede
      // the system prompt in the cacheable prefix.
      if (claude_route) {
        tools_array.back()["cache_control"] = {{"type", "ephemeral"}};
      }
      request["tools"] = std::move(tools_array);

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
                            request["tools"].size(),
                            options.tool_choice.to_string());
    }
  }

  if (is_oauth_ && request.contains("context_management")) {
    request.erase("context_management");
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
