#pragma once

#include "providers/cursor/cursor_exec.h"
#include "providers/cursor/cursor_kv.h"
#include "providers/cursor/cursor_request_builder.h"
#include "providers/cursor/cursor_response_parser.h"
#include "core/http_request_handler.h"
#include "providers/internal/base_provider_client.h"

#include <qcode/core/generate_options.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace qcode {
namespace cursor {

inline constexpr const char* kAiserverPath =
    "/aiserver.v1.AiService/GetUsableModels";
inline constexpr const char* kAgentModelsPath =
    "/agent.v1.AgentService/GetUsableModels";
inline constexpr const char* kAgentPath = "/agent.v1.AgentService/RunSSE";
inline constexpr const char* kClientVersion = "cli-2026.09.02-c22c1a3";
inline constexpr const char* kDefaultAgentBase =
    "https://agentn.global.api5.cursor.sh";
inline constexpr const char* kDefaultAiserverBase = "https://api2.cursor.sh";

providers::ProviderConfig make_agent_config(const std::string& api_key,
                                            const std::string& base_url);
std::unique_ptr<http::HttpRequestHandler> make_agent_handler(
    const std::string& url, int read_timeout_sec);

std::string random_id();
std::vector<std::pair<std::string, std::string>> cursor_run_headers(
    const std::string& token, const std::string& request_id);
std::string resolve_workspace(const GenerateOptions& options);

// Heartbeats mean the agent is still alive (Grok 4.6 thinks for a long time
// between visible tokens). Ending after 12s of text+heartbeat was cutting
// turns short. Use the same 3-minute hang detector for every idle case.
inline constexpr auto kIdleAfterTextTimeout = std::chrono::seconds(180);
inline constexpr auto kIdleAfterExecTimeout = std::chrono::seconds(180);
inline constexpr auto kIdleBeforeContentTimeout = std::chrono::seconds(180);

inline bool idle_end_on_heartbeat(
    bool has_text, bool saw_exec,
    std::chrono::steady_clock::time_point last_activity,
    std::chrono::steady_clock::time_point now =
        std::chrono::steady_clock::now()) {
  const auto idle = now - last_activity;
  if (saw_exec) return idle >= kIdleAfterExecTimeout;
  if (has_text) return idle >= kIdleAfterTextTimeout;
  return idle >= kIdleBeforeContentTimeout;
}

struct AgentSessionState {
  std::string collected_text;
  std::string collected_reasoning;
  std::string stream_error;
  std::atomic<bool> turn_ended{false};
  std::atomic<bool> context_replied{false};
  std::atomic<bool> saw_exec{false};
  std::chrono::steady_clock::time_point last_activity =
      std::chrono::steady_clock::now();
  ConnectFrameBuffer frame_buf;
};

struct AgentSessionHooks {
  std::function<void(const std::string&)> on_text_delta;
  std::function<void(const std::string&)> on_reasoning_delta;
  std::function<void()> on_finish;
  std::function<void(const CursorExecReply&)> on_exec_reply;
  std::function<bool()> is_stopped;
  const char* log_tag = "Cursor";
};

using BidiAppend = std::function<bool(const std::string&)>;

// Decode one HTTP/2 body chunk. Return false to abort the transfer.
bool feed_agent_chunk(AgentSessionState& state,
                      const AgentSessionHooks& hooks,
                      std::string_view chunk,
                      const BidiAppend& bidi_append,
                      const BidiAppend& bidi_append_kv,
                      CursorRequestBuilder* builder,
                      CursorKv& kv,
                      const GenerateOptions& options);

}  // namespace cursor
}  // namespace qcode
