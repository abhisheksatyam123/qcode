#include "cursor_client.h"

#include "ai/logger.h"
#include "providers/cursor/cursor_proto.h"
#include "providers/cursor/cursor_stream.h"

#include <stdexcept>

namespace ai {
namespace cursor {
namespace {

constexpr const char* kAiserverPath = "/aiserver.v1.AiService/GetUsableModels";
constexpr const char* kAgentPath = "/agent.v1.AgentService/RunSSE";
constexpr const char* kClientVersion = "cli-2026.07.09-a3815c0";

providers::ProviderConfig make_config(const std::string& api_key,
                                    const std::string& base_url) {
  providers::ProviderConfig cfg;
  cfg.api_key = api_key;
  cfg.base_url = base_url;
  cfg.completions_endpoint_path = kAgentPath;
  cfg.embeddings_endpoint_path = "";
  cfg.auth_header_name = "Authorization";
  cfg.auth_header_prefix = "Bearer ";
  cfg.extra_headers = {};
  return cfg;
}

std::unique_ptr<http::HttpRequestHandler> make_handler(const std::string& url,
                                                      int read_timeout_sec) {
  auto cfg = http::HttpRequestHandler::parse_base_url(url);
  cfg.read_timeout_sec = read_timeout_sec;
  return std::make_unique<http::HttpRequestHandler>(cfg);
}

}  // namespace

CursorClient::CursorClient(const std::string& access_token,
                           const std::string& session_token,
                           const std::string& aiserver_base_url,
                           const std::string& agent_base_url)
    : BaseProviderClient(
          make_config(access_token, agent_base_url),
          std::make_unique<CursorRequestBuilder>(),
          std::make_unique<CursorResponseParser>()),
      access_token_(access_token),
      session_token_(session_token.empty() ? access_token : session_token) {
  cursor_request_builder_ =
      static_cast<CursorRequestBuilder*>(request_builder_.get());
  aiserver_handler_ = make_handler(aiserver_base_url, 30);
  agent_handler_ = make_handler(agent_base_url, 30);
  LOG_DEBUG("Cursor client init: aiserver={} agent={}", aiserver_base_url,
            agent_base_url);
}

httplib::Headers CursorClient::build_headers(const std::string& token,
                                            bool /*agent*/) const {
  httplib::Headers headers;
  headers.emplace("Authorization", "Bearer " + token);
  headers.emplace("connect-protocol-version", "1");
  headers.emplace("x-cursor-client-type", "cli");
  headers.emplace("x-cursor-client-version", kClientVersion);
  headers.emplace("x-ghost-mode", "false");
  return headers;
}

std::vector<std::string> CursorClient::supported_models() const {
  try {
    auto headers = build_headers(session_token_, false);
    std::string body = cursor_request_builder_->build_get_usable_models_request();
    auto res =
        aiserver_handler_->post(kAiserverPath, headers, body, "application/proto");
    if (!res.is_success()) {
      LOG_ERROR("Cursor GetUsableModels failed: {}", res.error_message());
      return {};
    }
    return CursorResponseParser::parse_get_usable_models(res.text);
  } catch (const std::exception& e) {
    LOG_ERROR("Cursor GetUsableModels exception: {}", e.what());
    return {};
  }
}

GenerateResult CursorClient::generate_text(const GenerateOptions& options) {
  try {
    auto headers = build_headers(access_token_, true);
    // agent RunSSE: wrap the raw request in the connect-es envelope.
    std::string body = proto::envelope(
        cursor_request_builder_->build_agent_run_request(options));
    auto res =
        agent_handler_->post(kAgentPath, headers, body, "application/connect+proto");
    if (!res.is_success()) {
      return GenerateResult(res.error_message());
    }
    std::string text = CursorResponseParser::parse_agent_stream_body(res.text);
    GenerateResult result(text, kFinishReasonStop, Usage());
    result.model = options.model;
    return result;
  } catch (const std::exception& e) {
    return GenerateResult(std::string("cursor generate_text error: ") + e.what());
  }
}

StreamResult CursorClient::stream_text(const StreamOptions& options) {
  GenerateResult result = generate_text(options);
  std::string text = result.is_success() ? result.text : result.error_message();
  return StreamResult(std::make_unique<CursorBufferedStream>(text));
}

std::string CursorClient::provider_name() const { return "cursor"; }

bool CursorClient::supports_model(const std::string&) const { return true; }

std::string CursorClient::default_model() const { return "default"; }

std::string CursorClient::config_info() const {
  return "Cursor (access-token auth via ~/.config/cursor/auth.json)";
}

}  // namespace cursor
}  // namespace ai
