#include <functional>
#include <memory>
#include <string>
#include <utility>

#include <gtest/gtest.h>

#include "common/defs.h"
#include "../network_monitor_impl.h"

#ifdef __linux__
#include <linux/if_link.h>
#include <linux/rtnetlink.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

using namespace ag;

class SimulateStatusNetworkMonitor : public utils::NetworkMonitorImpl {
public:
    explicit SimulateStatusNetworkMonitor(
            std::function<void(const std::string &if_name, bool is_connected)> cmd_handler)
            : NetworkMonitorImpl(std::move(cmd_handler)) {}

    void simulate_status_change(const std::string &new_if_name, bool is_satisfied) {
        handle_network_change(new_if_name, is_satisfied);
    }
};

class NetworkMonitorTest : public testing::Test {
protected:
    std::unique_ptr<SimulateStatusNetworkMonitor> m_monitor;
    std::string m_bound_if;
    bool m_is_connected = false;

#ifdef __linux__
    UniquePtr<event_base, &event_base_free> m_ev_base;
#endif

    void SetUp() override {
        m_monitor = std::make_unique<SimulateStatusNetworkMonitor>(
                [this](const std::string &if_name, bool is_connected) {
            m_is_connected = is_connected;
            m_bound_if = if_name;
        });

#ifdef __linux__
        m_ev_base.reset(event_base_new());
#endif
    }

    void TearDown() override {
        m_bound_if.clear();
        m_is_connected = false;
#ifdef __linux__
        if (m_ev_base) {
            m_ev_base.reset();
        }
#endif
    }

    void start_monitor() {
#ifdef __linux__
        m_monitor->start(m_ev_base.get());
#elif defined(__APPLE__) || defined(_WIN32)
        m_monitor->start(nullptr);
#endif
    }
};


TEST_F(NetworkMonitorTest, GetDefaultInterface) {
    std::string if_name = m_monitor->get_default_interface();
    ASSERT_FALSE(if_name.empty());
}

// NetworkMonitorImpl doesn't support start/stop on Windows
#ifndef _WIN32
TEST_F(NetworkMonitorTest, StartAndStopMonitor) {
    start_monitor();
    ASSERT_TRUE(m_monitor->is_running());
    m_monitor->stop();
    ASSERT_FALSE(m_monitor->is_running());
}
#endif // _WIN32

TEST_F(NetworkMonitorTest, GetDefaultInterfaceIfStart) {
    std::string if_name = m_monitor->get_default_interface();

    start_monitor();
    std::string tmp = m_monitor->get_default_interface();
    // Interface name should be consistent before and after start
    ASSERT_EQ(tmp, if_name);
    m_monitor->stop();
}

TEST_F(NetworkMonitorTest, NetworkStatusChange) {
    std::string if_name = m_monitor->get_default_interface();
    std::string if_name_test_1 = "eth_tmp_1";
    std::string if_name_test_2 = "eth_tmp_2";

    start_monitor();

    m_monitor->simulate_status_change(if_name_test_1, true);
    ASSERT_TRUE(m_is_connected);
    ASSERT_EQ(m_bound_if, if_name_test_1);

    m_monitor->simulate_status_change(if_name_test_2, false);
    ASSERT_FALSE(m_is_connected);
    ASSERT_TRUE(m_bound_if.empty());

    m_monitor->simulate_status_change(if_name, true);
    ASSERT_TRUE(m_is_connected);
    ASSERT_EQ(m_bound_if, if_name);

    m_monitor->stop();
}

#ifdef __linux__
TEST(RouteEntryTest, Sorting) {
    utils::RouteEntry default_route(CidrRange({0, 0, 0, 0}, 0));
    default_route.metric = 100;

    utils::RouteEntry specific_route(CidrRange({192, 168, 1, 0}, 24));
    specific_route.metric = 200;

    utils::RouteEntry default_route_low_metric(CidrRange({0, 0, 0, 0}, 0));
    default_route_low_metric.metric = 50;

    std::vector<utils::RouteEntry> routes;
    routes.push_back(std::move(default_route));
    routes.push_back(std::move(specific_route));
    routes.push_back(std::move(default_route_low_metric));

    std::sort(routes.begin(), routes.end());

    ASSERT_EQ(routes[0].prefix.get_prefix_len(), 24);
    ASSERT_EQ(routes[1].prefix.get_prefix_len(), 0);
    ASSERT_EQ(routes[1].metric, 50);
    ASSERT_EQ(routes[2].prefix.get_prefix_len(), 0);
    ASSERT_EQ(routes[2].metric, 100);
}

TEST(RouteEntryTest, IsDefaultRoute) {
    utils::RouteEntry default_v4(CidrRange({0, 0, 0, 0}, 0));
    ASSERT_TRUE(default_v4.is_default_route());

    utils::RouteEntry specific(CidrRange({10, 0, 0, 0}, 8));
    ASSERT_FALSE(specific.is_default_route());
}

TEST_F(NetworkMonitorTest, NetlinkRoutingTableLoaded) {
    start_monitor();
    ASSERT_TRUE(m_monitor->is_running());

    std::string if_name = m_monitor->get_default_interface();
    ASSERT_FALSE(if_name.empty());

    m_monitor->stop();
}

TEST_F(NetworkMonitorTest, FallbackToIpRoute) {
    // Without calling start(), m_netlink_available remains false
    // This forces get_default_interface() to use ip route fallback
    ASSERT_FALSE(m_monitor->is_running());

    std::string if_name = m_monitor->get_default_interface();
    ASSERT_FALSE(if_name.empty());

    // Verify consistency: fallback result should match Netlink result
    start_monitor();
    std::string netlink_if_name = m_monitor->get_default_interface();
    ASSERT_EQ(if_name, netlink_if_name);

    m_monitor->stop();
}

class LinuxRoutingTableTest : public testing::Test {
protected:
    utils::LinuxRoutingTable m_table;

    // Helper to create mock nlmsghdr with route data
    struct MockRouteMsg {
        nlmsghdr nlh;
        rtmsg rtm;
        char attrs[256];

        MockRouteMsg(uint8_t family, uint8_t dst_len, uint8_t table, uint8_t type) {
            memset(this, 0, sizeof(*this));
            nlh.nlmsg_len = NLMSG_LENGTH(sizeof(rtmsg));
            nlh.nlmsg_type = RTM_NEWROUTE;
            rtm.rtm_family = family;
            rtm.rtm_dst_len = dst_len;
            rtm.rtm_table = table;
            rtm.rtm_type = type;
            rtm.rtm_protocol = RTPROT_BOOT;
            rtm.rtm_scope = RT_SCOPE_UNIVERSE;
        }

        void add_attr_u32(uint16_t type, uint32_t value) {
            auto *rta = reinterpret_cast<rtattr *>(
                    reinterpret_cast<char *>(&rtm) + NLMSG_ALIGN(sizeof(rtmsg))
                    + nlh.nlmsg_len - NLMSG_LENGTH(sizeof(rtmsg)));
            rta->rta_type = type;
            rta->rta_len = RTA_LENGTH(sizeof(uint32_t));
            memcpy(RTA_DATA(rta), &value, sizeof(value));
            nlh.nlmsg_len += RTA_ALIGN(rta->rta_len);
        }

        void add_attr_addr(uint16_t type, const std::vector<uint8_t> &addr) {
            auto *rta = reinterpret_cast<rtattr *>(
                    reinterpret_cast<char *>(&rtm) + NLMSG_ALIGN(sizeof(rtmsg))
                    + nlh.nlmsg_len - NLMSG_LENGTH(sizeof(rtmsg)));
            rta->rta_type = type;
            rta->rta_len = RTA_LENGTH(addr.size());
            memcpy(RTA_DATA(rta), addr.data(), addr.size());
            nlh.nlmsg_len += RTA_ALIGN(rta->rta_len);
        }
    };
};

TEST_F(LinuxRoutingTableTest, HandleNewRouteIPv4) {
    MockRouteMsg msg(AF_INET, 24, RT_TABLE_MAIN, RTN_UNICAST);
    msg.add_attr_addr(RTA_DST, {192, 168, 1, 0});
    msg.add_attr_u32(RTA_OIF, 2);
    msg.add_attr_u32(RTA_PRIORITY, 100);

    m_table.handle_new_route(&msg.nlh);

    ASSERT_EQ(m_table.get_routes_v4().size(), 1);
    ASSERT_EQ(m_table.get_routes_v4()[0].prefix.get_prefix_len(), 24);
    ASSERT_EQ(m_table.get_routes_v4()[0].if_index, 2);
    ASSERT_EQ(m_table.get_routes_v4()[0].metric, 100);
}

TEST_F(LinuxRoutingTableTest, HandleNewRouteDefaultIPv4) {
    MockRouteMsg msg(AF_INET, 0, RT_TABLE_MAIN, RTN_UNICAST);
    msg.add_attr_u32(RTA_OIF, 3);
    msg.add_attr_u32(RTA_PRIORITY, 50);

    m_table.handle_new_route(&msg.nlh);

    ASSERT_EQ(m_table.get_routes_v4().size(), 1);
    ASSERT_TRUE(m_table.get_routes_v4()[0].is_default_route());
    ASSERT_EQ(m_table.get_default_if_index(), 3);
}

TEST_F(LinuxRoutingTableTest, HandleDelRoute) {
    // Add route first
    MockRouteMsg add_msg(AF_INET, 24, RT_TABLE_MAIN, RTN_UNICAST);
    add_msg.add_attr_addr(RTA_DST, {10, 0, 0, 0});
    add_msg.add_attr_u32(RTA_OIF, 5);
    m_table.handle_new_route(&add_msg.nlh);
    ASSERT_EQ(m_table.get_routes_v4().size(), 1);

    // Delete route
    MockRouteMsg del_msg(AF_INET, 24, RT_TABLE_MAIN, RTN_UNICAST);
    del_msg.add_attr_addr(RTA_DST, {10, 0, 0, 0});
    del_msg.add_attr_u32(RTA_OIF, 5);
    del_msg.nlh.nlmsg_type = RTM_DELROUTE;
    m_table.handle_del_route(&del_msg.nlh);

    ASSERT_EQ(m_table.get_routes_v4().size(), 0);
}

TEST_F(LinuxRoutingTableTest, HasDefaultChangedAndReset) {
    // Initially no change
    ASSERT_FALSE(m_table.has_default_changed_and_reset());

    // Add default route - should trigger change
    MockRouteMsg msg(AF_INET, 0, RT_TABLE_MAIN, RTN_UNICAST);
    msg.add_attr_u32(RTA_OIF, 2);
    m_table.handle_new_route(&msg.nlh);

    ASSERT_TRUE(m_table.has_default_changed_and_reset());
    // After reset, should be false
    ASSERT_FALSE(m_table.has_default_changed_and_reset());
}

TEST_F(LinuxRoutingTableTest, DefaultRouteSelection) {
    // Add two default routes with different metrics
    MockRouteMsg msg1(AF_INET, 0, RT_TABLE_MAIN, RTN_UNICAST);
    msg1.add_attr_u32(RTA_OIF, 10);
    msg1.add_attr_u32(RTA_PRIORITY, 200);
    m_table.handle_new_route(&msg1.nlh);

    MockRouteMsg msg2(AF_INET, 0, RT_TABLE_MAIN, RTN_UNICAST);
    msg2.add_attr_u32(RTA_OIF, 20);
    msg2.add_attr_u32(RTA_PRIORITY, 100);
    m_table.handle_new_route(&msg2.nlh);

    // Should select route with lower metric (if_index=20, metric=100)
    ASSERT_EQ(m_table.get_default_if_index(), 20);
}

TEST_F(LinuxRoutingTableTest, FilterNonUnicast) {
    // Add non-unicast route (should be filtered)
    MockRouteMsg msg(AF_INET, 24, RT_TABLE_MAIN, RTN_BROADCAST);
    msg.add_attr_addr(RTA_DST, {192, 168, 1, 0});
    msg.add_attr_u32(RTA_OIF, 2);
    m_table.handle_new_route(&msg.nlh);

    ASSERT_EQ(m_table.get_routes_v4().size(), 0);
}

TEST_F(LinuxRoutingTableTest, FilterNonMainTable) {
    // Add route from non-main table (should be filtered)
    MockRouteMsg msg(AF_INET, 24, RT_TABLE_LOCAL, RTN_UNICAST);
    msg.add_attr_addr(RTA_DST, {192, 168, 1, 0});
    msg.add_attr_u32(RTA_OIF, 2);
    m_table.handle_new_route(&msg.nlh);

    ASSERT_EQ(m_table.get_routes_v4().size(), 0);
}

TEST_F(LinuxRoutingTableTest, IPv6DefaultRoute) {
    MockRouteMsg msg(AF_INET6, 0, RT_TABLE_MAIN, RTN_UNICAST);
    msg.add_attr_u32(RTA_OIF, 7);
    msg.add_attr_u32(RTA_PRIORITY, 100);
    m_table.handle_new_route(&msg.nlh);

    ASSERT_EQ(m_table.get_routes_v6().size(), 1);
    ASSERT_TRUE(m_table.get_routes_v6()[0].is_default_route());
    ASSERT_EQ(m_table.get_routes_v6()[0].if_index, 7);
}

TEST_F(LinuxRoutingTableTest, MultipleInterfaces) {
    // Add routes for different interfaces
    MockRouteMsg msg1(AF_INET, 24, RT_TABLE_MAIN, RTN_UNICAST);
    msg1.add_attr_addr(RTA_DST, {192, 168, 1, 0});
    msg1.add_attr_u32(RTA_OIF, 2);
    m_table.handle_new_route(&msg1.nlh);

    MockRouteMsg msg2(AF_INET, 16, RT_TABLE_MAIN, RTN_UNICAST);
    msg2.add_attr_addr(RTA_DST, {10, 0, 0, 0});
    msg2.add_attr_u32(RTA_OIF, 3);
    m_table.handle_new_route(&msg2.nlh);

    MockRouteMsg msg3(AF_INET, 0, RT_TABLE_MAIN, RTN_UNICAST);
    msg3.add_attr_u32(RTA_OIF, 4);
    msg3.add_attr_u32(RTA_PRIORITY, 100);
    m_table.handle_new_route(&msg3.nlh);

    ASSERT_EQ(m_table.get_routes_v4().size(), 3);
    // Routes should be sorted: /24 first, then /16, then /0
    ASSERT_EQ(m_table.get_routes_v4()[0].prefix.get_prefix_len(), 24);
    ASSERT_EQ(m_table.get_routes_v4()[1].prefix.get_prefix_len(), 16);
    ASSERT_EQ(m_table.get_routes_v4()[2].prefix.get_prefix_len(), 0);
}

TEST_F(LinuxRoutingTableTest, MalformedMessageTooShortDstAttr) {
    MockRouteMsg msg(AF_INET, 24, RT_TABLE_MAIN, RTN_UNICAST);
    // Manually create malformed RTA_DST with incorrect size
    auto *rta = reinterpret_cast<rtattr *>(
            reinterpret_cast<char *>(&msg.rtm) + NLMSG_ALIGN(sizeof(rtmsg)));
    rta->rta_type = RTA_DST;
    rta->rta_len = RTA_LENGTH(2); // Only 2 bytes instead of 4 for IPv4
    msg.nlh.nlmsg_len += RTA_ALIGN(rta->rta_len);

    m_table.handle_new_route(&msg.nlh);

    // Route is created with dst_addr filled with zeros (0.0.0.0/24)
    // This is acceptable behavior - malformed dst is treated as 0.0.0.0
    ASSERT_EQ(m_table.get_routes_v4().size(), 1);
    ASSERT_EQ(m_table.get_routes_v4()[0].prefix.get_prefix_len(), 24);
}

TEST_F(LinuxRoutingTableTest, MalformedMessageTooShortOifAttr) {
    MockRouteMsg msg(AF_INET, 0, RT_TABLE_MAIN, RTN_UNICAST);
    // Manually create malformed RTA_OIF with incorrect size
    auto *rta = reinterpret_cast<rtattr *>(
            reinterpret_cast<char *>(&msg.rtm) + NLMSG_ALIGN(sizeof(rtmsg)));
    rta->rta_type = RTA_OIF;
    rta->rta_len = RTA_LENGTH(2); // Only 2 bytes instead of 4
    msg.nlh.nlmsg_len += RTA_ALIGN(rta->rta_len);

    m_table.handle_new_route(&msg.nlh);

    // Route should be added but if_index should be 0 (not read)
    ASSERT_EQ(m_table.get_routes_v4().size(), 1);
    ASSERT_EQ(m_table.get_routes_v4()[0].if_index, 0);
}

TEST_F(LinuxRoutingTableTest, InvalidPrefixIgnored) {
    MockRouteMsg msg(AF_INET, 33, RT_TABLE_MAIN, RTN_UNICAST); // Invalid prefix_len > 32
    msg.add_attr_addr(RTA_DST, {192, 168, 1, 0});
    msg.add_attr_u32(RTA_OIF, 2);

    m_table.handle_new_route(&msg.nlh);

    // Should be ignored due to invalid prefix
    ASSERT_EQ(m_table.get_routes_v4().size(), 0);
}

/**
 * Tests for get_interface_kind() using socketpair to inject mock Netlink responses.
 *
 * socketpair(AF_UNIX, SOCK_DGRAM) creates two connected sockets:
 *   write(fds[0], data) → recv(fds[1]) reads that data
 *   send(fds[1], data)  → recv(fds[0]) reads that data
 *
 * We pre-load a crafted RTM_NEWLINK response into fds[0], then call
 * get_interface_kind(fds[1], ...). Inside, send() writes the request
 * to fds[0]'s buffer (ignored), and recv() reads our mock response.
 */
class GetInterfaceKindTest : public testing::Test {
protected:
    int m_fds[2] = {-1, -1};

    void SetUp() override {
        ASSERT_EQ(socketpair(AF_UNIX, SOCK_DGRAM, 0, m_fds), 0);
    }

    void TearDown() override {
        if (m_fds[0] >= 0) close(m_fds[0]);
        if (m_fds[1] >= 0) close(m_fds[1]);
    }

    // Helper to construct a mock RTM_NEWLINK Netlink response
    struct MockLinkMsg {
        nlmsghdr nlh;
        ifinfomsg ifm;
        char attrs[256];

        explicit MockLinkMsg(int if_index) {
            memset(this, 0, sizeof(*this));
            nlh.nlmsg_len = NLMSG_LENGTH(sizeof(ifinfomsg));
            nlh.nlmsg_type = RTM_NEWLINK;
            nlh.nlmsg_seq = 1;
            ifm.ifi_family = AF_UNSPEC;
            ifm.ifi_index = if_index;
        }

        // Add IFLA_LINKINFO { IFLA_INFO_KIND = kind_data } nested attribute
        void add_link_info_kind(const void *kind_data, size_t kind_len) {
            // Build inner IFLA_INFO_KIND attribute
            char nested_buf[128];
            memset(nested_buf, 0, sizeof(nested_buf));
            auto *kind_rta = reinterpret_cast<rtattr *>(nested_buf);
            kind_rta->rta_type = IFLA_INFO_KIND;
            kind_rta->rta_len = RTA_LENGTH(kind_len);
            memcpy(RTA_DATA(kind_rta), kind_data, kind_len);
            size_t nested_total = RTA_ALIGN(kind_rta->rta_len);

            // Build outer IFLA_LINKINFO wrapping the nested attribute
            char *attr_pos = reinterpret_cast<char *>(&ifm)
                    + NLMSG_ALIGN(sizeof(ifinfomsg))
                    + nlh.nlmsg_len - NLMSG_LENGTH(sizeof(ifinfomsg));
            auto *linkinfo_rta = reinterpret_cast<rtattr *>(attr_pos);
            linkinfo_rta->rta_type = IFLA_LINKINFO;
            linkinfo_rta->rta_len = RTA_LENGTH(nested_total);
            memcpy(RTA_DATA(linkinfo_rta), nested_buf, nested_total);
            nlh.nlmsg_len += RTA_ALIGN(linkinfo_rta->rta_len);
        }
    };

    void send_mock_response(const MockLinkMsg &msg) {
        ssize_t written = write(m_fds[0], &msg, msg.nlh.nlmsg_len);
        ASSERT_GT(written, 0);
    }

    std::string call_get_interface_kind(uint32_t if_index) {
        return utils::LinuxRoutingTable::get_interface_kind(m_fds[1], if_index);
    }
};

TEST_F(GetInterfaceKindTest, KindWithTrailingNull) {
    MockLinkMsg msg(42);
    msg.add_link_info_kind("tun", 4); // "tun\0" — 4 bytes including null terminator
    send_mock_response(msg);

    std::string kind = call_get_interface_kind(42);
    ASSERT_EQ(kind, "tun");
    ASSERT_EQ(kind.length(), 3);
}

TEST_F(GetInterfaceKindTest, KindWireguard) {
    MockLinkMsg msg(10);
    const char wg[] = "wireguard";
    msg.add_link_info_kind(wg, sizeof(wg)); // includes trailing \0
    send_mock_response(msg);

    std::string kind = call_get_interface_kind(10);
    ASSERT_EQ(kind, "wireguard");
}

TEST_F(GetInterfaceKindTest, NoLinkInfoAttribute) {
    MockLinkMsg msg(42);
    // No IFLA_LINKINFO added — e.g. physical interface like eth0
    send_mock_response(msg);

    std::string kind = call_get_interface_kind(42);
    ASSERT_TRUE(kind.empty());
}

TEST_F(GetInterfaceKindTest, InvalidFd) {
    std::string kind = utils::LinuxRoutingTable::get_interface_kind(-1, 42);
    ASSERT_TRUE(kind.empty());
}

TEST_F(GetInterfaceKindTest, ErrorResponse) {
    // Construct NLMSG_ERROR response
    struct {
        nlmsghdr nlh;
        nlmsgerr err;
    } err_msg{};
    err_msg.nlh.nlmsg_len = NLMSG_LENGTH(sizeof(nlmsgerr));
    err_msg.nlh.nlmsg_type = NLMSG_ERROR;
    err_msg.nlh.nlmsg_seq = 1;
    err_msg.err.error = -ENODEV;

    ssize_t written = write(m_fds[0], &err_msg, err_msg.nlh.nlmsg_len);
    ASSERT_GT(written, 0);

    std::string kind = call_get_interface_kind(99);
    ASSERT_TRUE(kind.empty());
}

/**
 * Tests that use socketpair to mock m_query_fd for reload() and TUN filtering.
 *
 * For reload(): we pre-load a mock RTM_GETROUTE dump (multiple RTM_NEWROUTE
 * messages + NLMSG_DONE) into the socketpair. reload() sends the request
 * (ignored), then reads our mock dump via recv().
 *
 * For TUN filtering: after adding routes via handle_new_route(), we enable
 * ignore_tun and set m_query_fd to a socketpair with a pre-loaded
 * RTM_NEWLINK response containing IFLA_INFO_KIND="tun".
 * Re-evaluation of default route then skips the TUN interface.
 */
class LinuxRoutingTableMockFdTest : public testing::Test {
protected:
    utils::LinuxRoutingTable m_table;
    int m_fds[2] = {-1, -1};

    void SetUp() override {
        ASSERT_EQ(socketpair(AF_UNIX, SOCK_DGRAM, 0, m_fds), 0);
    }

    void TearDown() override {
        m_table.set_query_fd(-1);
        if (m_fds[0] >= 0) close(m_fds[0]);
        if (m_fds[1] >= 0) close(m_fds[1]);
    }

    struct MockRouteMsg {
        nlmsghdr nlh;
        rtmsg rtm;
        char attrs[256];

        MockRouteMsg(uint8_t family, uint8_t dst_len, uint8_t table, uint8_t type) {
            memset(this, 0, sizeof(*this));
            nlh.nlmsg_len = NLMSG_LENGTH(sizeof(rtmsg));
            nlh.nlmsg_type = RTM_NEWROUTE;
            rtm.rtm_family = family;
            rtm.rtm_dst_len = dst_len;
            rtm.rtm_table = table;
            rtm.rtm_type = type;
            rtm.rtm_protocol = RTPROT_BOOT;
            rtm.rtm_scope = RT_SCOPE_UNIVERSE;
        }

        void add_attr_u32(uint16_t type, uint32_t value) {
            auto *rta = reinterpret_cast<rtattr *>(
                    reinterpret_cast<char *>(&rtm) + NLMSG_ALIGN(sizeof(rtmsg))
                    + nlh.nlmsg_len - NLMSG_LENGTH(sizeof(rtmsg)));
            rta->rta_type = type;
            rta->rta_len = RTA_LENGTH(sizeof(uint32_t));
            memcpy(RTA_DATA(rta), &value, sizeof(value));
            nlh.nlmsg_len += RTA_ALIGN(rta->rta_len);
        }

        void add_attr_addr(uint16_t type, const std::vector<uint8_t> &addr) {
            auto *rta = reinterpret_cast<rtattr *>(
                    reinterpret_cast<char *>(&rtm) + NLMSG_ALIGN(sizeof(rtmsg))
                    + nlh.nlmsg_len - NLMSG_LENGTH(sizeof(rtmsg)));
            rta->rta_type = type;
            rta->rta_len = RTA_LENGTH(addr.size());
            memcpy(RTA_DATA(rta), addr.data(), addr.size());
            nlh.nlmsg_len += RTA_ALIGN(rta->rta_len);
        }
    };

    struct MockLinkMsg {
        nlmsghdr nlh;
        ifinfomsg ifm;
        char attrs[256];

        explicit MockLinkMsg(int if_index) {
            memset(this, 0, sizeof(*this));
            nlh.nlmsg_len = NLMSG_LENGTH(sizeof(ifinfomsg));
            nlh.nlmsg_type = RTM_NEWLINK;
            nlh.nlmsg_seq = 1;
            ifm.ifi_family = AF_UNSPEC;
            ifm.ifi_index = if_index;
        }

        void add_link_info_kind(const void *kind_data, size_t kind_len) {
            char nested_buf[128];
            memset(nested_buf, 0, sizeof(nested_buf));
            auto *kind_rta = reinterpret_cast<rtattr *>(nested_buf);
            kind_rta->rta_type = IFLA_INFO_KIND;
            kind_rta->rta_len = RTA_LENGTH(kind_len);
            memcpy(RTA_DATA(kind_rta), kind_data, kind_len);
            size_t nested_total = RTA_ALIGN(kind_rta->rta_len);

            char *attr_pos = reinterpret_cast<char *>(&ifm)
                    + NLMSG_ALIGN(sizeof(ifinfomsg))
                    + nlh.nlmsg_len - NLMSG_LENGTH(sizeof(ifinfomsg));
            auto *linkinfo_rta = reinterpret_cast<rtattr *>(attr_pos);
            linkinfo_rta->rta_type = IFLA_LINKINFO;
            linkinfo_rta->rta_len = RTA_LENGTH(nested_total);
            memcpy(RTA_DATA(linkinfo_rta), nested_buf, nested_total);
            nlh.nlmsg_len += RTA_ALIGN(linkinfo_rta->rta_len);
        }
    };

    // Pack multiple Netlink messages into a single buffer and write as one datagram
    void write_dump(const std::vector<std::pair<const void *, size_t>> &msgs) {
        char buf[4096];
        size_t offset = 0;
        for (const auto &[data, len] : msgs) {
            memcpy(buf + offset, data, len);
            offset += NLMSG_ALIGN(len);
        }
        ssize_t written = write(m_fds[0], buf, offset);
        ASSERT_EQ(written, static_cast<ssize_t>(offset));
    }
};

// Test reload() with a fully mocked RTM_GETROUTE dump
TEST_F(LinuxRoutingTableMockFdTest, ReloadFromMockDump) {
    m_table.set_query_fd(m_fds[1]);

    // Build mock dump: 3 routes + NLMSG_DONE
    MockRouteMsg r1(AF_INET, 24, RT_TABLE_MAIN, RTN_UNICAST);
    r1.add_attr_addr(RTA_DST, {192, 168, 1, 0});
    r1.add_attr_u32(RTA_OIF, 2);
    r1.add_attr_u32(RTA_PRIORITY, 100);

    MockRouteMsg r2(AF_INET, 0, RT_TABLE_MAIN, RTN_UNICAST);
    r2.add_attr_u32(RTA_OIF, 3);
    r2.add_attr_u32(RTA_PRIORITY, 50);

    MockRouteMsg r3(AF_INET6, 0, RT_TABLE_MAIN, RTN_UNICAST);
    r3.add_attr_u32(RTA_OIF, 4);
    r3.add_attr_u32(RTA_PRIORITY, 200);

    nlmsghdr done_hdr{};
    done_hdr.nlmsg_len = NLMSG_LENGTH(0);
    done_hdr.nlmsg_type = NLMSG_DONE;
    done_hdr.nlmsg_seq = 1;

    write_dump({
        {&r1, r1.nlh.nlmsg_len},
        {&r2, r2.nlh.nlmsg_len},
        {&r3, r3.nlh.nlmsg_len},
        {&done_hdr, done_hdr.nlmsg_len},
    });

    ASSERT_TRUE(m_table.reload());

    // Verify IPv4 routes
    ASSERT_EQ(m_table.get_routes_v4().size(), 2);
    // Sorted: /24 first, then /0
    ASSERT_EQ(m_table.get_routes_v4()[0].prefix.get_prefix_len(), 24);
    ASSERT_EQ(m_table.get_routes_v4()[0].if_index, 2);
    ASSERT_EQ(m_table.get_routes_v4()[1].prefix.get_prefix_len(), 0);
    ASSERT_EQ(m_table.get_routes_v4()[1].if_index, 3);

    // Verify IPv6 route
    ASSERT_EQ(m_table.get_routes_v6().size(), 1);
    ASSERT_TRUE(m_table.get_routes_v6()[0].is_default_route());
    ASSERT_EQ(m_table.get_routes_v6()[0].if_index, 4);

    // Verify default interface selection
    ASSERT_EQ(m_table.get_default_if_index(), 3);
}

// Test reload() with NLMSG_ERROR in dump
TEST_F(LinuxRoutingTableMockFdTest, ReloadErrorResponse) {
    m_table.set_query_fd(m_fds[1]);

    struct {
        nlmsghdr nlh;
        nlmsgerr err;
    } err_msg{};
    err_msg.nlh.nlmsg_len = NLMSG_LENGTH(sizeof(nlmsgerr));
    err_msg.nlh.nlmsg_type = NLMSG_ERROR;
    err_msg.nlh.nlmsg_seq = 1;
    err_msg.err.error = -EPERM;

    ssize_t written = write(m_fds[0], &err_msg, err_msg.nlh.nlmsg_len);
    ASSERT_GT(written, 0);

    ASSERT_FALSE(m_table.reload());
    ASSERT_EQ(m_table.get_routes_v4().size(), 0);
}

// Test that TUN interface is skipped when selecting default route
TEST_F(LinuxRoutingTableMockFdTest, TunFilteringInDefaultRoute) {
    // Step 1: Add two default routes without TUN filtering
    // index=1 (lo, always exists), metric=100 — best default
    // index=42 (fake), metric=200 — fallback
    MockRouteMsg def_lo(AF_INET, 0, RT_TABLE_MAIN, RTN_UNICAST);
    def_lo.add_attr_u32(RTA_OIF, 1);
    def_lo.add_attr_u32(RTA_PRIORITY, 100);
    m_table.handle_new_route(&def_lo.nlh);

    MockRouteMsg def_fallback(AF_INET, 0, RT_TABLE_MAIN, RTN_UNICAST);
    def_fallback.add_attr_u32(RTA_OIF, 42);
    def_fallback.add_attr_u32(RTA_PRIORITY, 200);
    m_table.handle_new_route(&def_fallback.nlh);

    // Without filtering, lo (index=1) wins by metric
    ASSERT_EQ(m_table.get_default_if_index(), 1);

    // Step 2: Enable TUN filtering with mock socketpair
    m_table.set_ignore_tun_interfaces(true);
    m_table.set_query_fd(m_fds[1]);

    // Pre-load mock: lo (index=1) has IFLA_INFO_KIND = "tun\0"
    MockLinkMsg tun_resp(1);
    tun_resp.add_link_info_kind("tun", 4); // "tun\0"
    ssize_t written = write(m_fds[0], &tun_resp, tun_resp.nlh.nlmsg_len);
    ASSERT_GT(written, 0);

    // Step 3: Trigger re-evaluation by adding a non-default route
    MockRouteMsg trigger(AF_INET, 24, RT_TABLE_MAIN, RTN_UNICAST);
    trigger.add_attr_addr(RTA_DST, {10, 0, 0, 0});
    trigger.add_attr_u32(RTA_OIF, 99);
    m_table.handle_new_route(&trigger.nlh);

    // lo (index=1) is now ignored as TUN → fallback (index=42) becomes default
    // index=42 is fake, if_indextoname fails → is_interface_ignored returns false → selected
    ASSERT_EQ(m_table.get_default_if_index(), 42);
}
#endif // __linux__
