#include <gtest/gtest.h>

#include "common/url.h"

TEST(url, NormalizePath) {
    ASSERT_EQ("/a/c/d.html", ag::url::normalize_path("../a/b/../c/./d.html"));
    ASSERT_EQ("/a/c/d.html", ag::url::normalize_path("../a/b/../../a/./c/./d.html"));
    ASSERT_EQ("", ag::url::normalize_path(""));
    ASSERT_EQ("/a/b/c.d", ag::url::normalize_path("/a/b/c.d"));
}
