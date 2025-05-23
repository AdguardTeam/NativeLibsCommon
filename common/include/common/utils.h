#pragma once

#pragma GCC visibility push(default)
#include <future>
#pragma GCC visibility pop
#include <algorithm>
#include <array>
#include <functional>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <vector>
#include <cctype>
#include <charconv>
#include <istream>
#include <cstdio>
#include <utility>

#ifndef _WIN32
#include <sys/wait.h>
#endif

#include "common/defs.h"
#include "common/error.h"
#include "common/format.h"

/**
 * Macros to create constexpr value and type to check expression
 * @example AG_UTILS_DECLARE_CHECK_EXPRESSION(has_f, std::declval<T>().f)
 *          // Generates template<typename T> inline constexpr bool has_f;
 *          ...
 *          template<typename SomeType>
 *          void f() {
 *              static_assert(has_f<SomeType>, "Failed: SomeType::f does not exists");
 *          }
 */
#define AG_UTILS_DECLARE_CHECK_EXPRESSION(TRAITS_NAME, ...) \
namespace detail { \
template<typename TypeToCheck> \
struct TRAITS_NAME ## _impl { \
private: \
    template<typename T> \
    static auto test(void*) -> decltype(static_cast<void>(__VA_ARGS__), std::true_type{}); \
    template<typename> \
    static std::false_type test(...); \
public: \
    /* TODO use std::is_detected since C++20 */ \
    static constexpr auto value = decltype(test<TypeToCheck>(nullptr)){}; \
}; \
} \
template<typename T> \
struct TRAITS_NAME ## _type : std::bool_constant<detail::TRAITS_NAME ## _impl<T>::value> {}; \
template<typename T> \
inline constexpr bool TRAITS_NAME = TRAITS_NAME ## _type<T>::value;

/**
 * Macros to create constexpr value and type to check expression depended from number of parameters
 * @example AG_UTILS_DECLARE_CHECK_EXPRESSION(can_init, T((Is, ConvertibleToAny{})...))
 *          // Generates template<typename T, size_t N> inline constexpr bool can_init;
 *          ...
 *          template<typename SomeType>
 *          void f() {
 *              static_assert(can_init<SomeType, 2>, "Failed: Can't init with 2 params SomeType(arg1, arg2)");
 *          }
 */
#define AG_UTILS_DECLARE_CHECK_EXPRESSION_WITH_N(TRAITS_NAME, ...) \
namespace detail { \
template<typename TypeToCheck, size_t N> \
struct TRAITS_NAME ## _impl { \
private: \
    template<typename T, size_t... Is> \
    static auto test(void*, std::integer_sequence<size_t, Is...>) -> \
            decltype(static_cast<void>(__VA_ARGS__), std::true_type{}); \
    template<typename> \
    static std::false_type test(...); \
public: \
    static constexpr auto value = decltype(test<TypeToCheck>(nullptr, std::make_index_sequence<N>())){}; \
}; \
} \
template<typename T, size_t N> \
struct TRAITS_NAME ## _type : std::bool_constant<detail:: TRAITS_NAME ## _impl<T, N>::value> {}; \
template<typename T, size_t N> \
inline constexpr bool TRAITS_NAME = TRAITS_NAME ## _type<T, N>::value;

/**
 * Macros for fmt::format with compile-time checked FMT_STRING
 */
#define AG_FMT(FORMAT, ...) ag::format((FORMAT), __VA_ARGS__)

namespace ag {
namespace utils {

/**
 * Generates version 4 random uuid (according to RFC 4122)
 */
std::string generate_uuid();

/**
 * Check if the `rhs` is a subset of the `lhs`
 */
template<size_t N>
bool bitwise_includes(std::bitset<N> lhs, std::bitset<N> rhs) {
    return (lhs & rhs) == rhs;
}

/**
 * Just like `std::remove_if()`, but swaps elements to the tail instead of moving them
 */
template<typename Iterator, typename Predicate>
Iterator swap_remove_if(Iterator begin, Iterator end, Predicate p) {
    begin = std::find_if(begin, end, p);
    if (begin != end) {
        for (Iterator i = begin; ++i != end;) {
            if (!p(*i)) {
                std::swap(*begin++, *i);
            }
        }
    }
    return begin;
}

/**
 * Transform string to uppercase
 */
static inline std::string to_upper(std::string_view str) {
    std::string lwr;
    lwr.reserve(str.length());
    std::transform(str.cbegin(), str.cend(), std::back_inserter(lwr), (int (*)(int)) std::toupper);
    return lwr;
}
/**
 * Transform string to lowercase
 */
static inline std::string to_lower(std::string_view str) {
    std::string lwr;
    lwr.reserve(str.length());
    std::transform(str.cbegin(), str.cend(), std::back_inserter(lwr), (int (*)(int))std::tolower);
    return lwr;
}

/**
 * Return string view without whitespaces at the beginning of string
 * @param str String view
 */
static inline std::string_view ltrim(std::string_view str) {
    auto pos1 = std::find_if(str.begin(), str.end(), std::not_fn((int(*)(int))std::isspace));
    str.remove_prefix(std::distance(str.begin(), pos1));
    return str;
}

/**
 * Return string view without whitespaces at the end of string
 * @param str String view
 */
static inline std::string_view rtrim(std::string_view str) {
    auto pos2 = std::find_if(str.rbegin(), str.rend(), std::not_fn((int(*)(int))std::isspace));
    str.remove_suffix(std::distance(str.rbegin(), pos2));
    return str;
}

/**
 * Return string view without whitespaces at the beginning and at the end of string
 * @param str
 */
static inline std::string_view trim(std::string_view str) {
    return rtrim(ltrim(str));
}

/**
 * Check whether 2 strings are equal ignoring case
 */
static inline constexpr bool iequals(std::string_view lhs, std::string_view rhs) {
    return std::equal(lhs.begin(), lhs.end(), rhs.begin(), rhs.end(), [](char l, char r) {
        return std::tolower(l) == std::tolower(r);
    });
}

/**
 * Find the first occurrence of the given substring ignoring case
 * @return Position of the first character of the found substring or `npos` if no such substring is found.
 */
static inline constexpr std::string_view::size_type ifind(std::string_view haystack, std::string_view needle) {
    if (needle.empty()) {
        return 0;
    }
    if (haystack.empty()) {
        return std::string_view::npos;
    }

    auto c = std::tolower(needle.front());
    size_t pos = 0;
    do { // NOLINT(*-avoid-do-while)
        do { // NOLINT(*-avoid-do-while)
            if (haystack.length() <= pos || haystack.length() - pos < needle.length()) {
                return std::string_view::npos;
            }
        } while (std::tolower(haystack[pos++]) != c);
    } while (!iequals(haystack.substr(pos, needle.length() - 1), needle.substr(1)));

    return pos - 1;
}

/**
 * Check if string starts with prefix
 */
static inline constexpr bool starts_with(std::string_view str, std::string_view prefix) {
    return str.length() >= prefix.length()
            && 0 == str.compare(0, prefix.length(), prefix);
}

/**
 * Check if string ends with suffix
 */
static inline constexpr bool ends_with(std::string_view str, std::string_view suffix) {
    return str.length() >= suffix.length()
            && 0 == str.compare(str.length() - suffix.length(), suffix.length(), suffix);
}

/**
 * Check if `str` starts with `prefix` ignoring case.
 */
static inline constexpr bool istarts_with(std::string_view str, std::string_view prefix) {
    return str.length() >= prefix.length() && iequals(str.substr(0, prefix.length()), prefix);
}

/**
 * Check if `str` ends with `suffix` ignoring case.
 */
static inline constexpr bool iends_with(std::string_view str, std::string_view suffix) {
    return str.length() >= suffix.length()
            && iequals(str.substr(str.length() - suffix.length(), suffix.length()), suffix);
}

/**
 * Split string by delimiter
 */
std::vector<std::string_view> split_by(std::string_view str, int delim, bool include_empty = false, bool need_trim = true);
std::vector<std::string_view> split_by(std::string_view str, std::string_view delim, bool include_empty = false, bool need_trim = true);

/**
 * Split string by any character in delimiters
 */
std::vector<std::string_view> split_by_any_of(std::string_view str, std::string_view delim, bool include_empty = false, bool need_trim = true);

/**
 * Split string by first found delimiter for 2 parts
 */
std::array<std::string_view, 2> split2_by(std::string_view str, int delim, bool need_trim = true);

/**
 * Split string by last found delimiter for 2 parts
 */
std::array<std::string_view, 2> rsplit2_by(std::string_view str, int delim, bool need_trim = true);

/**
 * Split string into two parts at the first occurrence of any character in the provided delimiters.
 */
std::array<std::string_view, 2> split2_by_any_of(std::string_view str, std::string_view delim, bool need_trim = true);

/**
 * Splits string into pieces by chars which match the predicate
 * @param str String
 * @param p Predicate function
 * @param need_trim Whether need to trim whitespaces
 * @return List of strings
 */
template<class Predicate>
std::vector<std::string_view> split_if(std::string_view str, Predicate p, bool need_trim = true) {
    std::vector<std::string_view> strings;
    while (!str.empty()) {
        auto next = std::find_if(str.begin(), str.end(), p);
        size_t pos = (next != str.end()) ? std::distance(str.begin(), next) : str.length();
        std::string_view part = str.substr(0, pos);
        if (need_trim) {
            part = trim(part);
        }
        if (!part.empty()) {
            strings.push_back(part);
        }
        str.remove_prefix((std::min)(pos + 1, str.length()));
    }
    return strings;
}

/**
 * Check is T has `reserve(size_t{...})` member function or not
 * @example static_assert(has_reserve< std::vector<int> >, "std::vector<int> has reserve function");
 *          static_assert(has_reserve< std::list<int> > == false, "std::list<int> has no reserve function");
 */
AG_UTILS_DECLARE_CHECK_EXPRESSION(has_reserve, std::declval<T>().reserve(std::declval<size_t>()))

/**
 * Concatenate parts into a single container with result type R
 * @tparam R Result container type (required)
 */
template<typename R, typename T>
static inline R concat(const T &parts) {
    R result;
    if constexpr (has_reserve<R>) {
        size_t size = 0;
        for (const auto &p : parts) {
            size += std::size(p);
        }
        result.reserve(size);
    }
    for (const auto &p : parts) {
        result.insert(std::cend(result), std::cbegin(p), std::cend(p));
    }
    return result;
}

namespace detail {

template<typename T>
using IteratorFromBegin = decltype(std::begin(std::declval<T>()));

template<typename T>
using ValueTypeFromBegin = typename std::iterator_traits<IteratorFromBegin<T>>::value_type;

template<typename T, typename U>
using IsSameValueType = std::is_same<ValueTypeFromBegin<T>, U>;

template<typename T>
inline constexpr bool is_string_or_string_view = std::disjunction_v<IsSameValueType<T, std::string>,
                                                                    IsSameValueType<T, std::string_view>>;

} // namespace detail

/**
 * Concatenate parts into a single std::string
 * @param parts Container or C array with std::string or std::string_view
 */
template<typename T>
static inline std::enable_if_t<detail::is_string_or_string_view<T>, std::string> concat(const T &parts)
{
    return (concat<std::string>)(parts);
}

/**
 * Concatenate parts into a single container from comma-separated parts with possibly different types
 * @tparam R Result container type (required)
 * @param parts Comma-separated containers or C arrays (possibly with different types)
 * @return Container with type R with copy of data from parts
 */
template<typename R, typename... Ts>
static inline std::enable_if_t<sizeof...(Ts) >= 2, R> concat(const Ts&... parts) {
    R result;
    if constexpr (has_reserve<R>) {
        result.reserve((... + std::size(parts)));
    }
    (... , static_cast<void>(result.insert(std::cend(result), std::cbegin(parts), std::cend(parts))));
    return result;
}

/**
 * Joins vector of string to string with delimiter between parts
 * @param begin Begin iterator of vector of strings
 * @param end End iterator of vector of strings
 * @param delimiter String to separate parts
 * @return Joined string
 */
template<typename Iterator>
std::string join(Iterator begin, Iterator end, std::string_view delimiter) {
    std::string result;
    if (begin != end) {
        result.append(*begin);
        ++begin;
    }
    for (; begin != end; ++begin) {
        result.append(delimiter);
        result.append(*begin);
    }
    return result;
}

/**
 * Check if string is a valid IPv4 address
 */
bool is_valid_ip4(std::string_view str);

/**
 * Check if string is a valid IPv6 address
 */
bool is_valid_ip6(std::string_view str);

/**
 * Calculate hash of string
 */
static inline uint32_t hash(std::string_view str) {
    // DJB2 with XOR (Daniel J. Bernstein)
    uint32_t hash = 5381;
    for (size_t i = 0; i < str.length(); ++i) {
        hash = (hash * 33) ^ (uint32_t)str[i];
    }
    return hash;
}

/**
 * Calculate the hash of a byte slice
 */
static inline uint32_t hash(Uint8View v) {
    return hash({(const char *) v.data(), v.size()});
}

/**
 * Convert UTF-8 string to wide char string
 * @param sv UTF-8 string
 */
std::wstring to_wstring(std::string_view sv);

/**
 * Convert wide char string to UTF-8 string
 * @param wsv Wide char string
 */
std::string from_wstring(std::wstring_view wsv);

/**
 * Convert `str` to integer in base `base`. On failure, return `std::nullopt`.
 * The entirety of `str` must be a string representation of an integer in
 * range of the target type, without any leading or trailing whitespace.
 */
template <typename Int, std::enable_if_t<std::is_integral_v<Int>, int> = 0>
constexpr std::optional<Int> to_integer(std::string_view str, int base = 10) {
    Int result;
    const char *last_in = str.data() + str.size();
    auto[last_out, ec] = std::from_chars(str.data(), last_in, result, base);
    if (last_out == last_in && ec == std::errc()) {
        return result;
    }
    return std::nullopt;
}

namespace detail {

template<typename T>
auto data_from_begin(const T& value) {
    return &*std::begin(value);
}

template<typename T>
static inline constexpr auto make_string_view_impl(const T& value) {
    using ValueType = ValueTypeFromBegin<T>;
    return std::basic_string_view<ValueType>(detail::data_from_begin(value), std::size(value));
}

} // namespace detail

/**
 * Create string view from container or C array
 * @param value Value
 */
template<typename T>
static inline constexpr auto make_string_view(const T& value) {
    return detail::make_string_view_impl(value);
}

/**
 * Create string view from initializer list
 * @tparam T Value type (can be deduced)
 * @param value Value
 */
template<typename T>
static inline constexpr auto make_string_view(std::initializer_list<T> value) {
    return detail::make_string_view_impl(value);
}

/**
 * Create std::array from C array with known size S
 * @param value Value
 */
template<typename T, size_t S>
static inline auto to_array(const T (&value)[S]) {
    // TODO use std::to_array since C++20
    std::array<std::remove_cv_t<T>, S> result;
    std::copy(std::cbegin(value), std::cend(value), result.begin());
    return result;
}

/**
 * Create std::array from array with size S and type T
 * @param value Value
 */
template<size_t S, typename T>
static inline auto to_array(const T *value) {
    std::array<std::remove_cv_t<T>, S> result;
    std::copy(value, value + S, result.begin());
    return result;
}

/**
 * Conditionally returns optional or nullopt
 * @param condition Condition
 * @param value Value
 */
template<typename T>
static inline constexpr auto make_optional_if(bool condition, T&& value) {
    return condition ? std::make_optional(std::forward<T>(value)) : std::nullopt;
}

/**
 * Make unique ptr with std::free deleter
 * @param ptr Pointer to hold
 */
template<typename T>
static inline AllocatedPtr<T> make_allocated_unique(T *ptr) noexcept {
    return AllocatedPtr<T>{ptr};
}

/**
 * Timer measures time since create object
 */
class Timer {
public:
    /**
     * Returns elapsed time duration since create object
     * @tparam T Duration type
     */
    template<typename T>
    T elapsed() const {
        return std::chrono::duration_cast<T>(std::chrono::steady_clock::now() - m_start);
    }

    void reset() {
        m_start = std::chrono::steady_clock::now();
    }

private:
    std::chrono::steady_clock::time_point m_start = std::chrono::steady_clock::now();
};

/**
 * Like std::async(std::launch::async, f, vs...) but result future does not block on destructor
 * @param f Function to execute
 * @param vs Function parameters
 * @return Future with result of function
 */
template<typename F, typename... Ts, typename R = std::invoke_result_t<std::decay_t<F>, std::decay_t<Ts>...>>
std::future<R> async_detached(F&& f, Ts&&... vs) {
    std::packaged_task<R(std::decay_t<Ts>...)> packaged_task(std::forward<F>(f));
    auto future = packaged_task.get_future();
    std::thread(std::move(packaged_task), std::forward<Ts>(vs)...).detach();
    return future;
}

namespace detail {

struct ConvertibleToAny {
    template<typename T>
    operator T() const;
};

} // namespace detail

/**
 * Defines value list_initializable_with_n_params<T, N> to checks is possible to list init T{...with N params...}
 */
AG_UTILS_DECLARE_CHECK_EXPRESSION_WITH_N(list_initializable_with_n_params, T{(Is, ConvertibleToAny{})...})

namespace detail {

template<typename T, ssize_t C>
constexpr ssize_t list_init_params_count_impl(std::false_type) {
    return C - 1;
}

template<typename T, ssize_t C>
constexpr ssize_t list_init_params_count_impl(std::true_type) {
    return (list_init_params_count_impl<T, C + 1>)(list_initializable_with_n_params_type<T, C + 1>{});
}

} // namespace detail

/**
 * Count parameters to list init
 * @return If can't init with no parameters -1, first maximum parameters count to init otherwise
 */
template<typename T>
inline constexpr ssize_t list_init_params_count = detail::list_init_params_count_impl<T, 0>(
        list_initializable_with_n_params_type<T, 0>{});

/**
 * Calls the supplied function in destructor.
 * Useful to ensure cleanup if the control flow can exit the scope in multiple different ways.
 */
class ScopeExit {
private:
    std::function<void()> m_f;

public:
    explicit ScopeExit(std::function<void()> &&f) : m_f{std::move(f)} {}

    ~ScopeExit() {
        if (m_f) {
            m_f();
        }
    }
};

namespace detail {
// From boost 1.72
template <typename SizeT>
void hash_combine_impl(SizeT& seed, SizeT value) {
    seed ^= value + 0x9e3779b9 + (seed<<6) + (seed>>2);
}
} // namespace detail

/**
 * Compute and return the combined hash of objs
 * @param objs std::hash must be specialized for each of these objects
 */
template <typename... Ts>
size_t hash_combine(const Ts&... objs) {
    size_t seed = 0;
    (detail::hash_combine_impl(seed, std::hash<std::decay_t<Ts>>{}(objs)), ...);
    return seed;
}

/**
 * Function to be called from `for_each_line`
 * @param pos  position in containing data at which the line starts
 * @param line the line, trimmed of trailing and leading whitespace
 * @param arg  user argument
 * @return true line reading loop continues
 *         false line reading loop stops
 */
using LineAction = bool (*)(uint32_t pos, std::string_view line, void *arg);

/**
 * Apply user function to each line in string while user function returns true
 * @param str    string
 * @param action user function
 * @param arg    user argument
 * @return always zero
 */
int for_each_line(std::string_view str, LineAction, void *arg);

/**
 * Read a line at the given offset from a string
 * @param str string
 * @param pos offset
 * @return line, or nullopt in case of error
 */
std::optional<std::string_view> read_line(std::string_view str, size_t pos);

/**
 * @return Thread id
 */
uint32_t gettid(void);

/**
 * Encode input data to lowercase hexadecimal string
 * @param data Input buffer
 * @return Lowercase hexadecimal representation of input data
 */
std::string encode_to_hex(Uint8View data);

/**
 * Parse hex-encoded bytes
 * @param hex Input string, size must be a multiple of 2
 * @return Parsed bytes vector, empty in case of an invalid or empty string
 */
Uint8Vector decode_hex(std::string_view hex);

/**
 * Make pointer to C-string into string_view. If the pointer is null, returns empty string.
 */
std::string_view safe_string_view(const char *cstr);

} // namespace utils

template <class R>
concept Spannable = requires(R &&r) { std::span(std::forward<R>(r)); };

/**
 * @brief Converts a span of type T to a span of uint8_t.
 *
 * @tparam T The type of the elements in the source span. Must satisfy the Spannable concept.
 * @param source The source span to be converted.
 * @return A Uint8Span representing the source span as bytes.
 */
template <Spannable T>
Uint8Span as_u8s(T &&source) {
    std::span sp(std::forward<T>(source));
    auto byte_span = std::as_writable_bytes(sp);
    return Uint8Span(reinterpret_cast<uint8_t *>(byte_span.data()), byte_span.size());
}

/**
 * @brief Converts a span of type T to a string_view of uint8_t.
 *
 * @tparam T The type of the elements in the source span. Must satisfy the Spannable concept.
 * @param source The source span to be converted.
 * @return A Uint8View representing the source span as bytes.
 */
template <Spannable T>
Uint8View as_u8v(T &&source) {
    std::span sp(std::forward<T>(source));
    auto byte_span = std::as_bytes(sp);
    return Uint8View(reinterpret_cast<const uint8_t *>(byte_span.data()), byte_span.size());
}

/**
 * Class for automatic management of POD (Plain Old Data) objects with a custom deleter.
 * @tparam T The type of data this class manages.
 * @tparam DELETER The deleter function that is called when memory is freed.
 * @tparam std::enable_if_t<std::is_standard_layout_v<T> && std::is_trivial_v<T>, bool>
 * = true Constraint for types to be trivial and have a standard layout.
 */
template <typename T, auto DELETER, std::enable_if_t<std::is_standard_layout_v<T> && std::is_trivial_v<T>, bool> = true>
struct AutoPod {
    T data{};

    explicit AutoPod(const T &data)
            : data{data} {
    }

    AutoPod() = default;
    ~AutoPod() {
        reset();
    }

    AutoPod(const AutoPod &) = delete;
    AutoPod &operator=(const AutoPod &) = delete;

    AutoPod(AutoPod &&o) noexcept {
        *this = std::move(o);
    }
    AutoPod &operator=(AutoPod &&rhs) noexcept {
        std::swap(this->data, rhs.data);
        return *this;
    }

    const T *get() const {
        return &data;
    }
    T *get() {
        return &data;
    }

    T *operator->() {
        return get();
    }
    const T *operator->() const {
        return get();
    }

    T &operator*() {
        return data;
    }
    const T &operator*() const {
        return data;
    }

    void reset() {
        DELETER(get());
        release();
    }

    void reset(const T &d) {
        reset();
        data = d;
    }

    void release() {
        data = {};
    }
};

/** String view input stream */
class StringViewStream : public std::basic_istream<char, std::char_traits<char>> {
public:
    explicit StringViewStream(std::string_view v) : basic_istream(&m_buf), m_buf(v) {}

private:
    class StringViewBuf : public std::streambuf {
    public:
        explicit StringViewBuf(std::string_view v) {
            setg((char *) v.data(), (char *) v.data(), (char *) v.data() + v.size());
        }

    protected:
        pos_type seekoff(off_type off, std::ios_base::seekdir way, std::ios_base::openmode) override {
            char_type *p;
            switch (way) {
            case beg:
                p = eback();
                break;
            case cur:
                p = gptr();
                break;
            case end:
                p = egptr();
                break;
            default:
                return -1;
            }
            if (char_type *np = p + off; np >= eback() && np <= egptr()) {
                setg(eback(), np, egptr());
                return np - eback();
            }
            return -1;
        }

        pos_type seekpos(pos_type pos, std::ios_base::openmode) override {
            if (char_type *np = eback() + pos; np >= eback() && np <= egptr()) {
                setg(eback(), np, egptr());
                return np - eback();
            }
            return -1;
        }
    };

    StringViewBuf m_buf;
};

#if !defined(_WIN32)

enum ExecError : uint8_t {
    EE_POPEN,
    EE_PCLOSE,
};

template <>
struct ErrorCodeToString<ExecError> {
    std::string operator()(ExecError code) {
        // clang-format off
        switch (code) {
            case EE_POPEN: return "popen()";
            case EE_PCLOSE: return "pclose()";
        }
        // clang-format on
    }
};

struct ExecResult {
    std::string output;
    int status;
};

static inline ag::Result<ExecResult, ExecError> exec_with_output(std::string cmd) {
    cmd += " 2>&1";
    FILE *pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        return make_error(ExecError::EE_POPEN);
    }

    std::string output;
    char buf[128]{};
    size_t nread = 0;
    while ((nread = fread(buf, 1, sizeof(buf), pipe)) > 0) {
        output.append(buf, nread);
    }

    int ret = pclose(pipe);
    if (-1 == ret) {
        return make_error(ExecError::EE_PCLOSE);
    }

    return ExecResult{.output = std::move(output), .status = WEXITSTATUS(ret)};
}

/**
 * Format a command line using format string `fmt` and args `args`,
 * execute it with `sh`, and return the command output and exit status.
 *
 * Output redirection `2>&1` is automatically appended to the command line.
 */
template <typename... Ts>
Result<ExecResult, ExecError> fsystem(std::string_view fmt, Ts &&...args) {
    return exec_with_output(fmt::vformat(fmt, fmt::make_format_args(std::forward<Ts>(args)...)));
}

#endif // !defined(_WIN32)

} // namespace ag
