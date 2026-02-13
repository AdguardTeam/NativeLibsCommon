#pragma once

#include <functional>
#include <memory>
#include <string>

#ifdef __APPLE__
#include <Network/Network.h>
#endif // __APPLE__

#include <event2/event.h>

namespace ag::utils {

class NetworkMonitor {
public:
    explicit NetworkMonitor(std::function<void(const std::string &if_name, bool is_connected)> cmd_handler);

    NetworkMonitor(const NetworkMonitor &c) = delete;
    NetworkMonitor(NetworkMonitor &&c) = delete;
    NetworkMonitor &operator=(const NetworkMonitor &c) = delete;
    NetworkMonitor &operator=(NetworkMonitor &&c) = delete;

    virtual void start(struct event_base *ev_base) = 0;
    virtual void stop() = 0;

    virtual std::string get_default_interface() = 0;
    [[nodiscard]] virtual bool is_running() const = 0;

    virtual ~NetworkMonitor() = default;

protected:
    std::function<void(const std::string &if_name, bool is_connected)> m_cmd_handler = nullptr;
};

std::unique_ptr<NetworkMonitor> create_network_monitor(
    std::function<void(const std::string &if_name, bool is_connected)>&& cmd_handler);

}  // namespace ag::utils