#include <gtest/gtest.h>

#include "common/base64.h"

#include <string>
#include <string_view>

static std::string to_str(const std::optional<std::vector<uint8_t>> &vec) {
    if (!vec.has_value()) {
        return "";
    }
    return  {vec->begin(), vec->end()};
}

TEST(base64, basic) {
    std::string_view encoded = "SGVsbG8sIHdvcmxkIQ==";
    std::string_view expect = "Hello, world!";
    auto decoded = ag::decode_base64(encoded, false);
    ASSERT_TRUE(decoded.has_value());
    ASSERT_EQ(expect, to_str(decoded));
    ASSERT_EQ(ag::encode_to_base64({ (uint8_t *)decoded->data(), decoded->size() }, false), encoded);
    ASSERT_FALSE(ag::decode_base64(encoded.substr(0, 5), false).has_value());
}

TEST(base64, decode) {
    ASSERT_EQ("\xfa", to_str(ag::decode_base64("+g==", false)));
    ASSERT_EQ("\xfe", to_str(ag::decode_base64("/g==", false)));
    ASSERT_EQ("\xfa", to_str(ag::decode_base64("+g=", false)));
    ASSERT_EQ("\xfe", to_str(ag::decode_base64("/g=", false)));
    ASSERT_EQ("\xfa", to_str(ag::decode_base64("+g", false)));
    ASSERT_EQ("\xfe", to_str(ag::decode_base64("/g", false)));
}

TEST(base64, decodeurl) {
    ASSERT_EQ(std::vector<uint8_t>({0xfa}), ag::decode_base64("-g==", true));
    ASSERT_EQ(std::vector<uint8_t>({0xfe}), ag::decode_base64("_g==", true));
    ASSERT_EQ(std::vector<uint8_t>({0xfa}), ag::decode_base64("-g=", true));
    ASSERT_EQ(std::vector<uint8_t>({0xfe}), ag::decode_base64("_g=", true));
    ASSERT_EQ(std::vector<uint8_t>({0xfa}), ag::decode_base64("-g", true));
    ASSERT_EQ(std::vector<uint8_t>({0xfe}), ag::decode_base64("_g", true));
}

TEST(base64, encode_outputiter_version) {
    std::string_view origin = "Hello, world!";
    const auto *data = reinterpret_cast<const uint8_t*>(origin.data());
    std::string_view expect = "SGVsbG8sIHdvcmxkIQ==";

    std::string encoded;
    encoded.resize(ag::encode_base64_size(origin.size()));
    ag::encode_to_base64(data, false, encoded.begin());
    ASSERT_EQ(encoded, expect);

    encoded.clear();
    ag::encode_to_base64(data, false, std::back_inserter(encoded));
    ASSERT_EQ(encoded, expect);
}