#pragma once

#include <qcode/ui/chat_state.h>

#include <ftxui/component/screen_interactive.hpp>

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

namespace qcode {
namespace tui {

// Debounced, single-flight async git refresh. Fetch runs off the UI thread;
// apply_pending() must run on the UI thread.
class TuiGitMonitor {
 public:
  void request_refresh(ftxui::ScreenInteractive& screen);
  void apply_pending(ChatState& state);

 private:
  std::shared_ptr<std::atomic<bool>> running_ =
      std::make_shared<std::atomic<bool>>(false);
  std::shared_ptr<std::chrono::steady_clock::time_point> last_ =
      std::make_shared<std::chrono::steady_clock::time_point>(
          std::chrono::steady_clock::now() - std::chrono::seconds(10));
  std::shared_ptr<std::mutex> pending_mutex_ = std::make_shared<std::mutex>();
  std::shared_ptr<std::optional<std::vector<FileChangeEntry>>> pending_ =
      std::make_shared<std::optional<std::vector<FileChangeEntry>>>();
};

}  // namespace tui
}  // namespace qcode
