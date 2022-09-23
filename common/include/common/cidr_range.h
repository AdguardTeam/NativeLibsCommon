#pragma once

#ifdef _WIN32
// clang format off
#include <ws2tcpip.h>
#include <ws2ipdef.h>
#include <windns.h>
#include <iphlpapi.h>
// clang format on
#else
#include <arpa/inet.h>
#endif
#include <cstddef>
#include <cstdint>
#include <list>
#include <optional>
#include <vector>

#include "common/defs.h"
#include "common/error.h"
#include "common/utils.h"

namespace ag {

enum class CidrError {
    AE_IP_ADDR_TOO_LONG,
    AE_PARSE_NET_STRING_ERROR,
};

// clang-format off
template<>
struct ErrorCodeToString<CidrError> {
    std::string operator()(CidrError e) {
        switch (e) {
            case decltype(e)::AE_IP_ADDR_TOO_LONG: return "Address string too long";
            case decltype(e)::AE_PARSE_NET_STRING_ERROR: return "Failed to parse string to network address";
        }
    }
};
// clang-format on

class CidrRange {
    /**
     * Address bytes
     */
    Uint8Vector m_address;
    /**
     * Mask bytes
     */
    Uint8Vector m_mask;
    /**
     * Cached prefix length
     */
    size_t m_prefix_len = 0;
    /**
     * Error string
     */
    std::string m_error;

public:
    /**
     * Constructs CIDR range from string
     * @param cidr_string String of some CIDR range
     */
    explicit CidrRange(std::string_view cidr_string) {
        if (cidr_string.empty()) {
            m_error = "Empty CIDR range string";
            return;
        }
        cidr_string = utils::trim(cidr_string);
        auto cidr_parts = utils::split2_by(cidr_string, '/');
        bool has_prefix_len = !cidr_parts[1].empty();
        std::string_view address_string = cidr_parts.front();
        auto address = get_address_from_string(address_string);
        if (address.has_error()) {
            m_error = address.error()->str();
            return;
        }
        size_t prefix_len
                = has_prefix_len ? utils::to_integer<size_t>(cidr_parts[1]).value() : address.value().size() * 8;
        init(address.value(), prefix_len);
    }

    /**
     * Constructs CIDR range from string and prefix length
     * @param address_string String containing IP address
     * @param prefix_len Prefix length
     */
    CidrRange(std::string_view address_string, size_t prefix_len) {
        auto address = get_address_from_string(address_string);
        if (address.has_error()) {
            m_error = address.error()->str();
            return;
        }
        init(address.value(), prefix_len);
    }

    /**
     * Constructs CIDR range from address bytes and prefix length
     * @param address Address bytes
     * @param prefix_len Prefix length
     */
    CidrRange(Uint8View address, size_t prefix_len) {
        init(address, prefix_len);
    }

    /**
     * Constructs CIDR range from address bytes and prefix length
     * @param address Address bytes
     * @param prefix_len Prefix length
     */
    CidrRange(const Uint8Vector &address, size_t prefix_len) {
        init(address, prefix_len);
    }

private:
    void init(const Uint8Vector &address, size_t prefix_len) {
        init({address.data(), address.size()}, prefix_len);
    }

    void init(Uint8View addr, size_t prefix_len) {
        if (prefix_len > addr.size() * 8) {
            m_error = "Invalid prefix length";
            return;
        }
        m_address = Uint8Vector(addr.size());
        m_prefix_len = prefix_len;
        m_mask = Uint8Vector(addr.size());
        if (prefix_len != 0) {
            size_t prefix_len_significant_byte = (prefix_len - 1) / 8;
            for (size_t i = 0; i < prefix_len_significant_byte; i++) {
                m_mask[i] = (uint8_t) 0xff;
            }
            if (prefix_len_significant_byte < addr.size()) {
                int shift = 8 - (prefix_len - prefix_len_significant_byte * 8);
                m_mask[prefix_len_significant_byte] = (uint8_t) (0xff << shift);
            }
        }
        for (size_t i = 0; i < addr.size(); i++) {
            m_address[i] = (uint8_t) (addr[i] & m_mask[i]);
        }
    }

public:
    /**
     * Checks if this range contains given range
     * @param range CIDR range
     * @return True if this range contains given range, false otherwise
     */
    [[nodiscard]] bool contains(const CidrRange &range) const {
        if (range.m_address.size() != m_address.size()) {
            return false;
        }
        for (size_t i = 0; i < m_address.size(); i++) {
            if (m_mask[i] != (range.m_mask[i] & m_mask[i])) {
                return false;
            }
            if ((m_address[i] & m_mask[i]) != (range.m_address[i] & m_mask[i])) {
                return false;
            }
        }
        return true;
    }

    /**
     * Splits this range into two with prefixLen increased by one
     * @return Pair of CIDR ranges or null if this range is single IP
     */
    std::optional<std::pair<CidrRange, CidrRange>> split() {
        if (m_prefix_len == m_address.size() * 8) {
            // Can't split single IP
            return std::nullopt;
        }

        size_t new_prefix_len = m_prefix_len + 1;
        auto addr_left = Uint8Vector(m_address.size());
        auto addr_right = Uint8Vector(m_address.size());
        size_t prefix_len_significant_byte = (new_prefix_len - 1) / 8;
        for (size_t i = 0; i < m_address.size(); i++) {
            addr_left[i] = m_address[i];
            addr_right[i] = m_address[i];
            if (i == prefix_len_significant_byte) {
                int shift = 8 - (new_prefix_len - prefix_len_significant_byte * 8);
                addr_right[i] |= (uint8_t) (0x1 << shift);
            }
        }

        CidrRange cidr_left{addr_left, new_prefix_len};
        CidrRange cidr_right{addr_right, new_prefix_len};
        return std::make_pair(cidr_left, cidr_right);
    }

    /**
     * Exclude given range from single original range
     * @param original_range Single original range
     * @param excluded_range Single excluded range
     * @return List of resulting ranges that cover all IPs from original ranges excluding all IPs from excluded ranges
     */
    static std::vector<CidrRange> exclude(const CidrRange &original_range, const CidrRange &excluded_range) {
        return exclude(std::vector<CidrRange>({original_range}), std::vector<CidrRange>({excluded_range}));
    }

    /**
     * Exclude given ranges from single original range
     * @param original_range Single original range
     * @param excluded_ranges List of excluded ranges
     * @return List of resulting ranges that cover all IPs from original ranges excluding all IPs from excluded ranges
     */
    static std::vector<CidrRange> exclude(
            const CidrRange &original_range, const std::vector<CidrRange> &excluded_ranges) {
        return exclude(std::vector<CidrRange>({original_range}), excluded_ranges);
    }

    /**
     * Exclude single range from original ranges
     * @param original_ranges List of original ranges
     * @param excluded_range List of excluded ranges
     * @return List of resulting ranges that cover all IPs from original ranges excluding all IPs from excluded ranges
     */
    static std::vector<CidrRange> exclude(
            const std::vector<CidrRange> &original_ranges, const CidrRange &excluded_range) {
        return exclude(original_ranges, std::vector<CidrRange>({excluded_range}));
    }

    /**
     * Exclude given ranges from original ranges
     * @param original_ranges List of original ranges
     * @param excluded_ranges List of excluded ranges
     * @return List of resulting ranges that cover all IPs from original ranges excluding all IPs from excluded ranges.
     */
    static std::vector<CidrRange> exclude(
            const std::vector<CidrRange> &original_ranges, const std::vector<CidrRange> &excluded_ranges) {
        std::vector<CidrRange> done;
        std::list<CidrRange> stack{original_ranges.begin(), original_ranges.end()};
        stack.sort();
        while (!stack.empty()) {
            CidrRange range = stack.front();
            stack.pop_front();
            bool split = false;
            bool skip = false;
            for (const CidrRange &excluded_range : excluded_ranges) {
                if (excluded_range.contains(range)) {
                    skip = true;
                    break;
                }
                if (range.contains(excluded_range)) {
                    split = true;
                    break;
                }
            }
            if (skip) {
                continue;
            }
            if (split) {
                auto split_range = range.split();
                if (split_range) {
                    stack.push_front(split_range->second);
                    stack.push_front(split_range->first);
                }
            } else {
                done.push_back(range);
            }
        }
        return done;
    }

    /**
     * Get address bytes
     * @return Address bytes
     */
    [[nodiscard]] const Uint8Vector &get_address() const {
        return m_address;
    }

    /**
     * Get network mask bytes
     * @return Network mask bytes
     */
    [[nodiscard]] const Uint8Vector &get_mask() const {
        return m_mask;
    }

    /**
     * Get prefix length
     * @return Prefix length
     */
    [[nodiscard]] size_t get_prefix_len() const {
        return m_prefix_len;
    }

public:
    /**
     * Utility method for getting address bytes from string
     * @param address_string Address string (IPv4 or IPv6)
     * @return Address bytes or error if it is occurred
     */
    static Result<Uint8Vector, CidrError> get_address_from_string(std::string_view address_string) {
        if (address_string.find('.') != std::string_view::npos) {
            if (address_string.find(':') == std::string_view::npos) {
                return get_ipv4_address_from_string(address_string);
            } else {
                auto last_index = address_string.rfind(':');
                std::string_view address_string_ipv4 = address_string.substr(last_index + 1);
                auto address = get_ipv4_address_from_string(address_string_ipv4);
                if (address.has_error()) {
                    return address.error();
                }
                std::string address_string_part_before_ipv4 = std::string(address_string.substr(0, last_index + 1));
                return get_ipv6_address_from_string(AG_FMT("{}{:x}:{:x}", address_string_part_before_ipv4,
                        ((address.value()[0]) << 8) + (address.value()[1]),
                        ((address.value()[2]) << 8) + (address.value()[3])));
            }
        } else {
            return get_ipv6_address_from_string(address_string);
        }
    }

private:
    /**
     * Private utility method for getting address bytes from string
     * @param address_string Address string (IPv6)
     * @return Address bytes or error if it is occurred
     * @see CidrRange#get_address_from_string()
     */
    static Result<Uint8Vector, CidrError> get_ipv6_address_from_string(std::string_view address_string) {
        if (address_string.size() >= INET6_ADDRSTRLEN) {
            return make_error(CidrError::AE_IP_ADDR_TOO_LONG);
        }
        std::vector<uint8_t> address;
        address.resize(IPV6_ADDRESS_SIZE);
#ifdef _WIN32
        NET_ADDRESS_INFO info{};
        if (ParseNetworkString(
                    ag::utils::to_wstring(address_string).data(), NET_STRING_IPV6_ADDRESS, &info, nullptr, nullptr)
                        == ERROR_SUCCESS
                && info.Format == NET_ADDRESS_IPV6) {
            std::memcpy(address.data(), &info.Ipv6Address.sin6_addr, IPV6_ADDRESS_SIZE);
        } else {
            return make_error(CidrError::AE_PARSE_NET_STRING_ERROR);
        }
#else
        if (inet_pton(AF_INET6, std::string{address_string}.c_str(), address.data()) != 1) {
            return make_error(CidrError::AE_PARSE_NET_STRING_ERROR, AG_FMT("{}", strerror(errno)));
        }
#endif
        return std::move(address);
    }

    /**
     * Private utility method for getting address bytes from string
     * @param address_string Address string (IPv4)
     * @return Address bytes or error if it is occurred
     * @see CidrRange#get_address_from_string()
     */
    static Result<Uint8Vector, CidrError> get_ipv4_address_from_string(std::string_view address_string) {
        if (address_string.size() >= INET_ADDRSTRLEN) {
            return make_error(CidrError::AE_IP_ADDR_TOO_LONG);
        }
        std::vector<uint8_t> address;
        address.resize(IPV4_ADDRESS_SIZE);
#ifdef _WIN32
        NET_ADDRESS_INFO info{};
        if (ParseNetworkString(
                    ag::utils::to_wstring(address_string).data(), NET_STRING_IPV4_ADDRESS, &info, nullptr, nullptr)
                        == ERROR_SUCCESS
                && info.Format == NET_ADDRESS_IPV4) {
            std::memcpy(address.data(), &info.Ipv4Address.sin_addr, IPV4_ADDRESS_SIZE);
        } else {
            return make_error(CidrError::AE_PARSE_NET_STRING_ERROR);
        }
#else
        if (inet_pton(AF_INET, std::string{address_string}.c_str(), address.data()) != 1) {
            return make_error(CidrError::AE_PARSE_NET_STRING_ERROR, AG_FMT("{}", strerror(errno)));
        }
#endif
        return std::move(address);
    }

public:
    bool operator==(const CidrRange &range) const {
        if (range.m_address.size() != m_address.size()) {
            return false;
        }
        if (range.m_prefix_len != m_prefix_len) {
            return false;
        }
        for (size_t i = 0; i < m_address.size(); i++) {
            if (m_address[i] != range.m_address[i]) {
                return false;
            }
        }
        return true;
    }

    /**
     * Returns IPv4 address as integer
     * @return Integer representation of IPv4 address or -1 if address is IPv6
     */
    uint32_t to_uint32() {
        if (m_address.size() == 4) {
            return (m_address[0] << 24) + (m_address[1] << 16) + (m_address[2] << 8) + m_address[3];
        } else {
            return -1;
        }
    }

    /**
     * Get address string
     * @return Address string
     */
    [[nodiscard]] std::string get_address_as_string() const {
        char buf[INET6_ADDRSTRLEN];
        if (!m_error.empty()) {
            return m_error;
        }
        if (m_address.size() == IPV4_ADDRESS_SIZE) {
            return inet_ntop(AF_INET, m_address.data(), buf, INET_ADDRSTRLEN);
        } else {
            return inet_ntop(AF_INET6, m_address.data(), buf, INET6_ADDRSTRLEN);
        }
    }

    [[nodiscard]] std::string to_string() const {
        return AG_FMT("{}/{}", get_address_as_string(), std::to_string(m_prefix_len));
    }

    bool operator<(const CidrRange &range) const {
        if (m_address.size() != range.m_address.size()) {
            return m_address.size() < range.m_address.size();
        }
        for (size_t i = 0; i < m_address.size(); i++) {
            if (m_address[i] != range.m_address[i]) {
                return m_address[i] < range.m_address[i];
            }
        }
        return m_prefix_len < range.m_prefix_len;
    }

    [[nodiscard]] bool valid() const {
        return m_error.empty();
    }
};

} // namespace ag