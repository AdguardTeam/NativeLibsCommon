#include <functional>
#include <memory>
#include <string>
#include <utility>

#include <gtest/gtest.h>

#include "common/defs.h"
#include "../network_monitor_impl.h"

#ifdef __linux__
#include <linux/rtnetlink.h>
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
#endif // __linux__