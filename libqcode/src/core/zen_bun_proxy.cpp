#include "core/zen_bun_proxy.h"

#include <qcode/core/logger.h>

#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace qcode {
namespace http {
namespace {

constexpr std::string_view kProxyScript = R"JS(
// Local reverse proxy for OpenCode Zen inference. qcode sends plain HTTP here;
// Bun owns the upstream TLS connection so the request carries the same
// BoringSSL TLS fingerprint as the OpenCode client.
const UPSTREAM_HOST = "opencode.ai";
const UPSTREAM_PORT = 443;
const PARENT_PID = process.ppid;

function httpError(socket, status, message) {
  const body = Buffer.from(String(message || "proxy error"), "utf8");
  const head = Buffer.from(
    `HTTP/1.1 ${status} Error\r\nContent-Type: text/plain\r\nContent-Length: ${body.length}\r\nConnection: close\r\n\r\n`,
    "latin1",
  );
  try { socket.write(Buffer.concat([head, body])); } catch {}
  try { socket.end(); } catch {}
}

function startUpstream(local, st) {
  const headers = [];
  for (const [k, v] of st.headers) {
    const lk = k.toLowerCase();
    if (
      lk === "host" ||
      lk === "connection" ||
      lk === "proxy-connection" ||
      lk === "keep-alive" ||
      lk === "accept-encoding"
    ) {
      continue;
    }
    headers.push([k, v]);
  }
  let head = `${st.method} ${st.path} HTTP/1.1\r\nHost: ${UPSTREAM_HOST}\r\n`;
  for (const [k, v] of headers) head += `${k}: ${v}\r\n`;
  head += "\r\n";
  const req = Buffer.concat([
    Buffer.from(head, "latin1"),
    st.body.subarray(0, st.bodyLen),
  ]);

  let upstream = null;
  let finished = false;
  let responseStarted = false;
  let responseBuf = Buffer.alloc(0);
  let connectTimer = null;

  const finish = () => {
    if (finished) return;
    finished = true;
    if (connectTimer) clearTimeout(connectTimer);
    st.upstream = null;
    try { if (upstream) upstream.end(); } catch {}
    try { local.end(); } catch {}
  };

  connectTimer = setTimeout(() => {
    if (!responseStarted) {
      httpError(local, 504, "upstream connect timeout");
      finish();
    }
  }, 30000);

  Bun.connect({
    hostname: UPSTREAM_HOST,
    port: UPSTREAM_PORT,
    tls: { serverName: UPSTREAM_HOST },
    socket: {
      open(socket) {
        upstream = socket;
        st.upstream = socket;
        socket.write(req);
      },
      data(socket, data) {
        if (finished) return;
        if (connectTimer) {
          clearTimeout(connectTimer);
          connectTimer = null;
        }
        const chunk = Buffer.from(data);
        if (responseStarted) {
          try { local.write(chunk); } catch { finish(); }
          return;
        }
        responseBuf = Buffer.concat([responseBuf, chunk]);
        const end = responseBuf.indexOf("\r\n\r\n");
        if (end === -1) {
          if (responseBuf.length > 128 * 1024) {
            httpError(local, 502, "upstream response headers too large");
            finish();
          }
          return;
        }
        const rawHead = responseBuf.subarray(0, end).toString("latin1");
        const rest = responseBuf.subarray(end + 4);
        const out = [];
        for (const line of rawHead.split("\r\n")) {
          const lower = line.toLowerCase();
          if (lower.startsWith("connection:") || lower.startsWith("keep-alive:")) {
            continue;
          }
          out.push(line);
        }
        out.push("Connection: close");
        responseStarted = true;
        try {
          local.write(Buffer.from(out.join("\r\n") + "\r\n\r\n", "latin1"));
          local.write(rest);
        } catch { finish(); }
      },
      close() { finish(); },
      error(socket, err) {
        if (!responseStarted) httpError(local, 502, `upstream TLS error: ${err}`);
        finish();
      },
    },
  }).catch((err) => {
    if (!responseStarted) httpError(local, 502, `upstream connect error: ${err}`);
    finish();
  });
}

const server = Bun.listen({
  hostname: "127.0.0.1",
  port: 0,
  socket: {
    open(socket) {
      socket.data = {
        buf: Buffer.alloc(0),
        headersDone: false,
        bodyLen: 0,
        bodyOffset: 0,
        started: false,
        closed: false,
        upstream: null,
      };
    },
    data(socket, data) {
      const st = socket.data;
      if (!st || st.closed || st.started) return;
      st.buf = Buffer.concat([st.buf, Buffer.from(data)]);
      if (!st.headersDone) {
        const end = st.buf.indexOf("\r\n\r\n");
        if (end === -1) return;
        const head = st.buf.subarray(0, end).toString("latin1");
        const lines = head.split("\r\n");
        const parts = lines[0].split(" ");
        if (parts.length < 2) {
          httpError(socket, 400, "bad request line");
          st.closed = true;
          return;
        }
        st.method = parts[0];
        st.path = parts[1];
        st.headers = [];
        for (let i = 1; i < lines.length; i++) {
          const p = lines[i].indexOf(":");
          if (p > 0) {
            st.headers.push([
              lines[i].slice(0, p).trim(),
              lines[i].slice(p + 1).trim(),
            ]);
          }
        }
        const cl = st.headers.find(([k]) => k.toLowerCase() === "content-length");
        st.bodyLen = cl ? Number.parseInt(cl[1], 10) || 0 : 0;
        st.bodyOffset = end + 4;
        st.headersDone = true;
      }
      if (!st.started &&
          st.buf.length >= st.bodyOffset + st.bodyLen) {
        st.started = true;
        st.body = st.buf.subarray(st.bodyOffset, st.bodyOffset + st.bodyLen);
        startUpstream(socket, st);
      }
    },
    close(socket) {
      const st = socket.data;
      if (!st) return;
      st.closed = true;
      if (st.upstream) {
        try { st.upstream.end(); } catch {}
      }
    },
    error(socket, err) {
      const st = socket.data;
      if (st) st.closed = true;
    },
  },
});

console.log(`READY ${server.port}`);

// Exit when the qcode parent goes away, so an abnormal server exit does not
// leave orphaned Bun processes behind.
setInterval(() => {
  if (process.ppid !== PARENT_PID) process.exit(0);
}, 1000);
)JS";

bool executable(const std::string& path) {
  return !path.empty() && access(path.c_str(), X_OK) == 0;
}

std::string find_bun() {
  if (const char* configured = std::getenv("QCODE_BUN_PATH")) {
    if (executable(configured)) return configured;
  }
  if (const char* home = std::getenv("HOME")) {
    const std::string candidate = std::string(home) + "/.bun/bin/bun";
    if (executable(candidate)) return candidate;
  }
  if (const char* path = std::getenv("PATH")) {
    std::string_view view(path);
    while (!view.empty()) {
      const auto colon = view.find(':');
      const auto dir = view.substr(0, colon);
      if (!dir.empty()) {
        const std::string candidate =
            std::string(dir) + "/bun";
        if (executable(candidate)) return candidate;
      }
      if (colon == std::string_view::npos) break;
      view.remove_prefix(colon + 1);
    }
  }
  for (const char* candidate : {
           "/usr/local/bin/bun",
           "/opt/homebrew/bin/bun",
           "/usr/bin/bun",
           "/home/linuxbrew/.linuxbrew/bin/bun",
       }) {
    if (executable(candidate)) return candidate;
  }
  return {};
}

std::string write_script() {
  try {
    const auto dir = std::filesystem::temp_directory_path();
    std::string tmpl = (dir / "qcode-zen-proxy-XXXXXX.js").string();
    std::vector<char> mutable_path(tmpl.begin(), tmpl.end());
    mutable_path.push_back('\0');
    const int fd = mkstemps(mutable_path.data(), 3);
    if (fd < 0) {
      LOG_WARN("OpenCode Zen Bun proxy: mkstemps failed: {}",
               std::strerror(errno));
      return {};
    }
    const std::string path(mutable_path.data());
    size_t offset = 0;
    while (offset < kProxyScript.size()) {
      const ssize_t written =
          ::write(fd, kProxyScript.data() + offset,
                  kProxyScript.size() - offset);
      if (written <= 0) {
        ::close(fd);
        ::unlink(path.c_str());
        return {};
      }
      offset += static_cast<size_t>(written);
    }
    ::close(fd);
    return path;
  } catch (...) {
    return {};
  }
}

struct ProxyState {
  std::mutex mutex;
  bool attempted = false;
  std::optional<std::pair<std::string, int>> endpoint;
  pid_t child = -1;
};

ProxyState& proxy_state() {
  static ProxyState state;
  return state;
}

std::optional<int> read_ready_port(int fd, pid_t child) {
  std::string output;
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(8);
  while (std::chrono::steady_clock::now() < deadline) {
    pollfd pfd{fd, POLLIN, 0};
    const int ready = poll(&pfd, 1, 100);
    if (ready < 0 && errno != EINTR) return std::nullopt;
    if (ready > 0) {
      char buffer[256];
      const ssize_t n = read(fd, buffer, sizeof(buffer));
      if (n > 0) {
        output.append(buffer, static_cast<size_t>(n));
        for (;;) {
          const auto newline = output.find('\n');
          if (newline == std::string::npos) break;
          const std::string line = output.substr(0, newline);
          output.erase(0, newline + 1);
          if (line.starts_with("READY ")) {
            try {
              return std::stoi(line.substr(6));
            } catch (...) {
              return std::nullopt;
            }
          }
        }
      } else if (n == 0) {
        return std::nullopt;
      }
    }
    int status = 0;
    if (waitpid(child, &status, WNOHANG) == child) {
      return std::nullopt;
    }
  }
  return std::nullopt;
}

}  // namespace

bool is_opencode_zen_host(const std::string& host) {
  return host == "opencode.ai";
}

std::optional<std::pair<std::string, int>> zen_bun_proxy_endpoint() {
  auto& state = proxy_state();
  std::lock_guard<std::mutex> lock(state.mutex);
  if (state.attempted) {
    if (!state.endpoint || state.child <= 0) return state.endpoint;
    int status = 0;
    const pid_t reaped = waitpid(state.child, &status, WNOHANG);
    if (reaped == state.child || (reaped < 0 && errno == ECHILD)) {
      LOG_WARN("OpenCode Zen Bun proxy exited; restarting it");
      state.child = -1;
      state.endpoint.reset();
      state.attempted = false;
    } else {
      return state.endpoint;
    }
  }
  state.attempted = true;

  if (const char* disabled = std::getenv("QCODE_ZEN_NO_BUN_PROXY");
      disabled != nullptr && *disabled != '\0') {
    LOG_INFO("OpenCode Zen Bun proxy disabled by QCODE_ZEN_NO_BUN_PROXY");
    return std::nullopt;
  }

  const std::string bun = find_bun();
  if (bun.empty()) {
    LOG_WARN(
        "OpenCode Zen free-tier requests need Bun for the upstream TLS "
        "fingerprint; install bun or set QCODE_BUN_PATH");
    return std::nullopt;
  }

  const std::string script = write_script();
  if (script.empty()) {
    LOG_WARN("OpenCode Zen Bun proxy: could not write temporary script");
    return std::nullopt;
  }

  int out_pipe[2] = {-1, -1};
  if (pipe(out_pipe) != 0) {
    ::unlink(script.c_str());
    LOG_WARN("OpenCode Zen Bun proxy: pipe failed: {}", std::strerror(errno));
    return std::nullopt;
  }

  const std::string log_path =
      "/tmp/qcode-zen-proxy-" + std::to_string(getpid()) + ".log";
  const pid_t child = fork();
  if (child < 0) {
    close(out_pipe[0]);
    close(out_pipe[1]);
    ::unlink(script.c_str());
    LOG_WARN("OpenCode Zen Bun proxy: fork failed: {}", std::strerror(errno));
    return std::nullopt;
  }

  if (child == 0) {
    close(out_pipe[0]);
    dup2(out_pipe[1], STDOUT_FILENO);
    close(out_pipe[1]);
    const int null_fd = open("/dev/null", O_RDONLY);
    if (null_fd >= 0) {
      dup2(null_fd, STDIN_FILENO);
      close(null_fd);
    }
    const int log_fd = open(log_path.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0600);
    if (log_fd >= 0) {
      dup2(log_fd, STDERR_FILENO);
      close(log_fd);
    }
    // Do not use PR_SET_PDEATHSIG here: qcode starts the proxy from a
    // short-lived request thread, and Linux would signal the child when that
    // thread exits even though the server process is still alive. The Bun
    // script watches process.ppid instead.
    execl(bun.c_str(), bun.c_str(), script.c_str(), static_cast<char*>(nullptr));
    _exit(127);
  }

  close(out_pipe[1]);
  const auto port = read_ready_port(out_pipe[0], child);
  close(out_pipe[0]);
  ::unlink(script.c_str());

  if (!port || *port <= 0) {
    kill(child, SIGTERM);
    waitpid(child, nullptr, 0);
    LOG_WARN(
        "OpenCode Zen Bun proxy failed to start; see {} (Bun: {})",
        log_path, bun);
    return std::nullopt;
  }

  state.child = child;
  state.endpoint = std::make_pair(std::string("127.0.0.1"), *port);
  LOG_INFO("OpenCode Zen Bun proxy listening on 127.0.0.1:{} (pid {})",
           *port, child);
  return state.endpoint;
}

}  // namespace http
}  // namespace qcode
