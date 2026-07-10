#pragma once

#include <string>
#include <vector>
#include <utility>

#include <ai/tui/state.h>

namespace ai {
namespace tui {
namespace db {

// Initialize SQLite database (~/.qcode.db) and create tables
void init_database();

// Create a new session row in the database and return its UUID
std::string create_new_session(const std::string& provider, const std::string& model);

// Retrieve the last active session ID from the database, or empty if none
std::string get_last_active_session();


// Validate that an id has the canonical 36-char RFC 4122 UUID form.
bool is_valid_session_id(const std::string& id);
// Save a single chat message into the SQLite database
void save_message(const std::string& session_id, const std::string& sender, const std::string& content);

// Clear current session messages in state and reload from SQLite
void reload_session_history(const std::string& session_id, ChatState& state);

// Retrieve a list of all saved sessions (session_id, display_title)
std::vector<std::pair<std::string, std::string>> list_sessions();

// Delete a session and its associated messages
void delete_session(const std::string& session_id);

} // namespace db
} // namespace tui
} // namespace ai
