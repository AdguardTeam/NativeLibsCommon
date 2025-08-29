#pragma once

#include <event2/util.h>
#include <utility>

namespace ag {

class AutoFd {
    evutil_socket_t m_fd = -1;
public:
    AutoFd() = default;
    explicit AutoFd(evutil_socket_t fd) : m_fd(fd) {}
    ~AutoFd() {
        reset();
    }

    evutil_socket_t get() const {
        return m_fd;
    }

    evutil_socket_t release() {
        evutil_socket_t fd = std::exchange(m_fd, -1);
        return fd;
    }

    void reset(evutil_socket_t new_fd = -1) {
        evutil_socket_t fd = std::exchange(m_fd, new_fd);
        if (fd != -1 && new_fd != fd) {
            evutil_closesocket(fd);
        }
    }

    AutoFd(AutoFd &&other) noexcept {
        *this = std::move(other);
    }

    AutoFd &operator=(AutoFd &&other) noexcept {
        reset();
        m_fd = std::exchange(other.m_fd, -1);
        return *this;
    }

    AutoFd(const AutoFd &) = delete;
    void operator=(const AutoFd &) = delete;
};


} // namespace ag
