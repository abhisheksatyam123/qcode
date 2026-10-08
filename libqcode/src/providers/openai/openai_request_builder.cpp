#include "openai_request_builder.h"

#include <algorithm>
#include <functional>
#include <optional>
#include <vector>

#include <qcode/core/logger.h>
#include <qcode/core/random.h>
#include "providers/internal/opencode_zen_headers.h"
#include "core/message_utils.h"
#include <qcode/transform/gemini_transform.h>
#include "openai_cache.h"
#include "openai_responses.h"

namespace qcode {
namespace openai {

nlohmann::json OpenAIRequestBuilder::build_request_json(
    const GenerateOptions& options) {
  std::string model_id =
      ProviderTransform::chat_wire_model_id(transport_, options.model);
  std::string schema_provider = "openai";

  if (wire_protocol_ == "google") {
    schema_provider = "google";
  } else if (transport_ == ProviderTransform::ChatTransport::kOpenCodeZen) {
    schema_provider = "opencode";
  } else if (transport_ == ProviderTransform::ChatTransport::kOpenRouter) {
    schema_provider = "openrouter";
  }
  nlohmann::json request{{"model", model_id},
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

    // Close unpaired tool calls (TUI restart / abort mid-tool) so Responses
    // does not 400 with "No tool output found for function call". Tool
    // output is text-only on Chat and Responses: image tool results become
    // text plus a user image message after the tool messages.
    const auto messages = ProviderTransform::lift_tool_result_images(
        ProviderTransform::normalize_messages(
            options.messages, Model(options.model, schema_provider)));

    // Use provided messages
    for (const auto& msg : messages) {
      nlohmann::json message;

      // Handle different content types
      if (msg.has_tool_results()) {
        // OpenAI expects each tool result as a separate message with role
        // "tool"
        for (const auto& part : msg.content) {
          const auto* result = std::get_if<ToolResultContentPart>(&part);
          if (!result) continue;
          std::string result_id =
              ProviderTransform::canonicalize_tool_call_id(result->tool_call_id);
          if (!result->tool_call_id.empty() &&
              !seen_tool_call_ids.contains(result_id)) {
            LOG_DEBUG(
                "openai_request_builder: dropping orphaned tool result "
                "tool_call_id={}",
                result->tool_call_id);
            continue;
          }
          nlohmann::json tool_message;
          tool_message["role"] = "tool";
          tool_message["tool_call_id"] = std::move(result_id);

          if (!result->is_error) {
            tool_message["content"] = result->result.dump();
          } else {
            tool_message["content"] = "Error: " + result->result.dump();
          }

          request["messages"].push_back(std::move(tool_message));
        }
        continue;  // Skip adding the main message
      }

      // Handle messages with text and/or tool calls
      message["role"] = utils::message_role_to_string(msg.role);

      // Get text content (accumulate all text parts). Tool calls and images
      // are read in place below rather than copied out of the message.
      std::string text_content = msg.get_text();
      std::string reasoning_content = msg.get_reasoning();
      const bool has_tool_calls = msg.has_tool_calls();
      const bool has_images = msg.has_images();

      // Skip empty messages
      if (text_content.empty() && reasoning_content.empty() &&
          !has_tool_calls && !has_images) {
        continue;
      }

      // Set content - OpenAI expects both text and tool calls in the same
      // message. Image attachments force a content-part array (data URL for
      // Chat Completions, input_image for the Responses API).
      if (has_images) {
        nlohmann::json arr = nlohmann::json::array();
        if (!text_content.empty()) {
          arr.push_back({{"type", use_responses_ ? "input_text" : "text"},
                         {"text", std::move(text_content)}});
        }
        for (const auto& part : msg.content) {
          const auto* img = std::get_if<ImageContentPart>(&part);
          if (!img) continue;
          std::string url = "data:" + img->mime_type + ";base64," + img->data;
          if (use_responses_) {
            arr.push_back(
                {{"type", "input_image"}, {"image_url", std::move(url)}});
          } else {
            arr.push_back({{"type", "image_url"},
                           {"image_url", {{"url", std::move(url)}}}});
          }
        }
        message["content"] = std::move(arr);
      } else if (!text_content.empty()) {
        message["content"] = std::move(text_content);
      }
      if (!reasoning_content.empty()) {
        if (use_responses_) {
          // Responses API keeps the dedicated reasoning fields.
          message["reasoning"] = std::move(reasoning_content);
          for (const auto& part : msg.content) {
            if (const auto* reasoning =
                    std::get_if<ReasoningContentPart>(&part);
                reasoning != nullptr && !reasoning->signature.empty()) {
              message["reasoning_signature"] = reasoning->signature;
              break;
            }
          }
        } else if (const auto field = ProviderTransform::interleaved_replay_field(
                       transport_, options.model);
                   !field.empty()) {
          // OpenCode injects capabilities.interleaved.field onto
          // openaiCompatible (reasoning_content for Ox Alpha / DeepSeek,
          // reasoning for Muse Spark). OpenRouter skips this.
          message[field] = std::move(reasoning_content);
        }
      }

      if (has_tool_calls) {
        nlohmann::json tool_calls_array = nlohmann::json::array();
        for (const auto& part : msg.content) {
          const auto* tool_call = std::get_if<ToolCallContentPart>(&part);
          if (!tool_call) continue;
          std::string call_id =
              ProviderTransform::canonicalize_tool_call_id(tool_call->id);
          if (!tool_call->id.empty()) seen_tool_call_ids.insert(call_id);
          nlohmann::json encoded_call{
              {"id", std::move(call_id)},
              {"type", "function"},
              {"function",
               {{"name", tool_call->tool_name},
                {"arguments", tool_call->arguments.dump()}}}};
          // normalize_messages already drops signatures the target family
          // cannot replay. Encoding only when schema==google hid them from
          // Antigravity (inner builder stays openai, then convert_openai_to_gemini).
          if (!tool_call->thought_signature.empty()) {
            encoded_call["thought_signature"] =
                tool_call->thought_signature;
          }
          tool_calls_array.push_back(std::move(encoded_call));
        }
        message["tool_calls"] = std::move(tool_calls_array);
      }

      // Upstream lowerAssistantMessage sends an explicit null content for
      // reasoning/tool-call-only assistant messages.
      if (msg.role == kMessageRoleAssistant && !message.contains("content")) {
        message["content"] = nullptr;
      }

      request["messages"].push_back(std::move(message));
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
    Model model_info(options.model, schema_provider);

    for (const auto& tool_name : active_tool_names) {
      auto it = options.tools.find(tool_name);
      if (it != options.tools.end()) {
        const auto& tool = it->second;
        auto normalized_schema =
            ProviderTransform::normalize_schema(tool.parameters_schema, model_info);

        nlohmann::json tool_def = {{"type", "function"},
                                   {"function",
                                    {{"name", tool_name},
                                     {"description", tool.description},
                                     {"parameters", std::move(normalized_schema)}}}};

        tools_array.push_back(std::move(tool_def));
      }
    }

    if (!tools_array.empty()) {
      // Prompt-cache breakpoint on the last tool definition for Claude
      // models on cache-hinting transports (OpenRouter/OpenCode Zen honor
      // content-part cache_control; tool schemas are the first prefix
      // segment). Other transports rely on implicit prefix caching.
      const bool claude_on_hint_transport =
          (transport_ == ProviderTransform::ChatTransport::kOpenCodeZen ||
           transport_ == ProviderTransform::ChatTransport::kOpenRouter) &&
          is_claude_cache_model(options.model);
      if (claude_on_hint_transport) {
        tools_array.back()["cache_control"] = {{"type", "ephemeral"}};
      }
      request["tools"] = std::move(tools_array);

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
                            request["tools"].size(),
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

  const auto uses_opencode_cache_transport =
      transport_ == ProviderTransform::ChatTransport::kOpenCodeZen ||
      transport_ == ProviderTransform::ChatTransport::kOpenRouter;
  // OpenCode ProviderTransform.applyCaching: first 2 system + last 2
  // non-system, only for Claude/Anthropic. OpenRouter and Zen Claude honor
  // content-part cache_control; other models use implicit prefix cache.
  if (uses_opencode_cache_transport && is_claude_cache_model(model_id) &&
      request.contains("messages")) {
    apply_opencode_message_caching(request["messages"]);
  }

  // OpenCode options(): promptCacheKey = sessionID for Zen gpt-5 (not
  // gpt-5-chat). Do not send OpenRouter prompt_cache_key (undocumented).
  if (transport_ == ProviderTransform::ChatTransport::kOpenCodeZen &&
      is_opencode_gpt5_cache_model(model_id) && !options.session_id.empty()) {
    request["promptCacheKey"] = options.session_id;
    request["prompt_cache_key"] = options.session_id;
  }

  // OpenRouter only reports usage accounting (cached-token details) when
  // explicitly asked — mirrors upstream options(): usage.include = true.
  if (transport_ == ProviderTransform::ChatTransport::kOpenRouter && !use_responses_) {
    request["usage"] = {{"include", true}};
  }

  if (providers::is_opencode_zen_url(base_url_)) {
    // OpenCode Zen free tier requires bash and read tool definitions to be
    // declared in the wire request payload.
    if (!request.contains("tools") || !request["tools"].is_array()) {
      request["tools"] = nlohmann::json::array();
    }
    bool has_bash = false;
    bool has_read = false;
    for (const auto& t : request["tools"]) {
      if (t.contains("function") && t["function"].contains("name")) {
        const auto& name = t["function"]["name"];
        if (name == "bash") has_bash = true;
        if (name == "read") has_read = true;
      }
    }
    if (!has_bash) {
      request["tools"].push_back({
          {"type", "function"},
          {"function",
           {{"name", "bash"},
            {"description", "Run bash commands"},
            {"parameters",
             {{"type", "object"},
              {"properties", {{"command", {{"type", "string"}}}}},
              {"required", {"command"}}}}}}});
    }
    if (!has_read) {
      request["tools"].push_back({
          {"type", "function"},
          {"function",
           {{"name", "read"},
            {"description", "Read file contents"},
            {"parameters",
             {{"type", "object"},
              {"properties", {{"path", {{"type", "string"}}}}},
              {"required", {"path"}}}}}}});
    }
    if (!request.contains("tool_choice")) {
      request["tool_choice"] = "auto";
    }
    request["stream"] = true;
  }

  if (wire_protocol_ == "google") {
    return qcode::gemini::convert_openai_to_gemini(request);
  }

  if (!use_responses_) return request;

  return to_responses_request(request);
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

  // Config/JSON headers win over the built-in Referer/X-Title defaults.
  for (const auto& [key, value] : config.extra_headers) {
    headers.erase(key);
    headers.emplace(key, value);
  }

  // Apply last so a config/extra User-Agent cannot override the Zen client
  // identity. Without this, -free models return 429 FreeUsageLimitError.
  if (providers::is_opencode_zen_url(config.base_url)) {
    providers::apply_opencode_zen_headers(headers);
  }

  return headers;
}


nlohmann::json OpenAIRequestBuilder::build_stream_request_json(
    const StreamOptions& options) {
  auto request_json = build_request_json(options);
  if (wire_protocol_ != "google") {
    request_json["stream"] = true;
    ProviderTransform::apply_stream_options(
        request_json,
        ProviderTransform::chat_transport_for(base_url_),
        /*stream=*/true);
  }
  return request_json;
}

}  // namespace openai
}  // namespace qcode
