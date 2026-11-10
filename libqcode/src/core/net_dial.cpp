#include "core/net_dial.h"

#include <cerrno>
#include <chrono>
#include <cstring>
#include <mutex>
#include <unordered_map>

#ifndef _WIN32
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace qcode {
namespace net {

namespace {

// One probe answer per host:port.
struct CacheEntry {
    int family = AF_UNSPEC;
    std::chrono::steady_clock::time_point probed_at{};
};

std::mutex g_cache_mutex;
std::unordered_map<std::string, CacheEntry> g_cache;

// A working family is trusted for a long time: a healthy network never
// re-probes, and a route that comes back is still usable.
constexpr auto kPositiveTtl = std::chrono::minutes(30);
// "Nothing answered" is re-probed sooner, so a network that was down for a
// moment recovers without waiting out the full positive TTL.
constexpr auto kNegativeTtl = std::chrono::seconds(60);

bool is_ip_literal(const std::string& host) {
#ifdef _WIN32
    in_addr v4{};
    in6_addr v6{};
#else
    struct in_addr v4 {};
    struct in6_addr v6 {};
#endif
    return inet_pton(AF_INET, host.c_str(), &v4) == 1 ||
           inet_pton(AF_INET6, host.c_str(), &v6) == 1;
}

// True when the host has at least one address of this family.
bool has_address(const std::string& host, int family) {
    struct addrinfo hints {};
    hints.ai_family = family;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo* result = nullptr;
    if (getaddrinfo(host.c_str(), nullptr, &hints, &result) != 0) return false;
    const bool found = result != nullptr;
    if (result) freeaddrinfo(result);
    return found;
}

// Opens a TCP connection to the first address of `family` that answers and
// reports whether the handshake completed. The socket is non-blocking so a
// black-holed route cannot outlive `timeout_ms`.
bool can_connect(const std::string& host, int port, int family, int timeout_ms) {
    struct addrinfo hints {};
    hints.ai_family = family;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo* result = nullptr;
    if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints,
                    &result) != 0) {
        return false;
    }

    bool connected = false;
    for (auto* rp = result; rp && !connected; rp = rp->ai_next) {
        const auto fd =
            socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0) continue;
#ifndef _WIN32
        const int flags = fcntl(fd, F_GETFL, 0);
        if (flags >= 0) fcntl(fd, F_SETFL, flags | O_NONBLOCK);
#endif
        int rc = connect(fd, rp->ai_addr,
                         static_cast<socklen_t>(rp->ai_addrlen));
        if (rc != 0 && errno == EINPROGRESS) {
            struct pollfd pfd {};
            pfd.fd = fd;
            pfd.events = POLLOUT;
            rc = poll(&pfd, 1, timeout_ms);
            if (rc > 0) {
                int err = 0;
                socklen_t len = sizeof(err);
                if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) == 0 &&
                    err == 0) {
                    connected = true;
                }
            }
        } else if (rc == 0) {
            connected = true;
        }
        close(fd);
    }
    if (result) freeaddrinfo(result);
    return connected;
}

}  // namespace

int probe_address_family(const std::string& host, int port, int timeout_ms) {
    // A literal address needs no family choice, and an empty host is not
    // something to probe.
    if (host.empty() || port <= 0 || is_ip_literal(host)) return AF_UNSPEC;

    const std::string key = host + ":" + std::to_string(port);
    const auto now = std::chrono::steady_clock::now();
    {
        std::lock_guard<std::mutex> lock(g_cache_mutex);
        const auto it = g_cache.find(key);
        if (it != g_cache.end()) {
            const auto ttl = it->second.family == AF_UNSPEC ? kNegativeTtl
                                                             : kPositiveTtl;
            if (now - it->second.probed_at < ttl) return it->second.family;
        }
    }

    int family = AF_UNSPEC;
    // IPv6 first: it is the preferred path when it works, and the probe is
    // cheap. A black-holed IPv6 route costs one timeout, not one per request.
    if (has_address(host, AF_INET6) &&
        can_connect(host, port, AF_INET6, timeout_ms)) {
        family = AF_INET6;
    } else if (has_address(host, AF_INET) &&
               can_connect(host, port, AF_INET, timeout_ms)) {
        family = AF_INET;
    }

    {
        std::lock_guard<std::mutex> lock(g_cache_mutex);
        g_cache[key] = CacheEntry{family, std::chrono::steady_clock::now()};
    }
    return family;
}

}  // namespace net
}  // namespace qcode
