#include <gtest/gtest.h>
#include "utils.h"

TEST(UtilsTest, Trim) {
    EXPECT_EQ(Utils::Trim("  hello  "), "hello");
    EXPECT_EQ(Utils::Trim("world"), "world");
    EXPECT_EQ(Utils::Trim("   "), "");
    EXPECT_EQ(Utils::Trim(""), "");
}

TEST(UtilsTest, Split) {
    auto parts = Utils::Split("a,b,c", ',');
    ASSERT_EQ(parts.size(), 3);
    EXPECT_EQ(parts[0], "a");
    EXPECT_EQ(parts[1], "b");
    EXPECT_EQ(parts[2], "c");
}
