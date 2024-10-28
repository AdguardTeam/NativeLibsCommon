#include <cerrno>
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <iterator>
#include <utility>
#include <string>
#include <string_view>
#include <fmt/core.h>
#include <fmt/chrono.h>
#include <fmt/format.h>
#include <fmt/ostream.h>

#include "common/utils.h"
#include "common/logger.h"
#include "common/rotating_log_to_file.h"

ag::RotatingLogToFile::RotatingLogToFile(
        std::string log_file_path, size_t file_max_size_bytes, size_t files_count)
        : m_file_max_size_bytes(file_max_size_bytes)
        , m_files_count(files_count)
        , m_log_file_path(std::move(log_file_path)) {
    open_log_file();
}

void ag::RotatingLogToFile::operator()(LogLevel level, std::string_view message) {
    if (m_files_count == 0) {
        return;
    }

    if (m_files_count == 1) {
        log_to_ofstream(level, message);
        return;
    }

    if (auto pos = m_file_handle.tellp(); pos >= 0 && (size_t(pos) + message.size()) < m_file_max_size_bytes) {
        log_to_ofstream(level, message);
        return;
    }

    if (!rotate_files()) {
        log_to_ofstream(level, message);
        return;
    }

    log_to_ofstream(level, message);
}

void ag::RotatingLogToFile::open_log_file() {
    m_file_handle = std::ofstream{};
    m_file_handle.open(m_log_file_path, std::ios_base::app);
    if (m_file_handle.fail()) {
        log_to_ofstream(LOG_LEVEL_ERROR, AG_FMT("Error opening log file: {}", strerror(errno)));
    }
}

bool ag::RotatingLogToFile::rotate_files() {
    std::error_code error;
    const size_t first_index = 1;
    const size_t last_index = m_files_count - 1;

    for (auto index = last_index - 1; index >= first_index; --index) {
        std::string old_file_name = AG_FMT("{}.{}", m_log_file_path, index);
        std::string new_file_name = AG_FMT("{}.{}", m_log_file_path, index + 1);

        std::filesystem::rename(old_file_name, new_file_name, error);
        if (error && error != std::errc::no_such_file_or_directory) {
            log_to_ofstream(LOG_LEVEL_ERROR, AG_FMT("Error rotating log file: {}", old_file_name));
            return false;
        }
    }

    m_file_handle.close();
    std::string first_rotated_file_name = AG_FMT("{}.1", m_log_file_path);
    if (std::filesystem::rename(m_log_file_path, first_rotated_file_name, error); error) {
        open_log_file();
        log_to_ofstream(LOG_LEVEL_ERROR, AG_FMT("Error rotating log file: {}", m_log_file_path));
        return false;
    }

    open_log_file();
    return true;
}

static constexpr std::string_view ENUM_NAMES[] = {
        "ERROR",
        "WARN",
        "INFO",
        "DEBUG",
        "TRACE",
};
static constexpr size_t ENUM_NAMES_NUMBER = std::size(ENUM_NAMES);

void ag::RotatingLogToFile::log_to_ofstream(LogLevel level, std::string_view message) {
    std::string_view level_str = (level >= 0 && level < ENUM_NAMES_NUMBER) ? ENUM_NAMES[level] : "UNKNOWN";
    auto now = floor<Micros>(std::chrono::system_clock::now().time_since_epoch());
    auto secs = now.count() / 1000000;
    auto us = now.count() % 1000000;
    auto tm = fmt::localtime(secs);

    fmt::memory_buffer message_to_log;
    fmt::format_to(std::back_inserter(message_to_log),
            "{:%d.%m.%Y %H:%M:%S}.{:06} {:5} [{}] {}\n", tm, us, level_str, utils::gettid(), message);

    m_file_handle.write(message_to_log.data(), message_to_log.size());
    m_file_handle.flush();
    
    if (m_file_handle.fail()) {
        std::clog.write(message_to_log.data(), message_to_log.size());
        std::clog.flush();
    }
}
