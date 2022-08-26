#pragma once

#include <string_view>
#include <string>
#include <optional>
#include <cstdlib>

#include "common/defs.h"

namespace ag::file {

using Handle = int;
static constexpr Handle INVALID_HANDLE = -1;

enum flags {
    RDONLY = 0x0000,
    WRONLY = 0x0001,
    RDWR = 0x0002,
    CREAT = 0x0100,
    APPEND = 0x0200,
};

/**
 * Check if file handle is valid
 * @param[in]  f     file handle
 * @return     True if valid, false otherwise
 */
bool is_valid(Handle f);

/**
 * Open file by path
 * @param[in]  path   system path
 * @param[in]  flags  file mode flags
 * @return     Handle of file
 */
Handle open(const std::string &path, int flags);

/**
 * Close file
 * @param[in]  f     file handle
 */
void close(Handle f);

/**
 * Read data from file
 * @param[in]  f     file handle
 * @param      buf   buffer to store read data
 * @param[in]  size  buffer size
 * @return     Number of read bytes (<0 in case of error)
 */
ssize_t read(Handle f, char *buf, size_t size);

/**
 * Read data from file with given offset
 * (file offset is not changed)
 * @param[in]  f     file handle
 * @param      buf   buffer to store read data
 * @param[in]  size  buffer size
 * @param[in]  pos   file offset
 * @return     Number of read bytes (<0 in case of error)
 */
ssize_t pread(Handle f, char *buf, size_t size, size_t pos);

/**
 * Write buffer in file
 * @param[in]  f     file handle
 * @param[in]  buf   data to be written
 * @param[in]  size  data size
 * @return     Number of written bytes (<0 in case of error)
 */
ssize_t write(Handle f, const void *buf, size_t size);

/**
 * Read a line at given offset from file
 * @param[in]  f     file handle
 * @param[in]  pos   file offset
 * @return     Read line, or nullopt in case of error
 */
std::optional<std::string> read_line(Handle f, size_t pos);

/**
 * Get current file position
 * @param[in]  f     file handle
 * @return     Current position (<0 in case of error)
 */
ssize_t get_position(Handle f);

/**
 * Sets file position
 * @param[in]  f     file handle
 * @param[in]  pos   new position
 * @return     New postion value (<0 in case of error)
 */
ssize_t set_position(Handle f, size_t pos);

/**
 * Get file size
 * @param[in]  f     file handle
 * @return     File size (<0 in case of error)
 */
ssize_t get_size(Handle f);

/**
 * Get time of last file modification
 * @param[in] path    Path to file
 * @return    Time of last modification. Measured in seconds since 1.01.1970 (0 in case error)
 */
SystemTime get_modification_time(const std::string &path);

/**
 * Get time of last file modification
 * @param[in] f     file handler
 * @return    Time of last modification. Measured in seconds since 1.01.1970 (0 in case error)
 */
SystemTime get_modification_time(Handle f);

/**
 * Function to be called from `for_each_line`
 * @param file position of read line
 * @param read line
 * @param user argument
 * @return true file reading loop continues
 *         false the loop stops
 */
using LineAction = bool (*)(uint32_t, std::string_view, void *);

/**
 * Apply user function to each line in file while user function returns true
 * or eof not met
 * @param[in]  f       file handle
 * @param[in]  action  user function
 * @param      arg     user argument
 *
 * @return     >=0 in case of success,
 *             <0 otherwise
 */
int for_each_line(Handle f, LineAction action, void *arg);

} // namespace ag::file
