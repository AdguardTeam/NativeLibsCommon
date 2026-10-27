// clang-format off
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
// clang-format on

#include <array>
#include <cstdint>
#include <iterator>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "common/cidr_range.h"
#include "common/net_utils.h"
#include "common/socket_address.h"
#include "common/utils.h"
#include "common/wfp_firewall.h"

namespace ag::test {

/** Time to wait for a connection to be established or for a message to arrive. */
static constexpr long IO_TIMEOUT_SECS = 5;

static constexpr std::string_view PING = "ping";
static constexpr std::string_view PONG = "pong";

/** The whole `127.0.0.0/8` prefix is loopback, not just `127.0.0.1`. */
static constexpr std::string_view LOOPBACK_RANGE_V4 = "127.0.0.0/8";
static constexpr std::string_view LOOPBACK_RANGE_V6 = "::1/128";

/** Number of random IPv4 loopback addresses to test in each run. */
static constexpr size_t RANDOM_LOOPBACK_ADDRESS_COUNT = 4;

/**
 * Make a set of loopback addresses the traffic to/from which must never be restricted.
 * The IPv4 addresses are chosen randomly from the whole `127.0.0.0/8` prefix (except
 * the network and broadcast addresses), so that repeated runs cover the entire prefix
 * rather than the same few addresses.
 */
static std::vector<SocketAddress> make_loopback_addresses() {
    std::random_device rd;
    std::mt19937 rng{rd()};
    std::uniform_int_distribution<uint32_t> dist{1, 0xFFFFFEu};
    std::vector<SocketAddress> addresses;
    addresses.reserve(RANDOM_LOOPBACK_ADDRESS_COUNT + 1);
    for (size_t i = 0; i < RANDOM_LOOPBACK_ADDRESS_COUNT; i++) {
        uint32_t host = dist(rng);
        Ipv4Address bytes = {
                127,
                (uint8_t) (host >> 16),
                (uint8_t) (host >> 8),
                (uint8_t) host,
        };
        addresses.emplace_back(Uint8View{bytes.data(), bytes.size()}, 0);
    }
    addresses.emplace_back("::1", 0);
    addresses.emplace_back("127.0.0.1", 0);
    return addresses;
}

/** Restrictions to enable on the firewall under test. */
enum Restrictions : uint32_t {
    R_NONE = 0,
    R_BLOCK_IPV6 = 1u << 0u,
    R_RESTRICT_DNS = 1u << 1u,
    R_BLOCK_UNTUNNELED = 1u << 2u,
    R_ALL = R_BLOCK_IPV6 | R_RESTRICT_DNS | R_BLOCK_UNTUNNELED,
};

/** An owning wrapper over a Winsock socket. */
class Socket {
public:
    Socket() = default;

    explicit Socket(SOCKET fd)
            : m_fd(fd) {
    }

    Socket(Socket &&other) noexcept
            : m_fd(std::exchange(other.m_fd, INVALID_SOCKET)) {
    }

    Socket &operator=(Socket &&other) noexcept {
        std::swap(m_fd, other.m_fd);
        return *this;
    }

    Socket(const Socket &) = delete;
    Socket &operator=(const Socket &) = delete;

    ~Socket() {
        if (m_fd != INVALID_SOCKET) {
            closesocket(m_fd);
        }
    }

    [[nodiscard]] SOCKET get() const {
        return m_fd;
    }

    [[nodiscard]] bool valid() const {
        return m_fd != INVALID_SOCKET;
    }

private:
    SOCKET m_fd = INVALID_SOCKET;
};

static testing::AssertionResult wsa_failure(const std::string &what) {
    return testing::AssertionFailure() << what << " failed with code " << WSAGetLastError();
}

/**
 * Check whether the test process has the administrative privileges required to use WFP.
 * The tests never request elevation: if the process is not already elevated, they are skipped.
 * This allows them to run in CI, where the test process is normally elevated, without prompting.
 */
static bool is_process_elevated() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        return false;
    }
    TOKEN_ELEVATION elevation{};
    DWORD size = sizeof(elevation);
    BOOL ret = GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size);
    CloseHandle(token);
    return ret && elevation.TokenIsElevated;
}

/** Create a socket bound to `addr` and report the address it has actually been bound to. */
static testing::AssertionResult make_bound_socket(
        const SocketAddress &addr, int type, int protocol, Socket &out_socket, SocketAddress &out_addr) {
    Socket s{socket(addr.c_sockaddr()->sa_family, type, protocol)};
    if (!s.valid()) {
        return wsa_failure("socket()");
    }
    if (bind(s.get(), addr.c_sockaddr(), (int) addr.c_socklen()) != 0) {
        return wsa_failure(AG_FMT("bind({})", addr.str()));
    }
    DWORD timeout_ms = IO_TIMEOUT_SECS * 1000;
    if (setsockopt(s.get(), SOL_SOCKET, SO_RCVTIMEO, (const char *) &timeout_ms, sizeof(timeout_ms)) != 0) {
        return wsa_failure("setsockopt(SO_RCVTIMEO)");
    }
    std::optional<SocketAddress> local = utils::get_local_address((evutil_socket_t) s.get());
    if (!local.has_value()) {
        return wsa_failure("getsockname()");
    }
    out_addr = *local;
    out_socket = std::move(s);
    return testing::AssertionSuccess();
}

/** Check whether `addr` can be bound, so that a test can be skipped if the port is taken. */
static bool is_bindable(const SocketAddress &addr, int type, int protocol) {
    Socket s{socket(addr.c_sockaddr()->sa_family, type, protocol)};
    return s.valid() && bind(s.get(), addr.c_sockaddr(), (int) addr.c_socklen()) == 0;
}

static testing::AssertionResult wait_readable(SOCKET fd) {
    fd_set read_set;
    FD_ZERO(&read_set);
    FD_SET(fd, &read_set);
    timeval timeout{.tv_sec = IO_TIMEOUT_SECS, .tv_usec = 0};
    int ret = select(0, &read_set, nullptr, nullptr, &timeout);
    if (ret < 0) {
        return wsa_failure("select()");
    }
    if (ret == 0) {
        return testing::AssertionFailure() << "Timed out waiting for incoming data";
    }
    return testing::AssertionSuccess();
}

/** Wait for a non-blocking `connect()` started on `fd` to complete. */
static testing::AssertionResult wait_connected(SOCKET fd) {
    fd_set write_set;
    fd_set except_set;
    FD_ZERO(&write_set);
    FD_ZERO(&except_set);
    FD_SET(fd, &write_set);
    FD_SET(fd, &except_set);
    timeval timeout{.tv_sec = IO_TIMEOUT_SECS, .tv_usec = 0};
    int ret = select(0, nullptr, &write_set, &except_set, &timeout);
    if (ret < 0) {
        return wsa_failure("select()");
    }
    if (ret == 0) {
        return testing::AssertionFailure() << "Timed out waiting for the connection to be established";
    }
    if (FD_ISSET(fd, &except_set)) {
        int so_error = 0;
        int so_error_size = sizeof(so_error);
        getsockopt(fd, SOL_SOCKET, SO_ERROR, (char *) &so_error, &so_error_size);
        return testing::AssertionFailure() << "connect() failed with code " << so_error;
    }
    return testing::AssertionSuccess();
}

static testing::AssertionResult send_and_receive(SOCKET from, SOCKET to, std::string_view data) {
    if (send(from, data.data(), (int) data.size(), 0) != (int) data.size()) {
        return wsa_failure("send()");
    }
    if (auto result = wait_readable(to); !result) {
        return result;
    }
    std::array<char, 64> buffer{}; // NOLINT(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
    int ret = recv(to, buffer.data(), (int) buffer.size(), 0);
    if (ret < 0) {
        return wsa_failure("recv()");
    }
    if (std::string_view{buffer.data(), (size_t) ret} != data) {
        return testing::AssertionFailure() << "Received unexpected data";
    }
    return testing::AssertionSuccess();
}

static testing::AssertionResult send_datagram(SOCKET fd, const SocketAddress &to, std::string_view data) {
    if (sendto(fd, data.data(), (int) data.size(), 0, to.c_sockaddr(), (int) to.c_socklen()) != (int) data.size()) {
        return wsa_failure(AG_FMT("sendto({})", to.str()));
    }
    return testing::AssertionSuccess();
}

static testing::AssertionResult receive_datagram(SOCKET fd, std::string_view expected) {
    if (auto result = wait_readable(fd); !result) {
        return result;
    }
    std::array<char, 64> buffer{}; // NOLINT(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
    int ret = recvfrom(fd, buffer.data(), (int) buffer.size(), 0, nullptr, nullptr);
    if (ret < 0) {
        return wsa_failure("recvfrom()");
    }
    if (std::string_view{buffer.data(), (size_t) ret} != expected) {
        return testing::AssertionFailure() << "Received unexpected data";
    }
    return testing::AssertionSuccess();
}

/**
 * Accept a TCP connection on `server_addr` and exchange data over it in both directions.
 * The outbound half of the exchange goes through the WFP `ALE_AUTH_CONNECT` layer,
 * and the inbound one goes through the `ALE_AUTH_RECV_ACCEPT` layer.
 */
static testing::AssertionResult check_tcp(const SocketAddress &server_addr) {
    Socket listener;
    SocketAddress listen_addr;
    if (auto result = make_bound_socket(server_addr, SOCK_STREAM, IPPROTO_TCP, listener, listen_addr); !result) {
        return result;
    }
    if (listen(listener.get(), 1) != 0) {
        return wsa_failure("listen()");
    }

    Socket client{socket(listen_addr.c_sockaddr()->sa_family, SOCK_STREAM, IPPROTO_TCP)};
    if (!client.valid()) {
        return wsa_failure("socket()");
    }
    u_long non_blocking = 1;
    if (ioctlsocket(client.get(), FIONBIO, &non_blocking) != 0) {
        return wsa_failure("ioctlsocket(FIONBIO)");
    }
    if (connect(client.get(), listen_addr.c_sockaddr(), (int) listen_addr.c_socklen()) != 0
            && WSAGetLastError() != WSAEWOULDBLOCK) {
        return wsa_failure(AG_FMT("connect({})", listen_addr.str()));
    }
    if (auto result = wait_connected(client.get()); !result) {
        return result;
    }
    non_blocking = 0;
    if (ioctlsocket(client.get(), FIONBIO, &non_blocking) != 0) {
        return wsa_failure("ioctlsocket(FIONBIO)");
    }

    if (auto result = wait_readable(listener.get()); !result) {
        return testing::AssertionFailure() << "The connection has not been accepted: " << result.message();
    }
    Socket accepted{accept(listener.get(), nullptr, nullptr)};
    if (!accepted.valid()) {
        return wsa_failure("accept()");
    }

    if (auto result = send_and_receive(client.get(), accepted.get(), PING); !result) {
        return result;
    }
    return send_and_receive(accepted.get(), client.get(), PONG);
}

/**
 * Exchange UDP datagrams with `server_addr` in both directions. Sending goes through
 * the WFP `ALE_AUTH_CONNECT` layer, and receiving goes through the `ALE_AUTH_RECV_ACCEPT` layer.
 */
static testing::AssertionResult check_udp(const SocketAddress &server_addr) {
    Socket server;
    SocketAddress bound_server_addr;
    if (auto result = make_bound_socket(server_addr, SOCK_DGRAM, IPPROTO_UDP, server, bound_server_addr); !result) {
        return result;
    }

    SocketAddress client_bind_addr = server_addr;
    client_bind_addr.set_port(0);
    Socket client;
    SocketAddress bound_client_addr;
    if (auto result = make_bound_socket(client_bind_addr, SOCK_DGRAM, IPPROTO_UDP, client, bound_client_addr);
            !result) {
        return result;
    }

    if (auto result = send_datagram(client.get(), bound_server_addr, PING); !result) {
        return result;
    }
    if (auto result = receive_datagram(server.get(), PING); !result) {
        return result;
    }
    if (auto result = send_datagram(server.get(), bound_client_addr, PONG); !result) {
        return result;
    }
    return receive_datagram(client.get(), PONG);
}

/**
 * Determine the preferred non-loopback IPv4 address of this machine, if any.
 * `connect()` on a UDP socket only selects a route, it doesn't send anything.
 */
static std::optional<SocketAddress> find_non_loopback_ipv4() {
    Socket s{socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)};
    if (!s.valid()) {
        return std::nullopt;
    }
    SocketAddress probe{"203.0.113.1", 53}; // TEST-NET-3, never actually contacted
    if (connect(s.get(), probe.c_sockaddr(), (int) probe.c_socklen()) != 0) {
        return std::nullopt;
    }
    std::optional<SocketAddress> local = utils::get_local_address((evutil_socket_t) s.get());
    if (!local.has_value() || !local->valid() || !local->is_ipv4() || local->is_loopback() || local->is_any()) {
        return std::nullopt;
    }
    return SocketAddress{local->addr(), 0};
}

/**
 * Verify that loopback traffic is never affected by the firewall restrictions.
 *
 * These tests need administrative privileges, as WFP is unavailable without them, and are
 * skipped otherwise. While a test is running, the machine's non-loopback traffic really is
 * restricted, so the firewall is torn down as soon as the test is over.
 */
class WfpFirewallLoopbackTest : public testing::TestWithParam<uint32_t> {
protected:
    void SetUp() override {
        if (!is_process_elevated()) {
            GTEST_SKIP() << "WFP requires administrative privileges; run this test elevated";
        }
        WSADATA wsa_data{};
        ASSERT_EQ(0, WSAStartup(MAKEWORD(2, 2), &wsa_data));
        m_wsa_initialized = true;
        m_non_loopback_addr = find_non_loopback_ipv4();
        ASSERT_NO_FATAL_FAILURE(start_excluded_process());
        // The excluded process is not this one, so that the test process stays restricted.
        m_firewall = std::make_unique<WfpFirewall>(L"NativeLibsCommon test", m_excluded_pid);
        ASSERT_NO_FATAL_FAILURE(check_initialized());
        ASSERT_NO_FATAL_FAILURE(apply_restrictions(GetParam()));
    }

    void TearDown() override {
        m_firewall.reset();
        if (m_excluded_process != nullptr) {
            TerminateProcess(m_excluded_process, 0);
            CloseHandle(m_excluded_process);
            m_excluded_process = nullptr;
        }
        if (m_wsa_initialized) {
            WSACleanup();
            m_wsa_initialized = false;
        }
    }

    /**
     * Start a suspended process to be used as the firewall's excluded process.
     * It must have an image path different from the test executable's: the firewall excludes
     * processes by application ID, which is derived from the image path, so a copy of the test
     * executable would also exempt the test process itself. The process is never resumed.
     */
    void start_excluded_process() {
        wchar_t system_dir[MAX_PATH]{};
        ASSERT_NE(0u, GetSystemDirectoryW(&system_dir[0], std::size(system_dir)))
                << "GetSystemDirectoryW failed with code " << GetLastError();
        std::wstring path = std::wstring{&system_dir[0]} + L"\\cmd.exe";
        STARTUPINFOW startup_info{.cb = sizeof(STARTUPINFOW)};
        PROCESS_INFORMATION process_info{};
        ASSERT_TRUE(CreateProcessW(path.c_str(), nullptr, nullptr, nullptr, FALSE, CREATE_SUSPENDED, nullptr, nullptr,
                &startup_info, &process_info))
                << "CreateProcessW failed with code " << GetLastError();
        CloseHandle(process_info.hThread);
        m_excluded_process = process_info.hProcess;
        m_excluded_pid = process_info.dwProcessId;
    }

    /**
     * The firewall constructor reports failures through the log only, so probe it with a call
     * that does nothing but fails with `FE_NOT_INITIALIZED` if the construction didn't succeed.
     */
    void check_initialized() {
        auto error = m_firewall->block_untunneled(CidrRange{""}, CidrRange{""}, {}, {}, {});
        ASSERT_FALSE(error) << "The firewall failed to initialize: " << error->str();
    }

    void apply_restrictions(uint32_t restrictions) {
        if (restrictions & R_RESTRICT_DNS) {
            // No address is allowed, so all DNS traffic except the loopback one must be blocked.
            auto error = m_firewall->restrict_dns_to({}, {});
            ASSERT_FALSE(error) << error->str();
        }
        if (restrictions & R_BLOCK_IPV6) {
            auto error = m_firewall->block_ipv6();
            ASSERT_FALSE(error) << error->str();
        }
        if (restrictions & R_BLOCK_UNTUNNELED) {
            // The loopback prefixes themselves are declared tunneled, while the tunnel addresses
            // are not assigned to this machine. Without the loopback exclusion every single
            // loopback packet would therefore be denied as untunneled.
            CidrRange incl4[] = {CidrRange{LOOPBACK_RANGE_V4}};
            CidrRange incl6[] = {CidrRange{LOOPBACK_RANGE_V6}};
            auto error = m_firewall->block_untunneled(
                    CidrRange{"10.254.0.1/32"}, CidrRange{"fd00::1/128"}, incl4, incl6, {});
            ASSERT_FALSE(error) << error->str();
        }
    }

    std::unique_ptr<WfpFirewall> m_firewall;
    std::optional<SocketAddress> m_non_loopback_addr;
    HANDLE m_excluded_process = nullptr;
    uint32_t m_excluded_pid = 0;
    bool m_wsa_initialized = false;
};

TEST_P(WfpFirewallLoopbackTest, TcpIsAllowed) {
    for (const SocketAddress &address : make_loopback_addresses()) {
        SCOPED_TRACE(address.host_str());
        EXPECT_TRUE(check_tcp(address));
    }
}

TEST_P(WfpFirewallLoopbackTest, UdpIsAllowed) {
    for (const SocketAddress &address : make_loopback_addresses()) {
        SCOPED_TRACE(address.host_str());
        EXPECT_TRUE(check_udp(address));
    }
}

TEST_P(WfpFirewallLoopbackTest, TcpDnsIsAllowed) {
    for (SocketAddress address : make_loopback_addresses()) {
        SCOPED_TRACE(address.host_str());
        address.set_port((uint16_t) utils::PLAIN_DNS_PORT_NUMBER);
        if (!is_bindable(address, SOCK_STREAM, IPPROTO_TCP)) {
            GTEST_LOG_(WARNING) << "The DNS port on " << address.host_str() << " is taken, skipping";
            continue;
        }
        EXPECT_TRUE(check_tcp(address));
    }
}

TEST_P(WfpFirewallLoopbackTest, UdpDnsIsAllowed) {
    for (SocketAddress address : make_loopback_addresses()) {
        SCOPED_TRACE(address.host_str());
        address.set_port((uint16_t) utils::PLAIN_DNS_PORT_NUMBER);
        if (!is_bindable(address, SOCK_DGRAM, IPPROTO_UDP)) {
            GTEST_LOG_(WARNING) << "The DNS port on " << address.host_str() << " is taken, skipping";
            continue;
        }
        EXPECT_TRUE(check_udp(address));
    }
}

/**
 * A sanity check for the tests above: the restrictions must really be in effect,
 * i.e. the same exchange over a non-loopback address must be blocked.
 */
TEST_P(WfpFirewallLoopbackTest, NonLoopbackDnsIsBlocked) {
    if (!(GetParam() & R_RESTRICT_DNS)) {
        GTEST_SKIP() << "DNS is not restricted in this configuration";
    }
    if (!m_non_loopback_addr.has_value()) {
        GTEST_SKIP() << "The machine has no non-loopback IPv4 address";
    }
    SocketAddress addr = *m_non_loopback_addr;
    addr.set_port((uint16_t) utils::PLAIN_DNS_PORT_NUMBER);
    if (!is_bindable(addr, SOCK_DGRAM, IPPROTO_UDP)) {
        GTEST_SKIP() << "The DNS port on " << addr.host_str() << " is taken";
    }
    EXPECT_FALSE(check_udp(addr));
}

static std::string restrictions_name(uint32_t restrictions) {
    switch (restrictions) {
    case R_NONE:
        return "NoRestrictions";
    case R_BLOCK_IPV6:
        return "BlockIpv6";
    case R_RESTRICT_DNS:
        return "RestrictDns";
    case R_BLOCK_UNTUNNELED:
        return "BlockUntunneled";
    case R_ALL:
        return "AllRestrictions";
    default:
        return AG_FMT("Restrictions{}", restrictions);
    }
}

INSTANTIATE_TEST_SUITE_P(WfpFirewall, WfpFirewallLoopbackTest,
        testing::Values(R_NONE, R_BLOCK_IPV6, R_RESTRICT_DNS, R_BLOCK_UNTUNNELED, R_ALL),
        [](const testing::TestParamInfo<uint32_t> &info) {
            return restrictions_name(info.param);
        });

} // namespace ag::test
