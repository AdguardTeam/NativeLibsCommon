#pragma once

#include <fmt/format.h>

namespace ag {

template <typename Char> class CountingCompileParseContext : public fmt::detail::compile_parse_context<Char> {
public:
    using CompileParseCtx = fmt::detail::compile_parse_context<Char>;
    explicit FMT_CONSTEXPR CountingCompileParseContext(
            fmt::basic_string_view<Char> format_str, int num_args, const fmt::detail::type* types, int next_arg_id = 0)
            : CompileParseCtx(format_str, num_args, types, next_arg_id) {}

    int last_next_arg_id_ = 0;
    FMT_CONSTEXPR auto next_arg_id() -> int {
        int ret = CompileParseCtx::next_arg_id();
        last_next_arg_id_ = ret + 1;
        return ret;
    }

    FMT_CONSTEXPR void check_arg_id(int id) {
        last_next_arg_id_ = -1;
        CompileParseCtx::check_arg_id(id);
    }
};

template <typename Char, typename... Args> class StrictFormatStringChecker {
private:
    using parse_context_type = CountingCompileParseContext<Char>;
    static constexpr int num_args = sizeof...(Args);

    // Format specifier parsing function.
    // In the future basic_format_parse_context will replace compile_parse_context
    // here and will use is_constant_evaluated and downcasting to access the data
    // needed for compile-time checks: https://godbolt.org/z/GvWzcTjh1.
    using parse_func = const Char* (*)(parse_context_type&);

    fmt::detail::type types_[num_args > 0 ? static_cast<size_t>(num_args) : 1];
    parse_context_type context_;
    parse_func parse_funcs_[num_args > 0 ? static_cast<size_t>(num_args) : 1];

public:
    explicit FMT_CONSTEXPR StrictFormatStringChecker(fmt::basic_string_view<Char> fmt)
            : types_{fmt::detail::mapped_type_constant<Args, fmt::buffer_context<Char>>::value...},
            context_(fmt, num_args, types_),
            parse_funcs_{&fmt::detail::parse_format_specs<Args, parse_context_type>...} {}

    FMT_CONSTEXPR void on_text(const Char*, const Char*) {}

    FMT_CONSTEXPR auto on_arg_id() -> int { return context_.next_arg_id(); }
    FMT_CONSTEXPR auto on_arg_id(int id) -> int {
        return context_.check_arg_id(id), id;
    }
    FMT_CONSTEXPR auto on_arg_id(fmt::basic_string_view<Char> id) -> int {
#if FMT_USE_NONTYPE_TEMPLATE_ARGS
        auto index = fmt::detail::get_arg_index_by_name<Args...>(id);
        if (index < 0) on_error("named argument is not found");
        return index;
#else
        (void)id;
        on_error("compile-time checks for named arguments require C++20 support");
        return 0;
#endif
    }

    FMT_CONSTEXPR void on_replacement_field(int id, const Char* begin) {
        on_format_specs(id, begin, begin);  // Call parse() on empty specs.
    }

    FMT_CONSTEXPR auto on_format_specs(int id, const Char* begin, const Char*)
            -> const Char* {
        context_.advance_to(begin);
        // id >= 0 check is a workaround for gcc 10 bug (#2065).
        return id >= 0 && id < num_args ? parse_funcs_[id](context_) : begin;
    }

    FMT_CONSTEXPR void on_error(const char* message) {
        fmt::detail::throw_format_error(message);
    }

    FMT_CONSTEXPR void check_num_args() {
        if (context_.last_next_arg_id_ != -1 && context_.last_next_arg_id_ != context_.num_args()) {
            on_error("Number of arguments does not match");
        }
    }
};

/** A compile-time format string. */
template <typename Char, typename... Args> class BasicStrictFormatString {
private:
    fmt::basic_string_view<Char> str_;

public:
    template <typename S,
            FMT_ENABLE_IF(
                    std::is_convertible<const S&, fmt::basic_string_view<Char>>::value)>
    FMT_CONSTEVAL FMT_INLINE BasicStrictFormatString(const S& s) : str_(s) {
        static_assert(
                fmt::detail::count<
                        (std::is_base_of<fmt::detail::view, fmt::remove_reference_t<Args>>::value &&
                                std::is_reference<Args>::value)...>() == 0,
                "passing views as lvalues is disallowed");
#ifdef FMT_HAS_CONSTEVAL
        if constexpr (fmt::detail::count_named_args<Args...>() ==
                fmt::detail::count_statically_named_args<Args...>()) {
            using checker =
                    StrictFormatStringChecker<Char, fmt::remove_cvref_t<Args>...>;
            checker check(s);
            fmt::detail::parse_format_string<true>(str_, check);
            check.check_num_args();
        }
#else
        fmt::detail::check_format_string<Args...>(s);
#endif
    }
    BasicStrictFormatString(fmt::runtime_format_string<Char> fmt) : str_(fmt.str) {}

    FMT_INLINE operator fmt::basic_string_view<Char>() const { return str_; }
    FMT_INLINE auto get() const -> fmt::basic_string_view<Char> { return str_; }
};

template <typename... Args>
using StrictFormatString = BasicStrictFormatString<char, fmt::type_identity_t<Args>...>;

template <typename... T>
FMT_INLINE void print(StrictFormatString<T...> fmt, T&&... args) {
    const auto& vargs = fmt::make_format_args(args...);
    return fmt::vprint(fmt, vargs);
}

template <typename... T>
FMT_INLINE void print(FILE *file, StrictFormatString<T...> fmt, T&&... args) {
    const auto& vargs = fmt::make_format_args(args...);
    return fmt::vprint(file, fmt, vargs);
}

template <typename... T>
FMT_INLINE std::string format(StrictFormatString<T...> fmt, T&&... args) {
    const auto& vargs = fmt::make_format_args(args...);
    return fmt::vformat(fmt, vargs);
}

} // namespace ag