#include <gtest/gtest.h>
#include "config.h"

TEST(ConfigTest, InitLoadsDefaults) {
    Config::Init();
    auto& data = Config::Get();
    EXPECT_GT(data.level, 0); // Assuming there's a default level
}
