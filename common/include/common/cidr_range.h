#pragma once

#include <vector>
#include <cstdint>
#include <optional>
#include <list>
#include <cstddef>

#include "common/defs.h"
#include "common/utils.h"
#include "common/error.h"

namespace ag {

enum CidrErrorCode {
    IPV4_INVALID,
    IPV6_SHORT,
    IPV6_BAD_COLON,
};

template<>
struct ErrorCodeToString<CidrErrorCode> {
    std::string operator()(CidrErrorCode code) {
        switch (code) {
            case IPV4_INVALID: return "Invalid ipv4 address";
            case IPV6_SHORT: return "Can't parse IPv6 address: ambiguous short address";
            case IPV6_BAD_COLON: return "Can't parse IPv6 address: bad colon count";
        }
    }
};

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
     * ErrString
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
        size_t prefix_len = has_prefix_len ? utils::to_integer<size_t>(cidr_parts[1]).value()
                : address.value().size() * 8;
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
                m_mask[i] = (uint8_t)0xff;
            }
            if (prefix_len_significant_byte < addr.size()) {
                int shift = 8 - (prefix_len - prefix_len_significant_byte * 8);
                m_mask[prefix_len_significant_byte] = (uint8_t)(0xff << shift);
            }
        }
        for (size_t i = 0; i < addr.size(); i++) {
            m_address[i] = (uint8_t)(addr[i] & m_mask[i]);
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
                addr_right[i] |= (uint8_t)(0x1 << shift);
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
    static std::vector<CidrRange>
    exclude(const CidrRange &original_range, const std::vector<CidrRange> &excluded_ranges) {
        return exclude(std::vector<CidrRange>({original_range}), excluded_ranges);
    }

    /**
     * Exclude single range from original ranges
     * @param original_ranges List of original ranges
     * @param excluded_range List of excluded ranges
     * @return List of resulting ranges that cover all IPs from original ranges excluding all IPs from excluded ranges
     */
    static std::vector<CidrRange>
    exclude(const std::vector<CidrRange> &original_ranges, const CidrRange &excluded_range) {
        return exclude(original_ranges, std::vector<CidrRange>({excluded_range}));
    }

    /**
     * Exclude given ranges from original ranges
     * @param original_ranges List of original ranges
     * @param excluded_ranges List of excluded ranges
     * @return List of resulting ranges that cover all IPs from original ranges excluding all IPs from excluded ranges.
     */
    static std::vector<CidrRange>
    exclude(const std::vector<CidrRange> &original_ranges, const std::vector<CidrRange> &excluded_ranges) {
        std::vector<CidrRange> done;
        std::list<CidrRange> stack{original_ranges.begin(), original_ranges.end()};
        stack.sort();
        while (!stack.empty()) {
            CidrRange range = stack.front();
            stack.pop_front();
            bool split = false;
            bool skip = false;
            for (const CidrRange &excluded_range: excluded_ranges) {
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

private:
    static int count_matches(std::string_view str, std::string_view needle) {
        int count = 0;
        size_t pos = 0;
        while ((pos = str.find(needle, pos)) != std::string_view::npos) {
            ++count;
            pos += needle.size();
        }
        return count;
    }

    static std::string repeat(std::string_view str, std::string_view separator, int times) {
        std::string out;
        for (int i = 0; i < times; i++) {
            if (i != 0) {
                out += separator;
            }
            out += str;
        }
        return out;
    }

public:
    /**
     * Utility method for expanding IPv6 address from short form
     * @param address_string IPv6 address string (may be in short form)
     * @return Expanded IPv6 address string or error if it is occurred
     */
    static Result<std::string, CidrErrorCode> expand_ipv6_string(std::string address_string) {
        size_t four_dots_index = address_string.find("::");
        if (four_dots_index != std::string::npos) {
            if (address_string.length() == 2) {
                return Result<std::string, CidrErrorCode>(repeat("0", ":", 8));
            }
            int count = count_matches(address_string, ":");
            if (four_dots_index == 0) {
                std::string zeros = repeat("0", ":", 8 - count + 1);
                address_string = zeros + address_string.substr(1);
            } else if (four_dots_index == address_string.length() - 2) {
                std::string zeros = repeat("0", ":", 8 - count + 1);
                address_string = address_string.substr(0, four_dots_index + 1) + zeros;
            } else {
                std::string zeros = repeat("0", ":", 8 - count);
                address_string =
                        address_string.substr(0, four_dots_index + 1) + zeros +
                        address_string.substr(four_dots_index + 1);
            }
        }
        if (address_string.find("::") != std::string::npos) {
            return Result<std::string, CidrErrorCode>(make_error(CidrErrorCode::IPV6_SHORT));
        }
        return Result<std::string, CidrErrorCode>(address_string);
    }

    /**
     * Utility method for converting IPv6 address string to short form
     * @param address_string Address string
     * @return Address string in short form or error if it is occurred
     */
    static Result<std::string, CidrErrorCode> shorten_ipv6_string(const std::string &address_string) {
        // Expand string
        auto expanded = expand_ipv6_string(address_string);
        if (expanded.has_error()) {
            return Result<std::string, CidrErrorCode>(expanded.error());
        }

        // Find start and end of the series
        auto address_string_parts = utils::split_by(expanded.value(), ':');
        int start_series = 8;
        for (int i = 0; i < 8; i++) {
            while (address_string_parts[i].size() > 1 && address_string_parts[i][0] == '0') {
                address_string_parts[i] = address_string_parts[i].substr(1);
            }
        }
        for (int i = 0; i < 8; i++) {
            if (address_string_parts[i] == "0") {
                start_series = i;
                break;
            }
        }
        int end_series = 7;
        for (int i = start_series + 1; i < 8; i++) {
            if (address_string_parts[i] != "0") {
                end_series = i - 1;
                break;
            }
        }

        // Build result
        std::vector<std::string_view> parts{address_string_parts.begin(), address_string_parts.begin() + start_series};
        if (start_series < 8) {
            parts.emplace_back("");
        }
        if (end_series < 8) {
            std::copy(address_string_parts.begin() + end_series + 1, address_string_parts.begin() + 8,
                      std::back_inserter(parts));
        }
        return Result<std::string, CidrErrorCode>(AG_FMT("{}{}{}", (start_series == 0 ? ":" : ""),
                       utils::join(parts.begin(), parts.end(), ":"),
                       (end_series == 7 ? ":" : "")));
    }

    /**
     * Utility method for getting address bytes from string
     * @param address_string Address string (IPv4 or IPv6)
     * @return Address bytes or error if it is occurred
     */
    static Result<Uint8Vector, CidrErrorCode> get_address_from_string(std::string_view address_string) {
        if (address_string.find('.') != std::string_view::npos) {
            if (address_string.find(':') == std::string_view::npos) {
                return get_ipv4_address_from_string(address_string);
            } else {
                int last_index = address_string.rfind(':');
                std::string_view address_string_ipv4 = address_string.substr(last_index + 1);
                auto address = get_ipv4_address_from_string(address_string_ipv4);
                if (address.has_error()) {
                    return Result<Uint8Vector, CidrErrorCode>(address.error());
                }
                std::string address_string_part_before_ipv4 = std::string(address_string.substr(0, last_index + 1));
                return get_ipv6_address_from_string(
                        AG_FMT("{}{:x}:{:x}", address_string_part_before_ipv4,
                               ((address.value()[0] & 0xff) << 8) + (address.value()[1] & 0xff),
                               ((address.value()[2] & 0xff) << 8) + (address.value()[3] & 0xff)));
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
    static Result<Uint8Vector, CidrErrorCode> get_ipv6_address_from_string(std::string_view address_string) {
        auto expanded = expand_ipv6_string(std::string(address_string));
        if (expanded.has_error()) {
            return Result<Uint8Vector, CidrErrorCode>(expanded.error());
        }
        if (count_matches(expanded.value(), ":") != 7) {
            return Result<Uint8Vector, CidrErrorCode>(make_error(IPV6_BAD_COLON));
        }
        auto address = Uint8Vector(16);
        auto ip_sec = utils::split_by(expanded.value(), ':');
        for (int k = 0; k < 8; k++) {
            int two_bytes = utils::to_integer<int>(ip_sec[k], 16).value();
            address[2 * k] = (uint8_t)((two_bytes >> 8) & 0xff);
            address[2 * k + 1] = (uint8_t)(two_bytes & 0xff);
        }
        return Result<Uint8Vector, CidrErrorCode>(address);
    }

    /**
     * Private utility method for getting address bytes from string
     * @param address_string Address string (IPv4)
     * @return Address bytes or error if it is occurred
     * @see CidrRange#get_address_from_string()
     */
    static Result<Uint8Vector, CidrErrorCode> get_ipv4_address_from_string(std::string_view address_string) {
        auto address = Uint8Vector(4);
        auto addr_splitted = utils::split_by(address_string, '.');
        std::vector<std::string_view> ip_sec{addr_splitted.begin(), addr_splitted.end()};
        if (ip_sec.size() != 4) {
            return Result<Uint8Vector, CidrErrorCode>(make_error(IPV4_INVALID));
        }
        for (int k = 0; k < 4; k++) {
            address[k] = utils::to_integer<uint8_t>(ip_sec[k]).value();
        }
        return Result<Uint8Vector, CidrErrorCode>(address);
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
        if (!m_error.empty()) {
            return m_error;
        }
        if (m_address.size() == 4) {
            return AG_FMT("{}.{}.{}.{}",
                          m_address[0],
                          m_address[1],
                          m_address[2],
                          m_address[3]);
        } else {
            auto shortened = shorten_ipv6_string(AG_FMT("{:x}:{:x}:{:x}:{:x}:{:x}:{:x}:{:x}:{:x}",
                                                        ((m_address[0]) << 8) + (m_address[1]),
                                                        ((m_address[2]) << 8) + (m_address[3]),
                                                        ((m_address[4]) << 8) + (m_address[5]),
                                                        ((m_address[6]) << 8) + (m_address[7]),
                                                        ((m_address[8]) << 8) + (m_address[9]),
                                                        ((m_address[10]) << 8) + (m_address[11]),
                                                        ((m_address[12]) << 8) + (m_address[13]),
                                                        ((m_address[14]) << 8) + (m_address[15])));
            return shortened.has_error() ? shortened.error()->str() : shortened.value();
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
                return (m_address[i] & 0xff) < (range.m_address[i] & 0xff);
            }
        }
        return m_prefix_len < range.m_prefix_len;
    }

    [[nodiscard]] bool valid() const {
        return m_error.empty();
    }
};

} // namespace ag