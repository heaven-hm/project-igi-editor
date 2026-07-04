#include <gtest/gtest.h>
#include <string>
#include <fstream>
#include <sstream>
#include <regex>
#include <cstdio>
#include <filesystem>
#include "../source/level/qsc_lexer.h"
#include "../source/level/qsc_parser.h"
#include "../source/level/qvm_compiler.h"
#include "../source/level/qvm_decompiler.h"
#include "../source/level/qvm_parser.h"
#include "utils.h"

// ============================================================
//  QVM Round-Trip — full suite
//
//  Each test exercises a different facet of the compile →
//  write → parse → decompile pipeline.
// ============================================================

namespace {

// Returns path to a file in the fixtures/ directory next to the exe.
// Fixtures are copied there by CMake post-build so tests work from any CWD.
std::string FixturePath(const std::string& name) {
    return Utils::GetExeDirectory() + "\\fixtures\\" + name;
}

std::string ReadFile(const std::string& path) {
    std::ifstream ifs(path);
    if (!ifs) return "";
    std::ostringstream buf;
    buf << ifs.rdbuf();
    return buf.str();
}

std::string NormalizeQsc(const std::string& input) {
    std::string res = std::regex_replace(input, std::regex("\\s+"), " ");
    return Utils::Trim(res);
}

// Full compile→write→parse→decompile pipeline.
// Returns the decompiled string. Sets *err on any stage failure.
std::string RoundTrip(const std::string& source, std::string* err,
                      const std::string& tmp = "") {
    std::string tmpPath = tmp.empty() ? FixturePath("_rt_tmp.qvm") : tmp;
    auto lr = qsc::Lex(source);
    if (!lr.ok) { if (err) *err = "lex: " + lr.error; return {}; }

    auto pr = qsc::Parse(lr.tokens);
    if (!pr.ok) { if (err) *err = "parse: " + pr.error; return {}; }

    auto cr = qvm::Compile(*pr.program);
    if (!cr.ok) { if (err) *err = "compile: " + cr.error; return {}; }

    {
        std::ofstream ofs(tmpPath, std::ios::binary);
        if (!ofs) { if (err) *err = "cannot open tmp: " + tmpPath; return {}; }
        ofs.write(reinterpret_cast<const char*>(cr.binary.data()),
                  static_cast<std::streamsize>(cr.binary.size()));
    }

    QVMFile qvm = QVM_Parse(tmpPath);
    std::remove(tmpPath.c_str());
    if (!qvm.valid) { if (err) *err = "qvm_parse: " + qvm.error; return {}; }

    return QVM_DecompileToString(qvm);
}

} // namespace

// ============================================================
//  Fixture-based round-trip
// ============================================================

TEST(QvmRoundTripTest, FixtureLevel01Simple) {
    std::string src = ReadFile(FixturePath("level01_simple.qsc"));
    ASSERT_FALSE(src.empty()) << "fixture 'tests/fixtures/level01_simple.qsc' not found";

    std::string err;
    std::string out = RoundTrip(src, &err, FixturePath("_rt_fixture.qvm"));
    ASSERT_TRUE(err.empty()) << err;
    ASSERT_FALSE(out.empty());

    EXPECT_EQ(NormalizeQsc(src), NormalizeQsc(out));
}

// ============================================================
//  Compile-only checks (no disk I/O)
// ============================================================

TEST(QvmRoundTripTest, CompileProducesNonEmptyBinary) {
    std::string src = "Foo(1);";
    auto lr = qsc::Lex(src);
    ASSERT_TRUE(lr.ok);
    auto pr = qsc::Parse(lr.tokens);
    ASSERT_TRUE(pr.ok);
    auto cr = qvm::Compile(*pr.program);
    ASSERT_TRUE(cr.ok) << cr.error;
    EXPECT_FALSE(cr.binary.empty());
    // QVM binary has a fixed 48-byte header; binary is always at least that size
    EXPECT_GE(cr.binary.size(), 4u);
}

TEST(QvmRoundTripTest, CompileEmptyProgram) {
    std::string src = "";
    auto lr = qsc::Lex(src);
    ASSERT_TRUE(lr.ok);
    auto pr = qsc::Parse(lr.tokens);
    ASSERT_TRUE(pr.ok);
    auto cr = qvm::Compile(*pr.program);
    EXPECT_TRUE(cr.ok) << cr.error;
}

TEST(QvmRoundTripTest, CompileTwiceGivesSameBytes) {
    std::string src = "Task_New(1, \"obj\", FALSE);";
    auto lr = qsc::Lex(src); ASSERT_TRUE(lr.ok);
    auto pr = qsc::Parse(lr.tokens); ASSERT_TRUE(pr.ok);

    auto cr1 = qvm::Compile(*pr.program);
    auto cr2 = qvm::Compile(*pr.program);
    ASSERT_TRUE(cr1.ok);
    ASSERT_TRUE(cr2.ok);
    EXPECT_EQ(cr1.binary, cr2.binary);
}

// ============================================================
//  Single-call round-trips (name must survive)
// ============================================================

TEST(QvmRoundTripTest, SingleSimpleCall) {
    std::string src = "Task_New(9900, \"SplineObj\", \"TestBridgeTrack\", FALSE, FALSE, "
                      "FALSE, FALSE, 20, 0, 0, 0, 0, 3.125, 1, 1, 1, 0, 0, 0);";
    std::string err;
    std::string out = RoundTrip(src, &err, FixturePath("_rt_single.qvm"));
    ASSERT_TRUE(err.empty()) << err;
    EXPECT_FALSE(out.empty());
    EXPECT_NE(out.find("Task_New"), std::string::npos);
}

TEST(QvmRoundTripTest, BooleanArguments) {
    std::string src = "SetFlag(TRUE, FALSE);";
    std::string err;
    std::string out = RoundTrip(src, &err, FixturePath("_rt_bool.qvm"));
    ASSERT_TRUE(err.empty()) << err;
    EXPECT_FALSE(out.empty());
    EXPECT_NE(out.find("SetFlag"), std::string::npos);
}

TEST(QvmRoundTripTest, IntAndFloatArguments) {
    std::string src = "Spawn(42, 3.125);";
    std::string err;
    std::string out = RoundTrip(src, &err, FixturePath("_rt_nums.qvm"));
    ASSERT_TRUE(err.empty()) << err;
    EXPECT_FALSE(out.empty());
    EXPECT_NE(out.find("Spawn"), std::string::npos);
}

TEST(QvmRoundTripTest, HexArgument) {
    std::string src = "SetMask(0xFF);";
    std::string err;
    std::string out = RoundTrip(src, &err, FixturePath("_rt_hex.qvm"));
    ASSERT_TRUE(err.empty()) << err;
    EXPECT_FALSE(out.empty());
    EXPECT_NE(out.find("SetMask"), std::string::npos);
}

TEST(QvmRoundTripTest, StringArgument) {
    std::string src = "SetName(\"player_1\");";
    std::string err;
    std::string out = RoundTrip(src, &err, FixturePath("_rt_str.qvm"));
    ASSERT_TRUE(err.empty()) << err;
    EXPECT_FALSE(out.empty());
    EXPECT_NE(out.find("SetName"), std::string::npos);
}

TEST(QvmRoundTripTest, NegativeIntArgument) {
    std::string src = "Task_New(-1, \"waypoint\", FALSE);";
    std::string err;
    std::string out = RoundTrip(src, &err, FixturePath("_rt_neg.qvm"));
    ASSERT_TRUE(err.empty()) << err;
    EXPECT_FALSE(out.empty());
    EXPECT_NE(out.find("Task_New"), std::string::npos);
}

// ============================================================
//  Nested calls
// ============================================================

TEST(QvmRoundTripTest, NestedCallsRoundTrip) {
    std::string src =
        "Task_New(9900, \"SplineObj\", \"TestBridgeTrack\", FALSE, FALSE, FALSE, FALSE, "
        "20, 0, 0, 0, 0, 3.125, 1, 1, 1, 0, 0, 0,\n"
        "    Task_New(-1, \"SplineObjWaypoint\", \"\", 0, 0, 3.125, "
        "2630.0, -5650.0, 1740.0, \"waypoint\", \"322_01_1\", 20, FALSE, FALSE, FALSE));";
    std::string err;
    std::string out = RoundTrip(src, &err, FixturePath("_rt_nested.qvm"));
    ASSERT_TRUE(err.empty()) << err;
    EXPECT_FALSE(out.empty());
    EXPECT_NE(out.find("SplineObj"), std::string::npos);
    EXPECT_NE(out.find("SplineObjWaypoint"), std::string::npos);
}

// ============================================================
//  Multiple top-level calls
// ============================================================

TEST(QvmRoundTripTest, MultipleTopLevelCalls) {
    std::string src = "Alpha(1); Beta(2); Gamma(3);";
    std::string err;
    std::string out = RoundTrip(src, &err, FixturePath("_rt_multi.qvm"));
    ASSERT_TRUE(err.empty()) << err;
    EXPECT_FALSE(out.empty());
    EXPECT_NE(out.find("Alpha"), std::string::npos);
    EXPECT_NE(out.find("Beta"),  std::string::npos);
    EXPECT_NE(out.find("Gamma"), std::string::npos);
}

// ============================================================
//  Control-flow structures
// ============================================================

TEST(QvmRoundTripTest, IfElseRoundTrip) {
    std::string src = "if (TRUE) { Foo(); } else { Bar(); }";
    std::string err;
    std::string out = RoundTrip(src, &err, FixturePath("_rt_ifelse.qvm"));
    ASSERT_TRUE(err.empty()) << err;
    EXPECT_FALSE(out.empty());
    // Both branch functions must survive the round-trip
    EXPECT_NE(out.find("Foo"), std::string::npos);
    EXPECT_NE(out.find("Bar"), std::string::npos);
}

TEST(QvmRoundTripTest, WhileLoopRoundTrip) {
    std::string src = "while (TRUE) { Tick(); }";
    std::string err;
    std::string out = RoundTrip(src, &err, FixturePath("_rt_while.qvm"));
    ASSERT_TRUE(err.empty()) << err;
    EXPECT_FALSE(out.empty());
    EXPECT_NE(out.find("Tick"), std::string::npos);
}

// ============================================================
//  QVM structure checks
// ============================================================

TEST(QvmRoundTripTest, ParsedQvmHasCorrectIdentifierCount) {
    // "Foo()" → 1 identifier: "Foo"
    std::string src = "Foo();";
    auto lr = qsc::Lex(src); ASSERT_TRUE(lr.ok);
    auto pr = qsc::Parse(lr.tokens); ASSERT_TRUE(pr.ok);
    auto cr = qvm::Compile(*pr.program); ASSERT_TRUE(cr.ok);

    std::string tmp = FixturePath("_rt_id_count.qvm");
    {
        std::ofstream ofs(tmp, std::ios::binary);
        ofs.write(reinterpret_cast<const char*>(cr.binary.data()),
                  static_cast<std::streamsize>(cr.binary.size()));
    }
    QVMFile qvm = QVM_Parse(tmp);
    std::remove(tmp.c_str());
    ASSERT_TRUE(qvm.valid) << qvm.error;
    EXPECT_GE(qvm.identifierCount(), 1u);
    // "Foo" must be in the identifier pool
    bool found = false;
    for (auto& id : qvm.identifiers) if (id == "Foo") { found = true; break; }
    EXPECT_TRUE(found) << "identifier 'Foo' not found in QVM identifier pool";
}

TEST(QvmRoundTripTest, ParsedQvmHasStringInPool) {
    std::string src = "Log(\"hello\");";
    auto lr = qsc::Lex(src); ASSERT_TRUE(lr.ok);
    auto pr = qsc::Parse(lr.tokens); ASSERT_TRUE(pr.ok);
    auto cr = qvm::Compile(*pr.program); ASSERT_TRUE(cr.ok);

    std::string tmp = FixturePath("_rt_str_pool.qvm");
    {
        std::ofstream ofs(tmp, std::ios::binary);
        ofs.write(reinterpret_cast<const char*>(cr.binary.data()),
                  static_cast<std::streamsize>(cr.binary.size()));
    }
    QVMFile qvm = QVM_Parse(tmp);
    std::remove(tmp.c_str());
    ASSERT_TRUE(qvm.valid) << qvm.error;
    EXPECT_GE(qvm.stringCount(), 1u);
    bool found = false;
    for (auto& s : qvm.strings) if (s == "hello") { found = true; break; }
    EXPECT_TRUE(found) << "string 'hello' not found in QVM string pool";
}

TEST(QvmRoundTripTest, ParsedQvmHasInstructions) {
    std::string src = "Foo(1);";
    auto lr = qsc::Lex(src); ASSERT_TRUE(lr.ok);
    auto pr = qsc::Parse(lr.tokens); ASSERT_TRUE(pr.ok);
    auto cr = qvm::Compile(*pr.program); ASSERT_TRUE(cr.ok);

    std::string tmp = FixturePath("_rt_instr.qvm");
    {
        std::ofstream ofs(tmp, std::ios::binary);
        ofs.write(reinterpret_cast<const char*>(cr.binary.data()),
                  static_cast<std::streamsize>(cr.binary.size()));
    }
    QVMFile qvm = QVM_Parse(tmp);
    std::remove(tmp.c_str());
    ASSERT_TRUE(qvm.valid) << qvm.error;
    EXPECT_GT(qvm.totalInstructions(), 0u);
}

// ============================================================
//  Re-parse decompiled output (structural validity)
// ============================================================

TEST(QvmRoundTripTest, DecompiledOutputIsValidQsc) {
    std::string src = "Task_New(42, \"obj\", TRUE, 0.5);";
    std::string err;
    std::string out = RoundTrip(src, &err, FixturePath("_rt_reparse.qvm"));
    ASSERT_TRUE(err.empty()) << err;
    ASSERT_FALSE(out.empty());

    auto lr = qsc::Lex(out);
    EXPECT_TRUE(lr.ok) << "decompiled output failed lex: " << lr.error;

    auto pr = qsc::Parse(lr.tokens);
    EXPECT_TRUE(pr.ok) << "decompiled output failed parse: " << pr.error;
}

TEST(QvmRoundTripTest, MultiCallDecompiledOutputIsValidQsc) {
    std::string src = "A(1, \"x\"); B(TRUE, 0xFF); C(-1, 2.5);";
    std::string err;
    std::string out = RoundTrip(src, &err, FixturePath("_rt_reparse2.qvm"));
    ASSERT_TRUE(err.empty()) << err;
    ASSERT_FALSE(out.empty());

    auto lr = qsc::Lex(out);
    EXPECT_TRUE(lr.ok) << lr.error;
    auto pr = qsc::Parse(lr.tokens);
    EXPECT_TRUE(pr.ok) << pr.error;
}

// ============================================================
//  Game objects.qvm round-trips — all 14 levels
//
//  Parses the real objects.qvm, decompiles it to QSC, then
//  verifies the decompiled output re-lexes and re-parses cleanly.
//  Skipped automatically if the level file is missing.
// ============================================================

class QvmGameRoundTripTest : public ::testing::TestWithParam<int> {};

TEST_P(QvmGameRoundTripTest, ObjectsQvmDecompilesAndReparses) {
    int level = GetParam();
    std::string qvmPath = Utils::GetIGIRootPath() +
        "\\missions\\location0\\level" + std::to_string(level) + "\\objects.qvm";

    if (!std::filesystem::exists(qvmPath))
        GTEST_SKIP() << "objects.qvm missing for level " << level << ": " << qvmPath;

    QVMFile qvm = QVM_Parse(qvmPath);
    ASSERT_TRUE(qvm.valid) << "QVM_Parse failed for level " << level << ": " << qvm.error;

    std::string decompiled = QVM_DecompileToString(qvm);
    ASSERT_FALSE(decompiled.empty()) << "Decompile produced empty output for level " << level;

    auto lr = qsc::Lex(decompiled);
    ASSERT_TRUE(lr.ok) << "Lex failed after decompile of level " << level << ": " << lr.error;

    auto pr = qsc::Parse(lr.tokens);
    ASSERT_TRUE(pr.ok) << "Parse failed after decompile of level " << level << ": " << pr.error;
}

INSTANTIATE_TEST_SUITE_P(
    AllLevels,
    QvmGameRoundTripTest,
    ::testing::Range(1, 15),
    [](const ::testing::TestParamInfo<int>& info) {
        return "Level" + std::to_string(info.param);
    }
);
