#pragma once

#include <climits>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cuchar>
#include <ios>

#ifdef _WIN32
#include <basetsd.h>
typedef SSIZE_T ssize_t;
#endif

#include <array>
#include <bitset>
#include <chrono>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

#if defined(_LIBCPP_VERSION) && _LIBCPP_VERSION >= 180000

// LLVM removed non-standard-required char_traits specializations in 18.0.0.
// Add a specialization for `unsigned char` for use with `basic_string_view<uint8_t>` aka `Uint8View`.
namespace std {
template <>
struct char_traits<unsigned char> {
    using char_type = unsigned char; // NOLINT(*-identifier-naming)
    using int_type = int; // NOLINT(*-identifier-naming)
    using off_type = streamoff; // NOLINT(*-identifier-naming)
    using pos_type = streampos; // NOLINT(*-identifier-naming)
    using state_type = mbstate_t; // NOLINT(*-identifier-naming)
    using comparison_category = strong_ordering; // NOLINT(*-identifier-naming)

    static constexpr void assign(char_type &l, const char_type &r) noexcept {
        l = r;
    }
    static constexpr bool eq(char_type l, char_type r) noexcept {
        return l == r;
    }
    static constexpr bool lt(char_type l, char_type r) noexcept {
        return l < r;
    }

    static int compare(const char_type *l, const char_type *r, size_t s) noexcept {
        return strncmp((const char *) l, (const char *) r, s);
    }
    static size_t length(const char_type *s) noexcept {
        return strlen((const char *) s);
    }
    static const char_type *find(const char_type *haystack, size_t size, const char_type &needle) noexcept {
        return (const char_type *) memchr(haystack, (int_type) needle, size);
    }
    static char_type *move(char_type *dst, const char_type *src, size_t size) noexcept {
        return (char_type *) memmove(dst, src, size);
    }
    static char_type *copy(char_type *dst, const char_type *src, size_t size) noexcept {
        return (char_type *) memcpy(dst, src, size);
    }
    static constexpr char_type *assign(char_type *dst, size_t size, char_type c) noexcept {
        for (char_type *it = dst, *end = dst + size; it != end; ++it) {
            *it = c;
        }
        return dst;
    }

    static constexpr int_type not_eof(int_type c) noexcept {
        return eq_int_type(c, eof()) ? ~eof() : c;
    }
    static constexpr char_type to_char_type(int_type c) noexcept {
        return char_type(c);
    }
    static constexpr int_type to_int_type(char_type c) noexcept {
        return int_type((unsigned char) c);
    }
    static constexpr bool eq_int_type(int_type c1, int_type c2) noexcept {
        return c1 == c2;
    }
    static constexpr int_type eof() noexcept {
        return int_type(EOF);
    }
}; // char_traits<unsigned char>
} // namespace std

#endif

namespace ag {

// Functor template for zero-storage static deleters in unique_ptr
template<auto func>
using Ftor = std::integral_constant<decltype(func), func>;

using SystemClock = std::chrono::system_clock;
using SystemTime = SystemClock::time_point;
using Secs = std::chrono::seconds;
using Millis = std::chrono::milliseconds;
using Micros = std::chrono::microseconds;
using Nanos = std::chrono::nanoseconds;

using Uint8Span = std::span<uint8_t>;
using Uint8View = std::basic_string_view<uint8_t>;
using Uint8Vector = std::vector<uint8_t>;
template <typename K, typename V>
using HashMap = std::unordered_map<K, V>;
template <typename K>
using HashSet = std::unordered_set<K>;
template<size_t S>
using Uint8Array = std::array<uint8_t, S>;

template<typename T, auto D>
using UniquePtr = std::unique_ptr<T, Ftor<D>>;

template<typename T>
using AllocatedPtr = UniquePtr<T, &std::free>;

constexpr size_t IPV4_ADDRESS_SIZE = 4;
constexpr size_t IPV6_ADDRESS_SIZE = 16;
using Ipv4Address = Uint8Array<IPV4_ADDRESS_SIZE>;
using Ipv6Address = Uint8Array<IPV6_ADDRESS_SIZE>;
using IpAddress = std::variant<std::monostate, Ipv4Address, Ipv6Address>;

/** Network interface name or index */
using IfIdVariant = std::variant<std::monostate, uint32_t, std::string>;

// Convenient struct to tie a value and its mutex together
template<typename T, typename Mutex = std::mutex>
struct WithMtx {
    T val;
    Mutex mtx;
};

template <typename T>
constexpr size_t width_of() {
    return sizeof(T) * CHAR_BIT;
}

template <typename Enum, typename = std::enable_if_t<std::is_enum_v<Enum>>>
using EnumSet = std::bitset<width_of<Enum>()>;

} // namespace ag
