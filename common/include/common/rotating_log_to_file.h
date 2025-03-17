#pragma once

#include <cstddef>
#include <mutex>
#include <string>
#include <string_view>
#include <fstream>

#include "logger.h"

namespace ag {

/**
 * Class for logging messages to a file with file rotation
 * It logs messages to a file and automatically rotates files when the size limit is reached
 */
class RotatingLogToFile {
public:
    /**
     * Construct a RotatingLogToFile object
     * Opens the initial log file and sets up parameters for file rotation
     * @param log_file_path Path to the base log file
     * @param file_max_size_bytes Maximum size of each log file in bytes before rotation
     * @param files_count Maximum number of rotated log files to keep
     */
    RotatingLogToFile(std::string log_file_path, size_t file_max_size_bytes, size_t files_count);

    /**
     * Log a message to the current log file
     * If the current log file size exceeds the maximum limit, rotates to the next file
     * @param level Log level of the message (e.g., INFO, ERROR)
     * @param message The log message to be written
     */
    void operator()(LogLevel level, std::string_view message);

    RotatingLogToFile(const RotatingLogToFile &) = delete;
    RotatingLogToFile(RotatingLogToFile &&) = delete;
    RotatingLogToFile &operator=(const RotatingLogToFile &) = delete;
    RotatingLogToFile &operator=(RotatingLogToFile &&) = delete;

    ~RotatingLogToFile() = default;

private:
    const size_t m_file_max_size_bytes;
    const size_t m_files_count;
    std::string m_log_file_path;
    std::ofstream m_file_handle;
    std::mutex m_mutex;

    void open_log_file();
    bool rotate_files();
    void log_to_ofstream(LogLevel level, std::string_view message);
};

} // namespace ag