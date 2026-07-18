#pragma once

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

#include <qcode/session/session_store.h>
#include <qcode/state/state.h>

namespace qcode {
namespace tui {

// Case-insensitive substring match used by model/session/theme pickers.
inline bool matches_query(const std::string& target, const std::string& query) {
    if (query.empty()) return true;
    auto it = std::search(
        target.begin(), target.end(), query.begin(), query.end(),
        [](unsigned char ch1, unsigned char ch2) {
            return std::tolower(ch1) == std::tolower(ch2);
        });
    return it != target.end();
}

// Refresh ChatState.session_title from the DB (or a known title).
inline void sync_session_title(ChatState& state,
                               const std::string& known_title = "") {
    if (!state.session_title) return;
    if (!known_title.empty()) {
        *state.session_title = known_title;
        return;
    }
    if (!state.session_id || state.session_id->empty()) {
        state.session_title->clear();
        return;
    }
    *state.session_title = qcode::session::get_session_title(*state.session_id);
}

// Select the active session row index after a list refresh.
inline int index_of_session(const std::vector<qcode::session::SessionInfo>& entries,
                            const std::string& session_id) {
    for (int i = 0; i < static_cast<int>(entries.size()); ++i) {
        if (entries[i].id == session_id) return i;
    }
    return 0;
}

}  // namespace tui
}  // namespace qcode
