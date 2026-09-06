#pragma once

#include <string>
#include <vector>
#include <utility>

#include <qcode/config/provider_info.h>
#include <qcode/ui/chat_state.h>

namespace qcode {
namespace session {

// Initialize SQLite database (~/.qcode.db) and create tables
void init_database();

// Create a new session row and return its UUID id.
// custom_id is an optional display title (not the primary key).
std::string create_new_session(const std::string& provider, const std::string& model,
                               const std::string& workspace = "",
                               const std::string& custom_id = "");

// Retrieve the last active session ID from the database, or empty if none
std::string get_last_active_session();

// Get workspace for a session
std::string get_session_workspace(const std::string& session_id);

// Get display title for a session (empty if unknown)
std::string get_session_title(const std::string& session_id);

// Validate that an id has the canonical 36-char RFC 4122 UUID form.
bool is_valid_session_id(const std::string& id);

// Save a single chat message into the SQLite database
void save_message(const std::string& session_id, const std::string& sender, const std::string& content);

// Clear current session messages in state and reload from SQLite
void reload_session_history(const std::string& session_id, ChatState& state);

// Load all messages parsed into Message objects for a session, oldest first
std::vector<qcode::Message> load_session_history_parsed(const std::string& session_id);

// Overwrite all messages in SQLite database for a session
void overwrite_session_history(const std::string& session_id, const std::vector<qcode::Message>& messages);

// Load saved messages (sender, content) for a session, oldest first
std::vector<std::pair<std::string, std::string>> load_session_messages(const std::string& session_id);

// ── Persisted prompt queue (mirrors upstream: prompts submitted while busy
// become pending session data and survive restarts) ──
// Append a queued prompt for a session (FIFO by insertion).
void queued_prompt_add(const std::string& session_id, const std::string& content);
// Append text to the newest queued prompt for the session.
void queued_prompt_append_last(const std::string& session_id, const std::string& text);
// All queued prompts for a session, oldest first.
std::vector<std::string> queued_prompt_load(const std::string& session_id);
// Drop the oldest queued prompt (call after it starts running).
void queued_prompt_pop(const std::string& session_id);
// Remove the 1-based entry; no-op when out of range.
void queued_prompt_remove_at(const std::string& session_id, size_t index_1based);
// Drop every queued prompt for the session (user-initiated clear).
void queued_prompt_clear(const std::string& session_id);

// Retrieve a list of all saved sessions (session_id, display_title)
std::vector<std::pair<std::string, std::string>> list_sessions();

// Retrieve all saved sessions with workspace info
struct SessionInfo {
    std::string id;
    std::string title;
    std::string workspace;
    std::string provider;
    std::string model;
    long long last_active_at = 0;
    int message_count = 0;
};
std::vector<SessionInfo> list_sessions_full();

// Rename a session title
void rename_session(const std::string& session_id, const std::string& new_title);

// Update the provider/model associated with a session (keeps the stored
// selection in sync with what was actually used for generation)
void set_session_provider_model(const std::string& session_id, const std::string& provider, const std::string& model);

// Persisted per-session agent ("build"/"plan") and reasoning
// ("off"/"low"/...) modes. get returns {agent, reasoning}; either string is
// empty when the row predates migration v7 or the mode was never changed.
std::pair<std::string, std::string> get_session_modes(const std::string& session_id);
void set_session_modes(const std::string& session_id,
                       const std::string& agent_mode,
                       const std::string& reasoning_mode);

// Aggregate statistics for a session (computed from messages + live counters).
struct SessionStats {
    std::string id;
    std::string title;
    std::string workspace;
    std::string provider;
    std::string model;
    long long created_at = 0;
    int message_count = 0;
    int user_messages = 0;
    int assistant_messages = 0;
    int tool_calls = 0;
    int prompt_tokens = 0;
    int completion_tokens = 0;
    int total_tokens = 0;
    double total_tool_time_ms = 0.0;
};

SessionStats get_session_stats(const std::string& session_id,
                               int live_tool_calls = 0,
                               double live_tool_time_ms = 0.0,
                               int live_prompt_tokens = 0,
                               int live_completion_tokens = 0,
                               int live_total_tokens = 0);

// Persist cumulative token usage for a session. New values are ADDED on top of
// the previously stored totals (the database is the source of truth across
// restarts and session switches); pass exact per-turn deltas. No-op if the
// session id is invalid or the row is missing.
void persist_session_token_stats(const std::string& session_id,
                                 int prompt_tokens_delta,
                                 int completion_tokens_delta,
                                 int total_tokens_delta);

// Delete a session and its associated messages
void delete_session(const std::string& session_id);

// ── Model Capabilities & Performance Telemetry ─────────────────────────
struct ModelPerformanceSummary {
    std::string model_id;
    std::string provider;
    std::string model_name;
    std::string architecture_info;
    int context_window = 0;
    int output_limit = 0;
    bool tool_call_supported = true;
    bool multi_turn_reliable = true;
    std::string verified_benchmark;
    std::string recommended_for;

    // Dynamic runtime telemetry
    int total_turns = 0;
    int successful_turns = 0;
    int failed_turns = 0;
    int tool_loop_failures = 0;
    double avg_latency_ms = 0.0;
    int positive_feedback_count = 0;
    int negative_feedback_count = 0;
    long long last_used = 0;

    double success_rate() const {
        if (total_turns <= 0) return 100.0;
        return (static_cast<double>(successful_turns) / total_turns) * 100.0;
    }
};

// Record a generation turn result in SQLite telemetry
void record_generation_turn(const std::string& model_id, const std::string& provider,
                            bool success, bool is_loop_failure, double latency_ms);

// Record user feedback (thumbs up / down)
void record_user_feedback(const std::string& model_id, bool is_positive);

// Write picker models from opencode.json into the capabilities table.
void seed_model_capabilities(const std::vector<ProviderInfo>& providers);

// Get capability + performance summary for a specific model
ModelPerformanceSummary get_model_performance_summary(const std::string& model_id, const std::string& provider = "");

// List capability + performance summaries for all tracked models
std::vector<ModelPerformanceSummary> list_all_model_performance_summaries();

} // namespace session
} // namespace qcode
