#include "cursor_client.h"

#include "ai/logger.h"
#include "providers/cursor/cursor_http2.h"
#include "providers/cursor/cursor_proto.h"
#include "providers/cursor/cursor_stream.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <future>
#include <mutex>
#include <random>
#include <stdexcept>
#include <thread>

namespace ai {
namespace cursor {
namespace {

constexpr const char* kAiserverPath = "/aiserver.v1.AiService/GetUsableModels";
constexpr const char* kAgentModelsPath =
    "/agent.v1.AgentService/GetUsableModels";
constexpr const char* kBidiAppendPath = "/aiserver.v1.BidiService/BidiAppend";
constexpr const char* kAgentPath = "/agent.v1.AgentService/RunSSE";
constexpr const char* kClientVersion = "cli-2026.07.09-a3815c0";
constexpr const char* kDefaultAgentBase =
    "https://agentn.global.api5.cursor.sh";
constexpr const char* kDefaultAiserverBase = "https://api2.cursor.sh";

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

std::string random_id() {
  static std::random_device rd;
  static std::mt19937 gen(rd());
  static std::uniform_int_distribution<int> d(0, 15);
  static const char* hex = "0123456789abcdef";
  std::string s;
  s.reserve(36);
  for (int i = 0; i < 32; ++i) {
    if (i == 8 || i == 12 || i == 16 || i == 20) s.push_back('-');
    s.push_back(hex[d(gen)]);
  }
  return s;
}

std::vector<std::pair<std::string, std::string>> cursor_headers(
    const std::string& token, const std::string& request_id) {
  return {
      {"Authorization", "Bearer " + token},
      {"connect-protocol-version", "1"},
      {"x-cursor-client-type", "cli"},
      {"x-cursor-client-version", kClientVersion},
      {"x-ghost-mode", "false"},
      {"x-request-id", request_id},
  };
}

std::string default_workspace_path() {
  std::error_code ec;
  auto path = std::filesystem::current_path(ec);
  if (ec) return ".";
  return path.string();
}

std::string resolve_workspace(const GenerateOptions& options) {
  if (!options.workspace.empty()) return options.workspace;
  return default_workspace_path();
}

// Cursor often omits turn_ended on short replies. Heartbeats must NOT end the
// turn (they arrive while the agent is still tool-calling). End only when:
//   - explicit turn_ended, or
//   - stream closes, or
//   - idle: heartbeats keep arriving but no text/exec for kIdleEndTimeout.
constexpr auto kIdleEndTimeout = std::chrono::seconds(45);

bool idle_end_on_heartbeat(
    bool has_text,
    std::chrono::steady_clock::time_point last_activity) {
  if (!has_text) return false;
  return std::chrono::steady_clock::now() - last_activity >= kIdleEndTimeout;
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
      session_token_(session_token.empty() ? access_token : session_token),
      aiserver_base_url_(aiserver_base_url.empty() ? kDefaultAiserverBase
                                                   : aiserver_base_url),
      agent_base_url_(agent_base_url.empty() ? kDefaultAgentBase
                                             : agent_base_url) {
  cursor_request_builder_ =
      static_cast<CursorRequestBuilder*>(request_builder_.get());
  aiserver_handler_ = make_handler(aiserver_base_url_, 30);
  agent_handler_ = make_handler(agent_base_url_, 120);
  LOG_DEBUG("Cursor client init: aiserver={} agent={}", aiserver_base_url_,
            agent_base_url_);
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
  std::vector<std::string> ids;
  for (const auto& model : available_models()) {
    ids.emplace_back(model.id);
  }
  return ids;
}

std::vector<CursorModelInfo> CursorClient::available_models() const {
  try {
    const auto request_id = random_id();
    const auto body = cursor_request_builder_->build_get_usable_models_request();

    auto agent_headers = build_headers(access_token_, true);
    agent_headers.emplace("x-request-id", request_id);
    auto result = agent_handler_->post(kAgentModelsPath, agent_headers, body,
                                       "application/proto");
    if (result.is_success()) {
      auto models = CursorResponseParser::parse_get_usable_models(result.text);
      if (!models.empty()) return models;
      LOG_WARN("Cursor agent model catalog returned no models");
    } else {
      LOG_WARN("Cursor agent GetUsableModels failed: {}",
               result.error_message());
    }

    // Older Cursor accounts expose model discovery on the aiserver service.
    auto legacy_headers = build_headers(session_token_, false);
    legacy_headers.emplace("x-request-id", request_id);
    result = aiserver_handler_->post(kAiserverPath, legacy_headers, body,
                                     "application/proto");
    if (result.is_success()) {
      return CursorResponseParser::parse_get_usable_models(result.text);
    }
    LOG_ERROR("Cursor legacy GetUsableModels failed: {}",
              result.error_message());
  } catch (const std::exception& e) {
    LOG_ERROR("Cursor GetUsableModels exception: {}", e.what());
  }
  return {};
}

GenerateResult CursorClient::generate_text(const GenerateOptions& options) {
  try {
    const std::string request_id = random_id();
    const std::string run_sse_body =
        proto::envelope(proto::bytes_field(1, request_id));
    const std::string run_sse_url = agent_base_url_ + kAgentPath;
    auto headers = cursor_headers(access_token_, request_id);

    std::atomic<uint64_t> append_seqno{0};
    std::mutex append_mu;
    std::string collected_text;
    std::string stream_error;
    std::atomic<bool> turn_ended{false};
    std::atomic<bool> context_replied{false};
    auto last_activity = std::chrono::steady_clock::now();
    ConnectFrameBuffer frame_buf;

    auto bidi_append = [&](const std::string& client_message) -> bool {
      std::lock_guard<std::mutex> lock(append_mu);
      const uint64_t seq = append_seqno.fetch_add(1);
      const std::string bidi_request_id = proto::bytes_field(1, request_id);
      const std::string bidi_append_body =
          proto::bytes_field(2, bidi_request_id) +
          proto::varint_field(3, seq) +
          proto::bytes_field(4, client_message);

      auto append_headers = build_headers(access_token_, false);
      append_headers.emplace("x-request-id", request_id);
      auto append_res = aiserver_handler_->post(
          kBidiAppendPath, append_headers, bidi_append_body,
          "application/proto");
      if (!append_res.is_success()) {
        LOG_ERROR("Cursor BidiAppend failed seq={}: {}", seq,
                  append_res.error_message());
        return false;
      }
      LOG_DEBUG("Cursor BidiAppend ok request_id={} seq={}", request_id, seq);
      return true;
    };

    // Open RunSSE first, then append the run request (cursor-agent ordering).
    auto stream_future = std::async(std::launch::async, [&]() {
      return http2_post_stream(
          run_sse_url, headers, run_sse_body, "application/connect+proto",
          [&](std::string_view chunk) -> bool {
            for (const auto& payload : frame_buf.feed(chunk)) {
              const auto ev =
                  CursorResponseParser::classify_agent_payload(payload);
              using Kind = AgentStreamEvent::Kind;
              switch (ev.kind) {
                case Kind::kHeartbeat:
                  // Keepalives arrive during tool loops — never treat the first
                  // post-text heartbeat as end-of-turn. Only finish on idle.
                  if (idle_end_on_heartbeat(!collected_text.empty(),
                                            last_activity)) {
                    LOG_INFO(
                        "Cursor generate_text: idle end after heartbeat "
                        "(text_len={})",
                        collected_text.size());
                    turn_ended = true;
                    return false;
                  }
                  break;
                case Kind::kTextDelta:
                  collected_text += ev.text;
                  last_activity = std::chrono::steady_clock::now();
                  break;
                case Kind::kTurnEnded:
                  collected_text += ev.text;
                  turn_ended = true;
                  return false;  // abort HTTP/2 stream
                case Kind::kPostTextTokenDelta:
                  // Token accounting frames; do not end the turn here — longer
                  // replies interleave token_delta between text_delta chunks.
                  last_activity = std::chrono::steady_clock::now();
                  break;
                case Kind::kRequestContext: {
                  last_activity = std::chrono::steady_clock::now();
                  if (context_replied.exchange(true)) break;
                  const auto reply =
                      cursor_request_builder_->build_request_context_reply(
                          ev.exec_id, ev.exec_id_str,
                          resolve_workspace(options));
                  if (!bidi_append(reply)) {
                    stream_error = "Cursor request_context BidiAppend failed";
                    return false;
                  }
                  break;
                }
                case Kind::kError:
                  stream_error = ev.error;
                  return false;
                case Kind::kOther:
                  // Exec/tool frames while the native agent is working —
                  // keep the stream open and refresh activity.
                  last_activity = std::chrono::steady_clock::now();
                  break;
              }
            }
            return true;
          },
          /*timeout_sec=*/300);
    });

    // Give the stream a moment to establish before appending the request.
    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    const std::string client_message =
        cursor_request_builder_->build_agent_client_run_message(options);
    if (!bidi_append(client_message)) {
      (void)stream_future.get();
      return GenerateResult("Cursor BidiAppend failed for run_request");
    }

    auto http = stream_future.get();
    if (!stream_error.empty()) {
      return GenerateResult(stream_error);
    }
    if (!http.error.empty() && !http.aborted) {
      return GenerateResult(http.error);
    }
    if (!http.aborted && http.status != 0 && http.status != 200) {
      return GenerateResult(
          "Cursor RunSSE HTTP " + std::to_string(http.status) +
          (http.body.empty() ? "" : (": " + http.body.substr(0, 400))));
    }

    if (collected_text.empty()) {
      // Fallback: parse any buffered body we already have.
      collected_text =
          CursorResponseParser::parse_agent_stream_body(http.body);
    }
    if (collected_text.empty()) {
      LOG_WARN("Cursor RunSSE empty text body_size={} turn_ended={}",
               http.body.size(), turn_ended.load());
      return GenerateResult(
          "Cursor returned an empty response (check model id / auth).");
    }

    GenerateResult result(collected_text, kFinishReasonStop, Usage());
    result.model = options.model;
    result.provider_metadata =
        R"({"prompt_cache":"cursor_automatic","transport":"agent"})";
    return result;
  } catch (const std::exception& e) {
    return GenerateResult(std::string("cursor generate_text error: ") +
                          e.what());
  }
}

StreamResult CursorClient::stream_text(const StreamOptions& options) {
  auto impl = std::make_unique<CursorBufferedStream>();
  auto* impl_ptr = impl.get();

  std::thread t([impl_ptr, options, 
                 access_token = access_token_, 
                 agent_base_url = agent_base_url_, 
                 aiserver_base_url = aiserver_base_url_,
                 builder = cursor_request_builder_]() {
    ai::logger::set_thread_name("cursor-stream");
    try {
      const std::string request_id = random_id();
      const std::string run_sse_body =
          proto::envelope(proto::bytes_field(1, request_id));
      const std::string run_sse_url = agent_base_url + kAgentPath;
      auto headers = cursor_headers(access_token, request_id);

      std::atomic<uint64_t> append_seqno{0};
      std::mutex append_mu;
      std::string collected_text;
      std::string stream_error;
      std::atomic<bool> turn_ended{false};
      std::atomic<bool> context_replied{false};
      auto last_activity = std::chrono::steady_clock::now();
      ConnectFrameBuffer frame_buf;

      auto build_connect_headers = [](const std::string& token) {
        httplib::Headers h;
        h.emplace("Authorization", "Bearer " + token);
        h.emplace("connect-protocol-version", "1");
        h.emplace("x-cursor-client-type", "cli");
        h.emplace("x-cursor-client-version", kClientVersion);
        h.emplace("x-ghost-mode", "false");
        return h;
      };

      auto bidi_append = [&](const std::string& client_message) -> bool {
        std::lock_guard<std::mutex> lock(append_mu);
        const uint64_t seq = append_seqno.fetch_add(1);
        const std::string bidi_request_id = proto::bytes_field(1, request_id);
        const std::string bidi_append_body =
            proto::bytes_field(2, bidi_request_id) +
            proto::varint_field(3, seq) +
            proto::bytes_field(4, client_message);

        auto append_headers = build_connect_headers(access_token);
        append_headers.emplace("x-request-id", request_id);
        
        auto aiserver_h = make_handler(aiserver_base_url, 30);
        auto append_res = aiserver_h->post(
            kBidiAppendPath, append_headers, bidi_append_body,
            "application/proto");
        if (!append_res.is_success()) {
          LOG_ERROR("Cursor BidiAppend failed seq={}: {}", seq,
                    append_res.error_message());
          return false;
        }
        LOG_DEBUG("Cursor BidiAppend ok request_id={} seq={}", request_id, seq);
        return true;
      };

      // Open RunSSE stream first, then append the run request
      auto http_stream_task = [&]() -> Http2PostResult {
        return http2_post_stream(
            run_sse_url, headers, run_sse_body, "application/connect+proto",
            [&](std::string_view chunk) -> bool {
              if (impl_ptr->is_stopped()) {
                return false;
              }
              for (const auto& payload : frame_buf.feed(chunk)) {
                const auto ev =
                    CursorResponseParser::classify_agent_payload(payload);
                using Kind = AgentStreamEvent::Kind;
                switch (ev.kind) {
                  case Kind::kHeartbeat:
                    // Do not abort on the first post-text keepalive — Cursor
                    // keeps heartbeating while the native agent uses tools.
                    if (idle_end_on_heartbeat(!collected_text.empty(),
                                              last_activity)) {
                      LOG_INFO(
                          "Cursor stream_text: idle end after heartbeat "
                          "(text_len={})",
                          collected_text.size());
                      turn_ended = true;
                      impl_ptr->push_event(
                          StreamEvent(kStreamEventTypeFinish));
                      impl_ptr->mark_done();
                      return false;
                    }
                    break;
                  case Kind::kTextDelta:
                    collected_text += ev.text;
                    last_activity = std::chrono::steady_clock::now();
                    impl_ptr->push_event(StreamEvent(ev.text));
                    break;
                  case Kind::kTurnEnded:
                    if (!ev.text.empty()) {
                      collected_text += ev.text;
                      impl_ptr->push_event(StreamEvent(ev.text));
                    }
                    impl_ptr->push_event(StreamEvent(kStreamEventTypeFinish));
                    impl_ptr->mark_done();
                    turn_ended = true;
                    return false;
                  case Kind::kPostTextTokenDelta:
                    last_activity = std::chrono::steady_clock::now();
                    break;
                  case Kind::kRequestContext: {
                    last_activity = std::chrono::steady_clock::now();
                    if (context_replied.exchange(true)) break;
                    const auto reply =
                        builder->build_request_context_reply(
                            ev.exec_id, ev.exec_id_str,
                            resolve_workspace(options));
                    if (!bidi_append(reply)) {
                      stream_error = "Cursor request_context BidiAppend failed";
                      return false;
                    }
                    break;
                  }
                  case Kind::kError:
                    stream_error = ev.error;
                    return false;
                  case Kind::kOther:
                    last_activity = std::chrono::steady_clock::now();
                    break;
                }
              }
              return true;
            },
            /*timeout_sec=*/300);
      };

      auto stream_future = std::async(std::launch::async, http_stream_task);

      std::this_thread::sleep_for(std::chrono::milliseconds(150));
      if (impl_ptr->is_stopped()) {
        (void)stream_future.get();
        return;
      }

      const std::string client_message =
          builder->build_agent_client_run_message(options);
      if (!bidi_append(client_message)) {
        (void)stream_future.get();
        impl_ptr->push_event(StreamEvent(kStreamEventTypeError, "Cursor BidiAppend failed for run_request"));
        impl_ptr->mark_done();
        return;
      }

      auto http = stream_future.get();
      if (impl_ptr->is_stopped()) {
        return;
      }

      if (!stream_error.empty()) {
        impl_ptr->push_event(StreamEvent(kStreamEventTypeError, stream_error));
        impl_ptr->mark_done();
        return;
      }
      if (!http.error.empty() && !http.aborted) {
        impl_ptr->push_event(StreamEvent(kStreamEventTypeError, http.error));
        impl_ptr->mark_done();
        return;
      }
      if (!http.aborted && http.status != 0 && http.status != 200) {
        impl_ptr->push_event(StreamEvent(kStreamEventTypeError,
            "Cursor RunSSE HTTP " + std::to_string(http.status) +
            (http.body.empty() ? "" : (": " + http.body.substr(0, 400)))));
        impl_ptr->mark_done();
        return;
      }

      if (collected_text.empty() && !http.body.empty()) {
        collected_text =
            CursorResponseParser::parse_agent_stream_body(http.body);
        if (!collected_text.empty()) {
          impl_ptr->push_event(StreamEvent(collected_text));
        }
      }

      if (collected_text.empty()) {
        impl_ptr->push_event(StreamEvent(kStreamEventTypeError,
            "Cursor returned an empty response (check model id / auth)."));
      }
      impl_ptr->mark_done();
    } catch (const std::exception& e) {
      impl_ptr->push_event(StreamEvent(kStreamEventTypeError,
          std::string("Cursor streaming error: ") + e.what()));
      impl_ptr->mark_done();
    }
  });

  impl->start_thread(std::move(t));
  return StreamResult(std::move(impl));
}

std::string CursorClient::provider_name() const { return "cursor"; }

bool CursorClient::supports_model(const std::string&) const { return true; }

std::string CursorClient::default_model() const { return "default"; }

std::string CursorClient::config_info() const {
  return "Cursor (QCode AI SDK, HTTP/2 agent, automatic prompt caching)";
}

}  // namespace cursor
}  // namespace ai
