#include "file_diff_preview.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
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


}  // namespace

const std::string& get_file_diff(const std::string& path,
                                        size_t files_revision) {
    struct CacheEntry {
        std::string content;
        std::filesystem::file_time_type modified_at{};
        size_t last_used = 0;
    };
    static std::unordered_map<std::string, CacheEntry> cache;
    static size_t cache_bytes = 0;
    static size_t use_counter = 0;
    static size_t cached_revision = 0;
    static constexpr size_t kMaxCacheEntries = 16;
    static constexpr size_t kMaxCacheBytes = 4 * 1024 * 1024;
    static const std::string kEmpty;

    if (path.empty()) return kEmpty;
    if (cached_revision != files_revision) {
        cache.clear();
        cache_bytes = 0;
        cached_revision = files_revision;
    }

    auto& entry = cache[path];
    entry.last_used = ++use_counter;
    auto needs_update = entry.content.empty();
    try {
        if (std::filesystem::exists(path)) {
            const auto current_time = std::filesystem::last_write_time(path);
            needs_update = needs_update || entry.modified_at != current_time;
            entry.modified_at = current_time;
        }
    } catch (...) {
        needs_update = true;
    }

    if (needs_update) {
        cache_bytes -= entry.content.size();
        entry.content = get_file_diff_raw(path);
        cache_bytes += entry.content.size();
    }

    while (cache.size() > kMaxCacheEntries || cache_bytes > kMaxCacheBytes) {
        auto oldest = cache.end();
        for (auto it = cache.begin(); it != cache.end(); ++it) {
            if (it->first == path) continue;
            if (oldest == cache.end() ||
                it->second.last_used < oldest->second.last_used) {
                oldest = it;
            }
        }
        if (oldest == cache.end()) break;
        cache_bytes -= oldest->second.content.size();
        cache.erase(oldest);
    }
    return entry.content;
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
