#include <qcode/session/task_notes.h>

#include <qcode/core/logger.h>

#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>

namespace qcode::task_notes {

namespace fs = std::filesystem;

namespace {

std::mutex& log_mutex() {
  static std::mutex m;
  return m;
}

fs::path root_of(const std::string& workspace) {
  if (!workspace.empty()) return fs::path(workspace);
  std::error_code ec;
  auto cwd = fs::current_path(ec);
  return ec ? fs::path(".") : cwd;
}

std::string now_stamp() {
  const auto t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  std::tm tm{};
#if defined(_WIN32)
  localtime_s(&tm, &t);
#else
  localtime_r(&t, &tm);
#endif
  char buf[32];
  std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &tm);
  return buf;
}

std::string one_line(std::string s) {
  for (char& c : s) {
    if (c == '\n' || c == '\r' || c == '\t') c = ' ';
  }
  constexpr size_t kMax = 240;
  if (s.size() > kMax) s = s.substr(0, kMax - 3) + "...";
  return s;
}

}  // namespace

std::string default_template() {
  return "# Goal: <one line>\n\n"
         "## Tasks\n\n"
         "## Systems\n\n"
         "## Log\n";
}

Location resolve(const std::string& workspace) {
  const fs::path root = root_of(workspace);
  std::error_code ec;
  const fs::path dirs[] = {root / "todo", root / "scratchpad" / "todo"};
  const fs::path files[] = {root / "todo.md", root / "scratchpad" / "todo.md"};
  // Order: todo/, todo.md, scratchpad/todo/, scratchpad/todo.md
  for (int i = 0; i < 2; ++i) {
    if (fs::is_directory(dirs[i], ec)) {
      const fs::path idx = dirs[i] / "index.md";
      return {idx.string(), true, fs::is_regular_file(idx, ec)};
    }
    if (fs::is_regular_file(files[i], ec)) {
      return {files[i].string(), false, true};
    }
  }
  return {files[1].string(), false, false};
}

Location ensure(const std::string& workspace) {
  Location loc = resolve(workspace);
  if (loc.exists) return loc;
  std::error_code ec;
  fs::create_directories(fs::path(loc.path).parent_path(), ec);
  if (ec) {
    LOG_WARN("task_notes: cannot create dir for {}: {}", loc.path, ec.message());
    return {};
  }
  std::ofstream out(loc.path);
  if (!out) return {};
  out << default_template();
  out.flush();
  if (!out) return {};
  loc.exists = true;
  return loc;
}

bool append_log(const std::string& workspace, const std::string& actor,
                const std::string& event) {
  std::lock_guard<std::mutex> lock(log_mutex());
  const Location loc = ensure(workspace);
  if (loc.path.empty()) return false;

  std::string content;
  {
    std::ifstream in(loc.path);
    std::ostringstream ss;
    ss << in.rdbuf();
    content = ss.str();
  }
  const bool has_log = content.rfind("## Log", 0) == 0 ||
                       content.find("\n## Log") != std::string::npos;
  std::ofstream out(loc.path, std::ios::app);
  if (!out) return false;
  if (!content.empty() && content.back() != '\n') out << '\n';
  if (!has_log) out << "\n## Log\n";
  out << "- " << now_stamp() << " [" << one_line(actor) << "] "
      << one_line(event) << '\n';
  return static_cast<bool>(out);
}

}  // namespace qcode::task_notes
