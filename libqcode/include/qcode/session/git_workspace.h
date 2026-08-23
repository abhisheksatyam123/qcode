#pragma once

#include <qcode/core/state.h>

#include <vector>

namespace qcode {

/// Refresh ChatState.file_changes from `git diff` / `git status`.
void update_modified_files(ChatState& state);

/// Fetch git changes without mutating state — for async callers. Returns the
/// same vector update_modified_files would assign to file_changes.
std::vector<FileChangeEntry> fetch_modified_files();

}  // namespace qcode
