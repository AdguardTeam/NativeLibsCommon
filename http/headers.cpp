#include <algorithm>
#include <utility>

#include "common/http/headers.h"
#include "common/utils.h"

namespace ag::http {

void Headers::reserve(size_t n) {
    m_headers.reserve(n);
}

std::optional<std::string_view> Headers::get(std::string_view name) const {
    auto iter = std::find_if(m_headers.begin(), m_headers.end(), [name](const auto &h) {
        return utils::iequals(h.name, name);
    });
    return (iter != m_headers.end()) ? std::make_optional<std::string_view>(iter->value) : std::nullopt;
}

std::string_view Headers::gets(std::string_view name) const {
    return get(name).value_or("");
}

void Headers::put(std::string name, std::string value) {
    m_headers.emplace_back(Header<>{std::move(name), std::move(value)});
}

bool Headers::contains(std::string_view name) const {
    return get(name).has_value();
}

size_t Headers::remove(std::string_view name) {
    auto iter = std::remove_if(m_headers.begin(), m_headers.end(), [name](const auto &h) {
        return h.name == name;
    });
    size_t n = std::distance(iter, m_headers.end());
    m_headers.erase(iter, m_headers.end());
    return n;
}

Headers::Iterator Headers::erase(Iterator iter) {
    return m_headers.erase(iter);
}

Headers::Iterator Headers::erase(ConstIterator iter) {
    return m_headers.erase(iter);
}

Headers::ValueIterator<Headers::Iterator> Headers::erase(ValueIterator<Iterator> iter) {
    std::string_view name = iter.m_name;
    return {m_headers.erase(iter.m_current), m_headers.end(), name};
}

Headers::ValueIterator<Headers::Iterator> Headers::erase(ValueIterator<ConstIterator> iter) {
    std::string_view name = iter.m_name;
    return {m_headers.erase(iter.m_current), m_headers.end(), name};
}

size_t Headers::length() const {
    return m_headers.size();
}

bool Headers::has_body() const {
    return m_has_body;
}

void Headers::has_body(bool flag) {
    m_has_body = flag;
}

std::string Headers::str() const {
    return fmt::format("{}", *this);
}

Headers::Iterator Headers::begin() {
    return m_headers.begin();
}

Headers::Iterator Headers::end() {
    return m_headers.end();
}

Headers::ConstIterator Headers::begin() const {
    return m_headers.cbegin();
}

Headers::ConstIterator Headers::end() const {
    return m_headers.cend();
}

std::pair<Headers::ValueIterator<Headers::Iterator>, Headers::ValueIterator<Headers::Iterator>> Headers::value_range(
        std::string_view name) {
    return {{m_headers.begin(), m_headers.end(), name}, {m_headers.end(), m_headers.end(), name}};
}

std::pair<Headers::ValueIterator<Headers::ConstIterator>, Headers::ValueIterator<Headers::ConstIterator>>
Headers::value_range(std::string_view name) const {
    return {{m_headers.begin(), m_headers.end(), name}, {m_headers.end(), m_headers.end(), name}};
}

template <typename I>
Headers::ValueIterator<I>::ValueIterator(I begin, I end, std::string_view name)
        : m_current(begin)
        , m_end(end)
        , m_name(name) {
    if (m_current != m_end) {
        if (utils::iequals(m_current->name, m_name)) {
            m_value = m_current->value;
        } else {
            *this = this->operator++();
        }
    }
}

template <typename I>
typename Headers::ValueIterator<I>::reference Headers::ValueIterator<I>::operator*() const {
    return m_value.value(); // NOLINT(*-unchecked-optional-access)
}

template <typename I>
typename Headers::ValueIterator<I> &Headers::ValueIterator<I>::operator++() {
    while (++m_current != m_end && !utils::iequals(m_current->name, m_name)) {
        // do nothing
    }
    if (m_current != m_end) {
        m_value = m_current->value;
    } else {
        m_value.reset();
    }
    return *this;
}

template <typename I>
typename Headers::ValueIterator<I> Headers::ValueIterator<I>::operator++(int) { // NOLINT(cert-dcl21-cpp)
    auto ret = *this;
    this->operator++();
    return ret;
}

template <typename I>
bool Headers::ValueIterator<I>::operator==(ValueIterator rhs) const {
    return m_current == rhs.m_current;
}

template <typename I>
bool Headers::ValueIterator<I>::operator!=(ValueIterator rhs) const {
    return !(rhs == *this);
}

template class Headers::ValueIterator<Headers::Iterator>;
template class Headers::ValueIterator<Headers::ConstIterator>;

Request::Request(Version version)
        : m_version(version) {
}

Request::Request(Version version, std::string method)
        : Request(version) {
    m_method = std::move(method); // NOLINT(*-prefer-member-initializer)
}

Request::Request(Version version, std::string method, std::string path)
        : Request(version, std::move(method)) {
    m_path = std::move(path); // NOLINT(*-prefer-member-initializer)
}

Version Request::version() const {
    return m_version;
}

void Request::version(Version val) {
    m_version = val;
}

std::string_view Request::method() const {
    return m_method;
}

void Request::method(std::string val) {
    m_method = std::move(val);
}

std::string_view Request::path() const {
    return m_path;
}

void Request::path(std::string val) {
    m_path = std::move(val);
}

std::string_view Request::scheme() const {
    return m_scheme;
}

void Request::scheme(std::string val) {
    m_scheme = std::move(val);
}

std::string_view Request::authority() const {
    return m_authority;
}

void Request::authority(std::string val) {
    m_authority = std::move(val);
}

Headers &Request::headers() {
    return m_headers;
}

const Headers &Request::headers() const {
    return m_headers;
}

std::string Request::str() const {
    return fmt::format("{}", *this);
}

Headers Request::into_headers(Request self) {
    return std::move(self.m_headers);
}

Request::Iterator::Iterator(const Request *obj)
        : m_obj(obj)
        , m_state((obj == nullptr) ? DONE : METHOD) {
    update_current();
}

Request::Iterator::reference Request::Iterator::operator*() const {
    return m_current.value(); // NOLINT(*-unchecked-optional-access)
}

Request::Iterator &Request::Iterator::operator++() {
    if (m_state != HEADERS) {
        m_state = (State) std::min((int) DONE, (int) m_state + 1);
    } else if (m_headers_iter.value() == m_obj->headers().end()) { // NOLINT(*-unchecked-optional-access)
        m_state = DONE;
    }
    update_current();
    return *this;
}

Request::Iterator Request::Iterator::operator++(int) { // NOLINT(*-dcl21-cpp)
    auto ret = *this;
    this->operator++();
    return ret;
}

bool Request::Iterator::operator==(Iterator rhs) const {
    return m_state == rhs.m_state && (m_state != HEADERS || (m_headers_iter == rhs.m_headers_iter));
}

bool Request::Iterator::operator!=(Iterator rhs) const {
    return !(*this == rhs);
}

void Request::Iterator::update_current() {
    if (m_obj == nullptr) {
        m_state = DONE;
        m_current.reset();
        return;
    }

    switch (m_state) {
    case METHOD:
        if (!m_obj->m_method.empty()) {
            m_current = make_header<std::string_view>(PSEUDO_HEADER_NAME_METHOD, m_obj->m_method);
            break;
        }
        m_state = SCHEME;
        [[fallthrough]];
    case SCHEME:
        if (!m_obj->m_scheme.empty()) {
            m_current = make_header<std::string_view>(PSEUDO_HEADER_NAME_SCHEME, m_obj->m_scheme);
            break;
        }
        m_state = PATH;
        [[fallthrough]];
    case PATH:
        if (!m_obj->m_path.empty()) {
            m_current = make_header<std::string_view>(PSEUDO_HEADER_NAME_PATH, m_obj->m_path);
            break;
        }
        m_state = AUTHORITY;
        [[fallthrough]];
    case AUTHORITY:
        if (!m_obj->m_authority.empty()) {
            m_current = make_header<std::string_view>(PSEUDO_HEADER_NAME_AUTHORITY, m_obj->m_authority);
            break;
        }
        m_state = HEADERS;
        [[fallthrough]];
    case HEADERS:
        if (m_headers_iter.has_value()) {
            std::advance(m_headers_iter.value(), 1);
            if (m_headers_iter.value() != m_obj->headers().end()) {
                m_current = make_header<std::string_view>(m_headers_iter.value()->name, m_headers_iter.value()->value);
                break;
            }
        } else if (0 < m_obj->headers().length()) {
            m_headers_iter = m_obj->headers().begin();
            m_current = make_header<std::string_view>(m_headers_iter.value()->name, m_headers_iter.value()->value);
            break;
        }
        m_state = DONE;
        [[fallthrough]];
    case DONE:
        m_obj = nullptr;
        m_current.reset();
        break;
    }
}

Request::Iterator Request::begin() const {
    return Request::Iterator{this};
}

Request::Iterator Request::end() const {
    return Request::Iterator{nullptr};
}

Version Response::version() const {
    return m_version;
}

void Response::version(Version val) {
    m_version = val;
}

Response::Response(Version version)
        : m_version(version) {
}

Response::Response(Version version, int status_code)
        : Response(version) {
    m_status_code = status_code; // NOLINT(*-prefer-member-initializer)
    if (HTTP_1_1 < m_version) {
        m_status_string = std::to_string(m_status_code);
    }
}

int Response::status_code() const {
    return m_status_code;
}

void Response::status_code(int status_code) {
    m_status_code = status_code;
    if (HTTP_1_1 < m_version) {
        m_status_string = std::to_string(m_status_code);
    }
}

std::string_view Response::status_string() const {
    return m_status_string;
}

void Response::status_string(std::string val) {
    m_status_string = std::move(val);
}

Headers &Response::headers() {
    return m_headers;
}

const Headers &Response::headers() const {
    return m_headers;
}

std::string Response::str() const {
    return fmt::format("{}", *this);
}

Headers Response::into_headers(Response self) {
    return std::move(self.m_headers);
}

Response::Iterator::Iterator(const Response *obj)
        : m_obj(obj)
        , m_state((obj == nullptr) ? DONE : STATUS) {
    update_current();
}

Response::Iterator::reference Response::Iterator::operator*() const {
    return m_current.value(); // NOLINT(*-unchecked-optional-access)
}

Response::Iterator &Response::Iterator::operator++() {
    if (m_state != HEADERS) {
        m_state = (State) std::min((int) DONE, (int) m_state + 1);
    } else if (m_headers_iter.value() == m_obj->headers().end()) { // NOLINT(*-unchecked-optional-access)
        m_state = DONE;
    }
    update_current();
    return *this;
}

Response::Iterator Response::Iterator::operator++(int) { // NOLINT(*-dcl21-cpp)
    auto ret = *this;
    this->operator++();
    return ret;
}

bool Response::Iterator::operator==(Iterator rhs) const {
    return m_state == rhs.m_state && (m_state != HEADERS || (m_headers_iter == rhs.m_headers_iter));
}

bool Response::Iterator::operator!=(Iterator rhs) const {
    return !(*this == rhs);
}

void Response::Iterator::update_current() {
    if (m_obj == nullptr) {
        m_state = DONE;
        m_current.reset();
        return;
    }

    switch (m_state) {
    case STATUS:
        m_current = make_header<std::string_view>(PSEUDO_HEADER_NAME_STATUS, m_obj->m_status_string);
        break;
    case HEADERS:
        if (m_headers_iter.has_value()) {
            std::advance(m_headers_iter.value(), 1);
            if (m_headers_iter.value() != m_obj->headers().end()) {
                m_current = make_header<std::string_view>(m_headers_iter.value()->name, m_headers_iter.value()->value);
                break;
            }
        } else if (0 < m_obj->headers().length()) {
            m_headers_iter = m_obj->headers().begin();
            m_current = make_header<std::string_view>(m_headers_iter.value()->name, m_headers_iter.value()->value);
            break;
        }
        m_state = DONE;
        [[fallthrough]];
    case DONE:
        m_obj = nullptr;
        m_current.reset();
        break;
    }
}

Response::Iterator Response::begin() const {
    return Response::Iterator{this};
}

Response::Iterator Response::end() const {
    return Response::Iterator{nullptr};
}

} // namespace ag::http
