#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace qcode {
namespace server {

inline constexpr std::uintmax_t kMaxFsFileBytes = 2 * 1024 * 1024;  // 2 MiB

std::string shell_quote(const std::string& s);
std::string exec_capture(const std::string& cmd);
std::string expand_tilde(const std::string& s);
std::vector<std::string> split_lines(const std::string& s);

std::string resolve_session_workspace(const std::string& sid);

// Resolve a client-relative path against the session workspace. Rejects
// absolute paths and any path that escapes the workspace root.
// On success: sets abs_out to the absolute path and rel_out to the
// normalized relative path ("" for workspace root). Returns error message or "".
std::string resolve_workspace_path(const std::string& workspace,
                                   const std::string& rel_in,
                                   std::string& abs_out,
                                   std::string& rel_out);

bool looks_binary(const std::string& data);

}  // namespace server
}  // namespace qcode
