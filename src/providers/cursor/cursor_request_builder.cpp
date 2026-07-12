#include "cursor_request_builder.h"

#include "cursor_proto.h"

#include <variant>

#include <random>

namespace ai {
namespace cursor {

namespace {
std::string random_message_id() {
  static std::random_device rd;
  static std::mt19937 gen(rd());
  static std::uniform_int_distribution<int> d(0, 15);
  static const char* hex = "0123456789abcdef";
  std::string s;
  s.reserve(32);
  for (int i = 0; i < 32; ++i) s.push_back(hex[d(gen)]);
  return s;
}

// Pull a user-facing prompt out of GenerateOptions, preferring the explicit
// prompt and falling back to the last user message's text content.
std::string extract_prompt(const GenerateOptions& options) {
  if (!options.prompt.empty()) return options.prompt;
  for (auto it = options.messages.rbegin(); it != options.messages.rend();
       ++it) {
    for (const auto& part : it->content) {
      if (std::holds_alternative<TextContentPart>(part)) {
        const auto& tcp = std::get<TextContentPart>(part);
        if (!tcp.text.empty()) return tcp.text;
      } else if (std::holds_alternative<ReasoningContentPart>(part)) {
        const auto& rcp = std::get<ReasoningContentPart>(part);
        if (!rcp.text.empty()) return rcp.text;
      }
    }
  }
  return "";
}
}  // namespace

nlohmann::json CursorRequestBuilder::build_request_json(
    const GenerateOptions&) {
  return nlohmann::json::object();
}

nlohmann::json CursorRequestBuilder::build_request_json(
    const EmbeddingOptions&) {
  return nlohmann::json::object();
}

httplib::Headers CursorRequestBuilder::build_headers(
    const providers::ProviderConfig& config) {
  httplib::Headers headers;
  if (!config.api_key.empty()) {
    headers.emplace(config.auth_header_name,
                    config.auth_header_prefix + config.api_key);
  }
  for (const auto& [k, v] : config.extra_headers) headers.emplace(k, v);
  return headers;
}

std::string CursorRequestBuilder::build_get_usable_models_request() const {
  // GetUsableModelsRequest { 1: custom_model_ids:string[] } -- empty payload.
  // aiserver unary uses Content-Type "application/proto" and sends the RAW
  // protobuf message (NO connect envelope). An empty request is zero bytes.
  return "";
}

std::string CursorRequestBuilder::build_agent_run_request(
    const GenerateOptions& options) const {
  const std::string model = options.model.empty() ? "default" : options.model;
  const std::string prompt = extract_prompt(options);

  // 1: conversation_state { 1: root_prompt_messages_json }
  nlohmann::json root = nlohmann::json::array();
  root.push_back({{"role", "user"}, {"content", prompt}});
  const std::string cs_json = root.dump();
  const std::string conversation_state = proto::bytes_field(1, cs_json);

  // 2: action { 1: user_message_action { 1: user_message { 1:text, 2:id } } }
  const std::string message_id = random_message_id();
  const std::string user_message =
      proto::bytes_field(1, prompt) + proto::bytes_field(2, message_id);
  const std::string user_message_action = proto::bytes_field(1, user_message);
  const std::string action = proto::bytes_field(1, user_message_action);

  // 3: model_details { 1: model_id }
  const std::string model_details = proto::bytes_field(1, model);
  // 9: requested_model { 1: model_id }
  const std::string requested_model = proto::bytes_field(1, model);

  std::string request =
      proto::bytes_field(1, conversation_state) +
      proto::bytes_field(2, action) + proto::bytes_field(3, model_details) +
      proto::bytes_field(9, requested_model) + proto::bytes_field(18, model);
  return proto::envelope(request);
}

}  // namespace cursor
}  // namespace ai
