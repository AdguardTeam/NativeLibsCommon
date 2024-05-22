#pragma once

#include <fmt/format.h>

namespace ag {

/*
 * This header implements stricter format string checking than fmt does.
 * fmt only checks that the number of arguments passed is sufficient for the format string
 * but does not fail if there are extra arguments passed.
 * This is intended behaviour in fmt: https://github.com/fmtlib/fmt/issues/2413
 * However, this extra check is necessary for us in cases when switching from printf to fmt.
 * Otherwise, there may be leftovers with unconverted format strings.
 *
 * Therefore, we implement `StrictFormatString`, which checks that the number of arguments passed
 * equals the number of arguments in the format string.
 * The number of arguments in the format string matches the "next argument id",
 * assuming that argument ids are counted from 0.
 *
 * The main problem with the implementation is that the `basic_format_parse_context::next_arg_id_` field
 * is private, so we create a class `CountingCompileParseContext`, that predicts this field's value
 * at the constant evaluation phase.
 */

/**
 * This class predicts value of basic_format_parse_context::next_arg_id_ field which is private.
 */
template <typename Char> class CountingCompileParseContext : public fmt::detail::compile_parse_context<Char> {
public:
    using CompileParseCtx = fmt::detail::compile_parse_context<Char>;
    explicit FMT_CONSTEXPR CountingCompileParseContext(
            fmt::basic_string_view<Char> format_str, int num_args, const fmt::detail::type* types, int next_arg_id = 0)
            : CompileParseCtx(format_str, num_args, types, next_arg_id) {}

    int next_arg_id_prediction_ = 0;
    FMT_CONSTEXPR auto next_arg_id() -> int {
        int ret = CompileParseCtx::next_arg_id();
        next_arg_id_prediction_ = ret + 1;
        return ret;
    }

    FMT_CONSTEXPR void check_arg_id(int id) {
        next_arg_id_prediction_ = -1; // Disabling check if indexed args are used
        CompileParseCtx::check_arg_id(id);
    }
};

/**
 * This class is a copy of fmt::detail::format_strict_checker with additions:
 * 1. `parse_context_type` is `CountingCompileParseContext`.
 * 2. New method `check_num_args()` which performs the actual check.
 *    It verifies if the next argument id matches the number of args and fails if it does not.
 * 3. New method `on_finishing_arg()` which is added to handle the case of
 *    `ag::format("{:{}s}", "", 10);`. For nested braces,
 *    `basic_format_parse_context::next_arg_id()` is called instead of
 *    `CountingCompileParseContext::next_arg_id()`, causing the count to fail.
 *    To solve this issue, we add an additional argument type to `StrictFormatString`
 *    and call `on_finishing_arg()` to synchronize `CountingCompileParseContext` state.
 */
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

    FMT_CONSTEXPR void on_finishing_arg() {
        if (context_.next_arg_id_prediction_ == -1) {
            // Checking is disabled in format string with manual indexing
            return;
        }
        context_.next_arg_id();
    }
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
        if (context_.next_arg_id_prediction_ != -1
                && context_.next_arg_id_prediction_ != context_.num_args()) {
            on_error("Number of arguments does not match");
        }
    }
};

/**
 * This class is copy of `basic_format_string` but uses `StrictFormatStringChecker`
 * and its extended interface.
 */
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

            // Eat extra argument passed by us to synchronize state.
            // See StrictFormatStringChecker doc for more details.
            check.on_finishing_arg();
            // Finally, do number of arguments check.
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

// Additional arg is handled by calling `StrictFormatStringChecker::on_finishing_arg()`
// inside `BasicStringFormatString` constructor.
template <typename... Args>
using StrictFormatString = BasicStrictFormatString<char, fmt::type_identity_t<Args>..., int>;

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