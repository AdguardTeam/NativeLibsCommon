#include "common/system_error.h"

#ifdef _WIN32
#include <algorithm>
#include <cstring>
#include <string>
#include <windows.h>

#include "common/utils.h"
#endif

#if defined(__MACH__) || defined(__linux__)
#include <cerrno>
#include <cstring>
#endif

namespace ag::sys {

#ifdef _WIN32

static size_t get_wide_error_message(DWORD code, wchar_t *dst, size_t dst_cap) {
    DWORD n = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_MAX_WIDTH_MASK,
            nullptr, code, MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US), (LPWSTR) dst, DWORD(dst_cap), nullptr);
    if (n == 0) {
        return 0;
    }
    if (n > 2 && dst[n - 2] == '.' && dst[n - 1] == ' ') {
        n -= 2;
    }

    return n;
}

static std::string get_error_message(DWORD code) {
    wchar_t w[255];
    size_t n = get_wide_error_message(code, w, std::size(w));
    return utils::from_wstring({w, n});
}

int last_error() {
    return int(GetLastError());
}

const char *strerror(int code) {
    static thread_local char buffer[255];
    std::string str = get_error_message(DWORD(code));
    size_t len = std::min(str.length(), std::size(buffer) - 1);
    std::memcpy(buffer, str.data(), len);
    buffer[len] = '\0';
    return buffer;
}
#endif // _WIN32

#if defined(__MACH__) || defined(__linux__)
int last_error() {
    return errno;
}

const char *strerror(int code) {
    return ::strerror(code); // NOLINT(concurrency-mt-unsafe)
}
#endif

} // namespace ag::sys
