#pragma once

#include <qcode/core/stream_options.h>
#include "core/http_request_handler.h"
#include "providers/base_provider_client.h"
#include "providers/cursor/cursor_kv.h"
#include "providers/cursor/cursor_request_builder.h"
#include "providers/cursor/cursor_response_parser.h"

#include <memory>
#include <string>
#include <vector>

namespace qcode {
namespace cursor {

// Cursor provider client.
//
// Cursor exposes two protobuf services:
//   - aiserver.v1.AiService   (api2.cursor.sh)        : model catalog (GetUsableModels)
//   - agent.v1.AgentService    (agentn...api5.cursor.sh): chat (RunSSE, ServerStreaming)
//
// Both authenticate with the same access token (from ~/.config/cursor/auth.json).
// Requests are connect-es enveloped protobuf; we manage two HttpRequestHandlers
// (one per host) because each needs its own base URL + timeout.
class CursorClient : public providers::BaseProviderClient {
 public:
  CursorClient(const std::string& access_token,
               const std::string& session_token = "",
               const std::string& aiserver_base_url = "https://api2.cursor.sh",
               const std::string& agent_base_url =
                   "https://agentn.global.api5.cursor.sh");

  StreamResult stream_text(const StreamOptions& options) override;
  GenerateResult generate_text(const GenerateOptions& options) override;
  std::string provider_name() const override;
  std::vector<std::string> supported_models() const override;
  std::vector<CursorModelInfo> available_models() const;
  bool supports_model(const std::string& model_name) const override;
  std::string config_info() const override;
  std::string default_model() const override;

 private:
  httplib::Headers build_headers(const std::string& token, bool agent) const;

  std::string access_token_;
  std::string session_token_;
  std::string aiserver_base_url_;
  std::string agent_base_url_;
  std::unique_ptr<http::HttpRequestHandler> aiserver_handler_;
  std::unique_ptr<http::HttpRequestHandler> agent_handler_;
  CursorRequestBuilder* cursor_request_builder_ = nullptr;
  CursorKv kv_;
};

}  // namespace cursor
}  // namespace qcode
