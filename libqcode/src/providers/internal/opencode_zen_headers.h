#pragma once

#include <qcode/core/random.h>

#include <httplib.h>

#include <string>

namespace qcode {
namespace providers {

// OpenCode Zen's Console free pool only admits this User-Agent. Other UAs
// (including cpp-httplib's default) get 429 FreeUsageLimitError on -free
// models. Matches packages/opencode/src/session/llm/request.ts.
inline constexpr const char* kOpenCodeZenUserAgent = "opencode/1.18.18";

inline bool is_opencode_zen_url(const std::string& url_or_host) {
  return url_or_host.find("opencode.ai") != std::string::npos;
}

inline void apply_opencode_zen_headers(httplib::Headers& headers) {
  static const auto session_id = qcode::utils::new_uuid();
  headers.erase("User-Agent");
  headers.emplace("User-Agent", kOpenCodeZenUserAgent);
  headers.erase("x-opencode-client");
  headers.emplace("x-opencode-client", "cli");
  if (headers.find("x-opencode-session") == headers.end()) {
    headers.emplace("x-opencode-session", session_id);
  }
  headers.erase("x-opencode-request");
  headers.emplace("x-opencode-request", qcode::utils::new_uuid());
}

}  // namespace providers
}  // namespace qcode
