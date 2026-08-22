#include "openai_request_builder.h"

#include <algorithm>
#include <functional>
#include <unordered_set>

#include <qcode/core/logger.h>
#include <qcode/core/random.h>
#include "providers/opencode_zen_headers.h"
#include "utils/message_utils.h"

namespace qcode {


namespace openai {

nlohmann::json OpenAIRequestBuilder::build_request_json(
    const GenerateOptions& options) {
  nlohmann::json request{{"model", options.model},
                         {"messages", nlohmann::json::array()}};

  if (options.response_format) {
    request["response_format"] = options.response_format.value();
  }
  // Build messages array
  if (!options.messages.empty()) {
    // Prepend system as a message when caller supplies a messages array; the
    // OpenAI Chat Completions API takes the system prompt as a leading
    // role=system message rather than a separate top-level field.
    if (!options.system.empty()) {
      request["messages"].push_back(
          {{"role", "system"}, {"content", options.system}});
    }
    // Track assistant tool_call ids so orphaned tool results (common after
    // compaction / DB reload) are not sent — Claude rejects those with 400.
    std::unordered_set<std::string> seen_tool_call_ids;

    // Use provided messages
    for (const auto& msg : options.messages) {
      nlohmann::json message;

      // Handle different content types
      if (msg.has_tool_results()) {
        // OpenAI expects each tool result as a separate message with role
        // "tool"
        for (const auto& result : msg.get_tool_results()) {
          if (!result.tool_call_id.empty() &&
              !seen_tool_call_ids.contains(result.tool_call_id)) {
            LOG_WARN(
                "openai_request_builder: dropping orphaned tool result "
                "tool_call_id={}",
                result.tool_call_id);
            continue;
          }
          nlohmann::json tool_message;
          tool_message["role"] = "tool";
          tool_message["tool_call_id"] = result.tool_call_id;

          if (!result.is_error) {
            tool_message["content"] = result.result.dump();
          } else {
            tool_message["content"] = "Error: " + result.result.dump();
          }

          request["messages"].push_back(tool_message);
        }
        continue;  // Skip adding the main message
      }

      // Handle messages with text and/or tool calls
      message["role"] = utils::message_role_to_string(msg.role);

      // Get text content (accumulate all text parts)
      std::string text_content = msg.get_text();
      const auto reasoning_content = msg.get_reasoning();

      // Get tool calls
      auto tool_calls = msg.get_tool_calls();
      for (const auto& tool_call : tool_calls) {
        if (!tool_call.id.empty()) seen_tool_call_ids.insert(tool_call.id);
      }

      // Set content - OpenAI expects both text and tool calls in the same
      // message
      if (!text_content.empty()) {
        message["content"] = text_content;
      }
      if (!reasoning_content.empty()) {
        if (use_responses_) {
          // Responses API keeps the dedicated reasoning fields.
          message["reasoning"] = reasoning_content;
          for (const auto& part : msg.content) {
            if (const auto* reasoning =
                    std::get_if<ReasoningContentPart>(&part);
                reasoning != nullptr && !reasoning->signature.empty()) {
              message["reasoning_signature"] = reasoning->signature;
              break;
            }
          }
        } else {
          // Chat Completions transports replay interleaved reasoning through
          // the assistant "reasoning_content" back-channel, mirroring
          // upstream's lowerAssistantMessage (packages/llm openai-chat).
          message["reasoning_content"] = reasoning_content;
        }
      }

      if (!tool_calls.empty()) {
        nlohmann::json tool_calls_array = nlohmann::json::array();
        for (const auto& tool_call : tool_calls) {
          nlohmann::json encoded_call{
              {"id", tool_call.id},
              {"type", "function"},
              {"function",
               {{"name", tool_call.tool_name},
                {"arguments", tool_call.arguments.dump()}}}};
          if (!tool_call.thought_signature.empty()) {
            encoded_call["thought_signature"] =
                tool_call.thought_signature;
          }
          tool_calls_array.push_back(std::move(encoded_call));
        }
        message["tool_calls"] = tool_calls_array;
      }

      // Skip empty messages
      if (text_content.empty() && reasoning_content.empty() &&
          tool_calls.empty()) {
        continue;
      }

      request["messages"].push_back(message);
    }
  } else {
    // Build from system + prompt
    if (!options.system.empty()) {
      request["messages"].push_back(
          {{"role", "system"}, {"content", options.system}});
    }

    if (!options.prompt.empty()) {
      request["messages"].push_back(
          {{"role", "user"}, {"content", options.prompt}});
    }
  }

  // Add optional parameters
  if (options.temperature) {
    request["temperature"] = *options.temperature;
  }

  if (options.max_tokens) {
    if (use_responses_) {
      request["max_completion_tokens"] = *options.max_tokens;
    } else if (transport_ == ProviderTransform::ChatTransport::kOpenAI) {
      // Native OpenAI chat completions expects the completion-token budget.
      request["max_completion_tokens"] = *options.max_tokens;
    } else {
      // OpenAI-compatible endpoints (OpenCode Zen, OpenRouter, ...) follow the
      // upstream openai-chat lowering which uses max_tokens.
      request["max_tokens"] = *options.max_tokens;
    }
  } else if (options.reasoning_effort) {
    // o-series require a completion-token budget; provide a safe default.
    request[use_responses_ || transport_ == ProviderTransform::ChatTransport::kOpenAI
                ? "max_completion_tokens"
                : "max_tokens"] = 8192;
  }

  if (options.reasoning_effort && *options.reasoning_effort != "off" && !options.reasoning_effort->empty()) {
    if (use_responses_) {
      request["reasoning_effort"] = *options.reasoning_effort;
      request["reasoning"] = {{"effort", *options.reasoning_effort}};
    } else {
      // Transport-specific placement: OpenRouter wants reasoning:{effort},
      // plain compatible endpoints want reasoning_effort (upstream lowering).
      ProviderTransform::apply_reasoning_options(request, transport_,
                                                 options.reasoning_effort);
    }
  }

  if (options.top_p) {
    request["top_p"] = *options.top_p;
  }

  if (options.frequency_penalty) {
    request["frequency_penalty"] = *options.frequency_penalty;
  }

  if (options.presence_penalty) {
    request["presence_penalty"] = *options.presence_penalty;
  }

  if (options.seed) {
    request["seed"] = *options.seed;
  }

  // Add tools if specified
  if (options.has_tools()) {
    LOG_DEBUG("Adding {} tools to request", options.tools.size());

    nlohmann::json tools_array = nlohmann::json::array();
    auto active_tool_names = options.get_active_tool_names();

    for (const auto& tool_name : active_tool_names) {
      auto it = options.tools.find(tool_name);
      if (it != options.tools.end()) {
        const auto& tool = it->second;

        nlohmann::json tool_def = {{"type", "function"},
                                   {"function",
                                    {{"name", tool_name},
                                     {"description", tool.description},
                                     {"parameters", tool.parameters_schema}}}};

        tools_array.push_back(tool_def);
      }
    }

    if (!tools_array.empty()) {
      request["tools"] = tools_array;

      // Add tool choice if specified
      switch (options.tool_choice.type) {
        case ToolChoiceType::kAuto:
          request["tool_choice"] = "auto";
          break;
        case ToolChoiceType::kRequired:
          request["tool_choice"] = "required";
          break;
        case ToolChoiceType::kNone:
          request["tool_choice"] = "none";
          break;
        case ToolChoiceType::kSpecific:
          if (options.tool_choice.tool_name) {
            request["tool_choice"] = {
                {"type", "function"},
                {"function", {{"name", *options.tool_choice.tool_name}}}};
          }
          break;
      }

      LOG_DEBUG("Added {} tools with choice: {}",
                            tools_array.size(),
                            options.tool_choice.to_string());
    }
  }

  // OpenRouter / Anthropic-compatible Claude routes honor top-level
  // cache_control. Pure OpenAI ignores unknown fields safely.
  {
    const auto& model_id = options.model;
    const bool looks_claude =
        model_id.find("claude") != std::string::npos ||
        model_id.find("anthropic") != std::string::npos;
    if (looks_claude) {
      request["cache_control"] = {{"type", "ephemeral"}};
    }
  }

  // OpenRouter only reports usage accounting (cached-token details) when
  // explicitly asked — mirrors upstream options(): usage.include = true.
  if (transport_ == ProviderTransform::ChatTransport::kOpenRouter && !use_responses_) {
    request["usage"] = {{"include", true}};
  }

  if (!use_responses_) return request;

  nlohmann::json responses{{"model", request["model"]},
                           {"input", nlohmann::json::array()}};
  for (const auto& message : request["messages"]) {
    const auto role = message.value("role", "");
    if (role == "tool") {
      responses["input"].push_back(
          {{"type", "function_call_output"},
           {"call_id", message.value("tool_call_id", "")},
           {"output", message.value("content", "")}});
      continue;
    }
    if (message.contains("content")) {
      responses["input"].push_back(
          {{"role", role}, {"content", message["content"]}});
    }
    if (message.contains("tool_calls")) {
      for (const auto& call : message["tool_calls"]) {
        const auto& function = call["function"];
        responses["input"].push_back(
            {{"type", "function_call"},
             {"call_id", call.value("id", "")},
             {"name", function.value("name", "")},
             {"arguments", function.value("arguments", "{}")}});
      }
    }
  }
  if (request.contains("max_completion_tokens")) {
    responses["max_output_tokens"] = request["max_completion_tokens"];
  }
  if (request.contains("temperature")) {
    responses["temperature"] = request["temperature"];
  }
  if (request.contains("top_p")) responses["top_p"] = request["top_p"];
  if (request.contains("reasoning_effort")) {
    responses["reasoning"] = {{"effort", request["reasoning_effort"]}};
  }
  if (request.contains("tools")) {
    responses["tools"] = nlohmann::json::array();
    for (const auto& tool : request["tools"]) {
      const auto& function = tool["function"];
      responses["tools"].push_back(
          {{"type", "function"},
           {"name", function.value("name", "")},
           {"description", function.value("description", "")},
           {"parameters", function.value("parameters",
                                          nlohmann::json::object())}});
    }
  }
  return responses;
}

nlohmann::json OpenAIRequestBuilder::build_request_json(
    const EmbeddingOptions& options) {
  nlohmann::json request{{"model", options.model}, {"input", options.input}};

  // Set encoding format (default to float for compatibility)
  if (options.encoding_format) {
    request["encoding_format"] = options.encoding_format.value();
  } else {
    request["encoding_format"] = "float";
  }

  // Add dimensions if specified
  if (options.dimensions && options.dimensions.value() > 0) {
    request["dimensions"] = options.dimensions.value();
  }

  // Add user identifier if specified
  if (options.user) {
    request["user"] = options.user.value();
  }

  return request;
}

httplib::Headers OpenAIRequestBuilder::build_headers(
    const providers::ProviderConfig& config) {
  httplib::Headers headers;

  // Add auth header if api key is provided and not empty
  if (!config.api_key.empty() && config.api_key != "__EMPTY__") {
    headers.emplace(config.auth_header_name, config.auth_header_prefix + config.api_key);
  } else if (
      ProviderTransform::chat_transport_for(config.base_url) ==
      ProviderTransform::ChatTransport::kOpenCodeZen) {
    // Upstream injects apiKey="public" for the keyless Zen free pool
    // (packages/opencode/src/provider/provider.ts).
    headers.emplace(config.auth_header_name, config.auth_header_prefix + "public");
  }

  // OpenRouter specific headers
  if (config.base_url.find("openrouter.ai") != std::string::npos) {
    headers.emplace("HTTP-Referer", "https://opencode.ai");
    headers.emplace("X-Title", "OpenCode");
  }

  // Qualcomm specific headers (qpilot / qgenie)
  if (config.base_url.find("qualcomm.com") != std::string::npos) {
    std::string cli_name = "qgenie_cli";
    if (config.base_url.find("qpilot") != std::string::npos) {
      cli_name = "qpilot_cli";
    }
    std::string turn_id = qcode::utils::new_uuid();
    std::string session_id = qcode::utils::new_uuid();
    std::string enc_key = qcode::utils::random_hex(64);

    headers.emplace("originator", "codex_cli_rs");
    headers.emplace("version", "0.1.12");
    headers.emplace("user-agent", "x86_64 / linux / terminal / " + cli_name + " / 0.1.12");
    headers.emplace("session_id", session_id);
    headers.emplace("x-codex-beta-features", "multi_agent");
    headers.emplace("x-codex-turn-metadata", "{\"turn_id\":\"" + turn_id + "\",\"sandbox\":\"none\"}");
    headers.emplace("x-encrypted-key", enc_key);
  }

  // Add any extra headers
  for (const auto& [key, value] : config.extra_headers) {
    headers.emplace(key, value);
  }

  // Apply last so a config/extra User-Agent cannot override the Zen client
  // identity. Without this, -free models return 429 FreeUsageLimitError.
  if (providers::is_opencode_zen_url(config.base_url)) {
    providers::apply_opencode_zen_headers(headers);
  }

  return headers;
}

}  // namespace openai
}  // namespace qcode