#include "cursor_request_builder.h"

#include "cursor_proto.h"

#include <random>
#include <sstream>
#include <variant>

namespace ai {
namespace cursor {

namespace {
std::string random_id() {
  static std::random_device rd;
  static std::mt19937 gen(rd());
  static std::uniform_int_distribution<int> d(0, 15);
  static const char* hex = "0123456789abcdef";
  std::string s;
  s.reserve(32);
  for (int i = 0; i < 32; ++i) s.push_back(hex[d(gen)]);
  return s;
}

// Cursor manages prompt caching on its backend. Keep the system prompt and
// conversation in a stable prefix so subsequent turns can reuse that cache.
std::string build_conversation_prompt(const GenerateOptions& options) {
  std::ostringstream prompt;
  if (options.reasoning_effort && !options.reasoning_effort->empty()) {
    // Stable first line so cache prefixes stay aligned across turns.
    prompt << "System:\nReasoning effort: " << *options.reasoning_effort
           << ".\n";
    if (!options.system.empty()) {
      prompt << options.system << "\n\n";
    } else {
      prompt << "\n";
    }
  } else if (!options.system.empty()) {
    prompt << "System:\n" << options.system << "\n\n";
  }

  for (const auto& message : options.messages) {
    prompt << message.roleToString() << ":\n";
    for (const auto& part : message.content) {
      if (const auto* text = std::get_if<TextContentPart>(&part)) {
        prompt << text->text;
      } else if (const auto* reasoning =
                     std::get_if<ReasoningContentPart>(&part)) {
        prompt << reasoning->text;
      } else if (const auto* call =
                     std::get_if<ToolCallContentPart>(&part)) {
        prompt << "[tool call " << call->tool_name << ": "
               << call->arguments.dump() << ']';
      } else if (const auto* result =
                     std::get_if<ToolResultContentPart>(&part)) {
        prompt << "[tool result: " << result->result.dump() << ']';
      }
    }
    prompt << "\n\n";
  }

  if (options.messages.empty() && !options.prompt.empty()) {
    prompt << options.prompt;
  } else if (!options.prompt.empty() &&
             (options.messages.empty() ||
              options.messages.back().get_text() != options.prompt)) {
    prompt << "user:\n" << options.prompt;
  }
  return prompt.str();
}

// Prefer the TUI/server session id so multi-turn requests share one Cursor
// conversation id (helps backend prompt cache). Fall back to a random id.
std::string stable_conversation_id(const GenerateOptions& options) {
  if (!options.session_id.empty()) {
    return options.session_id;
  }
  return random_id();
}

// agent.v1.AgentMode.AGENT_MODE_AGENT
constexpr uint64_t kAgentModeAgent = 1;
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
  const std::string prompt = build_conversation_prompt(options);
  const std::string conversation_id = stable_conversation_id(options);
  const std::string message_id = random_id();

  // ConversationState: for a fresh turn an empty state is valid; the action
  // carries the user message. (root_prompt_messages_json is repeated string.)
  const std::string conversation_state;

  // UserMessage { 1:text, 2:message_id, 4:mode=AGENT }
  const std::string user_message =
      proto::bytes_field(1, prompt) + proto::bytes_field(2, message_id) +
      proto::varint_field(4, kAgentModeAgent);

  // UserMessageAction { 1:user_message }
  const std::string user_message_action = proto::bytes_field(1, user_message);

  // ConversationAction { 1:user_message_action (oneof action) }
  const std::string action = proto::bytes_field(1, user_message_action);

  // ModelDetails { 1:model_id }
  const std::string model_details = proto::bytes_field(1, model);
  // RequestedModel { 1:model_id }
  const std::string requested_model = proto::bytes_field(1, model);

  // AgentRunRequest — see agent.v1.AgentRunRequest field numbers.
  // Do NOT set field 18 (dev_raw_model_slug); that is not the model id.
  std::string request =
      proto::bytes_field(1, conversation_state) +
      proto::bytes_field(2, action) + proto::bytes_field(3, model_details) +
      proto::bytes_field(9, requested_model) +
      proto::bytes_field(5, conversation_id);

  // Return RAW protobuf. The caller wraps once in a connect-es envelope.
  return request;
}

std::string CursorRequestBuilder::build_agent_client_run_message(
    const GenerateOptions& options) const {
  // AgentClientMessage { 1: run_request }
  return proto::bytes_field(1, build_agent_run_request(options));
}

std::string CursorRequestBuilder::build_request_context_reply(
    uint32_t exec_id,
    const std::string& exec_id_str,
    const std::string& workspace_path) const {
  // RequestContextEnv { os_version, workspace_paths, shell, time_zone,
  //                     project_folder, process_working_directory }
  const std::string env =
      proto::bytes_field(1, "linux") +
      proto::bytes_field(2, workspace_path) + proto::bytes_field(3, "bash") +
      proto::varint_field(5, 0) +  // sandbox_enabled
      proto::bytes_field(10, "UTC") + proto::bytes_field(11, workspace_path) +
      proto::bytes_field(21, workspace_path);

  // RequestContext with env + "info complete" flags so the server proceeds
  // without waiting on further context blobs.
  const std::string request_context =
      proto::bytes_field(4, env) +
      proto::varint_field(33, 1) +   // git_repo_info_complete
      proto::varint_field(36, 1) +   // mcp_info_complete
      proto::varint_field(39, 1) +   // rules_info_complete
      proto::varint_field(40, 1) +   // env_info_complete
      proto::varint_field(41, 1) +   // repository_info_complete
      proto::varint_field(42, 1) +   // custom_subagents_info_complete
      proto::varint_field(43, 1) +   // agent_skills_info_complete
      proto::varint_field(44, 1);    // mcp_file_system_info_complete

  // RequestContextSuccess { 1: request_context }
  const std::string success = proto::bytes_field(1, request_context);
  // RequestContextResult { 1: success }
  const std::string result = proto::bytes_field(1, success);

  // ExecClientMessage { 1:id, 15:exec_id?, 10:request_context_result }
  std::string exec_msg = proto::varint_field(1, exec_id);
  if (!exec_id_str.empty()) {
    exec_msg += proto::bytes_field(15, exec_id_str);
  }
  exec_msg += proto::bytes_field(10, result);

  // AgentClientMessage { 2: exec_client_message }
  return proto::bytes_field(2, exec_msg);
}

}  // namespace cursor
}  // namespace ai
