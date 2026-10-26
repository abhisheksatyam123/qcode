#pragma once

#include <httplib.h>
#include <openssl/evp.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

namespace qcode {
namespace providers {

// OpenCode builds its client identity as opencode/<channel>/<version>/<name>
// (packages/core/src/app.ts). qcode mirrors the current stable release so the
// free pool sees the same identity as the shipped client.
inline constexpr const char* kOpenCodeZenChannel = "latest";
inline constexpr const char* kOpenCodeZenVersion = "2.0.29";
inline constexpr const char* kOpenCodeZenClient = "cli";
inline constexpr const char* kOpenCodeZenUserAgent =
    "opencode/latest/2.0.29/cli";

inline bool is_opencode_zen_url(const std::string& url_or_host) {
  return url_or_host.find("opencode.ai") != std::string::npos;
}

inline std::string sha1_hex(std::string_view data) {
  unsigned char hash[EVP_MAX_MD_SIZE];
  unsigned int hash_len = 0;
  EVP_Digest(data.data(), data.size(), hash, &hash_len, EVP_sha1(), nullptr);
  std::ostringstream ss;
  for (unsigned int i = 0; i < hash_len; ++i) {
    ss << std::hex << std::setw(2) << std::setfill('0')
       << static_cast<int>(hash[i]);
  }
  return ss.str();
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

// OpenCode session id format: ses_<12hex><14base62> (total 30 chars).
inline bool is_opencode_session_id(std::string_view id) {
  if (id.length() != 30 || !id.starts_with("ses_")) return false;
  static constexpr std::string_view kHex = "0123456789abcdefABCDEF";
  for (size_t i = 4; i < 16; ++i) {
    if (kHex.find(id[i]) == std::string_view::npos) return false;
  }
  static constexpr std::string_view kBase62 =
      "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
  for (size_t i = 16; i < 30; ++i) {
    if (kBase62.find(id[i]) == std::string_view::npos) return false;
  }
  return true;
}

inline std::string generate_opencode_session_id() {
  static std::atomic<uint64_t> counter{0};
  auto now_ms = static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch()).count());
  uint64_t r = now_ms * 0x1000ull + (++counter);
  uint64_t a = ~r;
  static constexpr char kHex[] = "0123456789abcdef";
  static constexpr char kBase62[] =
      "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
  std::string id = "ses_";
  id.reserve(30);
  for (int i = 0; i < 6; ++i) {
    uint8_t byte = static_cast<uint8_t>((a >> (40 - 8 * i)) & 0xff);
    id += kHex[(byte >> 4) & 0x0f];
    id += kHex[byte & 0x0f];
  }
  std::random_device rd;
  std::mt19937_64 gen(
      (static_cast<uint64_t>(rd()) << 32) ^ rd() ^
      static_cast<uint64_t>(
          std::chrono::steady_clock::now().time_since_epoch().count()));
  for (int i = 0; i < 14; ++i) {
    id += kBase62[gen() % 62];
  }
  return id;
}

inline std::string make_opencode_session_id(uint64_t state) {
  static constexpr char kHex[] = "0123456789abcdef";
  static constexpr char kBase62[] =
      "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
  uint64_t val = splitmix64(state);
  std::string id = "ses_";
  id.reserve(30);
  for (int i = 0; i < 6; ++i) {
    uint8_t byte = static_cast<uint8_t>((val >> (40 - 8 * i)) & 0xff);
    id += kHex[(byte >> 4) & 0x0f];
    id += kHex[byte & 0x0f];
  }
  for (int i = 0; i < 14; ++i) {
    id += kBase62[splitmix64(state) % 62];
  }
  return id;
}

inline std::string stable_opencode_session_id(std::string_view seed) {
  return make_opencode_session_id(opencode_id_hash(seed));
}

inline std::string normalize_opencode_session_id(std::string value) {
  if (value.empty()) return generate_opencode_session_id();
  if (is_opencode_session_id(value)) return value;
  return stable_opencode_session_id(value);
}

inline std::string find_git_config(std::filesystem::path dir) {
  namespace fs = std::filesystem;
  std::error_code ec;
  while (!dir.empty()) {
    auto git_path = dir / ".git";
    if (fs::is_directory(git_path, ec)) {
      auto cfg = git_path / "config";
      if (fs::exists(cfg, ec)) return cfg.string();
    } else if (fs::is_regular_file(git_path, ec)) {
      std::ifstream gitfile(git_path);
      std::string line;
      if (std::getline(gitfile, line)) {
        if (line.starts_with("gitdir:")) {
          std::string gitdir = line.substr(7);
          while (!gitdir.empty() && (gitdir.front() == ' ' || gitdir.front() == '\t')) {
            gitdir.erase(gitdir.begin());
          }
          fs::path p(gitdir);
          if (p.is_relative()) p = dir / p;
          auto cfg = p / "config";
          if (fs::exists(cfg, ec)) return cfg.string();
          auto commondir = p / "commondir";
          if (fs::exists(commondir, ec)) {
            std::ifstream cdf(commondir);
            std::string cdir;
            if (std::getline(cdf, cdir)) {
              fs::path cp(cdir);
              if (cp.is_relative()) cp = p / cp;
              auto common_cfg = cp / "config";
              if (fs::exists(common_cfg, ec)) return common_cfg.string();
            }
          }
        }
      }
    }
    auto parent = dir.parent_path();
    if (parent == dir) break;
    dir = parent;
  }
  return {};
}

inline std::string extract_git_remote_url(const std::string& config_path) {
  std::ifstream f(config_path);
  if (!f.is_open()) return {};
  std::string line;
  std::string origin_url;
  std::string any_url;
  bool in_origin_remote = false;
  bool in_any_remote = false;
  while (std::getline(f, line)) {
    size_t start = line.find_first_not_of(" \\t\\r\\n");
    if (start == std::string::npos) continue;
    size_t end = line.find_last_not_of(" \\t\\r\\n");
    std::string trimmed = line.substr(start, end - start + 1);
    if (trimmed.front() == '[' && trimmed.back() == ']') {
      in_origin_remote = (trimmed.find("remote \\\"origin\\\"") != std::string::npos);
      in_any_remote = (trimmed.find("remote ") != std::string::npos);
      continue;
    }
    if (in_origin_remote || in_any_remote) {
      if (trimmed.starts_with("url =") || trimmed.starts_with("url=")) {
        size_t eq = trimmed.find('=');
        std::string url = trimmed.substr(eq + 1);
        while (!url.empty() && (url.front() == ' ' || url.front() == '\t')) url.erase(url.begin());
        while (!url.empty() && (url.back() == ' ' || url.back() == '\t' || url.back() == '\r')) url.pop_back();
        if (in_origin_remote) {
          origin_url = url;
          break;
        } else if (any_url.empty()) {
          any_url = url;
        }
      }
    }
  }
  return origin_url.empty() ? any_url : origin_url;
}

inline std::string canonicalize_git_remote(std::string url) {
  while (!url.empty() && (url.front() == ' ' || url.front() == '\t')) url.erase(url.begin());
  while (!url.empty() && (url.back() == ' ' || url.back() == '\t')) url.pop_back();
  if (url.empty()) return {};

  std::string hostname;
  std::string path;

  size_t proto = url.find("://");
  if (proto != std::string::npos) {
    std::string rest = url.substr(proto + 3);
    size_t at = rest.find('@');
    if (at != std::string::npos && at < rest.find('/')) {
      rest = rest.substr(at + 1);
    }
    size_t slash = rest.find('/');
    if (slash != std::string::npos) {
      hostname = rest.substr(0, slash);
      path = rest.substr(slash + 1);
    }
  } else {
    size_t at = url.find('@');
    std::string rest = (at != std::string::npos) ? url.substr(at + 1) : url;
    size_t colon = rest.find(':');
    if (colon != std::string::npos) {
      hostname = rest.substr(0, colon);
      path = rest.substr(colon + 1);
    }
  }

  if (hostname.empty() || path.empty()) return {};

  while (!path.empty() && path.front() == '/') path.erase(path.begin());
  while (path.ends_with("/")) path.pop_back();
  if (path.ends_with(".git")) {
    path = path.substr(0, path.length() - 4);
  }
  while (path.ends_with("/")) path.pop_back();

  for (char& ch : hostname) {
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  }

  return hostname + "/" + path;
}

inline std::string stable_opencode_project_id(std::string_view directory) {
  namespace fs = std::filesystem;
  std::error_code ec;
  fs::path dir = directory.empty() ? fs::current_path(ec) : fs::path(directory);
  if (!directory.empty()) {
    auto canon = fs::canonical(dir, ec);
    if (!ec) dir = canon;
  }
  std::string cfg = find_git_config(dir);
  if (!cfg.empty()) {
    std::string remote_url = extract_git_remote_url(cfg);
    if (!remote_url.empty()) {
      std::string canonical_remote = canonicalize_git_remote(remote_url);
      if (!canonical_remote.empty()) {
        return sha1_hex(std::string("git-remote:") + canonical_remote);
      }
    }
  }
  return sha1_hex(std::string("directory:") + dir.string());
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

  auto auth_it = headers.find("Authorization");
  if (auth_it == headers.end() || auth_it->second.empty()) {
    headers.erase("Authorization");
    headers.emplace("Authorization", "Bearer public");
  }

  std::string project = value_of("x-opencode-project");
  if (project.empty()) {
    project = stable_opencode_project_id("");
  }
  headers.erase("x-opencode-project");
  headers.emplace("x-opencode-project", project);

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
  if (affinity.empty()) affinity = value_of("x-session-id");
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
  headers.erase("x-session-id");
  headers.emplace("x-session-id", affinity);

  headers.erase("x-opencode-parent-session-id");
  headers.erase("x-parent-session-id");
  if (!parent.empty()) {
    headers.emplace("x-opencode-parent-session-id", parent);
    headers.emplace("x-parent-session-id", parent);
  }

  headers.erase("x-opencode-request");
}

}  // namespace providers
}  // namespace qcode
