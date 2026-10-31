#pragma once

#include <optional>
#include <string>
#include <utility>

namespace qcode {
namespace http {

// True for the production OpenCode Zen inference host. Local test/mock hosts
// such as opencode.ai.localhost deliberately do not match.
bool is_opencode_zen_host(const std::string& host);

// Starts (once) the Bun TLS proxy used for OpenCode Zen requests and returns
// its local host/port. Returns nullopt when Bun is unavailable or startup
// fails; callers then fall back to the normal OpenSSL transport.
std::optional<std::pair<std::string, int>> zen_bun_proxy_endpoint();

}  // namespace http
}  // namespace qcode
