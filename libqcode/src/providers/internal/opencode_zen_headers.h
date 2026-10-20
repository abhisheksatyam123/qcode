#pragma once

#include <httplib.h>

#include <chrono>
#include <cstdint>
#include <random>
#include <string>
#include <string_view>
#include <utility>

namespace qcode {
namespace providers {

// OpenCode builds its client identity as opencode/<channel>/<version>/<name>
// (packages/core/src/app.ts). qcode mirrors the current stable release so the
// free pool sees the same identity as the shipped client.
inline constexpr const char* kOpenCodeZenChannel = "latest";
inline constexpr const char* kOpenCodeZenVersion = "2.1.0";
inline constexpr const char* kOpenCodeZenClient = "cli";
inline constexpr const char* kOpenCodeZenUserAgent =
    "opencode/latest/2.1.0/cli";

inline bool is_opencode_zen_url(const std::string& url_or_host) {
  return url_or_host.find("opencode.ai") != std::string::npos;
}

inline uint64_t opencode_id_hash(std::string_view text) {
  uint64_t hash = 14695981039346656037ull;
  for (unsigned char ch : text) {
    hash ^= ch;
    hash *= 1099511628211ull;
  }
  return hash;
}

inline uint64_t splitmix64(uint64_t& state) {
  uint64_t value = (state += 0x9e3779b97f4a7c15ull);
  value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ull;
  value = (value ^ (value >> 27)) * 0x94d049bb133111ebull;
  return value ^ (value >> 31);
}

inline std::string hex_u64(uint64_t value) {
  static constexpr char kHex[] = "0123456789abcdef";
  std::string out(16, '0');
  for (int i = 15; i >= 0; --i) {
    out[static_cast<std::size_t>(i)] = kHex[value & 0xf];
    value >>= 4;
  }
  return out;
}

inline std::string make_opencode_session_id(uint64_t state) {
  std::string id = "ses_";
  for (int i = 0; i < 4; ++i) id += hex_u64(splitmix64(state));
  return id;
}

inline std::string generate_opencode_session_id() {
  std::random_device device;
  uint64_t state =
      (static_cast<uint64_t>(device()) << 32) ^ device() ^
      static_cast<uint64_t>(
          std::chrono::steady_clock::now().time_since_epoch().count());
  return make_opencode_session_id(state);
}

// OpenCode uses ses_<64 hex> ids and routes provider caches by this value.
// qcode session ids are UUIDs, so derive a stable id from the UUID instead of
// minting a new one on every request.
inline std::string stable_opencode_session_id(std::string_view seed) {
  return make_opencode_session_id(opencode_id_hash(seed));
}

inline std::string normalize_opencode_session_id(std::string value) {
  if (value.empty()) return generate_opencode_session_id();
  if (value.starts_with("ses_")) return value;
  return stable_opencode_session_id(value);
}

// OpenCode project ids are opaque 40-char hex strings (sha1 for non-repo
// directories). Match that shape with a stable per-directory id; callers that
// already know the real project id should set ProviderOptions::project_id.
inline std::string stable_opencode_project_id(std::string_view directory) {
  std::string seed = "directory:";
  seed.append(directory);
  uint64_t state = opencode_id_hash(seed);
  std::string id;
  for (int i = 0; i < 3; ++i) id += hex_u64(splitmix64(state));
  id.resize(40);
  return id;
}

inline void apply_opencode_zen_headers(httplib::Headers& headers) {
  headers.erase("User-Agent");
  headers.emplace("User-Agent", kOpenCodeZenUserAgent);
  headers.erase("x-opencode-client");
  headers.emplace("x-opencode-client", kOpenCodeZenClient);

  const auto value_of = [&headers](const char* name) {
    const auto it = headers.find(name);
    return it == headers.end() ? std::string{} : it->second;
  };

  // x-opencode-session carries the cache affinity (children share their
  // parent's), while x-opencode-session-id is the concrete session.
  std::string session = value_of("x-opencode-session-id");
  if (session.empty()) session = value_of("x-opencode-session");
  session = normalize_opencode_session_id(std::move(session));

  std::string parent = value_of("x-opencode-parent-session-id");
  if (parent.empty()) parent = value_of("x-parent-session-id");
  if (!parent.empty()) {
    parent = normalize_opencode_session_id(std::move(parent));
  }

  std::string affinity = value_of("x-session-affinity");
  if (affinity.empty()) affinity = value_of("X-Session-Id");
  if (affinity.empty()) affinity = value_of("x-opencode-session");
  if (affinity.empty()) affinity = parent;
  if (affinity.empty()) affinity = session;
  affinity = normalize_opencode_session_id(std::move(affinity));

  headers.erase("x-opencode-session");
  headers.emplace("x-opencode-session", affinity);
  headers.erase("x-opencode-session-id");
  headers.emplace("x-opencode-session-id", session);
  headers.erase("x-session-affinity");
  headers.emplace("x-session-affinity", affinity);
  headers.erase("X-Session-Id");
  headers.emplace("X-Session-Id", affinity);

  headers.erase("x-opencode-parent-session-id");
  headers.erase("x-parent-session-id");
  if (!parent.empty()) {
    headers.emplace("x-opencode-parent-session-id", parent);
    headers.emplace("x-parent-session-id", parent);
  }

  // Upstream dropped this tracing-only header in v2; don't send stale extras.
  headers.erase("x-opencode-request");
}

}  // namespace providers
}  // namespace qcode
