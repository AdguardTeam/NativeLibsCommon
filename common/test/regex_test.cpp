#include <gtest/gtest.h>

#include "common/regex.h"

class RegexTest : public ::testing::Test {
protected:

    void SetUp() override {
    }

    void TearDown() override {
    }
};

TEST_F(RegexTest, RegexCompileValid) {
    auto compiled = ag::Regex::compile("(abc)(def)?");
    ASSERT_TRUE(std::holds_alternative<ag::Regex>(compiled));
    compiled = ag::Regex::compile("[abc]");
    ASSERT_TRUE(std::holds_alternative<ag::Regex>(compiled));
    compiled = ag::Regex::compile("a{2,4}");
    ASSERT_TRUE(std::holds_alternative<ag::Regex>(compiled));
    compiled = ag::Regex::compile("^abc$");
    ASSERT_TRUE(std::holds_alternative<ag::Regex>(compiled));
    compiled = ag::Regex::compile("\\d+\\s*");
    ASSERT_TRUE(std::holds_alternative<ag::Regex>(compiled));
}

TEST_F(RegexTest, RegexCompileInvalid) {
    auto compiled = ag::Regex::compile("(abc(def)?");
    ASSERT_TRUE(std::holds_alternative<ag::RegexCompileError>(compiled));
    compiled = ag::Regex::compile("[abc");
    ASSERT_TRUE(std::holds_alternative<ag::RegexCompileError>(compiled));
    compiled = ag::Regex::compile("a{2,1}");
    ASSERT_TRUE(std::holds_alternative<ag::RegexCompileError>(compiled));
    compiled = ag::Regex::compile("a[7-0]");
    ASSERT_TRUE(std::holds_alternative<ag::RegexCompileError>(compiled));
    compiled = ag::Regex::compile("(8*sca]");
    ASSERT_TRUE(std::holds_alternative<ag::RegexCompileError>(compiled));
    compiled = ag::Regex::compile("8**sca");
    ASSERT_TRUE(std::holds_alternative<ag::RegexCompileError>(compiled));
}

TEST_F(RegexTest, RegexTest) {
    std::string text = "111abc222";
    auto compiled = ag::Regex::compile("(abc)(def)?");
    ASSERT_TRUE(std::holds_alternative<ag::Regex>(compiled));
    auto regex = std::get<ag::Regex>(compiled);
    auto match = regex.match(text);
    ASSERT_TRUE(std::holds_alternative<ag::RegexMatch>(match));
    auto regex_match = std::get<ag::RegexMatch>(match);
    ASSERT_EQ(regex_match.match_groups.size(), 2);
    ASSERT_EQ(regex_match.match_groups[1].first, text.find("abc"));
    ASSERT_EQ(regex_match.match_groups[1].second, text.find("abc") + 3);
}

TEST_F(RegexTest, SimpleRegexTest) {
    // Valid regex
    ag::SimpleRegex regex {"(abc)(def)?"};
    ASSERT_TRUE(regex.is_valid());
    regex = ag::SimpleRegex("[abc]");
    ASSERT_TRUE(regex.is_valid());
    regex = ag::SimpleRegex("a{2,4}");
    ASSERT_TRUE(regex.is_valid());
    regex = ag::SimpleRegex("^abc$");
    ASSERT_TRUE(regex.is_valid());
    regex = ag::SimpleRegex("\\d+\\s*");
    ASSERT_TRUE(regex.is_valid());

    // Invalid regex
    regex = ag::SimpleRegex("(abc(def)?");
    ASSERT_FALSE(regex.is_valid());
    regex = ag::SimpleRegex("[abc");
    ASSERT_FALSE(regex.is_valid());
    regex = ag::SimpleRegex("a{2,1}");
    ASSERT_FALSE(regex.is_valid());
    regex = ag::SimpleRegex("a[7-0]");
    ASSERT_FALSE(regex.is_valid());
    regex = ag::SimpleRegex("(8*sca]");
    ASSERT_FALSE(regex.is_valid());
    regex = ag::SimpleRegex("8**sca");
    ASSERT_FALSE(regex.is_valid());

    // Common test
    regex = ag::SimpleRegex("(abc)(def)?");
    ASSERT_TRUE(regex.is_valid());
    ASSERT_TRUE(regex.match("111abc222"));
    ASSERT_TRUE(regex.match("111abcdef222"));
    ASSERT_FALSE(regex.match("111222"));
}

TEST_F(RegexTest, RegexMatchWithGroups) {
    std::string text = "111abcdef222";
    auto compiled = ag::Regex::compile("(abc)(def)?");
    ASSERT_TRUE(std::holds_alternative<ag::Regex>(compiled));
    auto regex = std::get<ag::Regex>(compiled);
    auto match = regex.match(text);
    ASSERT_TRUE(std::holds_alternative<ag::RegexMatch>(match));
    auto regex_match = std::get<ag::RegexMatch>(match);
    ASSERT_EQ(regex_match.match_groups.size(), 3);
    ASSERT_EQ(regex_match.match_groups[1].first, text.find("abc"));
    ASSERT_EQ(regex_match.match_groups[1].second, text.find("abc") + 3);
    ASSERT_EQ(regex_match.match_groups[2].first, text.find("def"));
    ASSERT_EQ(regex_match.match_groups[2].second, text.find("def") + 3);
}

TEST_F(RegexTest, RegexMatchWithNonCapturingGroups) {
    std::string text = "111abc222def333";
    auto compiled = ag::Regex::compile("(abc).*(?:def)");
    ASSERT_TRUE(std::holds_alternative<ag::Regex>(compiled));
    auto regex = std::get<ag::Regex>(compiled);
    auto match = regex.match(text);
    ASSERT_TRUE(std::holds_alternative<ag::RegexMatch>(match));
    auto regex_match = std::get<ag::RegexMatch>(match);
    ASSERT_EQ(regex_match.match_groups.size(), 2);
    ASSERT_EQ(regex_match.match_groups[1].first, text.find("abc"));
    ASSERT_EQ(regex_match.match_groups[1].second, text.find("abc") + 3);
    match = regex.match("111abc222bef333");
    ASSERT_TRUE(std::holds_alternative<ag::RegexNoMatch>(match));
}

TEST_F(RegexTest, RegexMatchWithoutGroups) {
    std::string text = "abcdef";
    auto compiled = ag::Regex::compile("abcdef");
    ASSERT_TRUE(std::holds_alternative<ag::Regex>(compiled));
    auto regex = std::get<ag::Regex>(compiled);
    auto match = regex.match(text);
    ASSERT_TRUE(std::holds_alternative<ag::RegexMatch>(match));
    auto regex_match = std::get<ag::RegexMatch>(match);
    ASSERT_EQ(regex_match.match_groups.size(), 1);
    ASSERT_EQ(regex_match.match_groups[0].first, 0);
    ASSERT_EQ(regex_match.match_groups[0].second, text.size());
}

TEST_F(RegexTest, RegexNoMatch) {
    std::string text = "abcdef";
    auto compiled = ag::Regex::compile("(ghi)");
    ASSERT_TRUE(std::holds_alternative<ag::Regex>(compiled));
    auto regex = std::get<ag::Regex>(compiled);
    auto match = regex.match(text);
    ASSERT_TRUE(std::holds_alternative<ag::RegexNoMatch>(match));
}

TEST_F(RegexTest, Utf8) {
    uint32_t width = 0;
    int retval = pcre2_config(PCRE2_CONFIG_COMPILED_WIDTHS, &width);
    ASSERT_TRUE(retval >= 0);
    ASSERT_TRUE(width & 1);
    ag::SimpleRegex regex{"ку", PCRE2_UTF | PCRE2_CASELESS};
    ASSERT_TRUE(regex.is_valid());
    ASSERT_TRUE(regex.match("Куклачев"));
}
