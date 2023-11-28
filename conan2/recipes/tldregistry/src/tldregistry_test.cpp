#include "tldregistry/tldregistry.h"

#include <string_view>

#define ASSERT_EQ(e_, a_) do { if ((e_) != (a_)) abort(); } while (0)
#define ASSERT_TRUE(x_) do { if (!(x_)) abort(); } while (0)
#define ASSERT_FALSE(x_) do { if (!!(x_)) abort(); } while (0)

int main() {
    ASSERT_TRUE(tldregistry::is_tld("co.uk"));
    ASSERT_TRUE(tldregistry::is_tld("org"));

    ASSERT_FALSE(tldregistry::is_tld("local"));
    ASSERT_FALSE(tldregistry::is_tld("example.org"));

    ASSERT_EQ("org", tldregistry::get_tld("example.org"));
    ASSERT_EQ("co.uk", tldregistry::get_tld("uk.example.co.uk"));
    ASSERT_EQ("org", tldregistry::get_tld("co.uk.org"));
    ASSERT_EQ(std::nullopt, tldregistry::get_tld("co.uk.local"));

    ASSERT_EQ("", tldregistry::reduce_domain("", 1));
    ASSERT_EQ("example.co.uk", tldregistry::reduce_domain("....bad.example.co.uk....", 1));
    ASSERT_EQ("bad.example.co.uk", tldregistry::reduce_domain("....bad.example.co.uk....", 2));
    ASSERT_EQ("bad.example.co.uk", tldregistry::reduce_domain("....bad.example.co.uk....", 42));
    ASSERT_EQ("co.uk", tldregistry::reduce_domain("....bad.example.co.uk....", 0));
    ASSERT_EQ("company.local", tldregistry::reduce_domain("...jira.company.local....", 1));
    ASSERT_EQ("local", tldregistry::reduce_domain("...jira.company.local....", 0));
    ASSERT_EQ("jira.company.local", tldregistry::reduce_domain("jira.company.local", 42));

    return 0;
}
