#pragma once

#include <string_view>
#include <vector>
#include <event2/util.h> // for sockaddr, sockaddr_storage, getaddrinfo, getnameinfo
#ifdef __ANDROID__
#include <netinet/in.h> // not building on android if not included
#endif // __ANDROID__

#include "common/defs.h"

namespace ag {

/**
 * Socket address (IP address and port)
 */
class SocketAddress {
public:
    SocketAddress();

    /**
     * @param numeric_host String containing the IP address
     * @param port         Port number
     */
    SocketAddress(std::string_view numeric_host, uint16_t port);
    /**
     * @param addr Vector containing IP address bytes. Should be 4 or 16 bytes length
     * @param port Port number
     */
    SocketAddress(ag::Uint8View addr, uint16_t port);

    /**
     * @param addr IP address variant (4 bytes length array, 16 bytes length array or std::monostate)
     * @param port Port number
     */
    SocketAddress(const ag::IpAddress &addr, uint16_t port);
    /**
     * @param addr C sockaddr struct
     */
    explicit SocketAddress(const sockaddr* addr);

    bool operator<(const SocketAddress &other) const;
    bool operator==(const SocketAddress &other) const;
    bool operator!=(const SocketAddress &other) const;

    /**
     * @return Pointer to sockaddr_storage structure
     */
    const sockaddr *c_sockaddr() const;

    /**
     * @return sizeof(sockaddr_in) for IPv4 and sizeof(sockaddr_in6) for IPv6
     */
    ev_socklen_t c_socklen() const;

    /**
     * @return IP address bytes
     */
    Uint8View addr() const;

    /**
     * @return If this is an IPv4-mapped address, return the IPv4 address bytes, otherwise, same as `addr()`
     */
    Uint8View addr_unmapped() const;

    /**
     * @return IP address variant
     */
    IpAddress addr_variant() const;

    /**
     * @return Port number
     */
    uint16_t port() const;

    /**
     * @return String containing IP address
     */
    std::string host_str() const;

    /**
     * @return String containing IP address and port
     */
    std::string str() const;

    /**
     * @return True if IP is valid (AF_INET or AF_INET6)
     */
    bool valid() const;

    /**
     * @return True if address family is AF_INET or address is IPv4 mapped
     */
    bool is_ipv4() const;

    /**
     * @return True if address is IPv4 mapped
     */
    bool is_ipv4_mapped() const;

    /**
     * @return True if address family is AF_INET6
     */
    bool is_ipv6() const;

    /**
     * Cast address to target address family.
     * IPv4 addresses are mapped/unmapped automatically.
     * If address cannot be cast, return invalid address
     * @param family Target address family
     * @return Address of target address family
     */
    SocketAddress socket_family_cast(int family) const;

    /**
     * Set the `port` (in the host byte order).
     */
    void set_port(uint16_t port);

private:
    /** sockaddr_storage structure. Internally this is just sockaddr_storage wrapper */
    sockaddr_storage m_ss;

    ag::SocketAddress to_ipv4_unmapped() const;
    ag::SocketAddress to_ipv4_mapped() const;
};

} // namespace ag

namespace std {
template<>
struct hash<ag::SocketAddress> {
    size_t operator()(const ag::SocketAddress &address) const {
        std::string_view bytes = {(const char *) address.c_sockaddr(), (size_t) address.c_socklen()};
        return std::hash<std::string_view>{}(bytes);
    }
};
} // namespace std
