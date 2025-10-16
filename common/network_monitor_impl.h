#pragma once

#include "common/defs.h"
#include "common/logger.h"
#include "common/network_monitor.h"

namespace ag::utils {

/**
 * @class NetworkMonitorImpl
 * Monitors network changes.
 */
class NetworkMonitorImpl : public NetworkMonitor {
public:
    /**
     * Constructs a NetworkMonitorImpl object with the specified command handler.
     * @param cmd_handler A function to handle network status changes
     */
    explicit NetworkMonitorImpl(std::function<void(const std::string &if_name, bool is_connected)> cmd_handler);

    NetworkMonitorImpl(const NetworkMonitorImpl &c) = delete;
    NetworkMonitorImpl(NetworkMonitorImpl &&c) = delete;
    NetworkMonitorImpl &operator=(const NetworkMonitorImpl &c) = delete;
    NetworkMonitorImpl &operator=(NetworkMonitorImpl &&c) = delete;

    /**
     * Starts monitoring the network status using the provided libevent base.
     * @param ev_base A pointer to the libevent `event_base`
     */
    void start(event_base *ev_base) override;
    /**
     * Stops monitoring the network status.
     */
    void stop() override;

    /**
     * Gets the default network interface name.
     * @return String representing the default network interface name.
     */
    std::string get_default_interface() override;
    /**
     * Checks if the network monitor is currently running.
     * @return True if the network monitor is running, false otherwise.
     */
    [[nodiscard]] bool is_running() const override;

    ~NetworkMonitorImpl() override;
protected:
    const Logger m_logger{"NETWORK_MONITORING"};

    std::string m_if_name;
#ifdef __APPLE__
    nw_path_monitor_t m_nw_path_monitor = nullptr;
    dispatch_queue_t m_dispatch_queue = nullptr;
    nw_path_t m_current_path = nullptr;
    bool m_first_update_done = false;
#endif // __APPLE__

#ifdef __linux__
    UniquePtr<event, &event_free> m_monitor_event;
    evutil_socket_t m_monitor_sock_fd = -1;

    bool create_socket();
    void close_socket();
#endif // __linux__

    void changed_handler();
    void handle_network_change(const std::string &new_if_name, bool is_satisfied);
};

} // namespace ag::utils
