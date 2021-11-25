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
}
