#include <qcode/workspace/git_workspace.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <unordered_set>

namespace ai {
namespace tui {

namespace {

std::string trim_trailing_newline(std::string s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) {
        s.pop_back();
    }
    return s;
}

// Count text lines for an untracked file; cap read so huge files stay cheap.
int count_file_lines_capped(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return 0;
    constexpr std::size_t kMaxBytes = 512 * 1024;
    std::size_t bytes = 0;
    int lines = 0;
    std::string line;
    while (std::getline(file, line)) {
        ++lines;
        bytes += line.size() + 1;
        if (bytes >= kMaxBytes) break;
    }
    return lines;
}

std::string numstat_path(std::string path) {
    // Renames: "old => new" — show/open the new path.
    const auto arrow = path.find(" => ");
    if (arrow != std::string::npos) {
        path = path.substr(arrow + 4);
    }
    return path;
}

bool read_git_lines(const char* cmd,
                    const std::function<void(const std::string&)>& on_line) {
    FILE* pipe = popen(cmd, "r");
    if (!pipe) return false;
    std::array<char, 512> buffer{};
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) !=
           nullptr) {
        on_line(trim_trailing_newline(buffer.data()));
    }
    pclose(pipe);
    return true;
}

}  // namespace

void update_modified_files(ChatState& state) {
    if (!state.file_changes) {
        state.file_changes = std::make_shared<std::vector<FileChangeEntry>>();
    }
    state.file_changes->clear();
    ++*state.files_revision;

    std::unordered_set<std::string> seen;

    // All staged + unstaged changes vs HEAD (matches get_file_diff_raw).
    read_git_lines(
        "git -c core.quotepath=false diff HEAD --numstat 2>/dev/null",
        [&](const std::string& line) {
            if (line.empty()) return;
            const auto t1 = line.find('\t');
            if (t1 == std::string::npos) return;
            const auto t2 = line.find('\t', t1 + 1);
            if (t2 == std::string::npos) return;

            const std::string add_s = line.substr(0, t1);
            const std::string del_s = line.substr(t1 + 1, t2 - t1 - 1);
            std::string path = numstat_path(line.substr(t2 + 1));
            if (path.empty() || !seen.insert(path).second) return;

            FileChangeEntry entry;
            entry.path = std::move(path);
            if (add_s == "-" || del_s == "-") {
                entry.binary = true;
            } else {
                try {
                    entry.additions = std::stoi(add_s);
                    entry.deletions = std::stoi(del_s);
                } catch (...) {
                    entry.binary = true;
                }
            }
            state.file_changes->push_back(std::move(entry));
        });

    // Untracked files/dirs from status (dirs stay as a single row).
    read_git_lines(
        "git -c core.quotepath=false status --porcelain "
        "--untracked-files=normal 2>/dev/null",
        [&](const std::string& line) {
            if (line.size() < 4) return;
            if (line[0] != '?' || line[1] != '?') return;
            std::string path = line.substr(3);
            if (!path.empty() && path.front() == '"' && path.back() == '"') {
                path = path.substr(1, path.size() - 2);
            }
            if (path.empty() || !seen.insert(path).second) return;

            FileChangeEntry entry;
            entry.path = path;
            entry.untracked = true;
            if (!path.empty() && path.back() == '/') {
                // Untracked directory — no line counts until opened.
                entry.additions = 0;
                entry.deletions = 0;
            } else if (std::filesystem::is_regular_file(path)) {
                entry.additions = count_file_lines_capped(path);
            }
            state.file_changes->push_back(std::move(entry));
        });

    if (state.file_changes->empty()) {
        state.selected_file = 0;
        state.files_detail_open = false;
    } else {
        state.selected_file = std::clamp(
            state.selected_file, 0,
            static_cast<int>(state.file_changes->size()) - 1);
    }
}

} // namespace tui
} // namespace ai
