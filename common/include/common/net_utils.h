#pragma once

#include <cstdint>
#include <chrono>
#include <event2/event.h>
#include <string>
#include <string_view>

#include "common/defs.h"
#include "common/error.h"
#include "common/socket_address.h"

#ifdef _WIN32
  // The order of includes is important
  #ifndef WIN32_LEAN_AND_MEAN
  #  define WIN32_LEAN_AND_MEAN
  #endif
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #include <iphlpapi.h>
  #include <netioapi.h>
  #include <ifdef.h>
#endif

namespace ag {
#ifndef __ANDROID__
enum RetrieveSystemDnsError: uint8_t {
    AE_INIT,
};
#endif // ifdef __ANDROID__

/**
 * A system DNS server descriptor. Based on Windows 11 model as the most comprehensive among
 * the supported platforms.
 */
struct SystemDnsServer {
    /** URL of the DNS server. The syntax corresponds to the one used in the DNS proxy. */
    std::string address;
    /**
     * The network address of the hostname from the URL in `address` field.
     * @note The library does not check whether the address matches the hostname.
     */
    std::optional<SocketAddress> resolved_host;
};

struct SystemDnsServers {
    std::vector<SystemDnsServer> main;
    std::vector<std::string> fallback;
    std::vector<std::string> bootstrap;
};

namespace utils {

enum class NetUtilsError {
    AE_INVALID_IF_INDEX,
    AE_UNSUPPORTED_FAMILY,
    AE_BIND_ERROR,
    AE_INVALID_IF_NAME,
    AE_IPV6_PORT_EMPTY,
    AE_IPV6_MISSING_RIGHT_BRACKET,
    AE_IPV6_MISSING_BRACKETS,
    AE_IPV4_PORT_EMPTY,
};

#ifndef _WIN32
static constexpr auto AG_ETIMEDOUT = ETIMEDOUT;
static constexpr auto AG_ECONNREFUSED = ECONNREFUSED;
static constexpr auto AG_ECONNRESET = ECONNRESET;
#else
static constexpr auto AG_ETIMEDOUT = WSAETIMEDOUT;
static constexpr auto AG_ECONNREFUSED = WSAECONNREFUSED;
static constexpr auto AG_ECONNRESET = WSAECONNRESET;
#endif

enum TransportProtocol {
    TP_UDP,
    TP_TCP,
};

static constexpr uint32_t PLAIN_DNS_PORT_NUMBER = 53;
static constexpr std::string_view AG_UNFILTERED_DNS_IPS_V4[] = {
    "46.243.231.30",
    "46.243.231.31",
};
static constexpr std::string_view AG_UNFILTERED_DNS_IPS_V6[] = {
    "2a10:50c0::1:ff",
    "2a10:50c0::2:ff",
};

#ifdef _WIN32
/**
 * Populate `physical_ifs` with the interface indices of physical network adapters.
 * @return An error code, or `ERROR_SUCCESS`.
 */
DWORD win_get_physical_interfaces(std::unordered_set<NET_IFINDEX> &physical_ifs);

/**
 * Return the network interface which is currently active.
 * May return 0 in case it is not found.
 */
std::uint32_t win_detect_active_if();

/**
 * Modify the DNS settings for a network interface.
 *
 * Equivalent to specifying the preferred/alternative DNS server in IPv4/IPv6 properties in the interface
 * properties GUI. An empty string is equivalent to selecting "Obtain DNS server address automatically".
 *
 * @param dns_list Comma-separated list of nameserver addresses.
 * @param if_guid Null-terminated interface GUID string. See the `ConvertInterface<X>To<Y>` functions in `netioapi.h`.
 * @param ipv6 `true` to modify the IPv6 properties, `false` for IPv4.
 * @return `ERROR_SUCCESS` or an error code defined in Winerror.h. `FormatMessage` with the
 *         `FORMAT_MESSAGE_FROM_SYSTEM` flag can be used to get a generic description of the error.
 */
DWORD win_set_if_nameserver(std::string_view dns_list, const char *if_guid, bool ipv6);

/**
 * Get the current value of the NameServer property of an interface. Return `std::nullopt` on any error,
 * including if the property does not exist or isn't a null-terminated string.
 *
 * @param if_guid Null-terminated interface GUID string. See the `ConvertInterface<X>To<Y>` functions in `netioapi.h`.
 * @param ipv6 `true` to get the IPv6 property, `false` for IPv4.
 */
std::optional<std::string> win_get_if_nameserver(const char *if_guid, bool ipv6);

#endif // defined _WIN32

/**
 * Split address string to host and port with error
 * @param address_string Address string
 * @param require_ipv6_addr_in_square_brackets Require IPv6 address in square brackets
 * @param require_non_empty_port Require non-empty port after colon
 * @return Host, port or error
 */
Result<std::pair<std::string_view, std::string_view>, NetUtilsError> split_host_port(
        std::string_view address_string, bool require_ipv6_addr_in_square_brackets = false,
        bool require_non_empty_port = false);

/**
 * Join host and port into address string
 * @param host Host
 * @param port Port
 * @return Address string
 */
std::string join_host_port(std::string_view host, std::string_view port);

/**
 * @return a string representation of an IP address, or
 *         an empty string if an error occured or addr is empty
 */
std::string addr_to_str(Uint8View addr);

/**
 * @param address a numeric IP address, with an optional port number
 * @return a SocketAddress parsed from the address string
 */
SocketAddress str_to_socket_address(std::string_view address);

/**
 * @param err Socket error
 * @return True if socket error is EAGAIN/EWOULDBLOCK
 */
bool socket_error_is_eagain(int err);

/**
 * Make a socket bound to the specified interface
 * @param fd       socket descriptor
 * @param family   socket family
 * @param if_index interface index
 * @return some error if failed
 */
Error<NetUtilsError> bind_socket_to_if(evutil_socket_t fd, int family, uint32_t if_index);

/**
 * Make a socket bound to the specified interface
 * @param fd      socket descriptor
 * @param family  socket family
 * @param if_name interface name
 * @return some error if failed
 */
Error<NetUtilsError> bind_socket_to_if(evutil_socket_t fd, int family, const char *if_name);

/**
 * Get the address of the peer connected to the socket
 * @return some address if successful
 */
std::optional<SocketAddress> get_peer_address(evutil_socket_t fd);

/**
 * Get the current address to which the socket is bound
 * @return some address if successful
 */
std::optional<SocketAddress> get_local_address(evutil_socket_t fd);

#if !defined(__ANDROID__)
/**
 * Retrieve DNS servers
 */
Result<SystemDnsServers, RetrieveSystemDnsError> retrieve_system_dns_servers();

#endif // ifdef __ANDROID__

} // namespace utils

#ifndef __ANDROID__
template <>
struct ErrorCodeToString<RetrieveSystemDnsError> {
    std::string operator()(RetrieveSystemDnsError code) {
        // clang-format off
        switch (code) {
        case AE_INIT: return "res_ninit()";
        }
        // clang-format on
    }
};
#endif // ifdef __ANDROID__

// clang-format off
template<>
struct ErrorCodeToString<utils::NetUtilsError> {
    std::string operator()(utils::NetUtilsError e) {
        switch (e) {
        case decltype(e)::AE_INVALID_IF_INDEX: return "if_indextoname() error";
        case decltype(e)::AE_UNSUPPORTED_FAMILY: return "Unsupported socket family";
        case decltype(e)::AE_BIND_ERROR: return "Failed to bind";
        case decltype(e)::AE_INVALID_IF_NAME: return "Invalid interface name";
        case decltype(e)::AE_IPV6_PORT_EMPTY: return "Port after colon is empty in IPv6 address";
        case decltype(e)::AE_IPV6_MISSING_RIGHT_BRACKET: return "IPv6 address contains `[` but not contains `]`";
        case decltype(e)::AE_IPV6_MISSING_BRACKETS: return "IPv6 address not in square brackets";
        case decltype(e)::AE_IPV4_PORT_EMPTY: return "Port after colon is empty in IPv4 address";
        }
    }
};
// clang-format on

} // namespace ag
