#pragma once

#include "common/defs.h"
#include "common/logger.h"
#include "common/network_monitor.h"

#ifdef __linux__
#include "common/cidr_range.h"
#include <linux/netlink.h>
#endif

namespace ag::utils {

#ifdef __linux__
/**
 * @struct RouteEntry
 * Represents a single routing table entry.
 * Sorting: more specific prefix (longer prefix_len) has higher priority,
 * then lower metric is preferred.
 */
struct RouteEntry {
    CidrRange prefix;           ///< Destination network prefix
    uint32_t if_index = 0;      ///< Output interface index
    uint32_t metric = 0;        ///< Route metric (priority, lower is better)

    explicit RouteEntry(CidrRange p) : prefix(std::move(p)) {}

    bool operator<(const RouteEntry &other) const {
        if (prefix.get_prefix_len() != other.prefix.get_prefix_len()) {
            return prefix.get_prefix_len() > other.prefix.get_prefix_len();
        }
        return metric < other.metric;
    }

    [[nodiscard]] bool is_default_route() const {
        return prefix.get_prefix_len() == 0;
    }
};

/**
 * @class LinuxRoutingTable
 * Manages the routing table obtained via Netlink.
 * Provides methods to reload the full table, handle incremental updates,
 * and detect default route changes.
 */
class LinuxRoutingTable {
public:
    bool reload(int netlink_fd);
    void handle_new_route(const nlmsghdr *nlh);
    void handle_del_route(const nlmsghdr *nlh);

    [[nodiscard]] std::optional<uint32_t> get_default_if_index() const;
    [[nodiscard]] std::string get_default_if_name() const;
    bool has_default_changed_and_reset();

    [[nodiscard]] const std::vector<RouteEntry> &get_routes_v4() const { return m_routes_v4; }
    [[nodiscard]] const std::vector<RouteEntry> &get_routes_v6() const { return m_routes_v6; }

private:
    std::vector<RouteEntry> m_routes_v4;
    std::vector<RouteEntry> m_routes_v6;
    std::optional<uint32_t> m_prev_default_v4;
    std::optional<uint32_t> m_prev_default_v6;
    bool m_default_changed = false;
    Logger m_logger{"ROUTING_TABLE"};

    void sort_and_update_cache();
    static std::optional<RouteEntry> parse_route_msg(const nlmsghdr *nlh);
    static std::optional<uint32_t> find_default_route(const std::vector<RouteEntry>& routes);

    std::vector<RouteEntry>& get_routes_by_addr_size(size_t addr_size);
    const std::vector<RouteEntry>& get_routes_by_addr_size(size_t addr_size) const;
};
#endif

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
    LinuxRoutingTable m_routing_table;
    bool m_netlink_available = false;

    bool create_socket();
    void close_socket();
    bool init_routing_table();
#endif // __linux__

    void changed_handler();
    void handle_network_change(const std::string &new_if_name, bool is_satisfied);
};

} // namespace ag::utils
