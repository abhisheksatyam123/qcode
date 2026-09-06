#include "cursor_agent_session.h"

#include <qcode/core/logger.h>
#include "providers/cursor/cursor_proto.h"

#include <filesystem>
#include <random>

namespace qcode {
namespace cursor {
namespace {

std::string default_workspace_path() {
  std::error_code ec;
  auto path = std::filesystem::current_path(ec);
  if (ec) return ".";
  return path.string();
}

std::string describe_frame(const std::string& payload) {
  std::string out;
  for (const auto& f : proto::parse_fields(payload)) {
    if (!out.empty()) out += ",";
    out += std::to_string(f.num);
    if (f.num == 1 && f.wire == 2) {
      out += "{";
      bool first = true;
      for (const auto& u : proto::parse_fields(f.bytes)) {
        if (!first) out += ",";
        first = false;
        out += std::to_string(u.num);
      }
      out += "}";
    }
  }
  return out;
}

bool answer_exec(const BidiAppend& bidi_append,
                 CursorRequestBuilder* builder,
                 const AgentStreamEvent& ev,
                 const std::string& workspace,
                 std::shared_ptr<std::atomic<bool>> abort_flag,
                 std::atomic<bool>& context_replied,
                 const std::function<void(const CursorExecReply&)>& on_reply,
                 const GenerateOptions* options) {
  if (ev.kind == AgentStreamEvent::Kind::kRequestContext) {
    if (context_replied.exchange(true)) return true;
    const auto reply =
        builder->build_request_context_reply(ev.exec_id, ev.exec_id_str, workspace);
    if (!bidi_append(reply)) return false;
    LOG_INFO("Cursor exec stream_close id={}", ev.exec_id);
    return bidi_append(CursorExec::stream_close_message(ev.exec_id));
  }
  if (ev.kind != AgentStreamEvent::Kind::kExec) return true;
  const auto req = CursorExec::from_event(ev);
  // Field 14 is a stream: AgentService waits for start before the command
  // ends. Sending start only after bash returns left the server hung.
  if (req.args_field == 14) {
    if (!bidi_append(CursorExec::shell_stream_start_message(req))) {
      return false;
    }
  }
  const auto reply = CursorExec::handle(req, workspace, abort_flag, options);
  if (on_reply) on_reply(reply);
  if (reply.client_messages.empty()) {
    LOG_ERROR("Cursor exec field={} produced no reply", ev.exec_field);
    return false;
  }
  for (const auto& msg : reply.client_messages) {
    if (!bidi_append(msg)) return false;
  }
  // Official client closes every exec stream. Unary results are accepted
  // without this; shell_stream stays open forever unless we send it.
  LOG_INFO("Cursor exec stream_close id={} field={}", req.id, req.args_field);
  return bidi_append(CursorExec::stream_close_message(req.id));
}

bool answer_kv(const BidiAppend& bidi_append, CursorKv& kv,
               const AgentStreamEvent& ev) {
  const auto reply = kv.handle(ev.kv_message);
  if (reply.empty()) {
    LOG_ERROR("Cursor kv produced no reply");
    return true;
  }
  return bidi_append(reply);
}

}  // namespace

providers::ProviderConfig make_agent_config(const std::string& api_key,
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

std::unique_ptr<http::HttpRequestHandler> make_agent_handler(
    const std::string& url, int read_timeout_sec) {
  auto cfg = http::HttpRequestHandler::parse_base_url(url);
  cfg.connection_timeout_sec = 3;
  cfg.read_timeout_sec = read_timeout_sec;
  retry::RetryConfig rcfg;
  rcfg.max_retries = 0;
  cfg.retry_config = rcfg;
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

std::vector<std::pair<std::string, std::string>> cursor_run_headers(
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

std::string resolve_workspace(const GenerateOptions& options) {
  if (!options.workspace.empty()) return options.workspace;
  return default_workspace_path();
}

bool feed_agent_chunk(AgentSessionState& state,
                      const AgentSessionHooks& hooks,
                      std::string_view chunk,
                      const BidiAppend& bidi_append,
                      const BidiAppend& bidi_append_kv,
                      CursorRequestBuilder* builder,
                      CursorKv& kv,
                      const GenerateOptions& options) {
  if (hooks.is_stopped && hooks.is_stopped()) {
    return false;
  }
  if (options.abort_flag && options.abort_flag->load()) {
    return false;
  }
  if (options.has_queued_work && options.has_queued_work()) {
    return false;
  }
  for (const auto& payload : state.frame_buf.feed(chunk)) {
    const auto ev = CursorResponseParser::classify_agent_payload(payload);
    using Kind = AgentStreamEvent::Kind;
    switch (ev.kind) {
      case Kind::kHeartbeat:
        if (idle_end_on_heartbeat(!state.collected_text.empty(),
                                  state.saw_exec.load(),
                                  state.last_activity)) {
          LOG_INFO("{}: idle end after heartbeat (text_len={})", hooks.log_tag,
                   state.collected_text.size());
          state.turn_ended = true;
          if (hooks.on_finish) hooks.on_finish();
          return false;
        }
        break;
      case Kind::kTextDelta:
        state.collected_text += ev.text;
        state.last_activity = std::chrono::steady_clock::now();
        if (hooks.on_text_delta) hooks.on_text_delta(ev.text);
        break;
      case Kind::kReasoningDelta:
        state.collected_reasoning += ev.text;
        state.last_activity = std::chrono::steady_clock::now();
        if (hooks.on_reasoning_delta) hooks.on_reasoning_delta(ev.text);
        break;
      case Kind::kTurnEnded:
        if (!ev.text.empty()) {
          state.collected_text += ev.text;
          if (hooks.on_text_delta) hooks.on_text_delta(ev.text);
        }
        state.turn_ended = true;
        if (hooks.on_finish) hooks.on_finish();
        return false;
      case Kind::kPostTextTokenDelta:
        state.last_activity = std::chrono::steady_clock::now();
        break;
      case Kind::kRequestContext:
      case Kind::kExec: {
        state.last_activity = std::chrono::steady_clock::now();
        if (ev.kind == Kind::kExec) state.saw_exec = true;
        if (!answer_exec(bidi_append, builder, ev, resolve_workspace(options),
                         options.abort_flag, state.context_replied,
                         hooks.on_exec_reply, &options)) {
          state.stream_error = ev.kind == Kind::kRequestContext
                                   ? "Cursor request_context BidiAppend failed"
                                   : "Cursor exec BidiAppend failed";
          return false;
        }
        state.last_activity = std::chrono::steady_clock::now();
        break;
      }
      case Kind::kKv: {
        state.last_activity = std::chrono::steady_clock::now();
        if (!answer_kv(bidi_append_kv, kv, ev)) {
          state.stream_error = "Cursor kv BidiAppend failed";
          return false;
        }
        state.last_activity = std::chrono::steady_clock::now();
        break;
      }
      case Kind::kError:
        state.stream_error = ev.error;
        return false;
      case Kind::kOther:
        state.last_activity = std::chrono::steady_clock::now();
        LOG_INFO("{} other frame={}", hooks.log_tag, describe_frame(payload));
        break;
    }
  }
  return true;
}

}  // namespace cursor
}  // namespace qcode
