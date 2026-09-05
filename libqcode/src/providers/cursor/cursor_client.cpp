#include "cursor_client.h"

#include <qcode/core/logger.h>
#include "providers/cursor/cursor_agent_session.h"
#include "providers/cursor/cursor_bidi.h"
#include "providers/cursor/cursor_http2.h"
#include "providers/cursor/cursor_proto.h"
#include "providers/cursor/cursor_stream.h"

#include <algorithm>
#include <future>
#include <stdexcept>
#include <thread>

namespace qcode {
namespace cursor {

CursorClient::CursorClient(const std::string& access_token,
                           const std::string& session_token,
                           const std::string& aiserver_base_url,
                           const std::string& agent_base_url)
    : BaseProviderClient(
          make_agent_config(access_token, agent_base_url),
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
  aiserver_handler_ = make_agent_handler(aiserver_base_url_, 30);
  agent_handler_ = make_agent_handler(agent_base_url_, 120);
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
    auto headers = cursor_run_headers(access_token_, request_id);

    AgentSessionState state;
    AgentSessionHooks hooks;
    hooks.log_tag = "Cursor generate_text";

    CursorBidi bidi(aiserver_base_url_, access_token_, request_id,
                    kClientVersion);
    BidiAppend bidi_append = [&](const std::string& client_message) -> bool {
      return bidi.append(client_message, /*wait=*/true);
    };
    BidiAppend bidi_append_kv = [&](const std::string& client_message) -> bool {
      return bidi.append(client_message, /*wait=*/false);
    };

    // Open RunSSE first, then append the run request (cursor-agent ordering).
    auto stream_future = std::async(std::launch::async, [&]() {
      return http2_post_stream(
          run_sse_url, headers, run_sse_body, "application/connect+proto",
          [&](std::string_view chunk) -> bool {
            return feed_agent_chunk(state, hooks, chunk, bidi_append,
                                    bidi_append_kv, cursor_request_builder_,
                                    kv_, options);
          },
          /*timeout_sec=*/0,
          [&options]() {
            return options.abort_flag && options.abort_flag->load();
          });
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    const std::string client_message =
        cursor_request_builder_->build_agent_client_run_message(options);
    if (!bidi_append(client_message)) {
      (void)stream_future.get();
      return GenerateResult("Cursor BidiAppend failed for run_request");
    }

    auto http = stream_future.get();
    (void)bidi.flush();
    if (options.abort_flag && options.abort_flag->load()) {
      return GenerateResult("Aborted by user");
    }
    if (!state.stream_error.empty()) {
      return GenerateResult(state.stream_error);
    }
    if (!http.error.empty() && !http.aborted) {
      return GenerateResult(http.error);
    }
    if (!http.aborted && http.status != 0 && http.status != 200) {
      return GenerateResult(
          "Cursor RunSSE HTTP " + std::to_string(http.status) +
          (http.body.empty() ? "" : (": " + http.body.substr(0, 400))));
    }

    if (state.collected_text.empty()) {
      state.collected_text =
          CursorResponseParser::parse_agent_stream_body(http.body);
    }
    if (state.collected_text.empty()) {
      LOG_WARN("Cursor RunSSE empty text body_size={} turn_ended={}",
               http.body.size(), state.turn_ended.load());
      return GenerateResult(
          "Cursor returned an empty response (check model id / auth).");
    }

    Usage usage;
    if (!state.collected_reasoning.empty()) {
      usage.reasoning_completion_tokens =
          std::max(1, static_cast<int>(state.collected_reasoning.size() / 4));
    }
    GenerateResult result(state.collected_text, kFinishReasonStop, usage);
    result.reasoning = state.collected_reasoning;
    if (!state.collected_reasoning.empty()) {
      result.response_messages.push_back(Message::assistant_with_reasoning(
          state.collected_text, state.collected_reasoning, ""));
    }
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
                 builder = cursor_request_builder_,
                 kv = &kv_]() {
    qcode::logger::set_thread_name("cursor-stream");
    try {
      const std::string request_id = random_id();
      const std::string run_sse_body =
          proto::envelope(proto::bytes_field(1, request_id));
      const std::string run_sse_url = agent_base_url + kAgentPath;
      auto headers = cursor_run_headers(access_token, request_id);

      AgentSessionState state;
      AgentSessionHooks hooks;
      hooks.log_tag = "Cursor stream_text";
      hooks.on_text_delta = [impl_ptr](const std::string& text) {
        impl_ptr->push_event(StreamEvent(text));
      };
      hooks.on_reasoning_delta = [impl_ptr](const std::string& text) {
        impl_ptr->push_event(StreamEvent::reasoning(text));
      };
      hooks.on_finish = [impl_ptr]() {
        impl_ptr->push_event(StreamEvent(kStreamEventTypeFinish));
        impl_ptr->mark_done();
      };
      hooks.on_exec_reply = [impl_ptr](const CursorExecReply& reply) {
        // Cursor AgentService still proposes read/grep/write.
        // We reject those locally; only surface bash.
        if (reply.tool_name != "bash") return;
        impl_ptr->push_event(StreamEvent::tool_call(
            reply.tool_call_id, reply.tool_name, reply.arguments.dump()));
        impl_ptr->push_event(StreamEvent::tool_result(
            reply.tool_call_id, reply.tool_name, reply.result.dump(),
            reply.is_error));
      };
      hooks.is_stopped = [impl_ptr]() { return impl_ptr->is_stopped(); };

      CursorBidi bidi(aiserver_base_url, access_token, request_id,
                      kClientVersion);
      BidiAppend bidi_append = [&](const std::string& client_message) -> bool {
        return bidi.append(client_message, /*wait=*/true);
      };
      BidiAppend bidi_append_kv = [&](const std::string& client_message) -> bool {
        return bidi.append(client_message, /*wait=*/false);
      };

      auto stream_future = std::async(std::launch::async, [&]() {
        return http2_post_stream(
            run_sse_url, headers, run_sse_body, "application/connect+proto",
            [&](std::string_view chunk) -> bool {
              return feed_agent_chunk(state, hooks, chunk, bidi_append,
                                      bidi_append_kv, builder, *kv, options);
            },
            /*timeout_sec=*/0,
            [impl_ptr, abort = options.abort_flag]() {
              return impl_ptr->is_stopped() ||
                     (abort && abort->load());
            });
      });

      std::this_thread::sleep_for(std::chrono::milliseconds(150));
      if (impl_ptr->is_stopped()) {
        (void)stream_future.get();
        return;
      }

      const std::string client_message =
          builder->build_agent_client_run_message(options);
      if (!bidi_append(client_message)) {
        (void)stream_future.get();
        impl_ptr->push_event(StreamEvent(
            kStreamEventTypeError, "Cursor BidiAppend failed for run_request"));
        impl_ptr->mark_done();
        return;
      }

      auto http = stream_future.get();
      (void)bidi.flush();
      if (impl_ptr->is_stopped() ||
          (options.abort_flag && options.abort_flag->load())) {
        impl_ptr->mark_done();
        return;
      }

      if (!state.stream_error.empty()) {
        impl_ptr->push_event(
            StreamEvent(kStreamEventTypeError, state.stream_error));
        impl_ptr->mark_done();
        return;
      }
      if (!http.error.empty() && !http.aborted) {
        impl_ptr->push_event(StreamEvent(kStreamEventTypeError, http.error));
        impl_ptr->mark_done();
        return;
      }
      if (!http.aborted && http.status != 0 && http.status != 200) {
        impl_ptr->push_event(StreamEvent(
            kStreamEventTypeError,
            "Cursor RunSSE HTTP " + std::to_string(http.status) +
                (http.body.empty() ? "" : (": " + http.body.substr(0, 400)))));
        impl_ptr->mark_done();
        return;
      }

      if (state.collected_text.empty() && !http.body.empty()) {
        state.collected_text =
            CursorResponseParser::parse_agent_stream_body(http.body);
        if (!state.collected_text.empty()) {
          impl_ptr->push_event(StreamEvent(state.collected_text));
        }
      }

      if (state.collected_text.empty()) {
        impl_ptr->push_event(StreamEvent(
            kStreamEventTypeError,
            "Cursor returned an empty response (check model id / auth)."));
      }
      impl_ptr->mark_done();
    } catch (const std::exception& e) {
      impl_ptr->push_event(StreamEvent(
          kStreamEventTypeError,
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
  return "Cursor (QCode qcode, HTTP/2 agent, automatic prompt caching)";
}

}  // namespace cursor
}  // namespace qcode
