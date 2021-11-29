#include "common/time_utils.h"
#include "common/utils.h"

#include <gtest/gtest.h>

TEST(TimeUtils, Test) {
    using namespace ag;
    using namespace std::chrono_literals;
    using namespace std::string_view_literals;
    auto tm1 = gmtime_from_timepoint(SystemTime{1234567890s});
    ASSERT_EQ(tm1.tm_year, 109); // 2009
    ASSERT_EQ(tm1.tm_mon, 1); // February (0 is January)
    ASSERT_EQ(tm1.tm_mday, 13);
    ASSERT_EQ(tm1.tm_hour, 23);
    ASSERT_EQ(tm1.tm_min, 31);
    ASSERT_EQ(tm1.tm_sec, 30);

    auto tm2 = localtime_from_timepoint(SystemTime{1234567890s});
    ASSERT_EQ(tm2.tm_year, 109); // 2009
    ASSERT_EQ(tm2.tm_min, 31);
    ASSERT_EQ(tm2.tm_sec, 30);

    auto tp1 = timepoint_from_gmtime(tm1);
    auto tp2 = timepoint_from_gmtime(tm2);
    ASSERT_EQ(to_secs((tp1 - tp2)).count(), get_timezone());

    std::string time3 = "2009-02-13 23:31:30";
    constexpr auto fmt3 = "%Y-%m-%d %H:%M:%S";
    auto [pos3, tm3] = parse_time(time3, fmt3);
    ASSERT_EQ(pos3, time3.size());
    ASSERT_EQ(tm1.tm_year, tm3.tm_year);
    ASSERT_EQ(tm1.tm_mon, tm3.tm_mon);
    ASSERT_EQ(tm1.tm_mday, tm3.tm_mday);
    ASSERT_EQ(tm1.tm_hour, tm3.tm_hour);
    ASSERT_EQ(tm1.tm_min, tm3.tm_min);
    ASSERT_EQ(tm1.tm_sec, tm3.tm_sec);

    std::string time4 = "2009-02-13 23:31:30 GMT";
    constexpr auto fmt4 = "%Y-%m-%d %H:%M:%S";
    auto [pos4, tm4] = parse_time(time4, fmt4);
    ASSERT_EQ(pos4, time4.size() - " GMT"sv.size());
    auto time5sv = std::string_view{time4}.substr(pos4);
    auto pos5 = validate_gmt_tz(time5sv);
    ASSERT_NE(pos5, time5sv.npos);
    ASSERT_EQ(time5sv.substr(pos5), "");

    std::string ftime6 = format_gmtime(SystemTime{1234567890s}, "%Y-%m-%d %H:%M:%S %z");
    ASSERT_EQ(ftime6, "2009-02-13 23:31:30 +0000");
    std::string ftime7 = format_localtime(SystemTime{1234567890s}, "%Y-%m-%d %H:%M:%S %z");
    ASSERT_NE(ftime7.find('+'), ftime7.npos);
    std::string ftime8 = format_localtime(SystemTime{1234567890s}, "%Y-%m-%d %H:%M:%S");
    std::string ftime9 = format_gmtime(tp2, "%Y-%m-%d %H:%M:%S");
    ASSERT_EQ(ftime8, ftime9);

    auto now = std::chrono::system_clock::now();
    int64_t now_us_diff;
    while ((now_us_diff = to_micros(now.time_since_epoch() - to_secs(now.time_since_epoch())).count()) == 0) {
        now = std::chrono::system_clock::now();
    }
    std::string ftime10 = format_gmtime(now, "%Y-%m-%d %H:%M:%S.%f %z");
    ASSERT_FALSE(ftime10.empty());
    ASSERT_NE(ftime10.find(AG_FMT(".{:06}", now_us_diff)), ftime10.npos);
}
