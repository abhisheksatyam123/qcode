#pragma once

#include <string>

namespace qcode::task_notes {

// Project-scoped task notes ("todo"). All task data lives in the workspace
// root - never in a global notes vault.
//
// Resolution order (first existing wins):
//   <ws>/todo/               folder form -> <ws>/todo/index.md
//   <ws>/todo.md             single file
//   <ws>/scratchpad/todo/    folder form -> index.md
//   <ws>/scratchpad/todo.md  default; created by ensure()
//
// Every task file has three top-level sections: ## Tasks, ## Systems, ## Log.
// Log is the last section and is append-only.

struct Location {
  std::string path;  // markdown file agents read/write
  bool is_folder = false;
  bool exists = false;
};

// Resolve without creating anything.
Location resolve(const std::string& workspace);

// Resolve, creating the default template file when nothing exists.
// Returns an empty path on failure.
Location ensure(const std::string& workspace);

// Default contents for a fresh task file.
std::string default_template();

// Append "- YYYY-MM-DD HH:MM [actor] event" to the Log section. Creates the
// file / Log section if missing. Serialized in-process so parallel subagents
// never interleave. Newlines in `event` collapse to spaces.
bool append_log(const std::string& workspace, const std::string& actor,
                const std::string& event);

}  // namespace qcode::task_notes
