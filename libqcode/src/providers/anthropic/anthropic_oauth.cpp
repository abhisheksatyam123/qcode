#include "anthropic_oauth.h"

#include <qcode/core/logger.h>
#include <qcode/core/ssl_config.h>

#include <chrono>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <thread>
#include <condition_variable>

#if defined(__linux__) || defined(__APPLE__)
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include <httplib.h>
#include <nlohmann/json.hpp>
#include <openssl/evp.h>
#include <openssl/rand.h>

namespace qcode {
namespace anthropic {

namespace {

using ordered_json = nlohmann::ordered_json;

#if defined(__linux__) || defined(__APPLE__)
// Best-effort: launch the platform opener with an argv array (no shell), with
// stdin/stdout/stderr pointed at /dev/null. A double fork lets the grandchild be
// reparented to init, so no zombie is left and the caller never blocks on the
// browser process. Failures are ignored.
void open_url_in_browser(const std::string& url) {
#if defined(__APPLE__)
  const char* opener = "open";
#else
  const char* opener = "xdg-open";
#endif
  char* argv[] = {const_cast<char*>(opener), const_cast<char*>(url.c_str()), nullptr};

  pid_t pid = fork();
  if (pid < 0) return;
  if (pid == 0) {
    // Intermediate child: spawn the grandchild and exit immediately.
    pid_t gpid = fork();
    if (gpid == 0) {
      int devnull = open("/dev/null", O_RDWR);
      if (devnull >= 0) {
        dup2(devnull, STDIN_FILENO);
        dup2(devnull, STDOUT_FILENO);
        dup2(devnull, STDERR_FILENO);
        if (devnull > STDERR_FILENO) close(devnull);
      }
      execvp(opener, argv);
      _exit(127);
    }
    _exit(gpid < 0 ? 1 : 0);
  }
  // Reap the short-lived intermediate child; it exits right after forking.
  int status = 0;
  while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
  }
}
#endif

int64_t current_time_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

std::string url_encode(std::string_view value) {
  std::ostringstream escaped;
  escaped.fill('0');
  escaped << std::hex;
  for (char ch : value) {
    auto c = static_cast<unsigned char>(ch);
    if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      escaped << ch;
    } else {
      escaped << '%' << std::setw(2) << static_cast<int>(c);
    }
  }
  return escaped.str();
}

void replace_all(std::string& str, std::string_view from, std::string_view to) {
  size_t pos = 0;
  while ((pos = str.find(from, pos)) != std::string::npos) {
    str.replace(pos, from.length(), to);
    pos += to.length();
  }
}

}  // namespace

std::filesystem::path get_claude_credentials_path() {
  if (const char* env_path = std::getenv("CLAUDE_CREDENTIALS_FILE");
      env_path && *env_path != '\0') {
    return env_path;
  }
  if (const char* env_path = std::getenv("ANTHROPIC_CREDENTIALS_FILE");
      env_path && *env_path != '\0') {
    return env_path;
  }
  if (const char* home = std::getenv("HOME"); home && *home != '\0') {
    return std::filesystem::path(home) / ".claude" / ".credentials.json";
  }
  return ".claude/.credentials.json";
}

std::optional<ClaudeCredentials> read_claude_credentials(
    const std::filesystem::path& path) {
  if (!std::filesystem::exists(path)) {
    return std::nullopt;
  }

  try {
    std::ifstream file(path);
    if (!file) return std::nullopt;
    ordered_json doc = ordered_json::parse(file);
    if (!doc.contains("claudeAiOauth") || !doc["claudeAiOauth"].is_object()) {
      return std::nullopt;
    }
    const auto& block = doc["claudeAiOauth"];
    ClaudeCredentials creds;
    creds.access_token = block.value("accessToken", "");
    creds.refresh_token = block.value("refreshToken", "");
    if (block.contains("expiresAt")) {
      if (block["expiresAt"].is_number()) {
        creds.expires_at_ms = block["expiresAt"].get<int64_t>();
      } else if (block["expiresAt"].is_string()) {
        creds.expires_at_ms = std::stoll(block["expiresAt"].get<std::string>());
      }
    }
    creds.subscription_type = block.value("subscriptionType", "");
    creds.rate_limit_tier = block.value("rateLimitTier", "");
    return creds;
  } catch (const std::exception& e) {
    LOG_WARN("Failed to read Claude credentials from {}: {}", path.string(), e.what());
    return std::nullopt;
  }
}

bool write_claude_credentials(
    const ClaudeCredentials& creds,
    const std::filesystem::path& path) {
  try {
    if (path.has_parent_path()) {
      std::filesystem::create_directories(path.parent_path());
    }

    ordered_json doc;
    if (std::filesystem::exists(path)) {
      try {
        std::ifstream in(path);
        if (in) doc = ordered_json::parse(in);
      } catch (...) {}
    }

    doc["claudeAiOauth"]["accessToken"] = creds.access_token;
    doc["claudeAiOauth"]["refreshToken"] = creds.refresh_token;
    doc["claudeAiOauth"]["expiresAt"] = creds.expires_at_ms;
    if (!creds.subscription_type.empty()) {
      doc["claudeAiOauth"]["subscriptionType"] = creds.subscription_type;
    }
    if (!creds.rate_limit_tier.empty()) {
      doc["claudeAiOauth"]["rateLimitTier"] = creds.rate_limit_tier;
    }

    const auto tmp_path = path.string() + ".tmp";
    {
      std::ofstream out(tmp_path, std::ios::trunc);
      if (!out) return false;
      out << doc.dump(2);
    }

    std::error_code ec;
    std::filesystem::permissions(
        tmp_path,
        std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
        std::filesystem::perm_options::replace, ec);
    std::filesystem::rename(tmp_path, path, ec);
    return !ec;
  } catch (const std::exception& e) {
    LOG_ERROR("Failed to write Claude credentials to {}: {}", path.string(), e.what());
    return false;
  }
}

bool is_claude_token_expired(int64_t expires_at_ms) {
  if (expires_at_ms <= 0) return false;
  return (current_time_ms() + kRefreshBufferMs) >= expires_at_ms;
}

std::optional<ClaudeCredentials> refresh_claude_token(
    const std::string& refresh_token,
    const std::filesystem::path& path) {
  if (refresh_token.empty()) return std::nullopt;

  httplib::Client client(kClaudeTokenHost);
  qcode::http::configure_client_tls(client, true);
  client.set_connection_timeout(10);
  client.set_read_timeout(15);

  ordered_json body = {
      {"grant_type", "refresh_token"},
      {"refresh_token", refresh_token},
      {"client_id", kClaudeClientId},
      {"scope", kClaudeScopes},
  };

  const auto res = client.Post(kClaudeTokenPath, body.dump(), "application/json");
  if (!res || res->status != 200) {
    LOG_ERROR("Claude token refresh failed (status={}): {}",
              res ? res->status : 0, res ? res->body : "no response");
    return std::nullopt;
  }

  try {
    const auto data = ordered_json::parse(res->body);
    const auto access_token = data.value("access_token", "");
    if (access_token.empty()) return std::nullopt;

    const auto new_refresh = data.value("refresh_token", refresh_token);
    const int expires_in = data.value("expires_in", 3600);
    const int64_t expires_at = current_time_ms() + (static_cast<int64_t>(expires_in) * 1000);

    ClaudeCredentials creds;
    creds.access_token = access_token;
    creds.refresh_token = new_refresh;
    creds.expires_at_ms = expires_at;

    // Preserve existing subscription metadata if present
    if (auto existing = read_claude_credentials(path)) {
      creds.subscription_type = existing->subscription_type;
      creds.rate_limit_tier = existing->rate_limit_tier;
    }

    write_claude_credentials(creds, path);
    LOG_INFO("Successfully refreshed Claude Code OAuth token, expires in {}s", expires_in);
    return creds;
  } catch (const std::exception& e) {
    LOG_ERROR("Failed to parse Claude token refresh response: {}", e.what());
    return std::nullopt;
  }
}

std::optional<ClaudeCredentials> exchange_code_for_tokens(
    const std::string& code,
    const std::string& state,
    const std::string& verifier,
    const std::string& redirect_uri) {
  httplib::Client client(kClaudeTokenHost);
  qcode::http::configure_client_tls(client, true);
  client.set_connection_timeout(10);
  client.set_read_timeout(15);

  ordered_json body = {
      {"grant_type", "authorization_code"},
      {"code", code},
      {"redirect_uri", redirect_uri},
      {"client_id", kClaudeClientId},
      {"code_verifier", verifier},
      {"state", state},
  };

  const auto res = client.Post(kClaudeTokenPath, body.dump(), "application/json");
  if (!res || res->status != 200) {
    LOG_ERROR("Claude token exchange failed (status={}): {}",
              res ? res->status : 0, res ? res->body : "no response");
    return std::nullopt;
  }

  try {
    const auto data = ordered_json::parse(res->body);
    const auto access_token = data.value("access_token", "");
    const auto refresh_token = data.value("refresh_token", "");
    const int expires_in = data.value("expires_in", 3600);
    const int64_t expires_at = current_time_ms() + (static_cast<int64_t>(expires_in) * 1000);

    if (access_token.empty() || refresh_token.empty()) return std::nullopt;

    ClaudeCredentials creds;
    creds.access_token = access_token;
    creds.refresh_token = refresh_token;
    creds.expires_at_ms = expires_at;

    write_claude_credentials(creds);
    LOG_INFO("Successfully completed Claude Code PKCE login");
    return creds;
  } catch (const std::exception& e) {
    LOG_ERROR("Failed to parse token exchange response: {}", e.what());
    return std::nullopt;
  }
}

std::string base64url_encode(const unsigned char* data, size_t len) {
  static const char table[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
  std::string out;
  out.reserve(((len + 2) / 3) * 4);
  for (size_t i = 0; i < len; i += 3) {
    uint32_t b0 = data[i];
    uint32_t b1 = (i + 1 < len) ? data[i + 1] : 0;
    uint32_t b2 = (i + 2 < len) ? data[i + 2] : 0;
    uint32_t triple = (b0 << 16) | (b1 << 8) | b2;
    out.push_back(table[(triple >> 18) & 0x3F]);
    out.push_back(table[(triple >> 12) & 0x3F]);
    if (i + 1 < len) out.push_back(table[(triple >> 6) & 0x3F]);
    if (i + 2 < len) out.push_back(table[triple & 0x3F]);
  }
  return out;
}

std::string generate_code_verifier() {
  unsigned char buf[32];
  if (RAND_bytes(buf, sizeof(buf)) != 1) {
    for (size_t i = 0; i < sizeof(buf); ++i) buf[i] = static_cast<unsigned char>(std::rand());
  }
  return base64url_encode(buf, sizeof(buf));
}

std::string generate_code_challenge(const std::string& verifier) {
  unsigned char hash[EVP_MAX_MD_SIZE];
  unsigned int hash_len = 0;
  EVP_Digest(verifier.data(), verifier.size(), hash, &hash_len, EVP_sha256(), nullptr);
  return base64url_encode(hash, hash_len);
}

std::string generate_state() {
  unsigned char buf[32];
  if (RAND_bytes(buf, sizeof(buf)) != 1) {
    for (size_t i = 0; i < sizeof(buf); ++i) buf[i] = static_cast<unsigned char>(std::rand());
  }
  return base64url_encode(buf, sizeof(buf));
}

std::string sha1_hex(const std::string& data) {
  unsigned char hash[EVP_MAX_MD_SIZE];
  unsigned int hash_len = 0;
  EVP_Digest(data.data(), data.size(), hash, &hash_len, EVP_sha1(), nullptr);
  std::ostringstream ss;
  ss << std::hex << std::setfill('0');
  for (unsigned int i = 0; i < hash_len; ++i) {
    ss << std::setw(2) << static_cast<int>(hash[i]);
  }
  return ss.str();
}

std::string rewrite_claude_prompt_tags(std::string text) {
  replace_all(text, "<directories>", "[directories]");
  replace_all(text, "</directories>", "[/directories]");
  replace_all(text, "<env>", "[env]");
  replace_all(text, "</env>", "[/env]");
  return text;
}

std::string generate_billing_fingerprint() {
  // Process-stable fingerprint: prompt caching requires a byte-stable prefix
  // from the first system token, so rotating sha1(now) every request would
  // guarantee a 0% cache hit rate. Compute once per process.
  static const std::string cached = sha1_hex(std::to_string(current_time_ms())).substr(0, 8);
  return cached;
}

std::string format_billing_header() {
  return "x-anthropic-billing-header: cc_version=2.1.300." +
         generate_billing_fingerprint() + "; cc_entrypoint=cli;\n";
}

std::string format_billing_header_value() {
  return "cc_version=2.1.300." + generate_billing_fingerprint() + "; cc_entrypoint=cli;";
}

bool login_anthropic_oauth(std::string* out_error) {
  httplib::Server server;
  int port = server.bind_to_any_port("127.0.0.1");
  if (port <= 0) {
    if (out_error) *out_error = "Failed to bind local loopback server for OAuth callback";
    return false;
  }

  const std::string verifier = generate_code_verifier();
  const std::string challenge = generate_code_challenge(verifier);
  const std::string state = generate_state();
  const std::string redirect_uri = "http://localhost:" + std::to_string(port) + "/callback";

  std::string auth_url = std::string(kClaudeAuthorizeUrl) +
                         "?code=true" +
                         "&client_id=" + kClaudeClientId +
                         "&response_type=code" +
                         "&redirect_uri=" + url_encode(redirect_uri) +
                         "&scope=" + url_encode(kClaudeScopes) +
                         "&code_challenge=" + challenge +
                         "&code_challenge_method=S256" +
                         "&state=" + state;

  std::mutex cv_m;
  std::condition_variable cv;
  bool done = false;
  std::string captured_code;

  server.Get("/callback", [&](const httplib::Request& req, httplib::Response& res) {
    const auto req_state = req.get_param_value("state");
    const auto req_code = req.get_param_value("code");

    if (req_state != state || req_code.empty()) {
      res.status = 400;
      res.set_content("Invalid OAuth state or missing code", "text/plain");
      std::lock_guard<std::mutex> lk(cv_m);
      done = true;
      cv.notify_one();
      return;
    }

    captured_code = req_code;
    res.set_redirect(kClaudeSuccessUrl);

    std::lock_guard<std::mutex> lk(cv_m);
    done = true;
    cv.notify_one();
  });

  std::thread server_thread([&server]() {
    server.listen_after_bind();
  });

  LOG_INFO("Claude Code OAuth Login: Visit the following URL in your browser:\n{}", auth_url);

#if defined(__linux__) || defined(__APPLE__)
  open_url_in_browser(auth_url);
#endif

  {
    std::unique_lock<std::mutex> lk(cv_m);
    if (!cv.wait_for(lk, std::chrono::seconds(180), [&done]() { return done; })) {
      server.stop();
      if (server_thread.joinable()) server_thread.join();
      if (out_error) *out_error = "OAuth login timed out waiting for browser callback (180s)";
      return false;
    }
  }

  server.stop();
  if (server_thread.joinable()) server_thread.join();

  if (captured_code.empty()) {
    if (out_error) *out_error = "No authorization code captured from callback";
    return false;
  }

  auto tokens = exchange_code_for_tokens(captured_code, state, verifier, redirect_uri);
  if (!tokens) {
    if (out_error) *out_error = "Failed to exchange authorization code for tokens";
    return false;
  }

  return true;
}

}  // namespace anthropic
}  // namespace qcode
