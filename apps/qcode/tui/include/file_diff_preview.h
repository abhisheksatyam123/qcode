#pragma once

#include <ftxui/dom/elements.hpp>

#include <cstddef>
#include <memory>
#include <string>

namespace qcode {
namespace tui {

// Quote a path as a single-quoted POSIX shell literal.
std::string shell_quote(const std::string& s);

enum class FilePreviewKind {
  kDiff,                // `git diff` of one file (full contents if untracked)
  kUntrackedDirSample,  // `git ls-files --others` sample of an untracked dir
};

// Files-tab preview text, cached per (files_revision, path) and bounded in
// memory. Never blocks on git: a cache miss hands the git call to a single
// background worker and returns a short "loading…" placeholder; when the
// result lands the active ftxui::ScreenInteractive gets Post(Event::Custom)
// so the next frame picks it up. Call from the UI thread (inside the loop).
std::shared_ptr<const std::string> get_file_preview(const std::string& path,
                                                    size_t files_revision,
                                                    FilePreviewKind kind);

ftxui::Element render_diff_content(const std::string& content);

}  // namespace tui
}  // namespace qcode
