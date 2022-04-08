#pragma once

#include <string_view>
#include <string>
#include <pcre2.h>

#include "common/logger.h"

namespace ag {

class Regex {
public:
    explicit Regex(std::string_view text, uint32_t pcre2_compile_options = PCRE2_CASELESS)
        : m_re(compile_regex(text, pcre2_compile_options))
    {}

    ~Regex() {
        pcre2_code_free(m_re);
    }

    Regex(const Regex &other) {
        *this = other;
    }

    Regex(Regex &&other) {
        *this = std::move(other);
    }

    Regex &operator=(const Regex &other) {
        m_re = pcre2_code_copy(other.m_re);
        return *this;
    }

    Regex &operator=(Regex &&other) {
        std::swap(m_re, other.m_re);
        return *this;
    }

    /**
     * Check if Regex compiled successfully
     */
    bool is_valid() const { return m_re != nullptr; }

    /**
     * Match string against Regex
     * @param[in]  str   string to match
     * @return     True if matches, false otherwise
     */
    bool match(std::string_view str) const;

    /**
     * Replace string by Regex
     * @param[in]  subject      string to process
     * @param[in]  replacement  replacement string
     * @return     Apply result (empty in case of error)
     */
    std::string replace(std::string_view subject, std::string_view replacement) const;

private:
    static Logger m_log;

    pcre2_code *m_re{nullptr};

    static pcre2_code *compile_regex(std::string_view text, uint32_t options);
};

} // namespace ag
