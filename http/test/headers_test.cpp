#include <algorithm>
#include <numeric>
#include <utility>

#include <gtest/gtest.h>

#include "common/http/headers.h"
#include "common/utils.h"

TEST(HttpHeaders, Contains) {
    ag::http::Headers hs;
    hs.put("a", "1");
    hs.put("b", "2");
    hs.put("C", "3");

    ASSERT_TRUE(hs.contains("a"));
    ASSERT_TRUE(hs.contains("b"));
    ASSERT_TRUE(hs.contains("C"));
    ASSERT_FALSE(hs.contains("d"));
}

TEST(HttpHeaders, Get) {
    ag::http::Headers hs;
    hs.put("a", "1");
    hs.put("b", "2");
    hs.put("C", "3");

    ASSERT_EQ(hs.get("a"), "1");
    ASSERT_EQ(hs.gets("a"), "1");
    ASSERT_EQ(hs.get("b"), "2");
    ASSERT_EQ(hs.gets("b"), "2");
    ASSERT_EQ(hs.get("C"), "3");
    ASSERT_EQ(hs.gets("C"), "3");
    ASSERT_EQ(hs.get("d"), std::nullopt);
    ASSERT_EQ(hs.gets("d"), "");
}

TEST(HttpHeaders, GetNonUniqueField) {
    ag::http::Headers hs;
    hs.put("a", "1");
    hs.put("a", "2");

    ASSERT_EQ(hs.get("a"), "1");
    ASSERT_EQ(hs.gets("A"), "1");
    ASSERT_EQ(hs.get("A"), "1");
    ASSERT_EQ(hs.gets("a"), "1");
}

TEST(HttpHeaders, ToString) {
    constexpr std::string_view EXPECTED = "a: 1\r\n"
                                          "A: 2\r\n"
                                          "b: 3\r\n"
                                          "c: 4\r\n";

    ag::http::Headers hs;
    hs.put("a", "1");
    hs.put("A", "2");
    hs.put("b", "3");
    hs.put("c", "4");

    ASSERT_EQ(EXPECTED, hs.str());
    ASSERT_EQ(EXPECTED, fmt::format("{}", hs));
}

TEST(HttpHeaders, NameValueIter) {
    // clang-format off
    const std::vector<std::pair<std::string_view, std::string_view>> HEADERS = {
            {"a", "1"}, {"b", "2"}, {"c", "3"}, {"A", "4"}, {"B", "5"}, {"C", "6"}, {"D", "7"}
    };
    // clang-format on

    ag::http::Headers hs(HEADERS.begin(), HEADERS.end(), [](const auto &x) {
        return ag::http::make_header<std::string>(x.first, x.second);
    });

    auto collected = std::accumulate(hs.begin(), hs.end(), std::vector<std::pair<std::string_view, std::string_view>>{},
            [](auto acc, const auto &h) {
                acc.emplace_back(h.name, h.value);
                return acc;
            });
    ASSERT_EQ(HEADERS, collected);
}

TEST(HttpHeaders, NameValueIterEmptyHeaders) {
    const std::vector<std::pair<std::string_view, std::string_view>> EXPECTED = {};

    ag::http::Headers hs;
    auto collected = std::accumulate(hs.begin(), hs.end(), std::vector<std::pair<std::string_view, std::string_view>>{},
            [](auto acc, const auto &h) {
                acc.emplace_back(h.name, h.value);
                return acc;
            });
    ASSERT_EQ(EXPECTED, collected);
}

TEST(HttpHeaders, NameValueIterErase) {
    // clang-format off
    const std::vector<std::pair<std::string_view, std::string_view>> HEADERS = {
            {"a", "1"}, {"b", "2"}, {"c", "3"}, {"A", "4"}, {"B", "5"}, {"C", "6"}, {"D", "7"}
    };
    // clang-format on

    ag::http::Headers hs;
    for (auto h : HEADERS) {
        hs.put(std::string(h.first), std::string(h.second));
    }

    hs.erase(hs.begin());
    hs.erase(hs.begin());

    auto collected = std::accumulate(hs.begin(), hs.end(), std::vector<std::pair<std::string_view, std::string_view>>{},
            [](auto acc, const auto &h) {
                acc.emplace_back(h.name, h.value);
                return acc;
            });
    const decltype(HEADERS) EXPECTED_HEADERS{std::next(HEADERS.begin(), 2), HEADERS.end()};
    ASSERT_EQ(EXPECTED_HEADERS, collected);
}

TEST(HttpHeaders, ValueIter) {
    // clang-format off
    static const std::vector<std::pair<std::string_view, std::string_view>> HEADERS = {
            {"a", "1"}, {"b", "2"}, {"c", "3"}, {"A", "4"}, {"B", "5"}, {"C", "6"}, {"D", "7"}
    };
    // clang-format on
    constexpr std::string_view NON_UNIQUE_HEADER = "a";
    constexpr std::string_view UNIQUE_HEADER = "d";

    constexpr auto COLLECT_EXPECTED = [](std::string_view name) -> std::vector<std::string_view> {
        return std::accumulate(std::begin(HEADERS), std::end(HEADERS), std::vector<std::string_view>{},
                [name](auto acc, const auto &h) {
                    if (ag::utils::iequals(h.first, name)) {
                        acc.emplace_back(h.second);
                    }
                    return acc;
                });
    };

    ag::http::Headers hs;
    for (auto h : HEADERS) {
        hs.put(std::string(h.first), std::string(h.second));
    }

    auto range = hs.value_range(NON_UNIQUE_HEADER);
    auto collected =
            std::accumulate(range.first, range.second, std::vector<std::string_view>{}, [](auto acc, const auto &v) {
                acc.emplace_back(v);
                return acc;
            });
    ASSERT_EQ(COLLECT_EXPECTED(NON_UNIQUE_HEADER), collected);

    range = hs.value_range(UNIQUE_HEADER);
    collected =
            std::accumulate(range.first, range.second, std::vector<std::string_view>{}, [](auto acc, const auto &v) {
                acc.emplace_back(v);
                return acc;
            });
    ASSERT_EQ(COLLECT_EXPECTED(UNIQUE_HEADER), collected);
}

TEST(HttpHeaders, ValueIterEmptyHeaders) {
    const std::vector<std::string_view> EXPECTED = {};

    ag::http::Headers hs;
    auto range = hs.value_range("a");
    auto collected =
            std::accumulate(range.first, range.second, std::vector<std::string_view>{}, [](auto acc, const auto &v) {
                acc.emplace_back(v);
                return acc;
            });
    ASSERT_EQ(EXPECTED, collected);
}

TEST(HttpHeaders, ValueIterErase) {
    // clang-format off
    static const std::vector<std::pair<std::string_view, std::string_view>> HEADERS = {
            {"a", "1"}, {"b", "2"}, {"c", "3"}, {"A", "4"}, {"B", "5"}, {"C", "6"}, {"D", "7"}
    };
    static const std::vector<std::pair<std::string_view, std::string_view>> EXPECTED = {
            {"b", "2"}, {"c", "3"}, {"A", "4"}, {"C", "6"}
    };
    // clang-format on

    ag::http::Headers hs;
    for (auto h : HEADERS) {
        hs.put(std::string(h.first), std::string(h.second));
    }

    ASSERT_EQ(*hs.erase(hs.value_range("a").first), "4");
    hs.erase(std::next(hs.value_range("b").first, 1));
    hs.erase(hs.value_range("d").first);

    auto collected = std::accumulate(hs.begin(), hs.end(), std::vector<std::pair<std::string_view, std::string_view>>{},
            [](auto acc, const auto &h) {
                acc.emplace_back(h.name, h.value);
                return acc;
            });
    ASSERT_EQ(EXPECTED, collected);
}

TEST(RequestHttpHeaders, H1ToString) {
    constexpr std::string_view EXPECTED = "GET /path HTTP/1.1\r\n"
                                          "a: 1\r\n"
                                          "a: 2\r\n"
                                          "b: 3\r\n"
                                          "c: 4\r\n"
                                          "\r\n";

    ag::http::Request req(ag::http::HTTP_1_1, "GET", "/path");
    req.headers().put("a", "1");
    req.headers().put("a", "2");
    req.headers().put("b", "3");
    req.headers().put("c", "4");

    ASSERT_EQ(EXPECTED, req.str());
    ASSERT_EQ(EXPECTED, fmt::format("{}", req));
}

TEST(RequestHttpHeaders, H2ToString) {
    constexpr std::string_view EXPECTED = "GET /path HTTP/2.0\r\n"
                                          ":scheme: https\r\n"
                                          ":authority: example.com\r\n"
                                          "a: 1\r\n"
                                          "a: 2\r\n"
                                          "b: 3\r\n"
                                          "c: 4\r\n"
                                          "\r\n";

    ag::http::Request req(ag::http::HTTP_2_0, "GET", "/path");
    req.scheme("https");
    req.authority("example.com");
    req.headers().put("a", "1");
    req.headers().put("a", "2");
    req.headers().put("b", "3");
    req.headers().put("c", "4");

    ASSERT_EQ(EXPECTED, req.str());
    ASSERT_EQ(EXPECTED, fmt::format("{}", req));
}

TEST(RequestHttpHeaders, H3ToString) {
    constexpr std::string_view EXPECTED = "OPTIONS * HTTP/3.0\r\n"
                                          ":scheme: https\r\n"
                                          ":authority: example.com\r\n"
                                          "a: 1\r\n"
                                          "a: 2\r\n"
                                          "b: 3\r\n"
                                          "c: 4\r\n"
                                          "\r\n";

    ag::http::Request req(ag::http::HTTP_3_0, "OPTIONS", "*");
    req.authority("example.com");
    req.scheme("https");
    req.headers().put("a", "1");
    req.headers().put("a", "2");
    req.headers().put("b", "3");
    req.headers().put("c", "4");

    ASSERT_EQ(EXPECTED, req.str());
    ASSERT_EQ(EXPECTED, fmt::format("{}", req));
}

TEST(RequestHttpHeaders, Iterator) {
    // clang-format off
    const std::vector<std::pair<std::string_view, std::string_view>> EXPECTED = {
            {":method", "GET"}, {":scheme", "scheme"}, {":path", "/path"}, {":authority", "authority"},
            {"a", "1"}, {"b", "2"}
    };
    // clang-format on

    ag::http::Request req(ag::http::HTTP_2_0, "GET", "/path");
    req.authority("authority");
    req.scheme("scheme");
    req.headers().put("a", "1");
    req.headers().put("b", "2");

    auto collected = std::accumulate(req.begin(), req.end(),
            std::vector<std::pair<std::string_view, std::string_view>>{}, [](auto acc, const auto &h) {
                acc.emplace_back(h.name, h.value);
                return acc;
            });

    ASSERT_EQ(EXPECTED, collected);
}

TEST(ResponseHttpHeaders, H1ToString) {
    constexpr std::string_view EXPECTED = "HTTP/1.1 527 Railgun Listener to Origin\r\n"
                                          "a: 1\r\n"
                                          "a: 2\r\n"
                                          "b: 3\r\n"
                                          "c: 4\r\n"
                                          "\r\n";

    ag::http::Response resp(ag::http::HTTP_1_1, 527); // NOLINT(*-magic-numbers)
    resp.status_string("Railgun Listener to Origin");
    resp.headers().put("a", "1");
    resp.headers().put("a", "2");
    resp.headers().put("b", "3");
    resp.headers().put("c", "4");

    ASSERT_EQ(EXPECTED, resp.str());
    ASSERT_EQ(EXPECTED, fmt::format("{}", resp));
}

TEST(ResponseHttpHeaders, H2ToString) {
    constexpr std::string_view EXPECTED = "HTTP/2.0 200\r\n"
                                          "a: 1\r\n"
                                          "a: 2\r\n"
                                          "b: 3\r\n"
                                          "c: 4\r\n"
                                          "\r\n";

    ag::http::Response resp(ag::http::HTTP_2_0, 200); // NOLINT(*-magic-numbers)
    resp.status_string("OK");
    resp.headers().put("a", "1");
    resp.headers().put("a", "2");
    resp.headers().put("b", "3");
    resp.headers().put("c", "4");

    ASSERT_EQ(EXPECTED, resp.str());
    ASSERT_EQ(EXPECTED, fmt::format("{}", resp));
}

TEST(ResponseHttpHeaders, H3ToString) {
    constexpr std::string_view EXPECTED = "HTTP/3.0 300\r\n"
                                          "a: 1\r\n"
                                          "a: 2\r\n"
                                          "b: 3\r\n"
                                          "c: 4\r\n"
                                          "\r\n";

    ag::http::Response resp(ag::http::HTTP_3_0, 300); // NOLINT(*-magic-numbers)
    resp.headers().put("a", "1");
    resp.headers().put("a", "2");
    resp.headers().put("b", "3");
    resp.headers().put("c", "4");

    ASSERT_EQ(EXPECTED, resp.str());
    ASSERT_EQ(EXPECTED, fmt::format("{}", resp));
}

TEST(ResponseHttpHeaders, Iterator) {
    // clang-format off
    const std::vector<std::pair<std::string_view, std::string_view>> EXPECTED = {
            {":status", "300"}, {"a", "1"}, {"a", "1"}
    };
    // clang-format on

    ag::http::Response resp(ag::http::HTTP_2_0, 300); // NOLINT(*-magic-numbers)
    resp.headers().put("a", "1");
    resp.headers().put("a", "1");

    auto collected = std::accumulate(resp.begin(), resp.end(),
            std::vector<std::pair<std::string_view, std::string_view>>{}, [](auto acc, const auto &h) {
                acc.emplace_back(h.name, h.value);
                return acc;
            });

    ASSERT_EQ(EXPECTED, collected);
}
