#include <gtest/gtest.h>
#include "cli/verify_level_core.h"
#include "utils.h"
#include <filesystem>

// ============================================================
//  verify_level_core — unit tests (no game files, no launch)
//
//  Fixtures are copied to the exe output dir at build time.
//  Paths are resolved via GetExeDirectory() so the tests work
//  regardless of which directory igi_tests.exe is run from.
// ============================================================

static std::string FixturePath(const std::string& name) {
    return Utils::GetExeDirectory() + "\\fixtures\\" + name;
}
static std::string kLogFixture()  { return FixturePath("verify_log_l1.txt"); }
static std::string kQscFixture()  { return FixturePath("level01_simple.qsc"); }
static std::string kMissingPath() { return FixturePath("nonexistent_file.txt"); }

// ---------------------------------------------------------------------------
//  PosMatch
// ---------------------------------------------------------------------------

TEST(PosMatchTest, IdenticalPositionsMatch) {
    VerifyObj a, b;
    a.px = b.px = 1000; a.py = b.py = 2000; a.pz = b.pz = 3000;
    EXPECT_TRUE(PosMatch(a, b));
}

TEST(PosMatchTest, DifferentXDoesNotMatch) {
    VerifyObj a, b;
    a.px = 1000; b.px = 1001; a.py = b.py = 0; a.pz = b.pz = 0;
    EXPECT_FALSE(PosMatch(a, b));
}

TEST(PosMatchTest, DifferentYDoesNotMatch) {
    VerifyObj a, b;
    a.px = b.px = 0; a.py = 500; b.py = 501; a.pz = b.pz = 0;
    EXPECT_FALSE(PosMatch(a, b));
}

TEST(PosMatchTest, DifferentZDoesNotMatch) {
    VerifyObj a, b;
    a.px = b.px = 0; a.py = b.py = 0; a.pz = 100; b.pz = 101;
    EXPECT_FALSE(PosMatch(a, b));
}

// ---------------------------------------------------------------------------
//  OriMatch
// ---------------------------------------------------------------------------

TEST(OriMatchTest, IdenticalAnglesMatch) {
    VerifyObj a, b;
    a.ox = b.ox = 0.0; a.oy = b.oy = 1.5708; a.oz = b.oz = 0.0;
    EXPECT_TRUE(OriMatch(a, b));
}

TEST(OriMatchTest, SmallDifferenceWithinEpsilonMatches) {
    VerifyObj a, b;
    a.ox = 0.0; b.ox = 0.04;   // < 0.05
    a.oy = b.oy = 0.0; a.oz = b.oz = 0.0;
    EXPECT_TRUE(OriMatch(a, b));
}

TEST(OriMatchTest, DifferenceAtEpsilonDoesNotMatch) {
    VerifyObj a, b;
    a.ox = 0.0; b.ox = 0.05;   // == 0.05, not strictly less
    a.oy = b.oy = 0.0; a.oz = b.oz = 0.0;
    EXPECT_FALSE(OriMatch(a, b));
}

TEST(OriMatchTest, LargeDifferenceDoesNotMatch) {
    VerifyObj a, b;
    a.ox = 0.0; b.ox = 1.0;
    a.oy = b.oy = 0.0; a.oz = b.oz = 0.0;
    EXPECT_FALSE(OriMatch(a, b));
}

// ---------------------------------------------------------------------------
//  ParseLog — basic field extraction
// ---------------------------------------------------------------------------

TEST(ParseLogTest, ExtractsModelIdAndType) {
    bool err = false; std::string msg;
    auto objs = ParseLog(kLogFixture(), 1, err, msg);
    ASSERT_FALSE(err) << msg;
    ASSERT_FALSE(objs.empty());
    EXPECT_EQ(objs[0].modelId, "100_01_1");
    EXPECT_EQ(objs[0].type,    "Building");
    EXPECT_EQ(objs[0].name,    "Fence");
}

TEST(ParseLogTest, ExtractsPosition) {
    bool err = false; std::string msg;
    auto objs = ParseLog(kLogFixture(), 1, err, msg);
    ASSERT_FALSE(err) << msg;
    ASSERT_FALSE(objs.empty());
    EXPECT_EQ(objs[0].px, 1000LL);
    EXPECT_EQ(objs[0].py, 2000LL);
    EXPECT_EQ(objs[0].pz, 3000LL);
}

TEST(ParseLogTest, SetsOriLoggedTrueWhenOriPresent) {
    bool err = false; std::string msg;
    auto objs = ParseLog(kLogFixture(), 1, err, msg);
    ASSERT_FALSE(err) << msg;
    ASSERT_FALSE(objs.empty());
    EXPECT_TRUE(objs[0].ori_logged);
    EXPECT_NEAR(objs[0].oy, 1.5708, 0.001);
}

TEST(ParseLogTest, SetsOriLoggedFalseWhenOriAbsent) {
    bool err = false; std::string msg;
    auto objs = ParseLog(kLogFixture(), 1, err, msg);
    ASSERT_FALSE(err) << msg;
    // Second object (Wall) has no Ori= field
    ASSERT_GE(objs.size(), 2u);
    EXPECT_FALSE(objs[1].ori_logged);
}

TEST(ParseLogTest, SetsTexAndMeshLoggedWhenPresent) {
    bool err = false; std::string msg;
    auto objs = ParseLog(kLogFixture(), 1, err, msg);
    ASSERT_FALSE(err) << msg;
    // Fourth object (Crate) has Tex= and Model=
    ASSERT_GE(objs.size(), 4u);
    EXPECT_TRUE(objs[3].tex_logged);
    EXPECT_EQ(objs[3].texId,  "tex_001");
    EXPECT_TRUE(objs[3].mesh_logged);
    EXPECT_EQ(objs[3].meshId, "crate_01_1");
}

TEST(ParseLogTest, ErrorOnMissingFile) {
    bool err = false; std::string msg;
    ParseLog(kMissingPath(), 1, err, msg);
    EXPECT_TRUE(err);
    EXPECT_FALSE(msg.empty());
}

TEST(ParseLogTest, ErrorWhenLevelMarkerAbsent) {
    bool err = false; std::string msg;
    // Level 99 is not in the fixture
    ParseLog(kLogFixture(), 99, err, msg);
    EXPECT_TRUE(err);
    EXPECT_NE(msg.find("99"), std::string::npos);
}

TEST(ParseLogTest, UsesLastOccurrenceOfLevelMarker) {
    // The fixture has level 1 marker twice; objects after the SECOND marker should be used.
    // Level 2 marker comes after that, so we expect exactly 4 objects (from the second run),
    // not 8 (which would be both runs combined).
    bool err = false; std::string msg;
    auto objs = ParseLog(kLogFixture(), 1, err, msg);
    ASSERT_FALSE(err) << msg;
    EXPECT_EQ(objs.size(), 4u);
}

TEST(ParseLogTest, DoesNotParseObjectsForOtherLevel) {
    // Level 2 has one object (999_99_9) in the fixture; it must not appear in level 1 results.
    bool err = false; std::string msg;
    auto objs = ParseLog(kLogFixture(), 1, err, msg);
    ASSERT_FALSE(err) << msg;
    for (const auto& o : objs)
        EXPECT_NE(o.modelId, "999_99_9");
}

// ---------------------------------------------------------------------------
//  CrossRef
// ---------------------------------------------------------------------------

static VerifyObj MakeObj(const std::string& id, long long x, long long y, long long z,
                          double ox = 0, double oy = 0, double oz = 0, bool oriLogged = false) {
    VerifyObj v;
    v.modelId = id; v.px = x; v.py = y; v.pz = z;
    v.ox = ox; v.oy = oy; v.oz = oz;
    v.ori_logged = oriLogged;
    return v;
}

TEST(CrossRefTest, ExactMatchGoesToFound) {
    LevelReport::Category cat;
    cat.label = "TEST";
    cat.expected.push_back(MakeObj("100_01_1", 1000, 2000, 3000, 0, 1.5708, 0, true));
    std::vector<VerifyObj> logged;
    logged.push_back(MakeObj("100_01_1", 1000, 2000, 3000, 0, 1.5708, 0, true));

    CrossRef(cat, logged, true);

    EXPECT_EQ(cat.found.size(),        1u);
    EXPECT_EQ(cat.missing.size(),      0u);
    EXPECT_EQ(cat.pos_mismatch.size(), 0u);
    EXPECT_EQ(cat.ori_mismatch.size(), 0u);
}

TEST(CrossRefTest, MissingWhenNotInLog) {
    LevelReport::Category cat;
    cat.label = "TEST";
    cat.expected.push_back(MakeObj("100_01_1", 1000, 2000, 3000));
    std::vector<VerifyObj> logged; // empty

    CrossRef(cat, logged, false);

    EXPECT_EQ(cat.missing.size(), 1u);
    EXPECT_EQ(cat.found.size(),   0u);
}

TEST(CrossRefTest, PosMismatchWhenSameIdDifferentPos) {
    LevelReport::Category cat;
    cat.label = "TEST";
    cat.expected.push_back(MakeObj("100_01_1", 1000, 2000, 3000));
    std::vector<VerifyObj> logged;
    logged.push_back(MakeObj("100_01_1", 9999, 9999, 9999)); // same ID, wrong pos

    CrossRef(cat, logged, false);

    EXPECT_EQ(cat.pos_mismatch.size(), 1u);
    EXPECT_EQ(cat.found.size(),        0u);
    EXPECT_EQ(cat.missing.size(),      0u);
}

TEST(CrossRefTest, OriMismatchWhenSamePosOriBad) {
    LevelReport::Category cat;
    cat.label = "TEST";
    cat.expected.push_back(MakeObj("100_01_1", 1000, 2000, 3000, 0, 1.5708, 0, true));
    std::vector<VerifyObj> logged;
    // Same pos, but ori differs > EPS (0.05)
    logged.push_back(MakeObj("100_01_1", 1000, 2000, 3000, 0, 0.0, 0, true));

    CrossRef(cat, logged, true);  // matchOri=true

    EXPECT_EQ(cat.ori_mismatch.size(), 1u);
    EXPECT_EQ(cat.found.size(),        0u);
}

TEST(CrossRefTest, OriNotCheckedWhenMatchOriFalse) {
    LevelReport::Category cat;
    cat.label = "TEST";
    cat.expected.push_back(MakeObj("100_01_1", 1000, 2000, 3000, 0, 1.5708, 0, true));
    std::vector<VerifyObj> logged;
    logged.push_back(MakeObj("100_01_1", 1000, 2000, 3000, 0, 99.0, 0, true)); // huge ori diff

    CrossRef(cat, logged, false);  // matchOri=false (AI category)

    EXPECT_EQ(cat.found.size(),        1u);  // still matches
    EXPECT_EQ(cat.ori_mismatch.size(), 0u);
}

TEST(CrossRefTest, RailObjectMatchedByModelIdOnly) {
    LevelReport::Category cat;
    cat.label = "TEST";
    VerifyObj rail = MakeObj("322_01_1", 2630, 0, 0);
    rail.posIsRail = true;
    cat.expected.push_back(rail);

    std::vector<VerifyObj> logged;
    // Logged at completely different world position — still matches because posIsRail
    logged.push_back(MakeObj("322_01_1", 9999, 8888, 7777));

    CrossRef(cat, logged, true);

    EXPECT_EQ(cat.found.size(),   1u);
    EXPECT_EQ(cat.missing.size(), 0u);
}

TEST(CrossRefTest, TexMismatch) {
    LevelReport::Category cat;
    cat.label = "TEST";
    VerifyObj exp = MakeObj("400_04_1", 9000, 8000, 7000, 0, 0, 0, true);
    exp.texId = "tex_001"; exp.tex_logged = true;
    cat.expected.push_back(exp);

    std::vector<VerifyObj> logged;
    VerifyObj got = MakeObj("400_04_1", 9000, 8000, 7000, 0, 0, 0, true);
    got.texId = "tex_999"; got.tex_logged = true;
    logged.push_back(got);

    CrossRef(cat, logged, true);

    EXPECT_EQ(cat.tex_mismatch.size(), 1u);
    EXPECT_EQ(cat.found.size(),        0u);
}

TEST(CrossRefTest, MeshMismatch) {
    LevelReport::Category cat;
    cat.label = "TEST";
    VerifyObj exp = MakeObj("400_04_1", 9000, 8000, 7000, 0, 0, 0, true);
    exp.meshId = "crate_01_1"; exp.mesh_logged = true;
    cat.expected.push_back(exp);

    std::vector<VerifyObj> logged;
    VerifyObj got = MakeObj("400_04_1", 9000, 8000, 7000, 0, 0, 0, true);
    got.meshId = "wrong_01_1"; got.mesh_logged = true;
    logged.push_back(got);

    CrossRef(cat, logged, true);

    EXPECT_EQ(cat.mesh_mismatch.size(), 1u);
}

// ---------------------------------------------------------------------------
//  ParseQscObjects
// ---------------------------------------------------------------------------

TEST(ParseQscObjectsTest, ParsesSplineObjWaypointFromFixture) {
    std::map<std::string, std::string> noNames;
    auto objs = ParseQscObjects(kQscFixture(), noNames);
    // level01_simple.qsc has one SplineObjWaypoint with model 322_01_1
    ASSERT_FALSE(objs.empty());
    bool found = false;
    for (const auto& o : objs)
        if (o.modelId == "322_01_1") { found = true; break; }
    EXPECT_TRUE(found) << "322_01_1 not found in parsed objects";
}

TEST(ParseQscObjectsTest, ReturnsEmptyForMissingFile) {
    std::map<std::string, std::string> noNames;
    auto objs = ParseQscObjects(kMissingPath(), noNames);
    EXPECT_TRUE(objs.empty());
}
