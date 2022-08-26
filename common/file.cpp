#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

#if defined(__linux__) || defined(__LINUX__) || defined(__MACH__)
#include <unistd.h>
#elif defined(_WIN32)
#include <io.h>
#include <windows.h>
#endif

#include "common/file.h"
#include "common/utils.h"

namespace ag::file {

static int to_platform_flags(int flags) {
    int ret_val = 0x0000;
#if !defined(_WIN32)
    if ((flags & CREAT)) {
        ret_val |= O_CREAT;
    }
    if (flags & APPEND) {
        ret_val |= O_APPEND;
    }
    switch (flags & 0x0003) {
    case RDONLY:
        ret_val |= O_RDONLY;
        break;
    case WRONLY:
        ret_val |= O_WRONLY;
        break;
    case RDWR:
        ret_val |= O_RDWR;
        break;
    }
#elif defined(_WIN32)
    if ((flags & CREAT)) {
        ret_val |= _O_CREAT;
    }
    if (flags & APPEND) {
        ret_val |= _O_APPEND;
    }
    switch (flags & 0x0003) {
    case RDONLY:
        ret_val |= _O_RDONLY;
        break;
    case WRONLY:
        ret_val |= _O_WRONLY;
        break;
    case RDWR:
        ret_val |= _O_RDWR;
        break;
    }
#endif
    return ret_val;
}

#if defined(__linux__) || defined(__LINUX__) || defined(__MACH__)

bool is_valid(const Handle f) {
    return f >= 0;
}

Handle open(const std::string &path, int flags) {
    return ::open(path.data(), to_platform_flags(flags), 0666);
}

void close(Handle f) {
    if (is_valid(f)) {
        ::close(f);
    }
}

ssize_t read(const Handle f, char *buf, size_t size) {
    return ::read(f, buf, size);
}

ssize_t pread(const Handle f, char *buf, size_t size, size_t pos) {
    return ::pread(f, buf, size, pos);
}

ssize_t write(const Handle f, const void *buf, size_t size) {
    return ::write(f, buf, size);
}

ssize_t get_position(const Handle f) {
    return ::lseek(f, 0, SEEK_CUR);
}

ssize_t set_position(Handle f, size_t pos) {
    return ::lseek(f, pos, SEEK_SET);
}

ssize_t get_size(const Handle f) {
    struct stat st;
    return (0 == fstat(f, &st)) ? st.st_size : -1;
}

SystemTime get_modification_time(const std::string &path) {
    struct stat st;
    return SystemTime{Secs{(0 == stat(path.data(), &st)) ? st.st_mtime : 0}};
}

SystemTime get_modification_time(Handle f) {
    struct stat st;
    return SystemTime{Secs{(0 == fstat(f, &st)) ? st.st_mtime : 0}};
}

#elif defined(_WIN32)

bool is_valid(const Handle f) {
    return f >= 0;
}

Handle open(const std::string &path, int flags) {
    return ::_wopen(ag::utils::to_wstring(path).c_str(), to_platform_flags(flags) | _O_BINARY, _S_IWRITE);
}

void close(Handle f) {
    if (is_valid(f)) {
        ::_close(f);
    }
}

ssize_t read(const Handle f, char *buf, size_t size) {
    return ::_read(f, buf, size);
}

ssize_t pread(const Handle f, char *buf, size_t size, size_t pos) {
    size_t old_pos = get_position(f);
    set_position(f, pos);
    ssize_t r = read(f, buf, size);
    set_position(f, old_pos);
    return r;
}

ssize_t write(const Handle f, const void *buf, size_t size) {
    return ::_write(f, buf, size);
}

ssize_t get_position(const Handle f) {
    return ::_lseek(f, 0, SEEK_CUR);
}

ssize_t set_position(Handle f, size_t pos) {
    return ::_lseek(f, pos, SEEK_SET);
}

ssize_t get_size(const Handle f) {
    struct _stat64 st;
    if (0 != _fstat64(f, &st)) {
        return -1;
    }
    if (uint64_t(st.st_size) > uint64_t(std::numeric_limits<ssize_t>::max())) {
        errno = EFBIG;
        return -1;
    }
    return ssize_t(st.st_size);
}

SystemTime get_modification_time(const std::string &path) {
    struct _stat64 st;
    return SystemTime{Secs{(0 == _wstat64(ag::utils::to_wstring(path).c_str(), &st)) ? st.st_mtime : 0}};
}

SystemTime get_modification_time(Handle f) {
    struct _stat64 st;
    return SystemTime{Secs{(0 == _fstat64(f, &st)) ? st.st_mtime : 0}};
}

#else
#error not supported
#endif

int for_each_line(const Handle f, LineAction action, void *arg) {
    static constexpr size_t MAX_CHUNK_SIZE = 64 * 1024;

    ssize_t file_size = get_size(f);
    if (file_size < 0) {
        return -1;
    }

    const size_t chunk_size = std::min(MAX_CHUNK_SIZE, (size_t) file_size);

    std::vector<char> buffer(chunk_size);
    std::string line;
    size_t file_idx = 0;
    size_t line_idx = 0;
    ssize_t r;
    while (0 < (r = read(f, &buffer[0], buffer.size()))) {
        for (int i = 0; i < r; ++i) {
            int c = buffer[i];
            if (c != '\r' && c != '\n') {
                line.push_back(c);
                continue;
            }
            std::string_view line_view = ag::utils::trim(line);
            if (!action(line_idx, line_view, arg)) {
                return 0;
            }
            line.clear();
            line_idx = file_idx + i + 1;
        }
        file_idx += r;
    }

    if ((size_t) (file_size - 1) > line_idx) {
        std::string_view line_view = ag::utils::trim(line);
        action(line_idx, line_view, arg);
    }

    return r < 0 ? -1 : 0;
}

std::optional<std::string> read_line(const Handle f, size_t pos) {
    static constexpr size_t CHUNK_SIZE = 4 * 1024;
    std::vector<char> buffer(CHUNK_SIZE);

    if (0 > set_position(f, pos)) {
        return std::nullopt;
    }

    std::string line;
    ssize_t r;
    while (0 < (r = read(f, &buffer[0], CHUNK_SIZE))) {
        int from = 0;
        int i;
        for (i = 0; i < r; ++i) {
            int c = buffer[i];
            if (c == '\r' || c == '\n') {
                size_t length = i - from;
                line.append(&buffer[from], length);
                break;
            }
        }

        if (i < r) {
            break;
        } else {
            line.append(&buffer[0], r);
        }
    }

    line = utils::trim(line);
    return line;
}

} // namespace ag::file
