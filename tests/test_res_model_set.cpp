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

TEST(ResModelSetTest, ResolvesAuthoredLodSuffixedReferenceFromArchivePath) {
    // Level archives list entries with LOCAL:models/ prefixes and LOD
    // suffixes.  An authored reference such as 426_02_1 must match the
    // 426_02_1.mef archive entry exactly (the editor auto-imports a foreign
    // family when the exact authored LOD stem is absent).
    ResModelSet s(MakeRes({
        "LOCAL:models/426_02_1.mef", "LOCAL:models/426_02_2.mef",
        "LOCAL:models/426_02_3.mef",
    }));
    EXPECT_TRUE(s.Contains("426_02_1"));
    EXPECT_TRUE(s.Contains("426_02_3"));
    EXPECT_FALSE(s.Contains("426_02"));   // base family is not itself an entry
    EXPECT_FALSE(s.Contains("426_02_4")); // higher LOD not present in archive
}

TEST(ResModelSetTest, HelperAndBareNumericNamesNeverMatchMeshArchive) {
    // Collision boxes, spline waypoints, and bare numeric spline indices are
    // authored "model" values but are not .mef meshes; they must never be
    // reported as present in a mesh archive.
    ResModelSet s(MakeRes({"models\\003_01_1.mef"}));
    EXPECT_FALSE(s.Contains("waypoint"));
    EXPECT_FALSE(s.Contains("colbox"));
    EXPECT_FALSE(s.Contains("colbox4"));
    EXPECT_FALSE(s.Contains("joint_fixer2"));
    EXPECT_FALSE(s.Contains("3"));
}
