#pragma once

#include <qcode/core/in_process_bus.h>
#include <qcode/core/session_file_logger.h>
#include <qcode/core/event.h>
#include <qcode/generation/generation_service.h>

#include <nlohmann/json.hpp>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace qcode {
namespace server {

// An event for the session's NDJSON streams: the turn it belongs to and its
// NDJSON line (with "turn" and the newline), encoded once for every stream.
struct StreamEvent {
    uint64_t turn = 0;
    std::shared_ptr<const std::string> line;
};

// Live generation state of one session. The fields after queue_mutex are
// guarded by it. Turn rows are written to the DB under it too, so a stream
// attaching mid-turn gets history + unsaved text + later events with no gap.
struct GenSession {
    std::string id;
    std::mutex queue_mutex;
    std::condition_variable queue_cv;  // new events or turn end
    // Recent events in bus order; events[i] has sequence number events_base + i.
    // Each stream keeps its own cursor, so two streams never steal events.
    // Kept only while a turn runs or a stream is attached (see attach_stream).
    std::deque<StreamEvent> events;
    uint64_t events_base = 0;
    int streams = 0;  // attached streams, i.e. cursors into `events`
    std::atomic<bool> generating{false};
    std::atomic<uint64_t> active_turn{0};
    std::shared_ptr<std::atomic<bool>> abort_flag{std::make_shared<std::atomic<bool>>(false)};
    // Prompts sent while the turn runs; its next model request takes them.
    std::vector<std::string> pending_prompts;
    std::string error;
    std::string assistant_text;  // streamed since the last flush to the DB
    std::string reasoning_text;
    std::string reasoning_signature;  // provider signature of reasoning_text
    std::shared_ptr<std::vector<qcode::bus::Subscription>> subs;
    std::atomic<int> live_prompt_tokens{0};
    std::atomic<int> live_completion_tokens{0};
    std::atomic<int> live_total_tokens{0};
    std::atomic<int> tool_call_count{0};
    std::atomic<int> total_tool_time_ms{0};
};

extern std::mutex g_sessions_mutex;
extern std::unordered_map<std::string, std::shared_ptr<GenSession>> g_sessions;

// Per-session file logger installed by main(). Null when logging to a single
// file (tests, embedded hosts). Exposed so route handlers can bind the calling
// thread to a session id and release the sink when a session is deleted.
extern std::shared_ptr<qcode::SessionFileLogger> g_session_logger;

// Route logs emitted by the calling thread to `sid`'s own log file.
// Scoped so a thread serving many requests does not keep a stale tag.
class ScopedSessionLog {
 public:
  explicit ScopedSessionLog(const std::string& sid) {
    previous_ = qcode::logger::thread_session_id();
    qcode::logger::set_thread_session_id(sid);
  }
  ~ScopedSessionLog() { qcode::logger::set_thread_session_id(previous_); }
  ScopedSessionLog(const ScopedSessionLog&) = delete;
  ScopedSessionLog& operator=(const ScopedSessionLog&) = delete;

 private:
  std::string previous_;
};

// Release the log sink for sessions that no longer exist. Files stay on disk.
void prune_session_logs();

// The subscribers hold `session` weakly; store the result in session->subs,
// so they end with the session (e.g. after DELETE /session/:id).
std::vector<qcode::bus::Subscription> subscribe_session(
    qcode::bus::BusRuntime& bus,
    const std::shared_ptr<GenSession>& session);

// Run the subscribers of every queued bus event on the calling thread. Used as
// the bus wake callback, so events are persisted and reach streams as soon as
// they are published, whether or not an HTTP client is attached.
void deliver_bus_events(qcode::bus::BusRuntime& bus);

// Save text streamed since the last flush as Reasoning / Assistant rows.
// Caller holds session.queue_mutex.
void flush_turn_text(GenSession& session);

// Abort the running turn and drop prompts still waiting for it.
void abort_turn(GenSession& session);

// Encode `j` as one NDJSON stream line of turn `turn`.
std::shared_ptr<const std::string> encode_stream_line(nlohmann::json j, uint64_t turn);

// Count a stream reading `events` from a cursor taken under the same lock;
// the caller holds session->queue_mutex. Events stay buffered until the
// returned token is released, so keep it as long as the stream reads.
std::shared_ptr<void> attach_stream(const std::shared_ptr<GenSession>& session);

// Free buffered events no one can read any more: no turn runs and no stream
// is attached (a new stream starts at the end). Caller holds queue_mutex.
void trim_events(GenSession& session);

}  // namespace server
}  // namespace qcode
