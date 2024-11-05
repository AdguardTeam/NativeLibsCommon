#include "common/regex.h"

namespace ag {

static std::string pcre2_error_to_string(int code) {
    static constexpr auto DEFAULT_ERROR_SIZE = 256;
    PCRE2_UCHAR error_message[DEFAULT_ERROR_SIZE];
    int ret = pcre2_get_error_message(code, error_message, std::size(error_message));
    if (ret == PCRE2_ERROR_BADDATA) {
        return AG_FMT("Unknown error code: {}", code);
    }

    if (ret == PCRE2_ERROR_NOMEMORY) {
        constexpr std::string_view TRUNCATION_MARKER = "...";
        std::memcpy(
                &error_message[std::size(error_message) - TRUNCATION_MARKER.length() - 1],
                TRUNCATION_MARKER.data(),
                TRUNCATION_MARKER.length()
        );
        ret = int(std::size(error_message));
    }

    return { error_message, error_message + ret };
}

std::string ag::RegexReplaceError::to_string() const {
    return pcre2_error_to_string(this->error);
}

RegexReplaceOnceResult Regex::replace(
        uint32_t options,
        std::string_view pattern,
        std::string_view replace_expression,
        pcre2_match_context *match_context
) const {
    std::string result(pattern.length(), '\0');

    // `+ 1` is needed as pcre counts the terminating null as well
    size_t result_length = result.length() + 1;
    options |= PCRE2_SUBSTITUTE_OVERFLOW_LENGTH; // Ask PCRE2 to compute required output length.
    int retval = pcre2_substitute(
            m_regex.get(),
            (PCRE2_SPTR8)pattern.data(), pattern.length(),
            /* start_offset */ 0,
            options,
            /* match_data */ nullptr,
            match_context,
            (PCRE2_SPTR8)replace_expression.data(), replace_expression.length(),
            (PCRE2_UCHAR8*)result.data(), &result_length
    );
    if (retval == PCRE2_ERROR_NOMEMORY) {
        result.resize(result_length);
        retval = pcre2_substitute(
                m_regex.get(),
                (PCRE2_SPTR8)pattern.data(), pattern.length(), 0, options, nullptr, nullptr,
                (PCRE2_SPTR8)replace_expression.data(), replace_expression.length(),
                (PCRE2_UCHAR8*)result.data(), &result_length
        );
    }

    if (retval < 0) {
        return RegexReplaceError { .error = retval, };
    }

    result.resize(result_length);
    return RegexReplaceOnceSuccess{
            .substitutions_number = size_t(retval),
            .string = std::move(result),
    };
}

RegexReplaceAllResult Regex::replace_all(
        uint32_t options,
        std::string pattern,
        std::string_view replace_expression,
        pcre2_match_context *match_context
) const {
    RegexReplaceAllResult all_result;

    while (true) {
        RegexReplaceOnceResult once_result = replace(
                options, pattern, replace_expression, match_context
        );

        if (auto *error = std::get_if<RegexReplaceError>(&once_result);
                error != nullptr) {
            all_result = *error;
            break;
        }

        auto &success = std::get<RegexReplaceOnceSuccess>(once_result);
        if (success.substitutions_number == 0) {
            all_result = RegexReplaceAllSuccess {
                    .string = std::move(success.string),
            };
            break;
        }

        pattern = std::move(success.string);
    }

    return all_result;
}

std::string ag::RegexCompileError::to_string() const {
    return AG_FMT(
            "{} (offset = {})\n"
            "\t\t{}\n"
            "\t\t{: >{}}^"
            , pcre2_error_to_string(this->error).c_str()
            , this->offset
            , this->pattern.c_str()
            , "", int(this->offset)
    );
}

std::string ag::RegexMatchError::to_string() const {
    return pcre2_error_to_string(this->error);
}

ag::Regex::Regex(Pcre2CodePtr regex)
        : m_regex(std::move(regex))
{}

ag::Regex &ag::Regex::operator=(const Regex &other) {
    if (this == &other) {
        return *this;
    }

    this->m_regex.reset(pcre2_code_copy(other.get()));
    return *this;
}

ag::Regex::CompileResult ag::Regex::compile(
        std::string_view regex,
        uint32_t options,
        pcre2_compile_context_8 *compile_context
) {
    int error = 0;
    PCRE2_SIZE error_offset = 0;
    Pcre2CodePtr compiled(pcre2_compile(
            (PCRE2_SPTR8)regex.data(), regex.length(), options, &error, &error_offset, compile_context
    ));
    if (compiled == nullptr) {
        return RegexCompileError {
                .pattern = std::string(regex),
                .error = error,
                .offset = error_offset,
        };
    }

    return ag::Regex(std::move(compiled));
}

ag::Regex::MatchResult ag::Regex::match(
        std::string_view text,
        uint32_t options,
        size_t start_offset,
        pcre2_match_context *mcontext
) const {
    const pcre2_code *re = this->get();

    UniquePtr<pcre2_match_data, &pcre2_match_data_free> match_data{pcre2_match_data_create_from_pattern(re, nullptr)};

    int res = pcre2_match(
            re, (PCRE2_SPTR8)text.data(), text.length(), start_offset, options, match_data.get(), mcontext
    );

    std::vector<std::pair<size_t, size_t>> matched_groups;
    if (res > 0) {
        matched_groups.reserve(res);
        size_t *offsets = pcre2_get_ovector_pointer(match_data.get());
        for (size_t group = 0; group < size_t(res); group++) {
            matched_groups.emplace_back(offsets[group * 2], offsets[group * 2 + 1]);
        }
    }

    if (res >= 0) {
        return RegexMatch{
                .match_groups = std::move(matched_groups),
        };
    }

    if (res == PCRE2_ERROR_NOMATCH || res == PCRE2_ERROR_PARTIAL) {
        return RegexNoMatch{};
    }

    return RegexMatchError{ .error = res };
}

int ag::Regex::callout_enumerate(std::function<int(pcre2_callout_enumerate_block *)> callback) const {
    return pcre2_callout_enumerate(
            m_regex.get(),
            [](pcre2_callout_enumerate_block *block, void *arg) {
                auto *callback = (std::function<int(pcre2_callout_enumerate_block *)> *)arg;
                return (*callback)(block);
            },
            &callback
    );
}

const pcre2_code *ag::Regex::get() const {
    return this->m_regex.get();
}

ag::LazyRegex::LazyRegex(std::string txt, uint32_t options, pcre2_compile_context *compile_context)
        : m_regex(std::in_place_type<Uncompiled>, Uncompiled{ std::move(txt), options, compile_context })
{}

const ag::Regex *ag::LazyRegex::get() const {
    if (std::optional error = this->compile(); error.has_value()) {
        return nullptr;
    }

    return &std::get<Regex>(this->m_regex);
}

ag::LazyRegex::MatchResult ag::LazyRegex::match(
        std::string_view text,
        uint32_t options,
        size_t start_offset,
        pcre2_match_context *mcontext
) const {
    if (std::optional<RegexCompileError> error = this->compile(); error.has_value()) {
        return *error;
    }

    struct ResultConverter {
        MatchResult operator()(RegexMatch match) const {
            return match;
        }
        MatchResult operator()(RegexNoMatch noMatch) const {
            return noMatch;
        }
        MatchResult operator()(RegexMatchError error) const {
            return error;
        }
    };

    const Regex *re = this->get();
    Regex::MatchResult result = re->match(text, options, start_offset, mcontext);
    return std::visit(ResultConverter(), result);
}

std::optional<ag::RegexCompileError> ag::LazyRegex::compile() const {
    if (const auto *uncompiled = std::get_if<Uncompiled>(&this->m_regex); uncompiled != nullptr) {
        Regex::CompileResult compile_result = Regex::compile(
                uncompiled->txt, uncompiled->options, uncompiled->compile_context
        );

        if (auto *error = std::get_if<RegexCompileError>(&compile_result); error != nullptr) {
            return *error;
        }

        this->m_regex = std::move(std::get<Regex>(compile_result));
    }

    return std::nullopt;
}

const Logger SimpleRegex::m_log = Logger{"regex"};

bool SimpleRegex::match(std::string_view str) const {
    if (!m_re) {
        return false;
    }
    auto match_result = m_re->match(str);
    if (auto *match = std::get_if<RegexMatch>(&match_result)) {
        return true;
    }
    if (auto *error = std::get_if<RegexMatchError>(&match_result)) {
        warnlog(m_log, "Error regex matching: {}", error->to_string());
    }
    return false;
}

std::optional<std::string> SimpleRegex::replace(std::string_view subject, std::string_view replacement) const {
    if (!m_re) {
        return std::nullopt;
    }
    uint32_t options = PCRE2_SUBSTITUTE_GLOBAL | PCRE2_SUBSTITUTE_UNSET_EMPTY | PCRE2_SUBSTITUTE_EXTENDED;
    auto replace_result = m_re->replace(options, subject, replacement);
    if (auto *replace = std::get_if<RegexReplaceOnceSuccess>(&replace_result)) {
        return replace->string;
    }
    if (auto *error = std::get_if<RegexReplaceError>(&replace_result)) {
        warnlog(m_log, "Failed to regex replace: {}", error->to_string());
    }
    return std::nullopt;
}

std::optional<Regex> SimpleRegex::compile_regex(std::string_view text, uint32_t options) {
    auto compile_result = Regex::compile(text, options);
    if (auto *regex = std::get_if<Regex>(&compile_result)) {
        return std::move(*regex);
    }
    if (auto *error = std::get_if<RegexCompileError>(&compile_result)) {
        warnlog(m_log, "Failed to compile Regex: {}", error->to_string());
    }
    return std::nullopt;
}

} // namespace ag
