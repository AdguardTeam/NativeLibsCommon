#pragma once

#include <optional>

#include <fmt/format.h>

namespace ag::http {

/**
 * Supported HTTP versions
 */
enum Version {
    HTTP_0_9 = 0x0009,
    HTTP_1_0 = 0x0100,
    HTTP_1_1 = 0x0101,
    HTTP_2_0 = 0x0200,
    HTTP_3_0 = 0x0300,
};

/**
 * Returns major digit of HTTP protocol version
 * @param v see @ref Version
 * @return major digit
 */
uint32_t version_get_major(Version v);

/**
 * Returns minor digit of HTTP protocol version
 * @param v see @ref Version
 * @return minor digit
 */
uint32_t version_get_minor(Version v);

std::optional<Version> make_version(uint8_t major, uint8_t minor);

} // namespace ag::http

template <>
struct fmt::formatter<ag::http::Version> {
    template <typename ParseContext>
    constexpr auto parse(ParseContext &ctx) {
        return ctx.begin();
    }

    template <typename FormatContext>
    auto format(const ag::http::Version &v, FormatContext &ctx) const {
        return fmt::format_to(ctx.out(), "HTTP/{}.{}", ag::http::version_get_major(v), ag::http::version_get_minor(v));
    }
};
