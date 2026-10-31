#include "file_diff_preview.h"

#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <sstream>
#include <system_error>
#include <thread>
#include <unordered_map>

namespace qcode {
namespace tui {

using namespace ftxui;

std::string shell_quote(const std::string& s) {
  std::string out = "'";
  for (char c : s) {
    if (c == '\'') {
      out += '\'';
      out += '\\';
      out += '\'';
      out += '\'';
    } else {
      out += c;
    }
  }
  out += "'";
  return out;
}

namespace {

std::string get_file_diff_raw(const std::string& path) {
    if (path.empty()) return "";

    static constexpr size_t kMaxPreviewBytes = 2 * 1024 * 1024;
    std::string diff_output;
    diff_output.reserve(64 * 1024);
    bool truncated = false;
    std::array<char, 512> buffer;
    
    // Check if the file is tracked
    std::string check_cmd = "git ls-files --error-unmatch " + shell_quote(path) + " 2>/dev/null";
    FILE* check_pipe = popen(check_cmd.c_str(), "r");
    bool is_tracked = false;
    if (check_pipe) {
        is_tracked = (pclose(check_pipe) == 0);
    }

    std::string cmd;
    if (is_tracked) {
        // Tracked file: show diff against HEAD (unstaged + staged changes)
        cmd = "git diff HEAD -- " + shell_quote(path) + " 2>/dev/null";
    } else {
        // Untracked file: show diff as a new file (all lines added)
        cmd = "git diff --no-index /dev/null " + shell_quote(path) + " 2>/dev/null";
    }

    FILE* pipe = popen(cmd.c_str(), "r");
    if (pipe) {
        while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
            const auto remaining = kMaxPreviewBytes - diff_output.size();
            if (remaining == 0) {
                truncated = true;
                continue;
            }
            diff_output.append(
                buffer.data(), std::min(remaining, std::strlen(buffer.data())));
            truncated = truncated ||
                        std::strlen(buffer.data()) > remaining;
        }
        pclose(pipe);
    }

    // Fallback if diff is empty (e.g., untracked new file outside git index entirely)
    if (diff_output.empty() && std::filesystem::exists(path)) {
        std::ifstream file(path);
        if (file.is_open()) {
            std::string line;
            while (std::getline(file, line)) {
                if (diff_output.size() + line.size() + 2 >
                    kMaxPreviewBytes) {
                    truncated = true;
                    break;
                }
                diff_output += "+" + line + "\n";
            }
        }
    }
    if (truncated) {
        diff_output += "\n[Diff preview truncated at 2 MiB]\n";
    }

    return diff_output;
}

std::string untracked_dir_sample_raw(const std::string& path) {
    std::string out = "Untracked directory — sample paths:\n\n";
    const std::string cmd =
        "git -c core.quotepath=false ls-files --others "
        "--exclude-standard -- " +
        shell_quote(path) + " 2>/dev/null | head -n 80";
    std::array<char, 512> buf{};
    int n = 0;
    if (FILE* pipe = popen(cmd.c_str(), "r")) {
        while (fgets(buf.data(), static_cast<int>(buf.size()), pipe) !=
               nullptr) {
            out += "+";
            out += buf.data();
            ++n;
        }
        pclose(pipe);
    }
    if (n == 0) {
        out += "(empty or ignored)\n";
    } else if (n >= 80) {
        out += "\n… truncated\n";
    }
    return out;
}

using Text = std::shared_ptr<const std::string>;

struct PreviewJob {
    std::string key;
    std::string path;
    FilePreviewKind kind = FilePreviewKind::kDiff;
    uint64_t ticket = 0;
    ftxui::ScreenInteractive* screen = nullptr;  // captured on the UI thread
};

// Shared by the UI thread and the single detached worker. The worker holds
// its own shared_ptr, so this stays alive even if it outlives static
// destruction at exit.
struct PreviewCache {
    struct Entry {
        Text content;  // null until the first result lands
        std::filesystem::file_time_type modified_at{};
        uint64_t ticket = 0;  // newest pending/in-flight request; 0 = none
        size_t last_used = 0;
    };
    static constexpr size_t kMaxEntries = 16;
    static constexpr size_t kMaxBytes = 4 * 1024 * 1024;

    std::mutex mu;
    std::unordered_map<std::string, Entry> entries;
    size_t bytes = 0;
    size_t use_counter = 0;
    size_t revision = 0;
    uint64_t next_ticket = 0;
    std::optional<PreviewJob> pending;  // newest request not yet started
    bool worker_running = false;
    bool shutting_down = false;

    void evict_locked(const std::string& keep) {
        while (entries.size() > kMaxEntries || bytes > kMaxBytes) {
            auto oldest = entries.end();
            for (auto it = entries.begin(); it != entries.end(); ++it) {
                if (it->first == keep) continue;
                if (oldest == entries.end() ||
                    it->second.last_used < oldest->second.last_used) {
                    oldest = it;
                }
            }
            if (oldest == entries.end()) break;
            if (pending && pending->key == oldest->first) pending.reset();
            bytes -= oldest->second.content ? oldest->second.content->size() : 0;
            entries.erase(oldest);
        }
    }
};

struct PreviewCacheHandle {
    std::shared_ptr<PreviewCache> cache = std::make_shared<PreviewCache>();
    ~PreviewCacheHandle() {
        // Static destruction: stop the worker from posting to the screen.
        std::lock_guard<std::mutex> lock(cache->mu);
        cache->shutting_down = true;
        cache->pending.reset();
    }
};

const std::shared_ptr<PreviewCache>& preview_cache() {
    static PreviewCacheHandle handle;
    return handle.cache;
}

// Drains the pending slot one job at a time, then exits. At most one worker
// thread exists at any time (guarded by worker_running).
void run_preview_worker(std::shared_ptr<PreviewCache> c) {
    std::unique_lock<std::mutex> lock(c->mu);
    while (c->pending && !c->shutting_down) {
        PreviewJob job = std::move(*c->pending);
        c->pending.reset();
        lock.unlock();

        std::string out;
        try {
            out = job.kind == FilePreviewKind::kUntrackedDirSample
                      ? untracked_dir_sample_raw(job.path)
                      : get_file_diff_raw(job.path);
        } catch (...) {
            out = "(preview unavailable)\n";
        }
        Text result = std::make_shared<const std::string>(std::move(out));

        lock.lock();
        // Store only if this is still the newest request for the key. A
        // files_revision change clears the map and any re-request bumps the
        // ticket, so results for an older key are dropped here.
        auto it = c->entries.find(job.key);
        if (it == c->entries.end() || it->second.ticket != job.ticket) continue;
        auto& entry = it->second;
        c->bytes -= entry.content ? entry.content->size() : 0;
        c->bytes += result->size();
        entry.content = std::move(result);
        entry.ticket = 0;
        c->evict_locked(job.key);
        if (job.screen != nullptr && !c->shutting_down) {
            job.screen->Post(ftxui::Event::Custom);
        }
    }
    c->worker_running = false;
}

// Caller holds c->mu. Puts a request for `entry` in the single pending slot
// (newest wins) and starts the worker if it is idle.
void request_preview_locked(const std::shared_ptr<PreviewCache>& c,
                            const std::string& key, const std::string& path,
                            FilePreviewKind kind,
                            PreviewCache::Entry& entry) {
    if (c->pending && c->pending->key != key) {
        // Superseded before it started: let that entry re-request (with a
        // fresh mtime check) the next time it is shown.
        auto it = c->entries.find(c->pending->key);
        if (it != c->entries.end() && it->second.ticket == c->pending->ticket) {
            it->second.ticket = 0;
            it->second.modified_at = {};
        }
    }
    entry.ticket = ++c->next_ticket;
    c->pending = PreviewJob{key, path, kind, entry.ticket,
                            ftxui::ScreenInteractive::Active()};
    if (c->worker_running) return;
    try {
        std::thread(run_preview_worker, c).detach();
        c->worker_running = true;
    } catch (const std::system_error&) {
        c->pending.reset();
        entry.ticket = 0;  // retried on a later frame
    }
}

}  // namespace

std::shared_ptr<const std::string> get_file_preview(const std::string& path,
                                                    size_t files_revision,
                                                    FilePreviewKind kind) {
    static const Text kEmpty = std::make_shared<const std::string>();
    static const Text kLoadingDiff =
        std::make_shared<const std::string>("loading diff…\n");
    static const Text kLoadingDir = std::make_shared<const std::string>(
        "Untracked directory — sample paths:\n\nloading…\n");

    if (path.empty()) return kEmpty;
    const bool is_diff = kind == FilePreviewKind::kDiff;

    // Diffs also refresh when the file's mtime changes; directory samples are
    // keyed by files_revision only. stat() happens outside the lock.
    std::optional<std::filesystem::file_time_type> mtime;
    if (is_diff) {
        std::error_code ec;
        const auto t = std::filesystem::last_write_time(path, ec);
        if (!ec) mtime = t;
    }

    const auto& c = preview_cache();
    std::lock_guard<std::mutex> lock(c->mu);
    if (c->revision != files_revision) {
        c->entries.clear();
        c->bytes = 0;
        c->pending.reset();
        c->revision = files_revision;
    }

    const std::string key = (is_diff ? "d:" : "u:") + path;
    auto& entry = c->entries[key];
    entry.last_used = ++c->use_counter;
    const bool mtime_changed = mtime && entry.modified_at != *mtime;
    if (mtime) entry.modified_at = *mtime;
    if (mtime_changed || (!entry.content && entry.ticket == 0)) {
        request_preview_locked(c, key, path, kind, entry);
    }
    c->evict_locked(key);

    if (entry.content) return entry.content;  // may be stale while refreshing
    return is_diff ? kLoadingDiff : kLoadingDir;
}


ftxui::Element render_diff_content(const std::string& content) {
    using namespace ftxui;
    Elements lines;
    std::stringstream ss(content);
    std::string line;
    while (std::getline(ss, line)) {
        Color c = Color::Default;
        if (!line.empty()) {
            if (line[0] == '+') c = Color::Green;
            else if (line[0] == '-') c = Color::Red;
            else if (line[0] == '@') c = Color::Cyan;
        }
        lines.push_back(text(line) | color(c));
    }
    return vbox(std::move(lines));
}


}  // namespace tui
}  // namespace qcode
