#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "common/defs.h"

namespace ag {

/**
 * Get the size of encoded data
 * @param data_size size of the original binary data
 * @param url_safe when using the URL-safe alphabet, we also don't write padding
 */
constexpr size_t encode_base64_size(size_t data_size, bool url_safe) noexcept {
    return url_safe ? (data_size * 4 + 2) / 3 // ceil(data_size * 4 / 3)
                    : (data_size + 2) / 3 * 4; // ceil(data_size / 3) * 4
}

/**
 * Creates Base64-encoded string from data
 * @param dest where to store the output
 * @note Caller should check that dest can be incremented encode_base64_size() times
 */
template <typename OutputIterator>
void encode_to_base64(Uint8View data, bool url_safe, OutputIterator dest);

/**
 * Creates Base64-encoded string from data
 * @param data data to encode
 * @param url_safe is string should be url safe or not
 */
std::string encode_to_base64(Uint8View data, bool url_safe);

/**
 * Decode data from Base64-encoded string
 * @param data Base64-encoded string
 * @param url_safe is string url safe or not
 * @return optional with bytes or null optional if string is not valid Base64-encoded
 */
std::optional<std::vector<uint8_t>> decode_base64(std::string_view data, bool url_safe);

} // namespace ag
