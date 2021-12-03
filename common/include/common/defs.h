#pragma once

#include <cstdint>
#include <cstddef>

#ifdef _WIN32
#include <basetsd.h>
typedef SSIZE_T ssize_t;
#endif

#include <array>
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

namespace ag {

// Functor template for zero-storage static deleters in unique_ptr
template<auto func>
using Ftor = std::integral_constant<decltype(func), func>;

using OptString = std::optional<std::string>;
using ErrString = OptString;
using OptStringView = std::optional<std::string_view>;
using ErrStringView = OptStringView;
using Uint8View = std::basic_string_view<uint8_t>;
using Uint8Vector = std::vector<uint8_t>;
template <typename K, typename V>
using HashMap = std::unordered_map<K, V>;
template <typename K>
using HashSet = std::unordered_set<K>;
template<size_t S>
using Uint8Array = std::array<uint8_t, S>;

template<typename T>
using AllocatedPtr = std::unique_ptr<T, Ftor<&std::free>>;

template<typename T, auto D>
using UniquePtr = std::unique_ptr<T, Ftor<D>>;

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

} // namespace ag
