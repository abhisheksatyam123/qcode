#pragma once

#include <httplib.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <random>
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

inline std::string generate_opencode_id(const std::string& prefix, bool descending) {
  static const char kBase62[] =
      "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
  static std::atomic<uint64_t> s_counter{0};

  auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                 std::chrono::system_clock::now().time_since_epoch())
                 .count();
  uint64_t count = s_counter.fetch_add(1, std::memory_order_relaxed) & 0xfff;
  uint64_t current = (static_cast<uint64_t>(now) * 0x1000) + count;
  uint64_t value = descending ? ~current : current;

  std::string result = prefix;
  char hex_buf[13];
  for (int i = 0; i < 6; ++i) {
    uint8_t byte = static_cast<uint8_t>((value >> (40 - 8 * i)) & 0xff);
    std::snprintf(hex_buf + i * 2, 3, "%02x", byte);
  }
  result.append(hex_buf, 12);

  thread_local std::mt19937_64 rng(std::random_device{}());
  std::uniform_int_distribution<size_t> dist(0, 61);
  for (int i = 0; i < 14; ++i) {
    result += kBase62[dist(rng)];
  }
  return result;
}

inline std::string generate_opencode_session_id() {
  return generate_opencode_id("ses_", true);
}

inline std::string generate_opencode_request_id() {
  return generate_opencode_id("msg_", false);
}

inline void apply_opencode_zen_headers(httplib::Headers& headers) {
  headers.erase("User-Agent");
  headers.emplace("User-Agent", kOpenCodeZenUserAgent);
  headers.erase("x-opencode-client");
  headers.emplace("x-opencode-client", "cli");
  auto it = headers.find("x-opencode-session");
  if (it == headers.end() || it->second.rfind("ses_", 0) != 0) {
    headers.erase("x-opencode-session");
    headers.emplace("x-opencode-session", generate_opencode_session_id());
  }
  headers.erase("x-opencode-request");
  headers.emplace("x-opencode-request", generate_opencode_request_id());
  if (headers.find("Authorization") == headers.end()) {
    headers.emplace("Authorization", "Bearer public");
  }
}

}  // namespace providers
}  // namespace qcode
