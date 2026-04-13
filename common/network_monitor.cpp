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
#include <linux/if_link.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

static constexpr size_t NETLINK_BUFFER_SIZE = 8192;
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
    if (m_netlink_available) {
        return m_routing_table.get_default_if_name();
    }

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
    m_routing_table.close_query_socket();
    if (m_monitor_sock_fd != -1) {
        close(m_monitor_sock_fd);
        m_monitor_sock_fd = -1;
    }
}

bool NetworkMonitorImpl::init_routing_table() {
    if (m_monitor_sock_fd < 0) {
        return false;
    }

    if (!m_routing_table.init_query_socket()) {
        warnlog(m_logger, "Failed to create query socket for routing table");
        return false;
    }

    if (!m_routing_table.reload()) {
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

    size_t addr_len = (rtm->rtm_family == AF_INET) ? IPV4_ADDRESS_SIZE : IPV6_ADDRESS_SIZE;

    auto *rta = RTM_RTA(rtm);
    int rta_len = static_cast<int>(RTM_PAYLOAD(nlh));

    for (; RTA_OK(rta, rta_len); rta = RTA_NEXT(rta, rta_len)) {
        switch (rta->rta_type) {
        case RTA_DST:
            if (RTA_PAYLOAD(rta) >= addr_len) {
                dst_addr.assign(static_cast<uint8_t *>(RTA_DATA(rta)),
                        static_cast<uint8_t *>(RTA_DATA(rta)) + addr_len);
            }
            break;
        case RTA_OIF:
            if (RTA_PAYLOAD(rta) >= sizeof(uint32_t)) {
                if_index = *static_cast<uint32_t *>(RTA_DATA(rta));
            }
            break;
        case RTA_PRIORITY:
            if (RTA_PAYLOAD(rta) >= sizeof(uint32_t)) {
                metric = *static_cast<uint32_t *>(RTA_DATA(rta));
            }
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

    return entry;
}

std::vector<RouteEntry>& LinuxRoutingTable::get_routes_by_addr_size(size_t addr_size) {
    return (addr_size == IPV4_ADDRESS_SIZE) ? m_routes_v4 : m_routes_v6;
}

const std::vector<RouteEntry>& LinuxRoutingTable::get_routes_by_addr_size(size_t addr_size) const {
    return (addr_size == IPV4_ADDRESS_SIZE) ? m_routes_v4 : m_routes_v6;
}

std::optional<uint32_t> LinuxRoutingTable::find_default_route(const std::vector<RouteEntry>& routes) const {
    auto it = std::ranges::find_if(routes, [this](const RouteEntry& r) {
        return r.is_default_route() && r.if_index != 0 && !is_interface_ignored(r.if_index);
    });
    return (it != routes.end()) ? std::optional{it->if_index} : std::nullopt;
}

void LinuxRoutingTable::set_ignore_tun_interfaces(bool ignore) {
    m_ignore_tun = ignore;
}

void LinuxRoutingTable::add_ignored_interface(const std::string& name) {
    m_ignored_interfaces.insert(name);
}

void LinuxRoutingTable::clear_ignored_interfaces() {
    m_ignored_interfaces.clear();
}

bool LinuxRoutingTable::init_query_socket() {
    if (m_query_fd >= 0) {
        return true;
    }
    m_query_fd = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_ROUTE);
    if (m_query_fd < 0) {
        return false;
    }

    timeval tv{.tv_sec = 5, .tv_usec = 0};
    if (setsockopt(m_query_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        warnlog(m_logger, "Failed to set receive timeout on query socket");
    }

    return true;
}

void LinuxRoutingTable::close_query_socket() {
    if (m_query_fd >= 0) {
        close(m_query_fd);
        m_query_fd = -1;
    }
}

std::string LinuxRoutingTable::get_interface_kind(int netlink_fd, uint32_t if_index) {
    if (netlink_fd < 0) {
        return {};
    }

    struct {
        nlmsghdr nlh;
        ifinfomsg ifm;
    } req{};

    req.nlh.nlmsg_len = NLMSG_LENGTH(sizeof(ifinfomsg));
    req.nlh.nlmsg_type = RTM_GETLINK;
    req.nlh.nlmsg_flags = NLM_F_REQUEST;
    req.nlh.nlmsg_seq = 1;
    req.ifm.ifi_family = AF_UNSPEC;
    req.ifm.ifi_index = static_cast<int>(if_index);

    if (send(netlink_fd, &req, req.nlh.nlmsg_len, 0) < 0) {
        return {};
    }

    char buf[4096];
    ssize_t len = recv(netlink_fd, buf, sizeof(buf), 0);

    if (len < 0) {
        return {};
    }

    auto *nlh = reinterpret_cast<nlmsghdr *>(buf);
    if (!NLMSG_OK(nlh, static_cast<size_t>(len)) || nlh->nlmsg_type == NLMSG_ERROR) {
        return {};
    }

    auto *rta = IFLA_RTA(NLMSG_DATA(nlh));
    int rta_len = static_cast<int>(IFLA_PAYLOAD(nlh));

    for (; RTA_OK(rta, rta_len); rta = RTA_NEXT(rta, rta_len)) {
        if (rta->rta_type == IFLA_LINKINFO) {
            auto *nested = static_cast<rtattr *>(RTA_DATA(rta));
            int nested_len = static_cast<int>(RTA_PAYLOAD(rta));

            for (; RTA_OK(nested, nested_len); nested = RTA_NEXT(nested, nested_len)) {
                if (nested->rta_type == IFLA_INFO_KIND) {
                    const char *data = static_cast<const char *>(RTA_DATA(nested));
                    size_t kind_len = RTA_PAYLOAD(nested);
                    if (kind_len > 0 && data[kind_len - 1] == '\0') {
                        --kind_len;
                    }
                    return std::string(data, kind_len);
                }
            }
        }
    }

    return {};
}

bool LinuxRoutingTable::is_interface_ignored(uint32_t if_index) const {
    if (if_index == 0) {
        return false;
    }

    char if_name[IF_NAMESIZE]{};
    if (if_indextoname(if_index, if_name) == nullptr) {
        return false;
    }

    if (m_ignored_interfaces.contains(if_name)) {
        dbglog(m_logger, "Interface {} (index={}) is in ignored list", if_name, if_index);
        return true;
    }

    if (m_ignore_tun && m_query_fd >= 0) {
        std::string kind = get_interface_kind(m_query_fd, if_index);
        if (kind == "tun") {
            dbglog(m_logger, "Interface {} (index={}) is TUN (IFLA_INFO_KIND), ignoring", if_name, if_index);
            return true;
        }
    }

    return false;
}

void LinuxRoutingTable::sort_and_update_cache() {
    std::sort(m_routes_v4.begin(), m_routes_v4.end());
    std::sort(m_routes_v6.begin(), m_routes_v6.end());

    std::optional<uint32_t> new_default_v4 = find_default_route(m_routes_v4);
    std::optional<uint32_t> new_default_v6 = find_default_route(m_routes_v6);

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

    auto &routes = get_routes_by_addr_size(entry->prefix.get_address().size());

    auto it = std::ranges::find_if(routes, [&entry](const RouteEntry &r) {
        return r.prefix == entry->prefix && r.if_index == entry->if_index;
    });

    if (it != routes.end()) {
        dbglog(m_logger, "Route updated: {} via if_index={} metric={}",
               entry->prefix.to_string(), entry->if_index, entry->metric);
        *it = std::move(*entry);
    } else {
        dbglog(m_logger, "Route added: {} via if_index={} metric={}",
               entry->prefix.to_string(), entry->if_index, entry->metric);
        routes.push_back(std::move(*entry));
    }

    sort_and_update_cache();
}

void LinuxRoutingTable::handle_del_route(const nlmsghdr *nlh) {
    auto entry = parse_route_msg(nlh);
    if (!entry) {
        return;
    }

    auto &routes = get_routes_by_addr_size(entry->prefix.get_address().size());

    size_t before = routes.size();
    std::erase_if(routes, [&entry](const RouteEntry &r) {
        return r.prefix == entry->prefix && r.if_index == entry->if_index;
    });

    if (routes.size() < before) {
        dbglog(m_logger, "Route deleted: {} via if_index={}",
               entry->prefix.to_string(), entry->if_index);
    }

    sort_and_update_cache();
}

bool LinuxRoutingTable::reload() {
    if (m_query_fd < 0) {
        errlog(m_logger, "Query socket not initialized");
        return false;
    }

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

    if (send(m_query_fd, &req, req.nlh.nlmsg_len, 0) < 0) {
        errlog(m_logger, "Failed to send RTM_GETROUTE: {}", strerror(errno));
        return false;
    }

    std::vector<RouteEntry> new_routes_v4;
    std::vector<RouteEntry> new_routes_v6;

    char buf[NETLINK_BUFFER_SIZE];
    bool done = false;
    while (!done) {
        ssize_t len = recv(m_query_fd, buf, sizeof(buf), 0);
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
                    auto &routes = (entry->prefix.get_address().size() == IPV4_ADDRESS_SIZE)
                            ? new_routes_v4 : new_routes_v6;
                    routes.push_back(std::move(*entry));
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
    nlmsghdr buf[NETLINK_BUFFER_SIZE / sizeof(nlmsghdr)];
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

    bool addr_changed = false;

    for (auto nlh = (nlmsghdr *)buf; NLMSG_OK(nlh, len); nlh = NLMSG_NEXT(nlh, len)) {
        switch (nlh->nlmsg_type) {
        case RTM_NEWADDR:
        case RTM_DELADDR:
            addr_changed = true;
            break;
        case RTM_NEWROUTE:
            if (m_netlink_available) {
                m_routing_table.handle_new_route(nlh);
            }
            break;
        case RTM_DELROUTE:
            if (m_netlink_available) {
                m_routing_table.handle_del_route(nlh);
            }
            break;
        default:
            break;
        }
    }

    if (m_netlink_available) {
        if (addr_changed || m_routing_table.has_default_changed_and_reset()) {
            auto new_if_name = m_routing_table.get_default_if_name();
            handle_network_change(new_if_name, !new_if_name.empty());
        }
    } else if (addr_changed) {
        auto new_if_name = get_default_interface();
        handle_network_change(new_if_name, !new_if_name.empty());
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
