#include <event2/thread.h>
#include <event2/util.h>

#include "common/net_utils.h"
#include "common/socket_address.h"
#include "common/system_error.h"
#include "common/utils.h"

#ifndef _WIN32
#include <fstream>
#include <net/if.h> // For if_nametoindex/if_indextoname
#include <resolv.h>
#else
#include <Iphlpapi.h>
#include <Winsock2.h>
#include <Ws2tcpip.h>
#include <netioapi.h>
#ifndef INET6_ADDRSTRLEN
#define INET6_ADDRSTRLEN 46
#endif
#undef IF_NAMESIZE
#define IF_NAMESIZE 256
#endif

namespace ag {
static const Logger g_logger("NET_UTILS");

Result<std::pair<std::string_view, std::string_view>, utils::NetUtilsError> utils::split_host_port(
        std::string_view address_string, bool require_ipv6_addr_in_square_brackets, bool require_non_empty_port) {
    if (!address_string.empty() && address_string.front() == '[') {
        auto pos = address_string.find("]:");
        if (pos != std::string_view::npos) {
            auto port = address_string.substr(pos + 2);
            if (require_non_empty_port && port.empty()) {
                return make_error(NetUtilsError::AE_IPV6_PORT_EMPTY);
            } else {
                return std::make_pair(address_string.substr(1, pos - 1), port);
            }

        } else if (address_string.back() == ']') {
            return std::make_pair(address_string.substr(1, address_string.size() - 2), std::string_view{});
        } else {
            return make_error(NetUtilsError::AE_IPV6_MISSING_RIGHT_BRACKET);
        }
    } else {
        auto pos = address_string.find(':');
        if (pos != std::string_view::npos) {
            auto rpos = address_string.rfind(':');
            if (pos != rpos) { // This is an IPv6 address without a port
                if (require_ipv6_addr_in_square_brackets) {
                    return make_error(NetUtilsError::AE_IPV6_MISSING_BRACKETS);
                } else {
                    return std::make_pair(address_string, std::string_view {});
                }
            }
            auto port = address_string.substr(pos + 1);
            if (require_non_empty_port && port.empty()) {
                return make_error(NetUtilsError::AE_IPV4_PORT_EMPTY);
            } else {
                return std::make_pair(address_string.substr(0, pos), port);
            }
        }
    }
    return std::make_pair(address_string, std::string_view {});
}

std::string utils::join_host_port(std::string_view host, std::string_view port) {
    if (host.find(':') != std::string_view::npos) {
        return AG_FMT("[{}]:{}", host, port);
    }
    return AG_FMT("{}:{}", host, port);
}

std::string utils::addr_to_str(Uint8View v) {
    char p[INET6_ADDRSTRLEN];
    if (v.size() == IPV4_ADDRESS_SIZE) {
        if (evutil_inet_ntop(AF_INET, v.data(), p, sizeof(p))) {
            return p;
        }
    } else if (v.size() == IPV6_ADDRESS_SIZE) {
        if (evutil_inet_ntop(AF_INET6, v.data(), p, sizeof(p))) {
            return p;
        }
    }
    return {};
}

SocketAddress utils::str_to_socket_address(std::string_view address) {
    auto split_result = utils::split_host_port(address);
    if (split_result.has_error()) {
        return {};
    }
    auto [host_view, port_view] = split_result.value();
    if (port_view.empty()) {
        return SocketAddress{host_view, 0};
    }

    std::string port_str{port_view};
    char *end = nullptr;
    auto port = std::strtoll(port_str.c_str(), &end, 10);

    if (end != &port_str.back() + 1 || port < 0 || port > UINT16_MAX) {
        return {};
    }

    return SocketAddress{host_view, (uint16_t) port};
}

bool utils::socket_error_is_eagain(int err) {
#ifndef _WIN32
    return err == EAGAIN || err == EWOULDBLOCK;
#else
    return err == WSAEWOULDBLOCK;
#endif
}

Error<utils::NetUtilsError> utils::bind_socket_to_if(evutil_socket_t fd, int family, uint32_t if_index) {
#if defined(__linux__)
    char buf[IF_NAMESIZE];
    const char *name = if_indextoname(if_index, buf);
    if (!name) {
        return make_error(NetUtilsError::AE_INVALID_IF_INDEX, AG_FMT("({}) {}", errno, strerror(errno)));
    }
    return bind_socket_to_if(fd, family, name);
#else
#if defined(_WIN32)
    constexpr int ipv4_opt = IP_UNICAST_IF;
    constexpr int ipv6_opt = IPV6_UNICAST_IF;
#else
    constexpr int ipv4_opt = IP_BOUND_IF;
    constexpr int ipv6_opt = IPV6_BOUND_IF;
#endif
    int option;
    int level;
    uint32_t value = if_index;
    switch (family) {
    case AF_INET:
        level = IPPROTO_IP;
        option = ipv4_opt;
#if defined(_WIN32)
        value = htonl(if_index);
#endif
        break;
    case AF_INET6:
        level = IPPROTO_IPV6;
        option = ipv6_opt;
        break;
    default:
        return make_error(NetUtilsError::AE_UNSUPPORTED_FAMILY, AG_FMT("family: {}", family));
    }
    int ret = setsockopt(fd, level, option, (char *) &value, sizeof(value)); // Cast to (char *) for Windows
    if (ret != 0) {
        int error = evutil_socket_geterror(fd);
        const char *error_str = evutil_socket_error_to_string(error);
        if (char buf[IF_NAMESIZE]; if_indextoname(if_index, buf)) {
            return make_error(
                    NetUtilsError::AE_BIND_ERROR, AG_FMT("fd {} to interface {}: ({}) {}", fd, buf, error, error_str));
        } else {
            return make_error(NetUtilsError::AE_BIND_ERROR,
                    AG_FMT("fd {} to interface {}: {}: {}", fd, if_index, error, error_str));
        }
    }
    return {};
#endif
}

Error<utils::NetUtilsError> utils::bind_socket_to_if(evutil_socket_t fd, int family, const char *if_name) {
#if defined(__linux__)
    (void) family;
    int ret = setsockopt(fd, SOL_SOCKET, SO_BINDTODEVICE, if_name, strlen(if_name));
    if (ret != 0) {
        return make_error(NetUtilsError::AE_BIND_ERROR,
                AG_FMT("fd {} to interface {}: ({}) {}", fd, if_name, errno, strerror(errno)));
    }
    return {};
#else
    uint32_t if_index = if_nametoindex(if_name);
    if (if_index == 0) {
        return make_error(NetUtilsError::AE_INVALID_IF_NAME, AG_FMT("{}", if_name));
    }
    return bind_socket_to_if(fd, family, if_index);
#endif
}

std::optional<SocketAddress> utils::get_peer_address(evutil_socket_t fd) {
    sockaddr_storage addr = {};
    socklen_t addrlen = sizeof(addr);
    if (::getpeername(fd, (sockaddr *) &addr, &addrlen) != 0) {
        return std::nullopt;
    }
    return SocketAddress((sockaddr *) &addr);
}

std::optional<SocketAddress> utils::get_local_address(evutil_socket_t fd) {
    sockaddr_storage addr = {};
    socklen_t addrlen = sizeof(addr);
    if (::getsockname(fd, (sockaddr *) &addr, &addrlen) != 0) {
        return std::nullopt;
    }
    return SocketAddress((sockaddr *) &addr);
}

#ifdef _WIN32
static bool is_physical_adapter(const IP_ADAPTER_ADDRESSES *aa) {
    if (!aa)
        return false;

    switch (aa->IfType) {
    case IF_TYPE_ETHERNET_CSMACD: // 6
    case IF_TYPE_IEEE80211:       // 71 (Wi-Fi)
    case IF_TYPE_WWANPP:          // 243
    case IF_TYPE_WWANPP2:         // 244
        break;
    default:
        return false;
    }

    // Should be online
    if (aa->OperStatus != IfOperStatusUp && aa->OperStatus != IfOperStatusDormant) {
        return false;
    }

    return aa->FirstUnicastAddress != nullptr;
}

static DWORD get_default_route_ifs(
        std::unordered_set<NET_IFINDEX> &net_ifs_v4, std::unordered_set<NET_IFINDEX> &net_ifs_v6) {
    PMIB_IPFORWARD_TABLE2 table_v4{};
    PMIB_IPFORWARD_TABLE2 table_v6{};
    DWORD error = ERROR_SUCCESS;
    if (error = GetIpForwardTable2(AF_INET, &table_v4); error != ERROR_SUCCESS) {
        errlog(g_logger, "Ipv4 GetIpForwardTable2(): {}", sys::strerror(error));
        return error;
    }
    if (error = GetIpForwardTable2(AF_INET6, &table_v6); error != ERROR_SUCCESS) {
        errlog(g_logger, "Ipv6 GetIpForwardTable2(): {}", sys::strerror(error));
        return error;
    }
    for (size_t i = 0; i < table_v4->NumEntries; i++) {
        if (SocketAddress((sockaddr *) &table_v4->Table[i].DestinationPrefix.Prefix.Ipv4).is_any()
                && table_v4->Table[i].DestinationPrefix.PrefixLength == 0) {
            net_ifs_v4.insert(table_v4->Table[i].InterfaceIndex);
                }
    }
    for (size_t i = 0; i < table_v6->NumEntries; i++) {
        if (SocketAddress((sockaddr *) &table_v6->Table[i].DestinationPrefix.Prefix.Ipv6).is_any()
                && table_v6->Table[i].DestinationPrefix.PrefixLength == 0) {
            net_ifs_v6.insert(table_v6->Table[i].InterfaceIndex);
                }
    }
    dbglog(g_logger, "Default route interfaces: ipv4 = {}, ipv6 = {}", net_ifs_v4, net_ifs_v6);
    return error;
}

/// return interface with minimal metric: <index, min_metric>
static std::pair<uint32_t, uint32_t> get_min_metric_if(std::unordered_set<NET_IFINDEX> &net_ifs, bool ipv6 = false) {
    auto ip_family = AF_INET;
    if (ipv6) {
        ip_family = AF_INET6;
    }
    uint32_t result_idx = 0;
    uint32_t min_metric = NL_MAX_METRIC_COMPONENT;
    for (const auto &index : net_ifs) {
        MIB_IPINTERFACE_ROW row;
        InitializeIpInterfaceEntry(&row);
        row.Family = ip_family;
        row.InterfaceIndex = index;
        if (DWORD error = GetIpInterfaceEntry(&row); error != ERROR_SUCCESS) {
            errlog(g_logger, "GetIpInterfaceEntry(): {}", sys::strerror(error));
        } else if (row.Connected && row.Metric < min_metric) {
            result_idx = row.InterfaceIndex;
            min_metric = row.Metric;
        }
    }
    return {result_idx, min_metric};
}

DWORD utils::win_get_physical_interfaces(std::unordered_set<NET_IFINDEX> &physical_ifs) {
    ULONG flags =
            GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER | GAA_FLAG_INCLUDE_GATEWAYS;

    ULONG size = 0;
    ULONG ret = GetAdaptersAddresses(AF_UNSPEC, flags, nullptr, nullptr, &size);
    if (ret != ERROR_BUFFER_OVERFLOW) {
        if (ret == NO_ERROR)
            return ERROR_SUCCESS;
        errlog(g_logger, "GetAdaptersAddresses(size probe) failed: {}", sys::strerror(ret));
        return ret;
    }

    std::vector<uint8_t> buf(size);
    ret = GetAdaptersAddresses(AF_UNSPEC, flags, nullptr, (IP_ADAPTER_ADDRESSES *) buf.data(), &size);
    if (ret != NO_ERROR) {
        errlog(g_logger, "GetAdaptersAddresses() failed: {}", sys::strerror(ret));
        return ret;
    }

    for (auto *p = (IP_ADAPTER_ADDRESSES *) buf.data(); p != nullptr; p = p->Next) {

        if (!is_physical_adapter(p))
            continue;

        if (p->IfIndex != 0)
            physical_ifs.insert(p->IfIndex);
        if (p->Ipv6IfIndex != 0)
            physical_ifs.insert(p->Ipv6IfIndex);
    }

    dbglog(g_logger, "Physical interfaces: {}", physical_ifs);
    return ERROR_SUCCESS;
}

uint32_t utils::win_detect_active_if() {
    // first find physical network cards interfaces
    std::unordered_set<NET_IFINDEX> physical_ifs;
    DWORD error = win_get_physical_interfaces(physical_ifs);
    if (physical_ifs.empty()) {
        SetLastError(error);
        errlog(g_logger, "get_physical_interfaces: {}", sys::strerror(error));
        return 0;
    }
    // get interfaces with default route from routing table
    std::unordered_set<NET_IFINDEX> net_ifs_v4;
    std::unordered_set<NET_IFINDEX> net_ifs_v6;
    error = get_default_route_ifs(net_ifs_v4, net_ifs_v6);
    if (error != ERROR_SUCCESS) {
        SetLastError(error);
        errlog(g_logger, "get_default_route_ifs: {}", sys::strerror(error));
        return 0;
    }
    // exclude non-physical interfaces
    std::erase_if(net_ifs_v4, [&](auto net_if) {
        return !physical_ifs.contains(net_if);
    });
    std::erase_if(net_ifs_v6, [&](auto net_if) {
        return !physical_ifs.contains(net_if);
    });

    // Then choose operational one with minimal metric
    // handle ipv4
    auto [index_v4, min_metric_v4] = get_min_metric_if(net_ifs_v4, false);
    dbglog(g_logger, "min_metric_v4 = {} with if_index = {}", min_metric_v4, index_v4);
    // handle ipv6
    auto [index_v6, min_metric_v6] = get_min_metric_if(net_ifs_v6, true);
    dbglog(g_logger, "min_metric_v6 = {} with if_index = {}", min_metric_v6, index_v6);
    // both checks failed
    if (min_metric_v4 == min_metric_v6 && min_metric_v4 == NL_MAX_METRIC_COMPONENT) {
        errlog(g_logger, "Both metric checks failed");
        return 0;
    }
    if (min_metric_v4 < min_metric_v6) {
        return index_v4;
    }
    return index_v6;
}

static constexpr std::string_view WINREG_INTERFACES_PATH_V4 =
        R"(SYSTEM\CurrentControlSet\Services\Tcpip\Parameters\Interfaces)";
static constexpr std::string_view WINREG_INTERFACES_PATH_V6 =
        R"(SYSTEM\CurrentControlSet\Services\Tcpip6\Parameters\Interfaces)";

DWORD utils::win_set_if_nameserver(std::string_view dns_list, const char *if_guid, bool ipv6) {
    HKEY current_key{};
    DWORD error = ERROR_SUCCESS;
    std::string_view interfaces_path = ipv6 ? WINREG_INTERFACES_PATH_V6 : WINREG_INTERFACES_PATH_V4;
    error = RegOpenKeyExA(HKEY_LOCAL_MACHINE, interfaces_path.data(), 0, KEY_ALL_ACCESS, &current_key);
    if (error == ERROR_SUCCESS) {
        error = RegSetKeyValueA(current_key, if_guid, "NameServer", REG_SZ, dns_list.data(), dns_list.size());
        RegCloseKey(current_key);
    }
    return error;
}

std::optional<std::string> utils::win_get_if_nameserver(const char *if_guid, bool ipv6) {
    HKEY current_key{};
    DWORD error = ERROR_SUCCESS;
    std::string_view interfaces_path = ipv6 ? WINREG_INTERFACES_PATH_V6 : WINREG_INTERFACES_PATH_V4;
    error = RegOpenKeyExA(HKEY_LOCAL_MACHINE, interfaces_path.data(), 0, KEY_ALL_ACCESS, &current_key);
    if (error != ERROR_SUCCESS) {
        return std::nullopt;
    }
    DWORD size = 0;
    error = RegGetValueA(current_key, if_guid, "NameServer", RRF_RT_REG_SZ, NULL, NULL, &size);
    if (error != ERROR_SUCCESS || size == 0) {
        RegCloseKey(current_key);
        return std::nullopt;
    }
    std::string value;
    value.resize(size - 1);
    error = RegGetValueA(current_key, if_guid, "NameServer", RRF_RT_REG_SZ, NULL, value.data(), &size);
    if (error != ERROR_SUCCESS) {
        RegCloseKey(current_key);
        return std::nullopt;
    }
    RegCloseKey(current_key);
    return value;
}

#elif defined(__MACH__)

Result<SystemDnsServers, RetrieveSystemDnsError> utils::retrieve_system_dns_servers() {
    struct __res_state res = {};
    if (0 != res_ninit(&res)) {
        return make_error(RetrieveSystemDnsError::AE_INIT);
    }

    Uint8Vector addrs_buf(res.nscount * sizeof(res_sockaddr_union));
    res_getservers(&res, (res_sockaddr_union *) addrs_buf.data(), res.nscount);

    SystemDnsServers servers;
    servers.main.reserve(res.nscount);
    for (const res_sockaddr_union &addr : std::span{(res_sockaddr_union *) addrs_buf.data(), size_t(res.nscount)}) {
        SocketAddress sock_addr((sockaddr *) &addr);
        if (!sock_addr.valid()) {
            warnlog(g_logger, "Skipping invalid address: {}", encode_to_hex({(uint8_t *) &addr, sizeof(addr)}));
            continue;
        }

        servers.main.emplace_back(SystemDnsServer{
            .address = sock_addr.host_str(),
        });
    }

    res_ndestroy(&res);
    return servers;
}

#elif defined(__GLIBC__)

Result<SystemDnsServers, RetrieveSystemDnsError> utils::retrieve_system_dns_servers() {
    struct __res_state res = {};
    if (0 != res_ninit(&res)) {
        return make_error(RetrieveSystemDnsError::AE_INIT);
    }

    SystemDnsServers servers;
    servers.main.reserve(res.nscount);
    for (int i = 0; i < res.nscount; ++i) {
        SocketAddress addr;
        if (res.nsaddr_list[i].sin_family == AF_INET) {
            addr = SocketAddress((sockaddr *) &res.nsaddr_list[i]);
        } else if (res._u._ext.nsaddrs[i]->sin6_family == AF_INET6) {
            addr = SocketAddress((sockaddr *) res._u._ext.nsaddrs[i]);
        }

        if (!addr.valid()) {
            warnlog(g_logger, "Skipping invalid address: {}", encode_to_hex({(uint8_t *) &res, sizeof(res)}));
            continue;
        }

#if defined __linux__
        static const std::array UNFILTERED_IPS {
            SocketAddress{AG_UNFILTERED_DNS_IPS_V4[0], PLAIN_DNS_PORT_NUMBER},
            SocketAddress{AG_UNFILTERED_DNS_IPS_V4[1], PLAIN_DNS_PORT_NUMBER},
            SocketAddress{AG_UNFILTERED_DNS_IPS_V6[0], PLAIN_DNS_PORT_NUMBER},
            SocketAddress{AG_UNFILTERED_DNS_IPS_V6[1], PLAIN_DNS_PORT_NUMBER},
        };
        if (addr.is_loopback() || std::ranges::find(UNFILTERED_IPS, addr) != UNFILTERED_IPS.end()) {
            warnlog(g_logger, "Skipping potential route loop address: {}", addr);
            continue;
        }
#endif // __linux__

        servers.main.emplace_back(SystemDnsServer{
                .address = addr.host_str(),
        });
    }

    res_nclose(&res);
    return servers;
}

#elif defined(__ANDROID__)
//
// Android retrieves system DNS servers in Java code
//
#else

Result<SystemDnsServers, RetrieveSystemDnsError> utils::retrieve_system_dns_servers() {
    SystemDnsServers servers;
    std::ifstream ifs{"/etc/resolv.conf"};
    std::string line;
    while (std::getline(ifs, line)) {
        std::string_view line_view = line;
        constexpr std::string_view NAMESERVER = "nameserver";
        if (line_view.starts_with(NAMESERVER)) {
            line_view.remove_prefix(NAMESERVER.size());
        }
        line_view = ltrim(line_view);
        line_view = line_view.substr(0,
                std::distance(line_view.begin(), std::ranges::find_if(line_view, isspace)));
        SocketAddress addr{line_view, PLAIN_DNS_PORT_NUMBER};

        if (!addr.valid()) {
            warnlog(g_logger, "Skipping invalid address: {}", line_view);
            continue;
        }

#if defined __linux__
        static const std::array UNFILTERED_IPS {
            SocketAddress{AG_UNFILTERED_DNS_IPS_V4[0], PLAIN_DNS_PORT_NUMBER},
            SocketAddress{AG_UNFILTERED_DNS_IPS_V4[1], PLAIN_DNS_PORT_NUMBER},
            SocketAddress{AG_UNFILTERED_DNS_IPS_V6[0], PLAIN_DNS_PORT_NUMBER},
            SocketAddress{AG_UNFILTERED_DNS_IPS_V6[1], PLAIN_DNS_PORT_NUMBER},
        };
        if (addr.is_loopback() || std::ranges::find(UNFILTERED_IPS, addr) != std::end(UNFILTERED_IPS)) {
            warnlog(g_logger, "Skipping potential route loop address: {}", addr);
            continue;
        }
#endif // __linux__

        servers.main.emplace_back(SystemDnsServer{std::string(line_view), std::nullopt});
    };
    return servers;
}

#endif

} // namespace ag