#include <functional>
#include <memory>
#include <string>
#include <utility>

#include <gtest/gtest.h>

#include "common/defs.h"
#include "../network_monitor_impl.h"

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
#elif defined(__APPLE__)
        m_monitor->start(nullptr);
#elif defined(_WIN32)
        m_monitor->start(nullptr);
#endif
    }
};


TEST_F(NetworkMonitorTest, GetDefaultInterface) {
    std::string if_name = m_monitor->get_default_interface();
    ASSERT_FALSE(if_name.empty());
}

TEST_F(NetworkMonitorTest, StartAndStopMonitor) {
    start_monitor();
    ASSERT_TRUE(m_monitor->is_running());
    m_monitor->stop();
    ASSERT_FALSE(m_monitor->is_running());
}

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