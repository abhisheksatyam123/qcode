#include <qcode/transform/gemini_transform.h>
#include <qcode/transform/provider_transform.h>
#include <qcode/core/logger.h>
#include <qcode/core/random.h>

#include <algorithm>
#include <chrono>
#include <unordered_map>

namespace qcode {
namespace gemini {
namespace {

std::string openai_message_text(const nlohmann::json& msg) {
  if (!msg.contains("content")) {
    return {};
  }
  const auto& content = msg["content"];
  if (content.is_string()) {
    return content.get<std::string>();
  }
  if (!content.is_array()) {
    return {};
  }
  std::string text;
  for (const auto& part : content) {
    if (part.is_string()) {
      text += part.get<std::string>();
      continue;
    }
    if (part.is_object() && part.contains("text") && part["text"].is_string()) {
      text += part["text"].get<std::string>();
    }
  }
  return text;
}

// Gemini/Antigravity Schema protobuf rejects JSON Schema keywords such as
// exclusiveMinimum. Convert integer exclusive bounds to inclusive ones and
// drop other keys the wire proto does not define.
nlohmann::json sanitize_gemini_schema(nlohmann::json node) {
  if (node.is_array()) {
    for (auto& item : node) {
      item = sanitize_gemini_schema(std::move(item));
    }
    return node;
  }
  if (!node.is_object()) {
    return node;
  }

  auto apply_exclusive = [](nlohmann::json& obj, const char* exclusive_key,
                            const char* inclusive_key, int delta) {
    if (!obj.contains(exclusive_key)) {
      return;
    }
    const auto& exclusive = obj[exclusive_key];
    if (exclusive.is_number_integer()) {
      const int converted = exclusive.get<int>() + delta;
      bool replace = !obj.contains(inclusive_key) || !obj[inclusive_key].is_number();
      if (!replace) {
        const double existing = obj[inclusive_key].get<double>();
        replace = delta > 0 ? existing < converted : existing > converted;
      }
      if (replace) {
        obj[inclusive_key] = converted;
      }
    }
    obj.erase(exclusive_key);
  };
  apply_exclusive(node, "exclusiveMinimum", "minimum", 1);
  apply_exclusive(node, "exclusiveMaximum", "maximum", -1);

  static const char* kDrop[] = {
      "$schema", "$id", "$ref", "$comment", "const", "if", "then", "else",
      "unevaluatedProperties", "uniqueItems", "contentEncoding", "propertyNames",
      "prefixItems", "readOnly", "writeOnly"};
  for (const char* key : kDrop) {
    node.erase(key);
  }

  for (auto it = node.begin(); it != node.end(); ++it) {
    *it = sanitize_gemini_schema(std::move(*it));
  }
  return node;
}

nlohmann::json convert_openai_to_gemini_impl(const nlohmann::json& openai_req,
                                             const GeminiOptions& options) {
  nlohmann::json gemini_req = nlohmann::json::object();
  nlohmann::json contents = nlohmann::json::array();
  std::string system_instruction;
  std::unordered_map<std::string, std::string> tool_names;

  if (openai_req.contains("messages") && openai_req["messages"].is_array()) {
    for (const auto& msg : openai_req["messages"]) {
      const auto role = msg.value("role", "");
      auto text_content = openai_message_text(msg);
      if (role == "system") {
        if (!system_instruction.empty()) system_instruction += "\n\n";
        system_instruction += text_content;
        continue;
      }

      nlohmann::json parts = nlohmann::json::array();
      if (role != "tool" && !text_content.empty()) {
        parts.push_back({{"text", std::move(text_content)}});
      }
      // Chat-completions image_url data URLs -> Gemini inlineData parts.
      if (role == "user" && msg.contains("content") &&
          msg["content"].is_array()) {
        for (const auto& item : msg["content"]) {
          if (item.value("type", "") != "image_url" ||
              !item.contains("image_url") || !item["image_url"].is_object() ||
              !item["image_url"].contains("url")) {
            continue;
          }
          // By reference: the url carries the whole base64 payload.
          const auto& url =
              item["image_url"]["url"].get_ref<const std::string&>();
          if (url.rfind("data:", 0) == 0 &&
              url.find(";base64,") != std::string::npos) {
            const auto comma = url.find(";base64,");
            parts.push_back({{"inlineData",
                              {{"mimeType", url.substr(5, comma - 5)},
                               {"data", url.substr(comma + 8)}}}});
          }
        }
      }
      // Gemini 2.5+ 400s on thought parts without a thoughtSignature. Unsigned
      // reasoning from Grok/Muse must not be replayed as thought:true.
      if (msg.contains("reasoning") && !msg["reasoning"].is_null() &&
          msg.contains("reasoning_signature") &&
          msg["reasoning_signature"].is_string() &&
          !msg["reasoning_signature"].get<std::string>().empty()) {
        nlohmann::json part{{"text", msg["reasoning"]}, {"thought", true}};
        part["thoughtSignature"] = msg["reasoning_signature"];
        parts.push_back(std::move(part));
      }
      if (msg.contains("tool_calls") && msg["tool_calls"].is_array()) {
        for (const auto& call : msg["tool_calls"]) {
          const auto& function = call["function"];
          auto args = function.value("arguments", nlohmann::json{});
          if (args.is_string()) {
            try {
              args = nlohmann::json::parse(
                  args.get_ref<const std::string&>());
            } catch (...) {
              args = nlohmann::json::object();
            }
          }
          const auto call_id =
              ProviderTransform::canonicalize_tool_call_id(call.value("id", ""));
          const auto name = function.value("name", "");
          tool_names[call_id] = name;
          nlohmann::json function_part{
              {"functionCall",
               {{"id", call_id}, {"name", name}, {"args", std::move(args)}}}};
          // Gemini 2.5+/3 and Antigravity 400 if a functionCall part has no
          // thoughtSignature. Replay unsigned calls (other models, closed
          // unpaired tools) with the documented skip token.
          std::string sig;
          if (call.contains("thought_signature") &&
              call["thought_signature"].is_string()) {
            sig = call["thought_signature"].get<std::string>();
          }
          if (sig.empty()) {
            sig = "skip_thought_signature_validator";
          }
          function_part["thoughtSignature"] = std::move(sig);
          parts.push_back(std::move(function_part));
        }
      }
      if (role == "tool") {
        const auto call_id = ProviderTransform::canonicalize_tool_call_id(
            msg.value("tool_call_id", ""));
        // Skip orphaned tool results whose tool_call_id has no matching
        // tool_use (functionCall) in a preceding assistant message.  Claude
        // models reject these with a 400 "unexpected tool_use_id" error.
        if (!call_id.empty() && !tool_names.contains(call_id)) {
          LOG_WARN("gemini_transform: dropping orphaned tool result "
                   "tool_call_id={}", call_id);
          continue;
        }
        auto response = msg.value("content", nlohmann::json{});
        if (response.is_string()) {
          try {
            response = nlohmann::json::parse(
                response.get_ref<const std::string&>());
          } catch (...) {
          }
        }
        // functionResponse.response must be an object.
        if (!response.is_object()) {
          response = {{"result", std::move(response)}};
        }

        parts.push_back(
            {{"functionResponse",
              {{"id", call_id},
               {"name", tool_names.contains(call_id) ? tool_names[call_id] : ""},
               {"response", std::move(response)}}}});
      }
      if (parts.empty()) continue;
      // Images lifted out of a tool result (lift_tool_result_images) ride in
      // the functionResponse turn they belong to.
      const bool images_only =
          std::all_of(parts.begin(), parts.end(), [](const nlohmann::json& p) {
            return p.contains("inlineData");
          });
      if (role == "user" && images_only && !contents.empty() &&
          contents.back()["parts"][0].contains("functionResponse")) {
        for (auto& p : parts) contents.back()["parts"].push_back(std::move(p));
      } else {
        contents.push_back(
            {{"role", role == "assistant" ? "model" : "user"},
             {"parts", std::move(parts)}});
      }
    }
  }

  gemini_req["contents"] = std::move(contents);

  if (!system_instruction.empty()) {
    gemini_req["systemInstruction"] = {
        {"parts", {{{"text", system_instruction}}}}};
  }

  nlohmann::json gen_config = nlohmann::json::object();
  if (openai_req.contains("temperature")) {
    gen_config["temperature"] = openai_req["temperature"];
  }
  // The opencode.json output budget (max_tokens / limit.output / variant)
  // comes from the builder; the request's own keys may hold transport
  // fallbacks (e.g. the 8192 reasoning default), so they are only used when
  // no budget was passed.
  if (options.max_output_tokens > 0) {
    gen_config["maxOutputTokens"] = options.max_output_tokens;
  } else if (openai_req.contains("max_completion_tokens")) {
    gen_config["maxOutputTokens"] = openai_req["max_completion_tokens"];
  }
  if (openai_req.contains("top_p")) gen_config["topP"] = openai_req["top_p"];
  if (openai_req.contains("seed")) gen_config["seed"] = openai_req["seed"];
  std::string effort;
  if (openai_req.contains("reasoning_effort") &&
      openai_req["reasoning_effort"].is_string()) {
    effort = openai_req["reasoning_effort"].get<std::string>();
  } else if (openai_req.contains("reasoning") &&
             openai_req["reasoning"].is_object()) {
    effort = openai_req["reasoning"].value("effort", "");
  }
  // opencode.json owns the mapping: the variant's wire effort is the
  // thinkingLevel, its budget_tokens the thinkingBudget, and thinking.display
  // "omitted" turns thought text off. No effort (or "off") sends no
  // thinkingConfig, so the model thinks at its own default.
  if (!effort.empty() && effort != "off" && options.type != "disabled") {
    // includeThoughts is required to get thought text parts back. Without
    // it Gemini still bills thoughtsTokenCount but returns only signatures.
    nlohmann::json thinking_config = {{"thinkingLevel", effort},
                                      {"includeThoughts", options.display != "omitted"}};
    if (options.budget_tokens > 0) {
      thinking_config["thinkingBudget"] = options.budget_tokens;
    }
    gen_config["thinkingConfig"] = std::move(thinking_config);
  }
  gemini_req["generationConfig"] = std::move(gen_config);

  if (openai_req.contains("tools") && openai_req["tools"].is_array()) {
    nlohmann::json declarations = nlohmann::json::array();
    for (const auto& tool : openai_req["tools"]) {
      if (!tool.contains("function")) continue;
      const auto& function = tool["function"];
      declarations.push_back(
          {{"name", function.value("name", "")},
           {"description", function.value("description", "")},
           {"parameters", sanitize_gemini_schema(function.value(
                              "parameters", nlohmann::json::object()))}});
    }
    if (!declarations.empty()) {
      gemini_req["tools"] = {{{"functionDeclarations",
                               std::move(declarations)}}};
    }
  }
  if (openai_req.contains("tool_choice")) {
    auto mode = "AUTO";
    nlohmann::json allowed = nlohmann::json::array();
    if (openai_req["tool_choice"].is_string()) {
      const auto choice = openai_req["tool_choice"].get<std::string>();
      if (choice == "none") mode = "NONE";
      if (choice == "required") mode = "ANY";
    } else if (openai_req["tool_choice"].contains("function")) {
      mode = "ANY";
      allowed.push_back(
          openai_req["tool_choice"]["function"].value("name", ""));
    }
    gemini_req["toolConfig"]["functionCallingConfig"]["mode"] = mode;
    if (!allowed.empty()) {
      gemini_req["toolConfig"]["functionCallingConfig"]
                ["allowedFunctionNames"] = std::move(allowed);
    }
  }
  return gemini_req;
}

nlohmann::json wrap_antigravity_envelope_impl(nlohmann::json gemini_req,
                                              const std::string& model,
                                              const std::string& project_id) {
  nlohmann::json env = nlohmann::json::object();
  env["project"] = project_id.empty() ? "rising-fact-p41fc" : project_id;

  std::string req_uuid = utils::new_uuid();
  unsigned long long timestamp =
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count();

  env["requestId"] =
      "agent/" + req_uuid + "/" + std::to_string(timestamp) + "/" + req_uuid + "/0";
  env["request"] = std::move(gemini_req);

  std::string mapped_model = model;
  constexpr std::string_view antigravity_prefix = "antigravity-";
  if (mapped_model.starts_with(antigravity_prefix)) {
    mapped_model.erase(0, antigravity_prefix.size());
  }
  const auto thinking_level = [&env]() -> std::string {
    const auto& request = env["request"];
    if (request.contains("generationConfig") &&
        request["generationConfig"].contains("thinkingConfig")) {
      return request["generationConfig"]["thinkingConfig"].value(
          "thinkingLevel", "");
    }
    return {};
  };

  // Antigravity 404s the unsuffixed Flash ids. Wire SKUs are always
  // <base>-{low,medium,high} (cortexkit/opencode-antigravity-auth).
  const auto flash_effort_sku = [&thinking_level](const std::string& base) {
    const auto level = thinking_level();
    if (level == "low") return base + "-low";
    if (level == "high" || level == "max") return base + "-high";
    return base + "-medium";
  };

  if (mapped_model == "gemini-3-flash") {
    mapped_model = "gemini-3-flash-agent";
  } else if (mapped_model == "gemini-3.8-flash" ||
             mapped_model == "gemini-3.8-flash-low" ||
             mapped_model == "gemini-3.8-flash-medium" ||
             mapped_model == "gemini-3.8-flash-high") {
    mapped_model = flash_effort_sku("gemini-3.8-flash");
  } else if (mapped_model == "gemini-3.6-flash" ||
             mapped_model == "gemini-3.6-flash-low" ||
             mapped_model == "gemini-3.6-flash-medium" ||
             mapped_model == "gemini-3.6-flash-high") {
    mapped_model = flash_effort_sku("gemini-3.6-flash");
  } else if (mapped_model == "gemini-3.7-flash" ||
             mapped_model == "gemini-3.7-flash-low" ||
             mapped_model == "gemini-3.7-flash-medium" ||
             mapped_model == "gemini-3.7-flash-high") {
    mapped_model = flash_effort_sku("gemini-3.7-flash");
  } else if (mapped_model == "gemini-3.1-pro" || mapped_model == "gemini-3.1-pro-low" || mapped_model == "gemini-3-pro-high" || mapped_model == "gemini-3-pro-low" || mapped_model == "gemini-3-pro") {
    mapped_model = "gemini-3.1-pro-low";
  }
  env["model"] = mapped_model;
  env["userAgent"] = "antigravity";
  env["requestType"] = "agent";
  // No cache_control on this envelope. The Antigravity Gemini endpoint now
  // rejects the field outright ("Invalid JSON payload received. Unknown name
  // \"cache_control\"", HTTP 400) on systemInstruction.parts, contents.parts
  // and tool objects, for both Claude and Gemini SKUs. Prompt caching is
  // implicit server-side, so the Anthropic-style markers are dropped here.

  return env;
}

nlohmann::json normalize_gemini_response_impl(const nlohmann::json& response) {
  nlohmann::json normalized_response = response;

  nlohmann::json gemini_data = response;
  if (response.contains("response") && response["response"].is_object()) {
    gemini_data = response["response"];
  }

  if (gemini_data.contains("candidates")) {
    normalized_response = nlohmann::json::object();
    normalized_response["id"] =
        gemini_data.value("responseId", response.value("requestId", ""));
    normalized_response["model"] =
        gemini_data.value("modelVersion", response.value("model", ""));
    normalized_response["created"] = 0;

    nlohmann::json choices = nlohmann::json::array();
    auto& candidates = gemini_data["candidates"];
    if (!candidates.empty()) {
      auto& cand = candidates[0];
      nlohmann::json choice = nlohmann::json::object();
      choice["index"] = 0;

      nlohmann::json message = nlohmann::json::object();
      message["role"] = "assistant";

      std::string text_content;
      std::string reasoning_content;
      nlohmann::json reasoning_details = nlohmann::json::array();
      nlohmann::json tool_calls = nlohmann::json::array();
      if (cand.contains("content") && cand["content"].contains("parts")) {
        auto& parts = cand["content"]["parts"];
        for (const auto& part : parts) {
          if (part.contains("text")) {
            const auto text = part["text"].get<std::string>();
            const bool is_thought =
                part.contains("thought") &&
                (part["thought"].is_boolean() ? part["thought"].get<bool>()
                                              : true);
            if (is_thought) {
              reasoning_content += text;
              nlohmann::json detail{{"type", "text"}, {"text", text}};
              if (part.contains("thoughtSignature")) {
                detail["signature"] = part["thoughtSignature"];
              }
              reasoning_details.push_back(std::move(detail));
            } else {
              text_content += text;
            }
          }
          if (part.contains("functionCall")) {
            const auto& function = part["functionCall"];
            nlohmann::json tool_call{
                {"id", function.value("id", utils::new_uuid())},
                {"type", "function"},
                {"function",
                 {{"name", function.value("name", "")},
                  {"arguments",
                   function.value("args", nlohmann::json::object()).dump()}}}};
            if (part.contains("thoughtSignature")) {
              tool_call["thought_signature"] = part["thoughtSignature"];
            }
            tool_calls.push_back(std::move(tool_call));
          }
        }
      }
      message["content"] = text_content;
      if (!reasoning_content.empty()) message["reasoning"] = reasoning_content;
      if (!reasoning_details.empty()) {
        message["reasoning_details"] = std::move(reasoning_details);
      }
      if (!tool_calls.empty()) message["tool_calls"] = std::move(tool_calls);

      choice["message"] = message;

      std::string finish_reason_str = cand.value("finishReason", "STOP");
      if (finish_reason_str == "STOP")
        choice["finish_reason"] =
            message.contains("tool_calls") ? "tool_calls" : "stop";
      else if (finish_reason_str == "MAX_TOKENS")
        choice["finish_reason"] = "length";
      else
        choice["finish_reason"] = "content_filter";

      choices.push_back(choice);
    }
    normalized_response["choices"] = choices;

    if (gemini_data.contains("usageMetadata")) {
      auto& usage_meta = gemini_data["usageMetadata"];
      nlohmann::json usage = nlohmann::json::object();
      usage["prompt_tokens"] = usage_meta.value("promptTokenCount", 0);
      // Gemini reports thoughts apart from candidates but bills both as
      // output; completion_tokens includes them (reasoning stays a subset),
      // matching the OpenAI/Anthropic convention the stats and cost use.
      usage["completion_tokens"] = usage_meta.value("candidatesTokenCount", 0) +
                                   usage_meta.value("thoughtsTokenCount", 0);
      usage["total_tokens"] = usage_meta.value("totalTokenCount", 0);
      if (usage_meta.contains("cachedContentTokenCount")) {
        usage["prompt_tokens_details"] = {
            {"cached_tokens", usage_meta.value("cachedContentTokenCount", 0)}};
      }
      if (usage_meta.contains("thoughtsTokenCount")) {
        usage["completion_tokens_details"] = {
            {"reasoning_tokens", usage_meta.value("thoughtsTokenCount", 0)}};
        usage["reasoning_tokens"] = usage_meta.value("thoughtsTokenCount", 0);
      }
      normalized_response["usage"] = usage;
    }
  }

  return normalized_response;
}

}  // namespace

std::string new_uuid() { return utils::new_uuid(); }
std::string random_hex(size_t len) { return utils::random_hex(len); }

nlohmann::json convert_openai_to_gemini(const nlohmann::json& openai_req,
                                        const GeminiOptions& options) {
  return convert_openai_to_gemini_impl(openai_req, options);
}

nlohmann::json wrap_antigravity_envelope(nlohmann::json gemini_req,
                                         const std::string& model,
                                         const std::string& project_id) {
  return wrap_antigravity_envelope_impl(std::move(gemini_req), model,
                                        project_id);
}

nlohmann::json unwrap_envelope(const nlohmann::json& json) {
  if (json.contains("response") && json["response"].is_object()) {
    return json["response"];
  }
  return json;
}

nlohmann::json normalize_gemini_response(const nlohmann::json& response) {
  return normalize_gemini_response_impl(response);
}

}  // namespace gemini
}  // namespace qcode
