#include <gtest/gtest.h>

#include "common/cesu8.h"
#include "common/socket_address.h"
#include "common/utils.h"
#include "common/url.h"

#include <span>
#include <vector>

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

    ASSERT_FALSE(ag::SocketAddress().valid());
    ag::IpAddress addr;
    ASSERT_FALSE(ag::SocketAddress(addr, 53).valid());
    addr = ag::Ipv4Address{127, 0, 0, 1};
    ASSERT_TRUE(ag::SocketAddress(addr, 53).valid());
    ASSERT_TRUE(ag::SocketAddress(addr, 53).is_ipv4());
    addr = ag::Ipv6Address{127, 0, 0, 1};
    ASSERT_TRUE(ag::SocketAddress(addr, 53).valid());
    ASSERT_TRUE(ag::SocketAddress(addr, 53).is_ipv6());
    ASSERT_FALSE(ag::SocketAddress(addr, 53).is_loopback());

    ASSERT_TRUE(!ag::SocketAddress("128.0.0.1:53").is_loopback());
    ASSERT_TRUE(ag::SocketAddress("127.0.0.1:53").is_loopback());
    ASSERT_TRUE(ag::SocketAddress("[::1]:53").is_loopback());
    ASSERT_TRUE(ag::SocketAddress("[::ffff:127.0.0.1]:53").is_loopback());
    ASSERT_TRUE(ag::SocketAddress("[::ffff:127.0.0.1]:53").is_ipv4_mapped());

    ASSERT_TRUE(!ag::SocketAddress("0.0.0.1:53").is_any());
    ASSERT_TRUE(!ag::SocketAddress("[::2]:53").is_any());
    ASSERT_TRUE(ag::SocketAddress("0.0.0.0:53").is_any());
    ASSERT_TRUE(ag::SocketAddress("[::]:53").is_any());
    ASSERT_TRUE(ag::SocketAddress("[::ffff:0.0.0.0]:53").is_any());
    ASSERT_TRUE(ag::SocketAddress("[::ffff:0.0.0.0]:53").is_ipv4_mapped());

    const char *str_array[] = {"aaa", "bbb", "ccc", "ddd"};
    ASSERT_EQ(ag::utils::join(std::begin(str_array), std::end(str_array), "::"), "aaa::bbb::ccc::ddd");
    ASSERT_EQ(ag::utils::join(std::begin(str_array) + 1, std::end(str_array), "::"), "bbb::ccc::ddd");

    ASSERT_TRUE(ag::SocketAddress("fe80::1", 53).valid());
    ASSERT_EQ(53, ag::SocketAddress("fe80::1", 53).port());

    ASSERT_TRUE(ag::SocketAddress("fe80::1%23", 53).valid());
    ASSERT_EQ(53, ag::SocketAddress("fe80::1%23", 53).port());

    ASSERT_TRUE(ag::SocketAddress("[fe80::1]:53").valid());
    ASSERT_EQ(53, ag::SocketAddress("[fe80::1]:53").port());
    ASSERT_TRUE(ag::SocketAddress("fe80::1:53").valid());
    ASSERT_EQ(0, ag::SocketAddress("fe80::1:53").port());

    ASSERT_TRUE(ag::SocketAddress("[fe80::1%23]:53").valid());
    ASSERT_EQ(53, ag::SocketAddress("[fe80::1%23]:53").port());

#if defined(__APPLE__) && defined(__MACH__)
    ASSERT_TRUE(ag::SocketAddress("fe80::1%abc", 53).valid());
    ASSERT_EQ(53, ag::SocketAddress("fe80::1%abc", 53).port());

    ASSERT_TRUE(ag::SocketAddress("fe80::1%eth0", 53).valid());
    ASSERT_EQ(42, ag::SocketAddress("fe80::1%eth0", 42).port());

    ASSERT_TRUE(ag::SocketAddress("fe80::1%utun1", 53).valid());
    ASSERT_EQ(53, ag::SocketAddress("fe80::1%utun1", 53).port());
#endif // defined(__APPLE__) && defined(__MACH__)
}

TEST(utils, iequals) {
    ASSERT_TRUE(ag::utils::iequals("AaAaA", "aaaaa"));
    ASSERT_TRUE(ag::utils::iequals("aaaaa", "AaAaA"));
}

TEST(utils, ifind) {
    ASSERT_EQ(ag::utils::ifind("AaAaB", "aaaab"), 0);
    ASSERT_EQ(ag::utils::ifind("aaaab", "AaAaB"), 0);
    ASSERT_EQ(ag::utils::ifind("", "aaaaa"), std::string_view::npos);
    ASSERT_EQ(ag::utils::ifind("aaa", ""), 0);
    ASSERT_EQ(ag::utils::ifind("aaaaa", "AaAaB"), std::string_view::npos);
    ASSERT_EQ(ag::utils::ifind("AaAaB", "aaaaa"), std::string_view::npos);
    ASSERT_EQ(ag::utils::ifind("AaAaB", "ab"), 3);
    ASSERT_EQ(ag::utils::ifind("AaBaB", "Ab"), 1);
    ASSERT_EQ(ag::utils::ifind("AaAaB", "aaaabb"), std::string_view::npos);
}

TEST(utils, encode_to_hex) {
    constexpr uint8_t DATA_0[] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x42};
    ASSERT_EQ("000102030442", ag::utils::encode_to_hex({DATA_0, std::size(DATA_0)}));
    ASSERT_EQ("", ag::utils::encode_to_hex({}));
}

TEST(utils, TestSplit2) {
    struct TestData {
        std::string original_str;
        int single_delim;
        std::string_view multiple_delim;
        std::array<std::string_view, 2> result;
        std::array<std::string_view, 2> r_result;
        std::array<std::string_view, 2> any_result;
    };

    const TestData TEST_DATA[] = {
            {"test-string", '-', "-", {"test", "string"}, {"test", "string"}, {"test", "string"}},
            {"another/string/with/multiple/delims", '/', "/", {"another", "string/with/multiple/delims"},
                    {"another/string/with/multiple", "delims"}, {"another", "string/with/multiple/delims"}},
            {"string_with spaces", ' ', "_ ", {"string_with", "spaces"}, {"string_with", "spaces"},
                    {"string", "with spaces"}},
            {"string_no_delims", 'x', "xy", {"string_no_delims", ""}, {"string_no_delims", ""},
                    {"string_no_delims", ""}},
            {"", ' ', "_ ", {"", ""}, {"", ""}, {"", ""}},
            {"two__delims", '_', " _", {"two", "_delims"}, {"two_", "delims"}, {"two", "_delims"}},
            {"two delims", ' ', " _", {"two", "delims"}, {"two", "delims"}, {"two", "delims"}},
            {"_nospaces", '_', "_ ", {"", "nospaces"}, {"", "nospaces"}, {"", "nospaces"}},
            {"trailing/delim/", '/', "/ ", {"trailing", "delim/"}, {"trailing/delim", ""}, {"trailing", "delim/"}},
            {"//doubleAtStart", '/', "/ ", {"", "/doubleAtStart"}, {"/", "doubleAtStart"}, {"", "/doubleAtStart"}},
    };

    for (const auto &data : TEST_DATA) {
        auto split_res = ag::utils::split2_by(data.original_str, data.single_delim);
        EXPECT_EQ(split_res, data.result);

        auto rsplit_res = ag::utils::rsplit2_by(data.original_str, data.single_delim);
        EXPECT_EQ(rsplit_res, data.r_result);

        auto split_by_any_res = ag::utils::split2_by_any_of(data.original_str, data.multiple_delim);
        EXPECT_EQ(split_by_any_res, data.any_result);
    }
}

TEST(utils, ToUpper) {
    EXPECT_EQ(ag::utils::to_upper("hello"), "HELLO");
    EXPECT_EQ(ag::utils::to_upper("Hello"), "HELLO");
    EXPECT_EQ(ag::utils::to_upper("HELLO"), "HELLO");
    EXPECT_EQ(ag::utils::to_upper("HeLlO"), "HELLO");

    EXPECT_EQ(ag::utils::to_upper("hello123"), "HELLO123");
    EXPECT_EQ(ag::utils::to_upper("123"), "123");

    EXPECT_EQ(ag::utils::to_upper("hello!@#"), "HELLO!@#");
    EXPECT_EQ(ag::utils::to_upper("HeLlo!@#"), "HELLO!@#");

    EXPECT_EQ(ag::utils::to_upper(""), "");
}

TEST(utils, ToLower) {
    EXPECT_EQ(ag::utils::to_lower("HELLO"), "hello");
    EXPECT_EQ(ag::utils::to_lower("Hello"), "hello");
    EXPECT_EQ(ag::utils::to_lower("hello"), "hello");
    EXPECT_EQ(ag::utils::to_lower("HeLlO"), "hello");

    EXPECT_EQ(ag::utils::to_lower("HELLO123"), "hello123");
    EXPECT_EQ(ag::utils::to_lower("123"), "123");

    EXPECT_EQ(ag::utils::to_lower("HELLO!@#"), "hello!@#");
    EXPECT_EQ(ag::utils::to_lower("HeLlo!@#"), "hello!@#");

    EXPECT_EQ(ag::utils::to_lower(""), "");
}

TEST(Uint8SpanTest, VectorTest) {
    std::vector<uint8_t> vec = {1, 2, 3, 4, 42};
    const auto vec_span = ag::as_u8s(vec);
    ASSERT_EQ(vec_span.size(), vec.size());
    for (size_t i = 0; i < vec.size(); ++i) {
        ASSERT_EQ(vec_span[i], vec[i]);
    }
}

TEST(Uint8SpanTest, StringTest) {
    std::string str = "Hello, world!";
    const auto str_span = ag::as_u8s(str);
    ASSERT_EQ(str_span.size(), str.size());
    for (size_t i = 0; i < str.size(); ++i) {
        ASSERT_EQ(str_span[i], static_cast<uint8_t>(str[i]));
    }
}

TEST(Uint8ViewTest, VectorTest) {
    std::vector<uint8_t> vec = {1, 2, 3, 4, 42};
    const auto vec_view = ag::as_u8v(vec);
    ASSERT_EQ(vec_view.size(), vec.size());
    for (size_t i = 0; i < vec.size(); ++i) {
        ASSERT_EQ(vec_view[i], vec[i]);
    }
}

TEST(Uint8ViewTest, StringTest) {
    std::string str = "Hello, world!";
    const auto str_view = ag::as_u8v(str);
    ASSERT_EQ(str_view.size(), str.size());
    for (size_t i = 0; i < str.size(); ++i) {
        ASSERT_EQ(str_view[i], static_cast<uint8_t>(str[i]));
    }
}

TEST(url, NormalizePath) {
    ASSERT_EQ("/a/c/d.html", ag::url::normalize_path("../a/b/../c/./d.html"));
    ASSERT_EQ("/a/c/d.html", ag::url::normalize_path("../a/b/../../a/./c/./d.html"));
    ASSERT_EQ("", ag::url::normalize_path(""));
    ASSERT_EQ("/a/b/c.d", ag::url::normalize_path("/a/b/c.d"));
}
