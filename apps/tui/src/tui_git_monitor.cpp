#include "tui_git_monitor.h"

#include <qcode/session/git_workspace.h>

#include <algorithm>
#include <thread>

namespace qcode {
namespace tui {

void TuiGitMonitor::request_refresh(ftxui::ScreenInteractive& screen) {
  if (running_->exchange(true)) return;
  auto now = std::chrono::steady_clock::now();
  if (now - *last_ < std::chrono::milliseconds(500)) {
    running_->store(false);
    return;
  }
  *last_ = now;
  std::thread([pending = pending_, pending_mutex = pending_mutex_, &screen,
               running = running_]() {
    auto vec = qcode::fetch_modified_files();
    {
      std::lock_guard<std::mutex> lock(*pending_mutex);
      *pending = std::move(vec);
    }
    running->store(false);
    screen.Post(ftxui::Event::Custom);
  }).detach();
}

void TuiGitMonitor::apply_pending(ChatState& state) {
  std::lock_guard<std::mutex> lock(*pending_mutex_);
  if (!pending_->has_value()) return;
  if (!state.file_changes) {
    state.file_changes = std::make_shared<std::vector<FileChangeEntry>>();
  }
  *state.file_changes = std::move(**pending_);
  ++*state.files_revision;
  if (state.file_changes->empty()) {
    state.selected_file = 0;
    state.files_detail_open = false;
  } else {
    state.selected_file = std::clamp(
        state.selected_file, 0, static_cast<int>(state.file_changes->size()) - 1);
  }
  pending_->reset();
}

}  // namespace tui
}  // namespace qcode
