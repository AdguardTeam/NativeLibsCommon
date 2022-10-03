#include <gtest/gtest.h>

#include "common/net_utils.h"

using namespace ag;

TEST(NetUtils, SplitHostTest) {
    std::string ipv4_addr_w_port = "111.112.113.114:4433";
    auto result = utils::split_host_port(ipv4_addr_w_port);
    ASSERT_EQ(result->first, "111.112.113.114");
    ASSERT_EQ(result->second, "4433");
    ASSERT_FALSE(result.has_error());

    std::string ipv4_addr = "111.112.113.114";
    result = utils::split_host_port(ipv4_addr);
    ASSERT_EQ(result->first, "111.112.113.114");
    ASSERT_EQ(result->second, "");
    ASSERT_FALSE(result.has_error());

    std::string ipv4_addr_empty_port = "111.112.113.114:";
    result = utils::split_host_port(ipv4_addr_empty_port);
    ASSERT_FALSE(result.has_error());
    ASSERT_EQ(result->first, "111.112.113.114");

    result = utils::split_host_port(ipv4_addr_empty_port, true, true);
    ASSERT_TRUE(result.has_error());
    ASSERT_EQ(result.error()->value(), utils::NetUtilsError::AE_IPV4_PORT_EMPTY);

    std::string ipv6_addr_w_port = "[ffff::0]:4433";
    result = utils::split_host_port(ipv6_addr_w_port, true, true);
    ASSERT_EQ(result->first, "ffff::0");
    ASSERT_EQ(result->second, "4433");
    ASSERT_FALSE(result.has_error());

    std::string ipv6_addr = "[ffff::0]";
    result = utils::split_host_port(ipv6_addr, true, true);
    ASSERT_EQ(result->first, "ffff::0");
    ASSERT_EQ(result->second, "");
    ASSERT_FALSE(result.has_error());

    std::string ipv6_addr_empty_port = "[ffff::0]:";
    result = utils::split_host_port(ipv6_addr_empty_port, true, true);
    ASSERT_TRUE(result.has_error());
    ASSERT_EQ(result.error()->value(), utils::NetUtilsError::AE_IPV6_PORT_EMPTY);

    std::string ipv6_addr_miss_brackets = "ffff::0";
    result = utils::split_host_port(ipv6_addr_miss_brackets);
    ASSERT_FALSE(result.has_error());
    ASSERT_EQ(result.value().first, "ffff::0");

    result = utils::split_host_port(ipv6_addr_miss_brackets, true, true);
    ASSERT_TRUE(result.has_error());
    ASSERT_EQ(result.error()->value(), utils::NetUtilsError::AE_IPV6_MISSING_BRACKETS);

    std::string ipv6_addr_miss_right_bracket = "[ffff::0";
    result = utils::split_host_port(ipv6_addr_miss_right_bracket, true, true);
    ASSERT_TRUE(result.has_error());
    ASSERT_EQ(result.error()->value(), utils::NetUtilsError::AE_IPV6_MISSING_RIGHT_BRACKET);
}
