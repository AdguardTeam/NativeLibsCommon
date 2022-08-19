#include "common/socket_address.h"
#include "common/utils.h"
#include <cstring>
#include <string>

#ifdef _WIN32
#include <ws2tcpip.h>
#endif

namespace ag {

static constexpr uint8_t IPV4_MAPPED_PREFIX[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff};

static size_t c_socklen(const sockaddr *addr) {
    return addr->sa_family == AF_INET6 ? sizeof(sockaddr_in6) : addr->sa_family == AF_INET ? sizeof(sockaddr_in) : 0;
}

bool SocketAddress::operator<(const SocketAddress &other) const {
    return std::memcmp(&m_ss, &other.m_ss, c_socklen()) < 0;
}

bool SocketAddress::operator==(const SocketAddress &other) const {
    return std::memcmp(&m_ss, &other.m_ss, c_socklen()) == 0;
}

bool SocketAddress::operator!=(const SocketAddress &other) const {
    return !operator==(other);
}

const sockaddr *SocketAddress::c_sockaddr() const {
    return reinterpret_cast<const sockaddr *>(&m_ss);
}

ev_socklen_t SocketAddress::c_socklen() const {
    return ::ag::c_socklen((const sockaddr *) &m_ss);
}

SocketAddress::SocketAddress()
        : m_ss{} {
}

SocketAddress::SocketAddress(const sockaddr *addr)
        : m_ss{} {

    if (addr) {
        std::memcpy(&m_ss, addr, ::ag::c_socklen(addr));
    }
}

static sockaddr_storage make_sockaddr_storage(Uint8View addr, uint16_t port) {
    sockaddr_storage ss{};
    if (addr.size() == 16) {
        auto *sin6 = (sockaddr_in6 *) &ss;
#ifdef SIN6_LEN // Platform with sin*_lens should have this macro
        sin6->sin6_len = sizeof(sockaddr_in6);
#endif // SIN6_LEN
        sin6->sin6_family = AF_INET6;
        sin6->sin6_port = htons(port);
        std::memcpy(&sin6->sin6_addr, addr.data(), addr.size());
    } else if (addr.size() == 4) {
        auto *sin = (sockaddr_in *) &ss;
#ifdef SIN6_LEN // Platform with sin*_lens should have this macro
        sin->sin_len = sizeof(sockaddr_in);
#endif // SIN6_LEN
        sin->sin_family = AF_INET;
        sin->sin_port = htons(port);
        std::memcpy(&sin->sin_addr, addr.data(), addr.size());
    }
    return ss;
}

static sockaddr_storage make_sockaddr_storage(std::string_view numeric_host, uint16_t port) {
    char p[INET6_ADDRSTRLEN];
    if (numeric_host.size() > sizeof(p) - 1) {
        return {};
    }
    memcpy(p, numeric_host.data(), numeric_host.size());
    p[numeric_host.size()] = '\0';

    Ipv6Address ip;
    if (1 == evutil_inet_pton(AF_INET, p, ip.data())) {
        return make_sockaddr_storage({ip.data(), IPV4_ADDRESS_SIZE}, port);
    } else if (1 == evutil_inet_pton(AF_INET6, p, ip.data())) {
        return make_sockaddr_storage({ip.data(), IPV6_ADDRESS_SIZE}, port);
    }

    return {};
}

SocketAddress::SocketAddress(std::string_view numeric_host, uint16_t port)
        : m_ss{make_sockaddr_storage(numeric_host, port)} {
}

SocketAddress::SocketAddress(Uint8View addr, uint16_t port)
        : m_ss{make_sockaddr_storage(addr, port)} {
}

Uint8View SocketAddress::addr() const {
    switch (m_ss.ss_family) {
    case AF_INET: {
        auto &sin = (const sockaddr_in &) m_ss;
        return {(uint8_t *) &sin.sin_addr, IPV4_ADDRESS_SIZE};
    }
    case AF_INET6: {
        auto &sin6 = (const sockaddr_in6 &) m_ss;
        return {(uint8_t *) &sin6.sin6_addr, IPV6_ADDRESS_SIZE};
    }
    default:
        return {};
    }
}

Uint8View SocketAddress::addr_unmapped() const {
    if (!is_ipv4_mapped()) {
        return addr();
    }
    auto *addr = (uint8_t *) &((const sockaddr_in6 *) &m_ss)->sin6_addr;
    return {addr + std::size(IPV4_MAPPED_PREFIX), IPV4_ADDRESS_SIZE};
}

IpAddress SocketAddress::addr_variant() const {
    switch (m_ss.ss_family) {
    case AF_INET: {
        auto &sin = (const sockaddr_in &) m_ss;
        return utils::to_array<IPV4_ADDRESS_SIZE>((const uint8_t *) &sin.sin_addr);
    }
    case AF_INET6: {
        auto &sin6 = (const sockaddr_in6 &) m_ss;
        return utils::to_array<IPV6_ADDRESS_SIZE>((const uint8_t *) &sin6.sin6_addr);
    }
    default:
        return std::monostate{};
    }
}

uint16_t SocketAddress::port() const {
    switch (m_ss.ss_family) {
    case AF_INET6:
        return ntohs(((const sockaddr_in6 &) m_ss).sin6_port);
    case AF_INET:
        return ntohs(((const sockaddr_in &) m_ss).sin_port);
    default:
        return 0;
    }
}

std::string SocketAddress::host_str() const {
    char host[INET6_ADDRSTRLEN] = "";
    getnameinfo(c_sockaddr(), c_socklen(), host, sizeof(host), nullptr, 0, NI_NUMERICHOST);
    if (m_ss.ss_family == AF_INET6) {
        return AG_FMT("[{}]", host);
    }
    return host;
}

std::string SocketAddress::str() const {
    char port[6] = "0";
    getnameinfo(c_sockaddr(), c_socklen(), nullptr, 0, port, sizeof(port), NI_NUMERICSERV);
    return AG_FMT("{}:{}", host_str(), port);
}

bool SocketAddress::valid() const {
    return m_ss.ss_family != AF_UNSPEC;
}

bool SocketAddress::is_ipv6() const {
    return m_ss.ss_family == AF_INET6;
}

bool SocketAddress::is_ipv4() const {
    return m_ss.ss_family == AF_INET || is_ipv4_mapped();
}

bool SocketAddress::is_ipv4_mapped() const {
    return m_ss.ss_family == AF_INET6
            && !memcmp(&((sockaddr_in6 *) &m_ss)->sin6_addr, IPV4_MAPPED_PREFIX, sizeof(IPV4_MAPPED_PREFIX));
}

SocketAddress SocketAddress::to_ipv4_unmapped() const {
    if (m_ss.ss_family == AF_INET) {
        return *this;
    }
    if (!is_ipv4_mapped()) {
        return {};
    }
    Uint8View v4 = addr();
    v4.remove_prefix(sizeof(IPV4_MAPPED_PREFIX));
    return {v4, port()};
}

SocketAddress SocketAddress::to_ipv4_mapped() const {
    if (m_ss.ss_family == AF_INET6) {
        return *this;
    }
    if (m_ss.ss_family != AF_INET) {
        return {};
    }
    uint8_t mapped[sizeof(in6_addr)];
    memcpy(mapped, IPV4_MAPPED_PREFIX, sizeof(IPV4_MAPPED_PREFIX));
    Uint8View v4 = addr();
    memcpy(mapped + sizeof(IPV4_MAPPED_PREFIX), v4.data(), v4.size());
    return SocketAddress({mapped, sizeof(mapped)}, port());
}

SocketAddress SocketAddress::socket_family_cast(int family) const {
    if (family == AF_INET) {
        return to_ipv4_unmapped();
    } else if (family == AF_INET6) {
        return to_ipv4_mapped();
    } else {
        return {};
    }
}

void SocketAddress::set_port(uint16_t port) {
    switch (m_ss.ss_family) {
    case AF_INET:
        ((sockaddr_in *) &m_ss)->sin_port = htons(port);
        break;
    case AF_INET6:
        ((sockaddr_in6 *) &m_ss)->sin6_port = htons(port);
        break;
    default:
        break;
    }
}

} // namespace ag
