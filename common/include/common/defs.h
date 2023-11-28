#pragma once

#include <cstdint>
#include <cstddef>
#include <limits.h>

#ifdef _WIN32
#include <basetsd.h>
typedef SSIZE_T ssize_t;
#endif

#include <array>
#include <bitset>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>
#include <chrono>

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
