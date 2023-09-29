#pragma once

#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <fmt/format.h>

#include "common/defs.h"
#include "common/http/util.h"

namespace ag::http {

constexpr std::string_view PSEUDO_HEADER_NAME_METHOD = ":method";
constexpr std::string_view PSEUDO_HEADER_NAME_SCHEME = ":scheme";
constexpr std::string_view PSEUDO_HEADER_NAME_AUTHORITY = ":authority";
constexpr std::string_view PSEUDO_HEADER_NAME_PATH = ":path";
constexpr std::string_view PSEUDO_HEADER_NAME_STATUS = ":status";

template <typename T = std::string>
struct Header {
    T name;
    T value;

    template <typename P>
    [[nodiscard]] bool operator==(const Header<P> &other) const {
        return this->name == other.name && this->value == other.value;
    }
};

template <typename T, typename U, typename V>
Header<T> make_header(U &&name, V &&value) {
    return Header<T>{
            .name{std::forward<U>(name)},
            .value{std::forward<V>(value)},
    };
}

class Headers {
private:
    std::vector<Header<>> m_headers;
    bool m_has_body = false;

public:
    using Iterator = decltype(m_headers)::iterator;
    using ConstIterator = decltype(m_headers)::const_iterator;

    /**
     * Yields the sequence of the values
     */
    template <typename I>
    class ValueIterator {
    public:
        using value_type = std::string_view;                 // NOLINT(*-identifier-naming)
        using reference = const value_type &;                // NOLINT(*-identifier-naming)
        using pointer = const value_type *;                  // NOLINT(*-identifier-naming)
        using difference_type = ssize_t;                     // NOLINT(*-identifier-naming)
        using iterator_category = std::forward_iterator_tag; // NOLINT(*-identifier-naming)

        ValueIterator(I begin, I end, std::string_view name);
        ~ValueIterator() = default;

        ValueIterator(const ValueIterator &) = default;
        ValueIterator &operator=(const ValueIterator &) = default;
        ValueIterator(ValueIterator &&) = default;
        ValueIterator &operator=(ValueIterator &&) = default;

        [[nodiscard]] reference operator*() const;
        ValueIterator &operator++();
        ValueIterator operator++(int); // NOLINT(cert-dcl21-cpp)
        [[nodiscard]] bool operator==(ValueIterator rhs) const;
        [[nodiscard]] bool operator!=(ValueIterator rhs) const;

    private:
        friend Headers;

        I m_current;
        I m_end;
        std::string_view m_name;
        std::optional<std::string_view> m_value;
    };

    Headers() = default;
    ~Headers() = default;
    Headers(const Headers &headers) = default;
    Headers &operator=(const Headers &headers) = default;
    Headers(Headers &&headers) = default;
    Headers &operator=(Headers &&headers) = default;

    Headers(ConstIterator begin, ConstIterator end)
            : m_headers(begin, end) {
    }

    template <typename I, typename F>
    Headers(I begin, I end, F convert) {
        m_headers.reserve(std::distance(begin, end));
        std::transform(begin, end, std::back_inserter(m_headers), convert);
    }

    /**
     * Reserve space for at least the specified number of elements
     */
    void reserve(size_t n);
    /**
     * Check if HTTP request/response has body
     * @return True if request/response has body
     */
    [[nodiscard]] bool has_body() const;
    /**
     * Set/unset flag signalling that HTTP request/response has bodt
     * @param flag Flag signalling that HTTP request/response has bodt
     */
    void has_body(bool flag);
    /**
     * Get value of the first found header with specified name
     * @param name Name of HTTP header
     * @return Some string with the value of HTTP header, none if not found
     */
    [[nodiscard]] std::optional<std::string_view> get(std::string_view name) const;
    /**
     * Safe version of `get` always returning some string (empty if not found)
     */
    [[nodiscard]] std::string_view gets(std::string_view name) const;
    /**
     * Put HTTP header field with specified name and value
     * @param name HTTP header field name
     * @param value HTTp header field value
     */
    void put(std::string name, std::string value);
    /**
     * Check if HTTP headers contains field with specified name
     * @param fieldName HTTP field name
     * @return True if such field exists
     */
    [[nodiscard]] bool contains(std::string_view fieldName) const;
    /**
     * Remove field with specified name from HTTP headers
     * @param name HTTP field name
     * @return number of removed fields
     */
    size_t remove(std::string_view name);
    /**
     * Erase the specified element
     * @return Iterator following the removed element
     */
    Iterator erase(Iterator iter);
    /**
     * Erase the specified element
     * @return Iterator following the removed element
     */
    Iterator erase(ConstIterator iter);
    /**
     * Erase the specified element
     * @return Iterator following the removed element
     */
    ValueIterator<Iterator> erase(ValueIterator<Iterator> iter);
    /**
     * Erase the specified element
     * @return Iterator following the removed element
     */
    ValueIterator<Iterator> erase(ValueIterator<ConstIterator> iter);
    /**
     * Get the number of the stored headers
     */
    [[nodiscard]] size_t length() const;
    /**
     * Get an iterator to the beginning
     */
    [[nodiscard]] Iterator begin();
    /**
     * Get an iterator to the end
     */
    [[nodiscard]] Iterator end();
    /**
     * Get a constant iterator to the beginning
     */
    [[nodiscard]] ConstIterator begin() const;
    /**
     * Get a constant iterator to the end
     */
    [[nodiscard]] ConstIterator end() const;
    /**
     * Get all values of header with specified name
     * @param name Name of the HTTP header
     */
    [[nodiscard]] std::pair<ValueIterator<Iterator>, ValueIterator<Iterator>> value_range(std::string_view name);
    /**
     * Get all values of header with specified name
     * @param name Name of the HTTP header
     */
    [[nodiscard]] std::pair<ValueIterator<ConstIterator>, ValueIterator<ConstIterator>> value_range(
            std::string_view name) const;
    /**
     * Get string representation of the headers
     */
    [[nodiscard]] std::string str() const;
};

class Request {
public:
    Request() = delete;
    ~Request() = default;
    Request(const Request &headers) = default;
    Request &operator=(const Request &headers) = default;
    Request(Request &&headers) = default;
    Request &operator=(Request &&headers) = default;

    explicit Request(Version version);
    Request(Version version, std::string method);
    Request(Version version, std::string method, std::string path);

    /**
     * Get HTTP protocol version
     * @return see `http_version_t` enum
     */
    [[nodiscard]] Version version() const;
    /**
     * Set HTTP protocol version
     * @return see `http_version_t` enum
     */
    void version(Version val);
    /**
     * Get HTTP method
     * @return HTTP method
     */
    [[nodiscard]] std::string_view method() const;
    /**
     * Set HTTP method
     * @param val HTTP method
     */
    void method(std::string val);
    /**
     * Get path of HTTP request (:path pseudo-header of HTTP/2 request or path field of HTTP/1 request)
     * @return Path of HTTP request
     */
    [[nodiscard]] std::string_view path() const;
    /**
     * Set path of HTTP request
     * @param val Path of HTTP request
     */
    void path(std::string val);
    /**
     * Get scheme (http/https) of HTTP request (HTTP/2 or HTTP-proxy requests)
     * @return Scheme of HTTP request
     */
    [[nodiscard]] std::string_view scheme() const;
    /**
     * Set scheme (http/https) of HTTP request (HTTP/2 or HTTP-proxy requests)
     * @param val Scheme of HTTP request
     */
    void scheme(std::string val);
    /**
     * Get host of HTTP request (HTTP/2 only, use "Host" header for HTTP/1)
     * @return Host of HTTP request
     */
    [[nodiscard]] std::string_view authority() const;
    /**
     * Set host of HTTP request (HTTP/2 only, use "Host" header for HTTP/1)
     * @param val Host of HTTP request
     */
    void authority(std::string val);
    /**
     * Get the headers
     */
    [[nodiscard]] Headers &headers();
    /**
     * Get the headers
     */
    [[nodiscard]] const Headers &headers() const;
    /**
     * Get string representation of the headers
     */
    [[nodiscard]] std::string str() const;
    /**
     * Extract inner headers object from the request
     */
    [[nodiscard]] static Headers into_headers(Request self);

    class Iterator {
    public:
        using value_type = Header<std::string_view>;         // NOLINT(*-identifier-naming)
        using reference = const value_type &;                // NOLINT(*-identifier-naming)
        using pointer = const value_type *;                  // NOLINT(*-identifier-naming)
        using difference_type = ssize_t;                     // NOLINT(*-identifier-naming)
        using iterator_category = std::forward_iterator_tag; // NOLINT(*-identifier-naming)

        explicit Iterator(const Request *obj);
        ~Iterator() = default;

        Iterator(const Iterator &) = default;
        Iterator &operator=(const Iterator &) = default;
        Iterator(Iterator &&) = default;
        Iterator &operator=(Iterator &&) = default;

        [[nodiscard]] reference operator*() const;
        Iterator &operator++();
        Iterator operator++(int); // NOLINT(cert-dcl21-cpp)
        [[nodiscard]] bool operator==(Iterator rhs) const;
        [[nodiscard]] bool operator!=(Iterator rhs) const;

    private:
        enum State {
            METHOD,
            SCHEME,
            PATH,
            AUTHORITY,
            HEADERS,
            DONE,
        };

        const Request *m_obj = nullptr;
        State m_state = METHOD;
        std::optional<Header<std::string_view>> m_current;
        std::optional<Headers::ConstIterator> m_headers_iter;

        void update_current();
    };

    /**
     * Get a constant iterator to the beginning.
     * At first, the iterator yields the pseudo-headers.
     * After that, the classic headers.
     */
    [[nodiscard]] Iterator begin() const;
    /**
     * Get a constant iterator to the end
     */
    [[nodiscard]] Iterator end() const;

private:
    Version m_version = HTTP_1_1;
    std::string m_method;
    std::string m_path;
    std::string m_scheme;
    std::string m_authority;
    Headers m_headers;
};

class Response {
public:
    Response() = delete;
    ~Response() = default;
    Response(const Response &headers) = default;
    Response &operator=(const Response &headers) = default;
    Response(Response &&headers) = default;
    Response &operator=(Response &&headers) = default;

    explicit Response(Version version);
    Response(Version version, int status_code);

    /**
     * Get HTTP protocol version
     * @return see `http_version_t` enum
     */
    [[nodiscard]] Version version() const;
    /**
     * Set HTTP protocol version
     * @return see `http_version_t` enum
     */
    void version(Version val);
    /**
     * Get status code of HTTP response
     * @return Status code of HTTP response
     */
    [[nodiscard]] int status_code() const;
    /**
     * Set status code of HTTP response
     * @param status_code Status code of HTTP response
     */
    void status_code(int status_code);
    /**
     * Get status string of HTTP response (HTTP/1 only)
     * @return Status string of HTTP response
     */
    [[nodiscard]] std::string_view status_string() const;
    /**
     * Set status string of HTTP response (HTTP/1 only)
     * @param val Status string of HTTP response
     */
    void status_string(std::string val);
    /**
     * Get the headers
     */
    [[nodiscard]] Headers &headers();
    /**
     * Get the headers
     */
    [[nodiscard]] const Headers &headers() const;
    /**
     * Get string representation of the headers
     */
    [[nodiscard]] std::string str() const;
    /**
     * Extract inner headers object from the response
     */
    [[nodiscard]] static Headers into_headers(Response self);

    class Iterator {
    public:
        using value_type = Header<std::string_view>;         // NOLINT(*-identifier-naming)
        using reference = const value_type &;                // NOLINT(*-identifier-naming)
        using pointer = const value_type *;                  // NOLINT(*-identifier-naming)
        using difference_type = ssize_t;                     // NOLINT(*-identifier-naming)
        using iterator_category = std::forward_iterator_tag; // NOLINT(*-identifier-naming)

        explicit Iterator(const Response *obj);
        ~Iterator() = default;

        Iterator(const Iterator &) = default;
        Iterator &operator=(const Iterator &) = default;
        Iterator(Iterator &&) = default;
        Iterator &operator=(Iterator &&) = default;

        [[nodiscard]] reference operator*() const;
        Iterator &operator++();
        Iterator operator++(int); // NOLINT(cert-dcl21-cpp)
        [[nodiscard]] bool operator==(Iterator rhs) const;
        [[nodiscard]] bool operator!=(Iterator rhs) const;

    private:
        enum State {
            STATUS,
            HEADERS,
            DONE,
        };

        const Response *m_obj = nullptr;
        State m_state;
        std::optional<Header<std::string_view>> m_current;
        std::optional<Headers::ConstIterator> m_headers_iter;

        void update_current();
    };

    /**
     * Get a constant iterator to the beginning.
     * At first, the iterator yields the pseudo-headers.
     * After that, the classic headers.
     */
    [[nodiscard]] Iterator begin() const;
    /**
     * Get a constant iterator to the end
     */
    [[nodiscard]] Iterator end() const;

private:
    Version m_version = HTTP_1_1;
    int m_status_code = 200; // NOLINT(*-magic-numbers)
    std::string m_status_string;
    Headers m_headers;
};

} // namespace ag::http

template <typename T>
struct fmt::formatter<ag::http::Header<T>> {
    template <typename ParseContext>
    constexpr auto parse(ParseContext &ctx) {
        return ctx.begin();
    }

    template <typename FormatContext>
    auto format(const ag::http::Header<T> &self, FormatContext &ctx) {
        return fmt::format_to(ctx.out(), "{}: {}", self.name, self.value);
    }
};

template <>
struct fmt::formatter<ag::http::Headers> {
    template <typename ParseContext>
    constexpr auto parse(ParseContext &ctx) {
        return ctx.begin();
    }

    template <typename FormatContext>
    auto format(const ag::http::Headers &self, FormatContext &ctx) {
        return fmt::format_to(ctx.out(), "{}\r\n", fmt::join(self.begin(), self.end(), "\r\n"));
    }
};

template <>
struct fmt::formatter<ag::http::Request> {
    template <typename ParseContext>
    constexpr auto parse(ParseContext &ctx) {
        return ctx.begin();
    }

    template <typename FormatContext>
    auto format(const ag::http::Request &self, FormatContext &ctx) {
        auto method = self.method();
        fmt::format_to(ctx.out(), "{} ", method.empty() ? "OPTIONS" : method);

        switch (self.version()) {
        case ag::http::HTTP_0_9:
        case ag::http::HTTP_1_0:
        case ag::http::HTTP_1_1: {
            auto path = self.path();
            fmt::format_to(ctx.out(), "{} {}\r\n", path.empty() ? "*" : path, self.version());
            break;
        }
        case ag::http::HTTP_2_0:
        case ag::http::HTTP_3_0: {
            fmt::format_to(ctx.out(), "{}\r\n", self.version());
            if (auto scheme = self.scheme(); !scheme.empty()) {
                fmt::format_to(ctx.out(), ":scheme: {}\r\n", scheme);
            }
            if (auto authority = self.authority(); !authority.empty()) {
                fmt::format_to(ctx.out(), ":authority: {}\r\n", authority);
            }
            if (auto path = self.path(); !path.empty()) {
                fmt::format_to(ctx.out(), ":path: {}\r\n", path);
            }
            break;
        }
        }

        return fmt::format_to(ctx.out(), "{}\r\n", self.headers());
    }
};

template <>
struct fmt::formatter<ag::http::Response> {
    template <typename ParseContext>
    constexpr auto parse(ParseContext &ctx) {
        return ctx.begin();
    }

    template <typename FormatContext>
    auto format(const ag::http::Response &self, FormatContext &ctx) {
        ag::http::Version version = self.version();
        std::string_view status_message = (version < ag::http::Version::HTTP_2_0) ? self.status_string() : "";
        if (self.headers().length() == 0) {
            return fmt::format_to(ctx.out(), "{} {}{}{}\r\n\r\n", version, self.status_code(),
                    status_message.empty() ? "" : " ", status_message);
        }
        return fmt::format_to(ctx.out(),
                "{} {}{}{}\r\n"
                "{}\r\n",
                version, self.status_code(), status_message.empty() ? "" : " ", status_message, self.headers());
    }
};
