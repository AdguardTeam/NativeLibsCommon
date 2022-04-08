#include <gtest/gtest.h>

#include "common/cidr_range.h"

using namespace ag;

#define IPADDR_LOOPBACK ((uint32_t)0x7f000001UL)

class CidrRangeTest : public ::testing::Test {
protected:

    void SetUp() override {

    }

    void TearDown() override {

    }
};

#ifndef _WIN32
static __int128_t number_of_ips(const CidrRange &range) {
    int pow = range.get_address().size() * 8 - range.get_prefix_len();
    return ((__int128_t)1) << pow;
}

static __int128_t number_of_ips(const std::vector<CidrRange> &ranges) {
    __int128_t result = 0;
    for (auto &range : ranges) {
        result += number_of_ips(range);
    }
    return result;
}
#endif // _WIN32

TEST_F(CidrRangeTest, testUtilMethods) {
    /* Shortened at end */
    auto expanded1 = CidrRange::expand_ipv6_string("2000::");
    ASSERT_EQ("2000:0:0:0:0:0:0:0", expanded1.value());
    /* Shortened at begin */
    auto expanded2 = CidrRange::expand_ipv6_string("::ffff:aaaa:bbbb");
    ASSERT_EQ("0:0:0:0:0:ffff:aaaa:bbbb", expanded2.value());
    /* Shortened in the middle */
    auto expanded3 = CidrRange::expand_ipv6_string("2001:db8:a::1");
    ASSERT_EQ("2001:db8:a:0:0:0:0:1", expanded3.value());
    /* Error! */
    auto expanded4 = CidrRange::expand_ipv6_string("2001:db8::a::1");
    ASSERT_TRUE(expanded4.has_error());
    /* Zero replaced by "::", has 8 ":"s but still valid */
    auto expanded5 = CidrRange::expand_ipv6_string("1:2:3:4:5:6:7::");
    ASSERT_EQ("1:2:3:4:5:6:7:0", expanded5.value());
    /* Zero replaced by "::", has 8 ":"s but still valid */
    auto expanded6 = CidrRange::expand_ipv6_string("::1:2:3:4:5:6:7");
    ASSERT_EQ("0:1:2:3:4:5:6:7", expanded6.value());

    auto shortened1 = CidrRange::shorten_ipv6_string("0:0:0:0:0:0:0:0");
    ASSERT_EQ("::", shortened1.value());
    auto shortened2 = CidrRange::shorten_ipv6_string("0000:0000:0000:0000:0000:0000:0000:1");
    ASSERT_EQ("::1", shortened2.value());
    auto shortened3 = CidrRange::shorten_ipv6_string("2000:0:0:0:0:0:0:0");
    ASSERT_EQ("2000::", shortened3.value());
    auto shortened4 = CidrRange::shorten_ipv6_string("0202:0000::0:0:0:0");
    ASSERT_EQ("202::", shortened4.value());
    auto shortened5 = CidrRange::shorten_ipv6_string("1:0:0:0:0:1:0:0");
    ASSERT_EQ("1::1:0:0", shortened5.value());

    auto addr1 = CidrRange::get_address_from_string("127.0.0.1");
    ASSERT_EQ(std::vector<uint8_t>({127, 0, 0, 1}), addr1.value());
    auto addr2 = CidrRange::get_address_from_string("::ffff:127.0.0.1");
    ASSERT_EQ(std::vector<uint8_t>({0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 255, 255, 127, 0, 0, 1}), addr2.value());
    auto addr3 = CidrRange::get_address_from_string("2001:db8:a::1");
    ASSERT_EQ(std::vector<uint8_t>({0x20, 0x01, 0xd, (uint8_t)0xb8, 0, 0xa, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1}), addr3.value());
}

TEST_F(CidrRangeTest, testCreate) {
    CidrRange range_from_empty("");
    CidrRange range1("2000::/3");
    CidrRange range2("2000::/3");
    ASSERT_EQ(range1, range2);
    CidrRange range3("172.16.0.0/16");
    CidrRange range4("172.16.0.0/16");
    ASSERT_EQ(range3, range4);
    CidrRange range5("192.168.0.3", 24);
    CidrRange range6("192.168.0.3/24");
    ASSERT_EQ(range5, range6);
    CidrRange loopBack("127.0.0.1");
    ASSERT_EQ(IPADDR_LOOPBACK, loopBack.to_uint32());
}

static void test_split_with_params(const std::string &original, const std::string &splitted_left, const std::string &splitted_right) {
    CidrRange range(original);
    CidrRange range_left_exp(splitted_left);
    CidrRange range_right_exp(splitted_right);
    auto splitted_range = range.split();
    ASSERT_NE(std::nullopt, splitted_range);
    ASSERT_EQ(range_left_exp, splitted_range->first);
    ASSERT_EQ(range_right_exp, splitted_range->second);
}


TEST_F(CidrRangeTest, testSplit) {
    test_split_with_params("::/0", "::/1", "8000::/1");
    test_split_with_params("::/1", "::/2", "4000::/2");
    test_split_with_params("::/2", "::/3", "2000::/3");
    test_split_with_params("2000::/3", "2000::/4", "3000::/4");
    test_split_with_params("2000::/4", "2000::/5", "2800::/5");
    test_split_with_params("2000::/5", "2000::/6", "2400::/6");
    test_split_with_params("2400::/6", "2400::/7", "2600::/7");
    test_split_with_params("2600::/7", "2600::/8", "2700::/8");
    test_split_with_params("2600::/8", "2600::/9", "2680::/9");
    test_split_with_params("2600::/9", "2600::/10", "2640::/10");
    test_split_with_params("2600::/15", "2600::/16", "2601::/16");
}

TEST_F(CidrRangeTest, testContains) {
    CidrRange range1("2000::/3");
    CidrRange range2("4000::/3");
    CidrRange small_range1("2600:1000::/28");
    CidrRange small_range2("2600:1010::/29");
    ASSERT_TRUE(range1.contains(small_range1));
    ASSERT_TRUE(range1.contains(small_range2));
    ASSERT_FALSE(range2.contains(small_range1));
    ASSERT_FALSE(range2.contains(small_range2));
}

static void test_excluding_ranges(const std::vector<CidrRange> &original_ranges,
                                  const std::vector<CidrRange> &excluded_ranges) {
    const std::vector<CidrRange> &resulting_ranges = CidrRange::exclude(original_ranges, excluded_ranges);
    for (auto &resulting_range : resulting_ranges) {
        std::cout << resulting_range.to_string() << std::endl;
    }

    // Check that list is sorted
    std::vector<CidrRange> resulting_ranges_sorted(resulting_ranges.begin(), resulting_ranges.end());
    std::sort(resulting_ranges_sorted.begin(), resulting_ranges_sorted.end());
    ASSERT_EQ(resulting_ranges, resulting_ranges_sorted);

    // Check that list doesn't contain all excluded routes
    for (auto &resulting_range : resulting_ranges) {
        for (auto &excluded_range : excluded_ranges) {
            ASSERT_FALSE(resulting_range.contains(excluded_range));
        }
    }

#ifndef _WIN32
    __int128_t ips_num = number_of_ips(resulting_ranges);
    __int128_t ips_num_exc = number_of_ips(excluded_ranges);
    __int128_t ips_num_with_exc = ips_num + ips_num_exc;
    __int128_t num = number_of_ips(original_ranges);

    std::cout << AG_FMT("Number of IPs in original ranges:               {0:#16x}{0:#16x}\n", (long long)(num >> 64), (long long)num);
    std::cout << AG_FMT("Number of IPs in excluded ranges:               {0:#16x}{0:#16x}\n", (long long)(ips_num_exc >> 64), (long long)ips_num_exc);
    std::cout << AG_FMT("Number of IPs in resulting ranges:              {0:#16x}{0:#16x}\n", (long long)(ips_num >> 64), (long long)ips_num);
    std::cout << AG_FMT("Number of IPs in excluded and resulting ranges: {0:#16x}{0:#16x}\n", (long long)(ips_num_with_exc >> 64), (long long)ips_num_with_exc);

    ASSERT_EQ(number_of_ips(original_ranges), ips_num_with_exc);
#endif // _WIN32
}

TEST_F(CidrRangeTest, testExcludeIpv6) {
    CidrRange range("2000::/3");
    std::vector<CidrRange> original_ranges;
    original_ranges.push_back(range);

    CidrRange excluded_range1("2600:1000::/28");
    CidrRange excluded_range2("2600:1010::/29");
    std::vector<CidrRange> excluded_ranges;
    excluded_ranges.push_back(excluded_range1);
    excluded_ranges.push_back(excluded_range2);

    test_excluding_ranges(original_ranges, excluded_ranges);
}


TEST_F(CidrRangeTest, testExcludeIpv4) {
    CidrRange range("0.0.0.0/0");
    std::vector<CidrRange> original_ranges;
    original_ranges.push_back(range);

    // Always exclude multicast
    CidrRange excluded_range1("224.0.0.0/3");
    // Exclude test IP
    CidrRange excluded_range2("1.2.3.4");
    std::vector<CidrRange> excluded_ranges;
    excluded_ranges.push_back(excluded_range1);
    excluded_ranges.push_back(excluded_range2);

    test_excluding_ranges(original_ranges, excluded_ranges);
}

TEST_F(CidrRangeTest, testErrors) {
    auto address = CidrRange::get_address_from_string("2.3.");
    ASSERT_EQ(address.has_error(), true);
    ASSERT_EQ(address.error()->value(), ag::CidrErrorCode::IPV4_INVALID);
    auto ipv6_address = CidrRange::get_address_from_string("12:2:");
    ASSERT_EQ(ipv6_address.has_error(), true);
    ASSERT_EQ(ipv6_address.error()->value(), ag::CidrErrorCode::IPV6_BAD_COLON);
    auto shorten_address = CidrRange::shorten_ipv6_string("::::");
    ASSERT_EQ(shorten_address.has_error(), true);
}