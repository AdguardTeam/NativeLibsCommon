#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "common/base64.h"

namespace ag {

using Base64Table = std::array<uint8_t, 65>;
using Basis = std::array<uint8_t, 256>;

static constexpr Base64Table url_safe_base64_table(const Base64Table &xs) noexcept {
    Base64Table result = xs;
    for (auto &x : result) {
        switch (x) {
        case '+':
            x = '-';
            continue;
        case '/':
            x = '_';
            continue;
        default:
            continue;
        }
    }
    return result;
}

static constexpr Basis url_safe_basis(const Basis &xs) noexcept {
    // TODO use constexpr std::swap since C++20
    auto constexpr_swap = [](auto &a, auto &b) {
        auto t = a;
        a = b;
        b = t;
    };
    Basis result = xs;
    constexpr_swap(result['-'], result['+']);
    constexpr_swap(result['_'], result['/']);
    return result;
}

static constexpr size_t decode_base64_max_size(size_t len) noexcept {
    return (len + 3) / 4 * 3;
}

static constexpr Base64Table BASE64_TABLE_DEFAULT{"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"};
static constexpr Base64Table BASE64_TABLE_URL_SAFE = url_safe_base64_table(BASE64_TABLE_DEFAULT);
// clang-format off
static constexpr Basis BASIS_DEFAULT{
        77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77,
        77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77,
        77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 62, 77, 77, 77, 63,
        52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 77, 77, 77, 77, 77, 77,
        77,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
        15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 77, 77, 77, 77, 77,
        77, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
        41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 77, 77, 77, 77, 77,

        77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77,
        77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77,
        77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77,
        77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77,
        77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77,
        77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77,
        77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77,
        77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77,
};
// clang-format on
static constexpr Basis BASIS_URL_SAFE = url_safe_basis(BASIS_DEFAULT);
static constexpr auto PADDING = '=';

template <typename OutputIterator>
void encode_to_base64(Uint8View data, bool url_safe, OutputIterator dest) {
    const auto &base64_table = url_safe ? BASE64_TABLE_URL_SAFE : BASE64_TABLE_DEFAULT;
    auto in_pos = data.data();
    auto end = data.data() + data.size();

    while (in_pos + 2 < end) {
        *dest++ = base64_table[in_pos[0] >> 2];
        *dest++ = base64_table[((in_pos[0] & 0x03) << 4) | (in_pos[1] >> 4)];
        *dest++ = base64_table[((in_pos[1] & 0x0f) << 2) | (in_pos[2] >> 6)];
        *dest++ = base64_table[in_pos[2] & 0x3f];
        in_pos += 3;
    }

    if (in_pos < end) {
        *dest++ = base64_table[in_pos[0] >> 2];
        if (end - in_pos == 1) {
            *dest++ = base64_table[(in_pos[0] & 0x03) << 4];
            if (!url_safe) {
                *dest++ = PADDING;
            }
        } else {
            *dest++ = base64_table[((in_pos[0] & 0x03) << 4) | (in_pos[1] >> 4)];
            *dest++ = base64_table[(in_pos[1] & 0x0f) << 2];
        }
        if (!url_safe) {
            *dest++ = PADDING;
        }
    }
}

std::string encode_to_base64(Uint8View data, bool url_safe) {
    std::string result;
    result.resize(encode_base64_size(data.size(), url_safe));
    encode_to_base64(data, url_safe, result.begin());

    return result;
}

std::optional<std::vector<uint8_t>> decode_base64(std::string_view data, bool url_safe) {
    const auto &basis = url_safe ? BASIS_URL_SAFE : BASIS_DEFAULT;
    auto src = data.data();
    auto src_len = data.size();
    size_t len = 0;
    for (len = 0; len < src_len; len++) {
        if (src[len] == PADDING) {
            break;
        }
        if (basis[src[len]] == 77) {
            return std::nullopt;
        }
    }
    if (len % 4 == 1) {
        return std::nullopt;
    }
    std::vector<uint8_t> result;
    result.reserve(decode_base64_max_size(src_len));
    auto s = src;
    while (len > 3) {
        result.emplace_back(basis[s[0]] << 2 | basis[s[1]] >> 4);
        result.emplace_back(basis[s[1]] << 4 | basis[s[2]] >> 2);
        result.emplace_back(basis[s[2]] << 6 | basis[s[3]]);
        s += 4;
        len -= 4;
    }
    if (len > 1) {
        result.emplace_back(basis[s[0]] << 2 | basis[s[1]] >> 4);
    }
    if (len > 2) {
        result.emplace_back(basis[s[1]] << 4 | basis[s[2]] >> 2);
    }
    return result;
}

template void encode_to_base64(Uint8View data, bool url_safe, std::string::iterator dest);
template void encode_to_base64(Uint8View data, bool url_safe, std::back_insert_iterator<std::string> dest);

} // namespace ag
