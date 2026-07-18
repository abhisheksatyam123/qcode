#pragma once

#include <qcode/state/state.h>

namespace qcode {

/// Refresh ChatState.file_changes from `git diff` / `git status`.
void update_modified_files(ChatState& state);

}  // namespace qcode
