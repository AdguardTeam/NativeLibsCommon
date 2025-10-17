#pragma once

namespace ag::sys {

/**
 * Get last system error code (errno on POSIX, GetLastError on Windows).
 * @return Current errno value (POSIX) or GetLastError result (Windows)
 */
int last_error();

/**
 * Get error description for errno (POSIX) or GetLastError/WSAGetLastError (Windows).
 * @param code Error code (errno on POSIX, GetLastError/WSAGetLastError result on Windows)
 * @return Static thread-local buffer, no need to free. Returns generic message for unknown codes.
 */
const char *strerror(int code);

} // namespace ag::sys
