#include <gtest/gtest.h>
#include "common/utils.h"
#include "common/socket_address.h"

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
}
