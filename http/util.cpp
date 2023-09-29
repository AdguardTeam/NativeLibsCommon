#include "common/http/util.h"

namespace ag::http {

uint32_t version_get_major(Version v) {
    return v >> 8u;
}

uint32_t version_get_minor(Version v) {
    return v & 0xffu; // NOLINT(*-magic-numbers)
}

std::optional<Version> make_version(uint8_t major, uint8_t minor) {
    auto v = Version((uint32_t(major) << 8u) | minor);
    uint32_t conv_major = version_get_major(v);
    uint32_t conv_minor = version_get_minor(v);
    return (major == conv_major && minor == conv_minor) ? std::make_optional(v) : std::nullopt;
}

} // namespace ag::http
