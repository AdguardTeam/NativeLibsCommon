#include <gtest/gtest.h>
#include <unordered_set>
#include <utility>
#include <fmt/format.h>

#include "common/socket_address.h"

using namespace ag;

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

class SocketAddressTest : public ::testing::Test {
protected:
#ifdef _WIN32
    void SetUp() override {
        WSADATA wsa_data = {};
        ASSERT_EQ(0, WSAStartup(MAKEWORD(2, 2), &wsa_data));
    }
#endif
};

TEST_F(SocketAddressTest, ConstructIPv4AndPort) {
    SocketAddress addr("1.2.3.4", 8080);
    EXPECT_TRUE(addr.valid());
    EXPECT_TRUE(addr.is_ipv4());
    EXPECT_FALSE(addr.is_ipv6());
    EXPECT_EQ(addr.port(), 8080);
    EXPECT_EQ(addr.host_str(false), "1.2.3.4");
    EXPECT_EQ(addr.str(), "1.2.3.4:8080");
    EXPECT_EQ(addr.c_socklen(), sizeof(sockaddr_in));
}

TEST_F(SocketAddressTest, ConstructIPv6AndPort) {
    SocketAddress addr("::1", 443);
    EXPECT_TRUE(addr.valid());
    EXPECT_TRUE(addr.is_ipv6());
    EXPECT_EQ(addr.port(), 443);
    EXPECT_EQ(addr.host_str(false), "::1");
    EXPECT_EQ(addr.host_str(true), "[::1]");
    EXPECT_EQ(addr.str(), "[::1]:443");
    EXPECT_EQ(addr.c_socklen(), sizeof(sockaddr_in6));
}

TEST_F(SocketAddressTest, ConstructFromHostPortString) {
    SocketAddress addr("1.2.3.4:65535");
    EXPECT_TRUE(addr.valid());
    EXPECT_EQ(addr.port(), 65535);
    EXPECT_EQ(addr.str(), "1.2.3.4:65535");

    addr = SocketAddress("[::1]:53");
    EXPECT_TRUE(addr.valid());
    EXPECT_EQ(addr.port(), 53);
    EXPECT_EQ(addr.str(), "[::1]:53");

    SocketAddress invalid("not_an_ip");
    EXPECT_FALSE(invalid.valid());
}

TEST_F(SocketAddressTest, IPv4MappedBehavior) {
    SocketAddress addr("::ffff:192.0.2.1", 53);
    EXPECT_TRUE(addr.valid());
    EXPECT_TRUE(addr.is_ipv6());
    EXPECT_TRUE(addr.is_ipv4_mapped());
    EXPECT_TRUE(addr.is_ipv4());

    addr = addr.socket_family_cast(AF_INET);
    EXPECT_TRUE(addr.valid());
    EXPECT_TRUE(addr.is_ipv4());
    EXPECT_EQ(addr.host_str(false), "192.0.2.1");
    EXPECT_EQ(addr.port(), 53);

    addr = addr.socket_family_cast(AF_INET6);
    EXPECT_TRUE(addr.valid());
    EXPECT_TRUE(addr.is_ipv4_mapped());
    EXPECT_EQ(addr.str(), addr.str());
}

TEST_F(SocketAddressTest, FamilyCastInvalidWhenPureIPv6ToIPv4) {
    SocketAddress addr("2001:db8::1", 80);
    addr = addr.socket_family_cast(AF_INET);
    EXPECT_FALSE(addr.valid());
}

TEST_F(SocketAddressTest, LoopbackAndAny) {
    SocketAddress lo4("127.0.0.1", 0);
    EXPECT_TRUE(lo4.is_loopback());
    EXPECT_FALSE(lo4.is_any());

    SocketAddress any4("0.0.0.0", 0);
    EXPECT_TRUE(any4.is_any());
    EXPECT_FALSE(any4.is_loopback());

    SocketAddress lo6("::1", 0);
    EXPECT_TRUE(lo6.is_loopback());
    EXPECT_FALSE(lo6.is_any());

    SocketAddress any6("::", 0);
    EXPECT_TRUE(any6.is_any());
    EXPECT_FALSE(any6.is_loopback());

    SocketAddress mapped_lo("::ffff:127.0.0.1", 0);
    EXPECT_TRUE(mapped_lo.is_ipv4_mapped());
    EXPECT_TRUE(mapped_lo.is_loopback());
}

TEST_F(SocketAddressTest, SetPort) {
    SocketAddress addr("1.2.3.4", 80);
    addr.set_port(5353);
    EXPECT_EQ(addr.port(), 5353);

    addr = SocketAddress("::1", 80);
    addr.set_port(443);
    EXPECT_EQ(addr.port(), 443);
}

TEST_F(SocketAddressTest, EqualityAndHashConsistency) {
    SocketAddress addr1("1.2.3.4", 8080);
    SocketAddress addr2("1.2.3.4:8080");
    EXPECT_TRUE(addr1 == addr2);
    EXPECT_FALSE(addr1 != addr2);

    size_t h1 = std::hash<SocketAddress>{}(addr1);
    size_t h2 = std::hash<SocketAddress>{}(addr2);
    EXPECT_EQ(h1, h2);

    std::unordered_set<SocketAddress> set;
    set.insert(addr1);
    EXPECT_EQ(set.count(addr2), 1u);
}

TEST_F(SocketAddressTest, OrderingSanity) {
    SocketAddress a("1.2.3.4", 1);
    SocketAddress b("1.2.3.4", 1);
    SocketAddress c("1.2.3.5", 1);

    EXPECT_FALSE(a < b);
    EXPECT_FALSE(b < a);
    if (a < c) {
        EXPECT_FALSE(c < a);
    } else if (c < a) {
        EXPECT_FALSE(a < c);
    }
}

TEST_F(SocketAddressTest, FmtFormatAsWorks) {
    SocketAddress addr("10.0.0.1", 1234);
    EXPECT_EQ(AG_FMT("{}", addr), addr.str());
    EXPECT_EQ(AG_FMT("peer={}", addr), "peer=" + addr.str());
}

TEST_F(SocketAddressTest, IPv4) {
    const std::vector<uint8_t> ip4{192, 0, 2, 1};
    SocketAddress addr({ip4.data(), ip4.size()}, 5353);

    EXPECT_TRUE(addr.valid());
    EXPECT_TRUE(addr.is_ipv4());
    EXPECT_FALSE(addr.is_ipv6());
    EXPECT_EQ(addr.port(), 5353);
    EXPECT_EQ(addr.host_str(false), "192.0.2.1");
    EXPECT_EQ(addr.c_socklen(), sizeof(sockaddr_in));
}

TEST_F(SocketAddressTest, IPv4_Bytes) {
    const std::vector<uint8_t> ip4{0x5D, 0xB8, 0xD8, 0x22};
    SocketAddress addr({ip4.data(), ip4.size()}, 0);

    EXPECT_TRUE(addr.valid());
    EXPECT_TRUE(addr.is_ipv4());
    EXPECT_FALSE(addr.is_ipv6());
    EXPECT_EQ(addr.str(), "93.184.216.34:0");
}

TEST_F(SocketAddressTest, IPv6) {
    std::array<uint8_t, 16> ip6{};
    ip6[15] = 1;
    SocketAddress addr({ip6}, 443);

    EXPECT_TRUE(addr.valid());
    EXPECT_TRUE(addr.is_ipv6());
    EXPECT_FALSE(addr.is_ipv4());
    EXPECT_FALSE(addr.is_ipv4_mapped());
    EXPECT_EQ(addr.port(), 443);
    EXPECT_EQ(addr.host_str(false), "::1");
    EXPECT_EQ(addr.host_str(true), "[::1]");
    EXPECT_EQ(addr.str(), "[::1]:443");
    EXPECT_EQ(addr.c_socklen(), sizeof(sockaddr_in6));
}

TEST_F(SocketAddressTest, IPv6_Bytes) {
    // 2606:2800:220:1:248:1893:25c8:1946
    const std::vector<uint8_t> ip6 = {
            0x26,0x06,0x28,0x00,0x02,0x20,0x00,0x01,0x02,0x48,0x18,0x93,0x25,0xC8,0x19,0x46
    };
    SocketAddress addr({ip6.data(), ip6.size()}, 0);

    EXPECT_TRUE(addr.valid());
    EXPECT_TRUE(addr.is_ipv6());
    EXPECT_FALSE(addr.is_ipv4());
    EXPECT_FALSE(addr.is_ipv4_mapped());
    ASSERT_EQ(addr.str(), "[2606:2800:220:1:248:1893:25c8:1946]:0");
}

TEST_F(SocketAddressTest, IPv4MappedInIPv6) {
    // ::ffff:198.51.100.5
    std::array<uint8_t, 16> ip6_mapped{};
    // префикс ::ffff:0:0:
    ip6_mapped[10] = 0xff;
    ip6_mapped[11] = 0xff;
    // хвост — IPv4
    ip6_mapped[12] = 198;
    ip6_mapped[13] = 51;
    ip6_mapped[14] = 100;
    ip6_mapped[15] = 5;

    SocketAddress m({ip6_mapped}, 8080);

    EXPECT_TRUE(m.valid());
    EXPECT_TRUE(m.is_ipv6());
    EXPECT_TRUE(m.is_ipv4_mapped());
    EXPECT_TRUE(m.is_ipv4());

    SocketAddress v4 = m.socket_family_cast(AF_INET);
    EXPECT_TRUE(v4.valid());
    EXPECT_TRUE(v4.is_ipv4());
    EXPECT_EQ(v4.host_str(false), "198.51.100.5");
    EXPECT_EQ(v4.port(), 8080);

    SocketAddress back = v4.socket_family_cast(AF_INET6);
    EXPECT_TRUE(back.valid());
    EXPECT_TRUE(back.is_ipv4_mapped());
    EXPECT_EQ(back.port(), 8080);
}

TEST_F(SocketAddressTest, InvalidSizeYieldsInvalidAddress) {
    std::array<uint8_t, 15> bad{};
    SocketAddress a(ag::Uint8View{bad.data(), bad.size()}, 1);
    EXPECT_FALSE(a.valid());

    SocketAddress b(ag::Uint8View{nullptr, 0}, 0);
    EXPECT_FALSE(b.valid());
}

TEST_F(SocketAddressTest, HashAndEqualityConsistency) {
    std::array<uint8_t, 4> ip4{203, 0, 113, 7};
    SocketAddress a1({ip4}, 12345);
    SocketAddress a2("203.0.113.7", 12345);

    ASSERT_TRUE(a1.valid());
    ASSERT_TRUE(a2.valid());
    EXPECT_TRUE(a1 == a2);

    size_t h1 = std::hash<SocketAddress>{}(a1);
    size_t h2 = std::hash<SocketAddress>{}(a2);
    EXPECT_EQ(h1, h2);

    std::unordered_set<SocketAddress> set;
    set.insert(a1);
    EXPECT_EQ(set.count(a2), 1u);
}

