#include <gtest/gtest.h>
#include "../source/renderer/res_writer.h"
#include "utils.h"
#include <string>

// ============================================================
//  RES Parser — level1.res structural tests
//
//  Path: <exe_dir>\missions\location0\level1\models\level1.res
// ============================================================

static std::string ResPath() {
    return Utils::GetIGIRootPath() +
           "\\missions\\location0\\level1\\models\\level1.res";
}

class ResParserTest : public ::testing::Test {
protected:
    RESFile res;
    void SetUp() override {
        res = RES_Parse(ResPath());
    }
};

TEST_F(ResParserTest, FileExistsAndParsesValid) {
    ASSERT_TRUE(res.valid) << "RES parse failed: " << res.error
                           << "\nPath: " << ResPath();
}

TEST_F(ResParserTest, HasEntries) {
    EXPECT_GT(res.entries.size(), 0u);
}

TEST_F(ResParserTest, AllEntryNamesNonEmpty) {
    for (const auto& e : res.entries)
        EXPECT_FALSE(e.name.empty()) << "Empty entry name in RES";
}

TEST_F(ResParserTest, AllEntryDataNonEmpty) {
    for (const auto& e : res.entries)
        EXPECT_GT(e.data.size(), 0u) << "Empty data for entry: " << e.name;
}

TEST_F(ResParserTest, ForEachCallbackFires) {
    int count = 0;
    std::string err;
    RES_ForEachEntry(ResPath(),
        [&](const std::string&, const uint8_t*, size_t) { ++count; },
        err);
    EXPECT_GT(count, 0) << "ForEachEntry fired 0 times; error: " << err;
}

TEST_F(ResParserTest, ExtractFirstEntryReturnsData) {
    ASSERT_FALSE(res.entries.empty());
    const std::string firstName = res.entries[0].name;
    auto data = RES_Extract(ResPath(), firstName);
    EXPECT_GT(data.size(), 0u) << "RES_Extract returned empty for: " << firstName;
}
