// Checks the address-family probe that keeps provider requests off a broken
// IPv6 route. httplib takes the first address getaddrinfo returns and gives
// up on the whole list when that connect times out, so a machine whose v6
// route is black-holed would otherwise stall on every model call.
#include <core/net_dial.h>

#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <string>

namespace qcode {
namespace net {
namespace {

// A loopback listener that stays open for the test: a probe needs something
// on the other end to complete its handshake.
class LoopbackListener {
 public:
    LoopbackListener() {
        fd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (fd_ < 0) return;
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;
        if (bind(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 ||
            listen(fd_, 4) != 0) {
            close(fd_);
            fd_ = -1;
            return;
        }
        sockaddr_in bound{};
        socklen_t len = sizeof(bound);
        if (getsockname(fd_, reinterpret_cast<sockaddr*>(&bound), &len) != 0) {
            close(fd_);
            fd_ = -1;
            return;
        }
        port_ = ntohs(bound.sin_port);
    }

    ~LoopbackListener() {
        if (fd_ >= 0) close(fd_);
    }

    LoopbackListener(const LoopbackListener&) = delete;
    LoopbackListener& operator=(const LoopbackListener&) = delete;

    int port() const { return port_; }

 private:
    int fd_ = -1;
    int port_ = 0;
};

TEST(NetDialTest, EmptyHostNeedsNoFamilyChoice) {
    EXPECT_EQ(probe_address_family("", 443), AF_UNSPEC);
}

TEST(NetDialTest, IpLiteralNeedsNoFamilyChoice) {
    // A literal address already fixes the family; probing it would only add
    // latency.
    EXPECT_EQ(probe_address_family("127.0.0.1", 443), AF_UNSPEC);
    EXPECT_EQ(probe_address_family("::1", 443), AF_UNSPEC);
}

TEST(NetDialTest, ClosedPortHasNoReachableFamily) {
    // Nothing listens on this port, so no family completes a handshake and
    // the probe reports "no preference" instead of guessing.
    EXPECT_EQ(probe_address_family("localhost", 1), AF_UNSPEC);
}

TEST(NetDialTest, ListeningHostYieldsAWorkingFamily) {
    const LoopbackListener listener;
    ASSERT_GT(listener.port(), 0);

    const int family = probe_address_family("localhost", listener.port());
    EXPECT_TRUE(family == AF_INET || family == AF_INET6);
}

TEST(NetDialTest, ProbeAnswerIsCached) {
    const LoopbackListener listener;
    ASSERT_GT(listener.port(), 0);

    const int first = probe_address_family("localhost", listener.port());
    const int second = probe_address_family("localhost", listener.port());
    EXPECT_EQ(first, second);
}

}  // namespace
}  // namespace net
}  // namespace qcode
