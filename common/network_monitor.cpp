#include "common/logger.h"
#include "common/net_utils.h"
#include "common/utils.h"
#include "network_monitor_impl.h"

#include <functional>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

#ifndef _WIN32
#include <net/if.h>
#endif // _WIN32

#ifdef __APPLE__
#include <Network/path.h>
#include <Network/path_monitor.h>
#include <Network/interface.h>
#include <Network/nw_object.h>
#include <dispatch/dispatch.h>
#endif // __APPLE__

#ifdef __linux__
#include <fcntl.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#endif // __linux__

namespace ag::utils {

#ifdef __APPLE__
std::string get_interface_name(nw_path_t path) {
    __block std::string if_name;
    nw_path_enumerate_interfaces(path, ^(nw_interface_t iface) {
        if (std::string name = nw_interface_get_name(iface);
                !name.starts_with("utun") && !name.starts_with("ipsec")) {
            if_name = std::move(name);
            return false;
        }
        return true;
    });
    return if_name;
}
#endif // __APPLE__

NetworkMonitorImpl::NetworkMonitorImpl(std::function<void(const std::string &, bool)> cmd_handler)
: NetworkMonitor(std::move(cmd_handler)) {}

void NetworkMonitorImpl::start(event_base *ev_base) {
    dbglog(m_logger, "...");
    if (is_running()) {
        return;
    }
#ifdef __APPLE__
    m_nw_path_monitor = nw_path_monitor_create();
    if (!m_nw_path_monitor) {
        errlog(m_logger, "Failed to create path monitor");
        return;
    }
    m_dispatch_queue = dispatch_queue_create("set_outbound_interface", nullptr);
    if (!m_dispatch_queue) {
        errlog(m_logger, "Failed to create dispatch queue");
        nw_release(m_nw_path_monitor);
        m_nw_path_monitor = nullptr;
        return;
    }

    dispatch_group_t group = dispatch_group_create();
    dispatch_group_enter(group);

    nw_path_monitor_set_update_handler(m_nw_path_monitor, ^(nw_path_t path) {
        if (m_current_path) {
            nw_release(m_current_path);
        }
        m_current_path = (nw_path_t)nw_retain(path);
        auto old_first_update_done = m_first_update_done;
        changed_handler();
        if (!old_first_update_done && m_first_update_done) {
            dispatch_group_leave(group);
        }
    });

    nw_path_monitor_set_queue(m_nw_path_monitor, m_dispatch_queue);
    nw_path_monitor_start(m_nw_path_monitor);

    dispatch_group_wait(group, DISPATCH_TIME_FOREVER);
    dispatch_release(group);
#endif // __APPLE__

#ifdef __linux__
    if (!ev_base) {
        return;
    }

    if (!create_socket()) {
        return;
    }

    init_routing_table();

    m_monitor_event.reset(event_new(ev_base, m_monitor_sock_fd, EV_READ | EV_PERSIST,
        [](evutil_socket_t fd, short, void *arg) {
            auto *monitor = static_cast<NetworkMonitorImpl *>(arg);
            if (fd == monitor->m_monitor_sock_fd) {
                monitor->changed_handler();
            }
        }, this));

    event_add(m_monitor_event.get(), nullptr);
#endif // __linux__
}

void NetworkMonitorImpl::stop() {
    dbglog(m_logger, "...");
    if (!is_running()) {
        return;
    }
#ifdef __APPLE__
    nw_path_monitor_cancel(m_nw_path_monitor);
    m_nw_path_monitor = nullptr;

    dispatch_release(m_dispatch_queue);
    m_dispatch_queue = nullptr;

    m_first_update_done = false;
#endif // __APPLE__

#ifdef __linux__
    event_del(m_monitor_event.get());
    m_monitor_event.reset();
    close_socket();
#endif // __linux__
    m_if_name.clear();
}

std::string NetworkMonitorImpl::get_default_interface() {
#ifdef _WIN32
    uint32_t if_index = utils::win_detect_active_if();
    if (if_index == 0) {
        errlog(m_logger, "Couldn't detect active network interface");
        return "";
    }
    char if_name[IF_NAMESIZE]{};
    if_indextoname(if_index, if_name);
    return if_name;
#endif // _WIN32

#ifdef __APPLE__
    bool needed_stop_monitor = false;

    if (!m_nw_path_monitor) {
        start(nullptr);
        needed_stop_monitor = true;
    }

    std::string if_name = get_interface_name(m_current_path);

    if (needed_stop_monitor) {
        stop();
    }

    return if_name;
#endif // __APPLE__

#ifdef __linux__
    constexpr std::string_view CMD = "ip -o route show to default";
    infolog(m_logger, "{} {}", (geteuid() == 0) ? '#' : '$', CMD);
    Result result = fsystem(CMD);
    if (result.has_error()) {
        errlog(m_logger,
                "Couldn't detect the outbound interface automatically. Please specify it manually. Error: {}",
                result.error()->str());
        return "";
    }

    const auto& output = result.value().output;
    dbglog(m_logger, "Command output: {}", output);
    std::vector parts = split_by(output, ' ');
    auto found = std::ranges::find(parts, "dev");
    if (found == parts.end() || std::next(found) == parts.end()) {
        if (!output.empty()) {
            warnlog(m_logger, "Couldn't find the outbound interface name automatically");
        }
        return "";
    }

    return std::string(*std::next(found));
#endif // __linux__
}

bool NetworkMonitorImpl::is_running() const {
#ifdef __APPLE__
    return m_nw_path_monitor != nullptr;
#endif // __APPLE__

#ifdef __linux__
    return m_monitor_event != nullptr;
#endif // __linux__

#ifdef _WIN32
    return true;
#endif // _WIN32
}

NetworkMonitorImpl::~NetworkMonitorImpl() {
    stop();
}

#ifdef __linux__
bool NetworkMonitorImpl::create_socket() {
    m_monitor_sock_fd = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_ROUTE);
    if (m_monitor_sock_fd < 0) {
        errlog(m_logger, "Couldn't create netlink socket");
        return false;
    }

    // noneblock
    int flags = fcntl(m_monitor_sock_fd, F_GETFL, 0);
    if (flags == -1) {
        errlog(m_logger, "Couldn't get socket flags");
        close_socket();
        return false;
    }
    flags |= O_NONBLOCK;
    if (fcntl(m_monitor_sock_fd, F_SETFL, flags) == -1) {
        errlog(m_logger, "Couldn't set socket to non-blocking mode");
        close_socket();
        return false;
    }

    sockaddr_nl sa{
        .nl_family = AF_NETLINK,
        .nl_groups = RTMGRP_LINK | RTMGRP_IPV4_IFADDR | RTMGRP_IPV6_IFADDR
                | RTMGRP_IPV4_ROUTE | RTMGRP_IPV6_ROUTE
    };
    if (bind(m_monitor_sock_fd, (sockaddr*)&sa, sizeof(sa)) < 0) {
        errlog(m_logger, "Couldn't bind netlink socket");
        close_socket();
        return false;
    }

    return true;
}

void NetworkMonitorImpl::close_socket() {
    if (m_monitor_sock_fd != -1) {
        close(m_monitor_sock_fd);
        m_monitor_sock_fd = -1;
    }
}

bool NetworkMonitorImpl::init_routing_table() {
    if (m_monitor_sock_fd < 0) {
        return false;
    }

    if (!m_routing_table.reload(m_monitor_sock_fd)) {
        warnlog(m_logger, "Failed to load routing table via Netlink, will use fallback");
        return false;
    }

    m_routing_table.has_default_changed_and_reset();
    m_netlink_available = true;
    infolog(m_logger, "Routing table loaded via Netlink");
    return true;
}

std::optional<RouteEntry> LinuxRoutingTable::parse_route_msg(const nlmsghdr *nlh) {
    auto *rtm = static_cast<const rtmsg *>(NLMSG_DATA(nlh));

    if (rtm->rtm_type != RTN_UNICAST) {
        return std::nullopt;
    }
    if (rtm->rtm_table != RT_TABLE_MAIN && rtm->rtm_table != RT_TABLE_DEFAULT) {
        return std::nullopt;
    }

    Uint8Vector dst_addr;
    uint32_t if_index = 0;
    uint32_t metric = 0;
    Uint8Vector gateway;

    size_t addr_len = (rtm->rtm_family == AF_INET) ? IPV4_ADDRESS_SIZE : IPV6_ADDRESS_SIZE;

    auto *rta = RTM_RTA(rtm);
    int rta_len = static_cast<int>(RTM_PAYLOAD(nlh));

    for (; RTA_OK(rta, rta_len); rta = RTA_NEXT(rta, rta_len)) {
        switch (rta->rta_type) {
        case RTA_DST:
            dst_addr.assign(static_cast<uint8_t *>(RTA_DATA(rta)),
                    static_cast<uint8_t *>(RTA_DATA(rta)) + addr_len);
            break;
        case RTA_OIF:
            if_index = *static_cast<uint32_t *>(RTA_DATA(rta));
            break;
        case RTA_PRIORITY:
            metric = *static_cast<uint32_t *>(RTA_DATA(rta));
            break;
        case RTA_GATEWAY:
            gateway.assign(static_cast<uint8_t *>(RTA_DATA(rta)),
                    static_cast<uint8_t *>(RTA_DATA(rta)) + addr_len);
            break;
        default:
            break;
        }
    }

    if (dst_addr.empty()) {
        dst_addr.resize(addr_len, 0);
    }

    CidrRange prefix(dst_addr, rtm->rtm_dst_len);
    if (!prefix.valid()) {
        return std::nullopt;
    }

    RouteEntry entry(std::move(prefix));
    entry.if_index = if_index;
    entry.metric = metric;
    entry.protocol = rtm->rtm_protocol;
    entry.scope = rtm->rtm_scope;
    entry.type = rtm->rtm_type;
    entry.table = rtm->rtm_table;
    entry.gateway = std::move(gateway);

    return entry;
}

void LinuxRoutingTable::sort_and_update_cache() {
    std::sort(m_routes_v4.begin(), m_routes_v4.end());
    std::sort(m_routes_v6.begin(), m_routes_v6.end());

    std::optional<uint32_t> new_default_v4;
    std::optional<uint32_t> new_default_v6;

    for (const auto &route : m_routes_v4) {
        if (route.is_default_route() && route.if_index != 0) {
            new_default_v4 = route.if_index;
            break;
        }
    }
    for (const auto &route : m_routes_v6) {
        if (route.is_default_route() && route.if_index != 0) {
            new_default_v6 = route.if_index;
            break;
        }
    }

    if (new_default_v4 != m_prev_default_v4 || new_default_v6 != m_prev_default_v6) {
        m_default_changed = true;
        m_prev_default_v4 = new_default_v4;
        m_prev_default_v6 = new_default_v6;
    }
}

void LinuxRoutingTable::handle_new_route(const nlmsghdr *nlh) {
    auto entry = parse_route_msg(nlh);
    if (!entry) {
        return;
    }

    auto &routes = (entry->prefix.get_address().size() == IPV4_ADDRESS_SIZE)
            ? m_routes_v4 : m_routes_v6;

    auto it = std::find_if(routes.begin(), routes.end(), [&entry](const RouteEntry &r) {
        return r.prefix == entry->prefix && r.if_index == entry->if_index;
    });

    if (it != routes.end()) {
        *it = std::move(*entry);
    } else {
        routes.push_back(std::move(*entry));
    }

    sort_and_update_cache();
}

void LinuxRoutingTable::handle_del_route(const nlmsghdr *nlh) {
    auto entry = parse_route_msg(nlh);
    if (!entry) {
        return;
    }

    auto &routes = (entry->prefix.get_address().size() == IPV4_ADDRESS_SIZE)
            ? m_routes_v4 : m_routes_v6;

    std::erase_if(routes, [&entry](const RouteEntry &r) {
        return r.prefix == entry->prefix && r.if_index == entry->if_index;
    });

    sort_and_update_cache();
}

bool LinuxRoutingTable::reload(int netlink_fd) {
    struct {
        nlmsghdr nlh;
        rtmsg rtm;
    } req{};

    req.nlh.nlmsg_len = NLMSG_LENGTH(sizeof(rtmsg));
    req.nlh.nlmsg_type = RTM_GETROUTE;
    req.nlh.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
    req.nlh.nlmsg_seq = 1;
    req.rtm.rtm_family = AF_UNSPEC;
    req.rtm.rtm_table = RT_TABLE_MAIN;

    if (send(netlink_fd, &req, req.nlh.nlmsg_len, 0) < 0) {
        errlog(m_logger, "Failed to send RTM_GETROUTE: {}", strerror(errno));
        return false;
    }

    std::vector<RouteEntry> new_routes_v4;
    std::vector<RouteEntry> new_routes_v6;

    char buf[8192];
    bool done = false;
    while (!done) {
        ssize_t len = recv(netlink_fd, buf, sizeof(buf), 0);
        if (len < 0) {
            errlog(m_logger, "recv() failed: {}", strerror(errno));
            return false;
        }

        for (auto *nlh = reinterpret_cast<nlmsghdr *>(buf);
                NLMSG_OK(nlh, len); nlh = NLMSG_NEXT(nlh, len)) {
            if (nlh->nlmsg_type == NLMSG_DONE) {
                done = true;
                break;
            }
            if (nlh->nlmsg_type == NLMSG_ERROR) {
                auto *err = static_cast<nlmsgerr *>(NLMSG_DATA(nlh));
                errlog(m_logger, "Netlink error: {}", strerror(-err->error));
                return false;
            }
            if (nlh->nlmsg_type == RTM_NEWROUTE) {
                if (auto entry = parse_route_msg(nlh)) {
                    if (entry->prefix.get_address().size() == IPV4_ADDRESS_SIZE) {
                        new_routes_v4.push_back(std::move(*entry));
                    } else {
                        new_routes_v6.push_back(std::move(*entry));
                    }
                }
            }
        }
    }

    m_routes_v4 = std::move(new_routes_v4);
    m_routes_v6 = std::move(new_routes_v6);
    sort_and_update_cache();

    dbglog(m_logger, "Loaded {} IPv4 and {} IPv6 routes", m_routes_v4.size(), m_routes_v6.size());
    return true;
}

std::optional<uint32_t> LinuxRoutingTable::get_default_if_index() const {
    if (m_prev_default_v4.has_value()) {
        return m_prev_default_v4;
    }
    return m_prev_default_v6;
}

std::string LinuxRoutingTable::get_default_if_name() const {
    auto if_index = get_default_if_index();
    if (!if_index.has_value() || *if_index == 0) {
        return "";
    }
    char if_name[IF_NAMESIZE]{};
    if (if_indextoname(*if_index, if_name) == nullptr) {
        return "";
    }
    return if_name;
}

bool LinuxRoutingTable::has_default_changed_and_reset() {
    bool changed = m_default_changed;
    m_default_changed = false;
    return changed;
}
#endif // __linux__

void NetworkMonitorImpl::changed_handler() {
#ifdef __APPLE__
    bool is_satisfied = nw_path_get_status(m_current_path) == nw_path_status_satisfied;
    dbglog(m_logger, "is_satisfied: {}", is_satisfied);

    const std::string new_if_name = get_interface_name(m_current_path);

    if (m_first_update_done) {
        handle_network_change(new_if_name, is_satisfied);
    } else {
        m_if_name = new_if_name;
        m_first_update_done = true;
    }
#endif // __APPLE__

#ifdef __linux__
    nlmsghdr buf[8192/sizeof(struct nlmsghdr)];
    sockaddr_nl sa{};
    iovec iov = {buf, sizeof(buf)};
    msghdr msg = {
            .msg_name = (void*)&sa,
            .msg_namelen = sizeof(sa),
            .msg_iov = &iov,
            .msg_iovlen = 1,
            .msg_control = nullptr,
            .msg_controllen = 0,
            .msg_flags = 0
    };
    ssize_t len = recvmsg(m_monitor_sock_fd, &msg, 0);
    if (len < 0) {
        errlog(m_logger, "Couldn't recvmsg netlink socket");
        return;
    }

    for (auto nlh = (nlmsghdr *)buf; NLMSG_OK(nlh, len); nlh = NLMSG_NEXT(nlh, len)) {
        if (nlh->nlmsg_type == RTM_NEWADDR || nlh->nlmsg_type == RTM_DELADDR) {
            auto new_if_name = get_default_interface();
            handle_network_change(new_if_name, !new_if_name.empty());
        }
    }
#endif // __linux__
}

void NetworkMonitorImpl::handle_network_change(const std::string &new_if_name, bool is_satisfied) {
    if (!is_satisfied) {
        dbglog(m_logger, "Network not connected");
        if (m_cmd_handler) {
            m_cmd_handler("", false);
        }
        m_if_name.clear();
    } else if (m_if_name != new_if_name) {
        dbglog(m_logger, "Network connected {}", new_if_name);
        if (m_cmd_handler) {
            m_cmd_handler(new_if_name, true);
        }
        m_if_name = new_if_name;
    }
}

NetworkMonitor::NetworkMonitor(std::function<void(const std::string &, bool)> cmd_handler)
    : m_cmd_handler(std::move(cmd_handler)) {}

std::unique_ptr<NetworkMonitor> create_network_monitor(
    std::function<void(const std::string &if_name, bool is_connected)>&& cmd_handler) {

    return std::make_unique<NetworkMonitorImpl>(std::move(cmd_handler));
}

} // namespace ag::utils
