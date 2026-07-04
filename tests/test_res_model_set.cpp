#include <gtest/gtest.h>
#include "level/res_model_set.h"
#include "../source/renderer/res_writer.h"

static RESFile MakeRes(std::initializer_list<std::string> names) {
    RESFile r; r.valid = true;
    for (auto& n : names) r.entries.push_back(RESEntry{n, {1,2,3}});
    return r;
}

TEST(ResModelSetTest, MatchesMefEntryCaseInsensitive) {
    ResModelSet s(MakeRes({"models\\426_02_1.MEF", "models\\003_01_1.mef"}));
    EXPECT_TRUE(s.Contains("426_02_1"));
    EXPECT_TRUE(s.Contains("003_01_1"));
}

TEST(ResModelSetTest, ReportsMissingModel) {
    ResModelSet s(MakeRes({"models\\003_01_1.mef"}));
    EXPECT_FALSE(s.Contains("999_99_9"));
}

TEST(ResModelSetTest, IgnoresNonMefEntries) {
    ResModelSet s(MakeRes({"textures\\foo.tga", "003_01_1.mef"}));
    EXPECT_FALSE(s.Contains("foo"));
    EXPECT_TRUE(s.Contains("003_01_1"));
}
