#include <gtest/gtest.h>
#include "parsers/dat_parser.h"
#include "utils.h"
#include <string>

// ============================================================
//  DAT Parser — level1.dat structural tests
//
//  Path: <exe_dir>\missions\location0\level1\level1.dat
//  Game files must be co-located with igi_tests.exe.
// ============================================================

static std::string DatPath() {
    return Utils::GetIGIRootPath() + "\\missions\\location0\\level1\\level1.dat";
}

class DatParserTest : public ::testing::Test {
protected:
    DATFile dat;
    void SetUp() override {
        dat = DAT_Parse(DatPath());
    }
};

TEST_F(DatParserTest, FileExistsAndParsesValid) {
    ASSERT_TRUE(dat.valid) << "DAT parse failed: " << dat.error
                           << "\nPath: " << DatPath();
}

TEST_F(DatParserTest, DeclaredModelCountIsPositive) {
    EXPECT_GT(dat.declaredModelCount, 0);
}

TEST_F(DatParserTest, ModelsCountMatchesDeclared) {
    EXPECT_EQ((int)dat.models.size(), dat.declaredModelCount);
}

TEST_F(DatParserTest, AllModelNamesNonEmpty) {
    for (const auto& m : dat.models)
        EXPECT_FALSE(m.modelName.empty()) << "Empty model name found in DAT";
}

TEST_F(DatParserTest, AllTexturesNonEmpty) {
    EXPECT_GT(dat.allTextures.size(), 0u);
    for (const auto& t : dat.allTextures)
        EXPECT_FALSE(t.empty()) << "Empty texture name found in DAT";
}

TEST_F(DatParserTest, JsonOutputIsWellFormed) {
    std::string json = DAT_FormatJSON(dat);
    ASSERT_FALSE(json.empty());
    EXPECT_EQ(json.front(), '[');
    EXPECT_EQ(json.back(),  ']');
}
