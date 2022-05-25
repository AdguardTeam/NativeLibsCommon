#include "common/regex.h"

namespace ag {

Logger Regex::m_log = Logger{"regex"};

bool Regex::match(std::string_view str) const {
    if (!is_valid()) {
        return false;
    }

    pcre2_match_data *match_data = pcre2_match_data_create_from_pattern(m_re, nullptr);
    int retval = pcre2_match(m_re, (PCRE2_SPTR8) str.data(), str.length(), 0, 0, match_data, nullptr);
    pcre2_match_data_free(match_data);
    if (retval < 0 && retval != PCRE2_ERROR_NOMATCH && retval != PCRE2_ERROR_PARTIAL) {
        errlog(m_log, "Error matching string '{}': {}", str, retval);
    }
    return retval >= 0;
}

std::string Regex::replace(std::string_view subject, std::string_view replacement) const {
    uint32_t options = PCRE2_SUBSTITUTE_GLOBAL | PCRE2_SUBSTITUTE_UNSET_EMPTY | PCRE2_SUBSTITUTE_EXTENDED;

    std::string result;
    size_t result_length = subject.length() + 1;
    result.resize(result_length - 1);

    int retval = pcre2_substitute(m_re, (PCRE2_SPTR8) subject.data(), subject.length(), 0, options, nullptr, nullptr,
            (PCRE2_SPTR8) replacement.data(), replacement.length(), (PCRE2_UCHAR8 *) result.data(), &result_length);
    if (retval == PCRE2_ERROR_NOMEMORY) {
        result.resize(result_length - 1);
        retval = pcre2_substitute(m_re, (PCRE2_SPTR8) subject.data(), subject.length(), 0, options, nullptr, nullptr,
                (PCRE2_SPTR8) replacement.data(), replacement.length(), (PCRE2_UCHAR8 *) result.data(), &result_length);
    }
    if (retval >= 0) {
        result.resize(result_length);
    } else if (retval < 0) {
        PCRE2_UCHAR err_message[256];
        pcre2_get_error_message(retval, err_message, sizeof(err_message));
        errlog(m_log, "Failed to remove special characters from '{}': {}", subject, err_message);
        result.clear();
    }
    return result;
}

pcre2_code *Regex::compile_regex(std::string_view text, uint32_t options) {
    int err = 0;
    PCRE2_SIZE err_offset = 0;
    pcre2_code *re = pcre2_compile((PCRE2_SPTR8) text.data(), text.length(), options, &err, &err_offset, nullptr);
    if (re == nullptr) {
        PCRE2_UCHAR error_message[256];
        pcre2_get_error_message(err, error_message, sizeof(error_message));
        errlog(m_log, "Failed to compile Regex '{}': {} (offset={})", text, error_message, err_offset);
        return nullptr;
    }
    return re;
}

} // namespace ag
