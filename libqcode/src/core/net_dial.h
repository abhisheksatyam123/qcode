#pragma once

#ifdef _WIN32
#include <winsock2.h>
#else
#include <sys/socket.h>
#endif

#include <string>

namespace qcode {
namespace net {

// Picks the address family that actually reaches `host:port` from this
// machine, or AF_UNSPEC when the answer does not matter.
//
// getaddrinfo() hands back every address a host has, and a machine can hold
// an IPv6 address whose route to the internet is black-holed (a router that
// advertises a prefix but silently drops the traffic, a tunnel that stopped
// carrying v6). httplib takes the first address getaddrinfo returns and, when
// that connect times out, gives up on the whole address list instead of
// trying the next one — so the provider looks dead even though IPv4 would
// have answered immediately. Every model call then burns the full connect
// timeout, times out, and is retried into the same wall.
//
// This probes the candidate families with a short connect timeout and returns
// the first that completes a TCP handshake, so an unroutable family costs
// milliseconds once instead of the provider's connect timeout per request.
// The answer is cached per host:port, so the probe runs once per provider.
int probe_address_family(const std::string& host, int port,
                         int timeout_ms = 1500);

}  // namespace net
}  // namespace qcode
