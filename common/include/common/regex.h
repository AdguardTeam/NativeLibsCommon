#pragma once

#include <string_view>
#include <string>
#include <pcre2.h>

#include "common/defs.h"
#include "common/utils.h"
#include "common/logger.h"

namespace ag {

using Pcre2CodePtr = UniquePtr<pcre2_code, &pcre2_code_free>;

struct RegexReplaceError {
    /** Error code */
    int error;

    [[nodiscard]] std::string to_string() const;
};

/** `regexReplaceOnce()` success */
struct RegexReplaceOnceSuccess {
    /** Number of substitutions made */
    size_t substitutions_number;
    /** Modified text */
    std::string string;
};

using RegexReplaceOnceResult = std::variant<
        RegexReplaceOnceSuccess
        , RegexReplaceError
>;

/** `regexReplaceAll()` success */
struct RegexReplaceAllSuccess {
    /** Modified text */
    std::string string;
};

using RegexReplaceAllResult = std::variant<
        RegexReplaceAllSuccess
        , RegexReplaceError
>;

struct RegexCompileError {
    /** The regular expression text */
    std::string pattern;
    /** Error code */
    int error;
    /** Error offset */
    size_t offset;

    [[nodiscard]] std::string to_string() const;
};

struct RegexMatchError {
    /** Error code */
    int error;

    [[nodiscard]] std::string to_string() const;
};

struct RegexMatch {
    /// Match groups including whole match as first group
    std::vector<std::pair<size_t, size_t>> match_groups;

    std::string_view group_substr(std::string_view text, size_t group_id) const {
        if (group_id >= match_groups.size()) {
            return {};
        }
        const auto &[begin, end] = match_groups.at(group_id);
        return text.substr(0, end).substr(begin);
    }
};

struct RegexNoMatch {};

class Regex {
public:
    using CompileResult = std::variant<
            Regex
            , RegexCompileError
    >;

    using MatchResult = std::variant<
            RegexMatch
            , RegexNoMatch
            , RegexMatchError
    >;

    Regex() = delete;
    Regex(Regex &&) = default;
    Regex &operator=(Regex &&) = default;
    Regex(const Regex &other) { *this = other; }
    Regex &operator=(const Regex &);
    ~Regex() = default;

    /**
     * Compile the regular expression
     * @param regex String representation of the expression
     * @param options The expression options (optional)
     * @param compile_context The compile context (optional)
     * @return see `RegexCompileResult`
     */
    static CompileResult compile(
            std::string_view regex,
            uint32_t options = 0,
            pcre2_compile_context *compile_context = nullptr
    );

    /**
     * Match the regular expression
     * @param text  text to match against
     * @param options  option bits
     * @param start_offset  where to start in the subject string
     * @param mcontext  points a PCRE2 context
     * @return See `MatchResult`
     */
    [[nodiscard]] MatchResult match(
            std::string_view text,
            uint32_t options = 0,
            size_t start_offset = 0,
            pcre2_match_context *mcontext = nullptr
    ) const;

    /**
    * Apply the replace expression one time
    * @param options The regular expression options
    * @param pattern The string to modify
    * @param replace_expression The replace expression
    * @param match_context The match context (optional)
    * @return see `RegexReplaceOnceResult`
    */
    RegexReplaceOnceResult replace(
            uint32_t options,
            std::string_view pattern,
            std::string_view replace_expression,
            pcre2_match_context *match_context = nullptr
    ) const;

    /**
    * Apply the replace expression multiple times until it is applicable
    * @param options The regular expression options
    * @param pattern The string to modify
    * @param replace_expression The replace expression
    * @param match_context The match context (optional)
    * @return see `ReplaceAllResult`
    */
    RegexReplaceAllResult replace_all(
            uint32_t options,
            std::string pattern,
            std::string_view replace_expression,
            pcre2_match_context *match_context = nullptr
    ) const;

    int callout_enumerate(std::function<int(pcre2_callout_enumerate_block *)> callback) const;

    /**
     * Get the underlying regex representation
     */
    [[nodiscard]] const pcre2_code *get() const;

private:
    Pcre2CodePtr m_regex;

    explicit Regex(Pcre2CodePtr);
};

class LazyRegex {
public:
    using MatchResult = std::variant<
            RegexMatch
            , RegexNoMatch
            , RegexCompileError
            , RegexMatchError
    >;

    explicit LazyRegex(
            std::string txt,
            uint32_t options = 0,
            pcre2_compile_context *compile_context = nullptr
    );

    LazyRegex() = default;
    LazyRegex(LazyRegex &&) = default;
    LazyRegex &operator=(LazyRegex &&) = default;
    LazyRegex(const LazyRegex &other) = default;
    LazyRegex &operator=(const LazyRegex &) = default;
    ~LazyRegex() = default;

    /**
     * Get the compiled expression compiling it if it is needed.
     * @note: Drops a compilation error.
     */
    [[nodiscard]] const Regex *get() const;

    /**
     * Match the regular expression
     * @param text  text to match against
     * @param options  option bits
     * @param start_offset  where to start in the subject string
     * @param mcontext  points a PCRE2 context
     * @return See `MatchResult`
     */
    [[nodiscard]] MatchResult match(
            std::string_view text,
            uint32_t options = 0,
            size_t start_offset = 0,
            pcre2_match_context *mcontext = nullptr
    ) const;

private:
    struct Uncompiled {
        std::string txt;
        uint32_t options = 0;
        pcre2_compile_context *compile_context = nullptr;
    };
    using Compiled = Regex;

    mutable std::variant<Uncompiled, Compiled> m_regex = Uncompiled{};

    [[nodiscard]] std::optional<RegexCompileError> compile() const;
};

/**
 * Regular expression wrapper which, as an optimization, checks presence of
 * the shortcut in the matched text before applying the regular expression.
 * @tparam Regex the underlying regular expression
 */
template<class Regex>
class ShortcuttedRegex {
public:
    ShortcuttedRegex(bool case_sensitive, std::string shortcut, Regex re)
            : m_case_sensitive(case_sensitive)
            , m_shortcut(std::move(shortcut))
            , m_underlying_regex(std::move(re))
    {}

    ShortcuttedRegex(const ShortcuttedRegex &) = default;
    ShortcuttedRegex &operator=(const ShortcuttedRegex &) = default;
    ShortcuttedRegex(ShortcuttedRegex &&) noexcept = default;
    ShortcuttedRegex &operator=(ShortcuttedRegex &&) noexcept = default;
    ~ShortcuttedRegex() = default;

    template<class Result = decltype(std::declval<Regex>().match("")), typename... Ts>
    Result match(std::string_view text, Ts&&... args) const {
        if ((this->m_case_sensitive && text.find(this->m_shortcut) == text.npos)
                || (!this->m_case_sensitive && std::string::npos == utils::ifind(text, this->m_shortcut))) {
            return RegexNoMatch{};
        }
        return this->m_underlying_regex.match(text, std::forward<Ts>(args)...);
    }

private:
    bool m_case_sensitive = false;
    std::string m_shortcut;
    Regex m_underlying_regex;
};


class SimpleRegex {
public:
    explicit SimpleRegex(std::string_view text, uint32_t pcre2_compile_options = PCRE2_CASELESS)
        : m_re(compile_regex(text, pcre2_compile_options))
    {}

    explicit SimpleRegex(Regex re)
        : m_re(std::move(re))
    {}

    ~SimpleRegex() = default;

    SimpleRegex(const SimpleRegex &other) {
        *this = other;
    }

    SimpleRegex(SimpleRegex &&other) noexcept {
        *this = std::move(other);
    }

    SimpleRegex &operator=(const SimpleRegex &other) = default;

    SimpleRegex &operator=(SimpleRegex &&other) noexcept {
        std::swap(m_re, other.m_re);
        return *this;
    }

    /**
     * Check if Regex compiled successfully
     */
    [[nodiscard]] bool is_valid() const { return m_re.has_value(); }

    /**
     * Match string against Regex
     * @param[in]  str   string to match
     * @return     True if matches, false otherwise
     */
    [[nodiscard]] bool match(std::string_view str) const;

    /**
     * Replace string by Regex
     * @param[in]  subject      string to process
     * @param[in]  replacement  replacement string
     * @return     Apply result (std::nullopt in case of error)
     */
    [[nodiscard]] std::optional<std::string> replace(std::string_view subject, std::string_view replacement) const;

private:
    static const Logger m_log;

    std::optional<Regex> m_re;

    static std::optional<Regex> compile_regex(std::string_view text, uint32_t options);
};

} // namespace ag
