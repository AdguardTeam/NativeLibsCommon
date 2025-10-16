#pragma once

#include <event2/util.h>
#include <utility>

#ifndef _WIN32
#include <unistd.h>
#endif

namespace ag {

/**
 * A class for automatic file descriptor management
 *
 * This class manages a file descriptor, ensuring it's closed when the object is destroyed.
 * It supports both duplication and moving of file descriptors.
 *
 * @note This class can't be used to manage file descriptors obtained with `_open`, `_wopen`,
 *       `_sopen_s` and `_wsopen_s` methods on Windows.
 */
class AutoFd {
    evutil_socket_t m_fd = EVUTIL_INVALID_SOCKET;
public:
    /**
     * Default constructor
     */
    AutoFd() = default;

#ifndef _WIN32
    /**
     * Create an AutoFd by duplicating a file descriptor
     *
     * @param fd File descriptor to duplicate
     * @return AutoFd object managing the duplicated file descriptor
     */
    static AutoFd dup_fd(evutil_socket_t fd) {
        AutoFd result;
        if (fd != EVUTIL_INVALID_SOCKET) {
            result.m_fd = ::dup(fd);
        }
        return result;
    }
#endif

    /**
     * Create an AutoFd by adopting a file descriptor
     *
     * @param fd File descriptor to adopt (take ownership of)
     * @return AutoFd object managing the adopted file descriptor
     */
    static AutoFd adopt_fd(evutil_socket_t fd) {
        AutoFd result;
        if (fd != EVUTIL_INVALID_SOCKET) {
            result.m_fd = fd;
        }
        return result;
    }

    /**
     * Destructor
     *
     * Closes the file descriptor if it's valid
     */
    ~AutoFd() {
        reset();
    }

    /**
     * Get the file descriptor
     *
     * @return The file descriptor
     */
    [[nodiscard]] evutil_socket_t get() const {
        return m_fd;
    }

    /**
     * Check if the file descriptor is valid
     *
     * @return true if the file descriptor is valid, false otherwise
     */
    [[nodiscard]] bool is_valid() const {
        return m_fd != EVUTIL_INVALID_SOCKET;
    }

    /**
     * Release the file descriptor
     *
     * @return The file descriptor, which is no longer managed by this object
     */
    [[nodiscard]] evutil_socket_t release() {
        evutil_socket_t fd = std::exchange(m_fd, EVUTIL_INVALID_SOCKET);
        return fd;
    }

    /**
     * Reset the file descriptor and replace it with new one
     *
     * @param new_fd File descriptor to replace with
     */
    void reset(evutil_socket_t new_fd = EVUTIL_INVALID_SOCKET) {
        evutil_socket_t fd = std::exchange(m_fd, new_fd);
        if (fd != EVUTIL_INVALID_SOCKET && new_fd != fd) {
            evutil_closesocket(fd);
        }
    }

    /**
     * Move constructor
     *
     * @param other AutoFd to move from
     */
    AutoFd(AutoFd &&other) noexcept {
        *this = std::move(other);
    }

    /**
     * Move assignment operator
     *
     * @param other AutoFd to move from
     * @return Reference to this object
     */
    AutoFd &operator=(AutoFd &&other) noexcept {
        reset();
        m_fd = std::exchange(other.m_fd, EVUTIL_INVALID_SOCKET);
        return *this;
    }

    /**
     * Copy constructor (deleted)
     */
    AutoFd(const AutoFd &) = delete;

    /**
     * Copy assignment operator (deleted)
     */
    void operator=(const AutoFd &) = delete;
};


} // namespace ag
