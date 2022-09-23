#pragma once

#include <chrono>
#include <ctime>
#include <event2/event.h>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

#include "common/defs.h"
#include "common/error.h"
#include "common/socket_address.h"

namespace ag {
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
#else
static constexpr auto AG_ETIMEDOUT = WSAETIMEDOUT;
static constexpr auto AG_ECONNREFUSED = WSAECONNREFUSED;
#endif

enum TransportProtocol {
    TP_UDP,
    TP_TCP,
};

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

} // namespace utils

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