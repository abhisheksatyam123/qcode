#pragma once

#include <string>
#include <vector>
#include <utility>

#include <qcode/state/state.h>

namespace qcode {
namespace tui {
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

// Retrieve a list of all saved sessions (session_id, display_title)
std::vector<std::pair<std::string, std::string>> list_sessions();

// Retrieve all saved sessions with workspace info
struct SessionInfo {
    std::string id;
    std::string title;
    std::string workspace;
    std::string provider;
    std::string model;
};
std::vector<SessionInfo> list_sessions_full();

// Rename a session title
void rename_session(const std::string& session_id, const std::string& new_title);

// Update the provider/model associated with a session (keeps the stored
// selection in sync with what was actually used for generation)
void set_session_provider_model(const std::string& session_id, const std::string& provider, const std::string& model);

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
// Compute aggregate statistics for a session (token/tool totals come from the
// per-generation live counters; historical totals are accumulated from any
// stored counters, so the latest generation dominates).
SessionStats get_session_stats(const std::string& session_id,
                               int live_tool_calls = 0,
                               double live_tool_time_ms = 0.0,
                               int live_prompt_tokens = 0,
                               int live_completion_tokens = 0,
                               int live_total_tokens = 0);

// Delete a session and its associated messages
void delete_session(const std::string& session_id);

} // namespace session
} // namespace tui
} // namespace qcode
