#include <gtest/gtest.h>

#include "common/cesu8.h"
#include "common/socket_address.h"
#include "common/utils.h"

TEST(utils, GenerallyWork) {
    ASSERT_TRUE(ag::utils::is_valid_ip6("::"));
    ASSERT_TRUE(ag::utils::is_valid_ip6("::1"));
    ASSERT_TRUE(ag::utils::is_valid_ip4("0.0.0.0"));
    ASSERT_TRUE(ag::utils::is_valid_ip4("127.0.0.1"));

    ASSERT_FALSE(ag::utils::is_valid_ip6("[::]:80"));
    ASSERT_FALSE(ag::utils::is_valid_ip6("[::1]:80"));
    ASSERT_FALSE(ag::utils::is_valid_ip6("45:67"));
    ASSERT_FALSE(ag::utils::is_valid_ip4("0.0.0.0:80"));
    ASSERT_FALSE(ag::utils::is_valid_ip4("127.0.0.1:80"));
    ASSERT_FALSE(ag::utils::is_valid_ip4("45:67"));
    ASSERT_FALSE(ag::utils::is_valid_ip6("[::]"));
    ASSERT_FALSE(ag::utils::is_valid_ip6("[::1]"));
    ASSERT_FALSE(ag::utils::is_valid_ip6("[1.2.3.4]"));

    ASSERT_EQ(15, ag::utils::to_integer<uint16_t>("f", 16));
    ASSERT_EQ(15, ag::utils::to_integer<uint16_t>("F", 16));
    ASSERT_EQ(0xabcd, ag::utils::to_integer<uint16_t>("abcd", 16));
    ASSERT_EQ(0xabcd, ag::utils::to_integer<uint16_t>("ABCD", 16));
    ASSERT_EQ(-0xabcd, ag::utils::to_integer<int32_t>("-ABCD", 16));
    ASSERT_EQ(12345, ag::utils::to_integer<int32_t>("12345"));
    ASSERT_EQ(-12345, ag::utils::to_integer<int32_t>("-12345"));
    ASSERT_EQ(10, ag::utils::to_integer<int32_t>("010"));
    ASSERT_EQ(8, ag::utils::to_integer<int32_t>("010", 8));
    ASSERT_EQ(8, ag::utils::to_integer<int32_t>("10", 8));
    ASSERT_EQ(53, ag::utils::to_integer<uint16_t>("99999").value_or(53));
    ASSERT_FALSE(ag::utils::to_integer<uint8_t>("abcd", 16));
    ASSERT_FALSE(ag::utils::to_integer<uint8_t>("1234"));
    ASSERT_FALSE(ag::utils::to_integer<uint64_t>("-1"));
    ASSERT_FALSE(ag::utils::to_integer<uint16_t>("65538"));
    ASSERT_FALSE(ag::utils::to_integer<uint16_t>(""));
    ASSERT_FALSE(ag::utils::to_integer<uint16_t>("asdf"));
    ASSERT_FALSE(ag::utils::to_integer<uint16_t>(" 1 "));
    ASSERT_FALSE(ag::utils::to_integer<uint16_t>("1 asdf"));
    ASSERT_FALSE(ag::utils::to_integer<uint16_t>("+1"));
    ASSERT_FALSE(ag::utils::to_integer<uint16_t>("1asdf"));
    ASSERT_FALSE(ag::utils::to_integer<uint16_t>("asdf1"));

    ASSERT_EQ(ag::utf8_to_cesu8("asdasd"), "asdasd");
    ASSERT_EQ(ag::utf8_to_cesu8("12345678"), "12345678");
    ASSERT_EQ(ag::utf8_to_cesu8("фыва"), "фыва");
    ASSERT_EQ(ag::utf8_to_cesu8("фы1ва"), "фы1ва");
    ASSERT_EQ(ag::utf8_to_cesu8("\xff\xff"), "��");
    ASSERT_EQ(ag::utf8_to_cesu8("\xF0\x90\x90\x80"), "\xED\xA0\x81\xED\xB0\x80");
    ASSERT_EQ(ag::cesu8_len("\xF0\x9F\x98\x81"), 6);
    ASSERT_EQ(ag::cesu8_len("asdasd"), 6);
    ASSERT_EQ(ag::cesu8_len("12345678"), 8);
    ASSERT_EQ(ag::cesu8_len(""), 0);

    std::vector<std::string> string_vec{"111", "222", "333", "444"};
    ASSERT_EQ(ag::utils::join(string_vec.begin(), string_vec.end(), ":"), "111:222:333:444");
    ASSERT_EQ(ag::utils::join(string_vec.begin() + 2, string_vec.end(), ":"), "333:444");

    std::vector<std::string_view> string_view_vec{"111", "222", "333", "444"};
    ASSERT_EQ(ag::utils::join(string_view_vec.begin(), string_view_vec.end(), ":"), "111:222:333:444");
    ASSERT_EQ(ag::utils::join(string_view_vec.begin() + 2, string_view_vec.end(), ":"), "333:444");

    const char *str_array[] = {"aaa", "bbb", "ccc", "ddd"};
    ASSERT_EQ(ag::utils::join(std::begin(str_array), std::end(str_array), "::"), "aaa::bbb::ccc::ddd");
    ASSERT_EQ(ag::utils::join(std::begin(str_array) + 1, std::end(str_array), "::"), "bbb::ccc::ddd");
}