#include <gtest/gtest.h>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <string>
#include <fstream>
#include <iostream>
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
//
// Two candidate directories, because the two lanes lay the tree out
// differently.  On Windows CMake copies `tests/fixtures` next to igi_tests.exe
// (CMakeLists.txt:655-660), so `<exeDir>/fixtures` is the answer.  The
// host-native lane runs the binary with `GetExeDirectory()` == the repository
// root, where the fixtures are at `tests/fixtures`; before this search the
// suite built a literal `\fixtures\<name>` path there, which is one filename on
// POSIX, so every fixture-backed test failed.  The scratch files the round-trip
// tests write land in whichever directory is chosen, so this picks a directory
// (not an existing file) and falls back to the Windows layout.
std::string FixturePath(const std::string& name) {
    const std::filesystem::path exeDir(Utils::GetExeDirectory());
    const std::filesystem::path dirs[] = {
        exeDir / "fixtures",
        exeDir / "tests" / "fixtures",
    };
    for (const std::filesystem::path& dir : dirs) {
        if (std::filesystem::is_directory(dir)) return (dir / name).string();
    }
    return (dirs[0] / name).string();
}

// The installed retail game tree.  Mirrors `Utils::GetIGIRootPath()`
// (source/utils.cpp:501-537) without linking it: `IGI_GAME_PATH` when set,
// otherwise the executable directory.  source/utils.cpp is not host-portable,
// so the test-local copy is what lets this suite build and run natively.
std::string CorpusRootString() {
    if (const char* env = std::getenv("IGI_GAME_PATH"); env != nullptr && *env != '\0') {
        return std::string(env);
    }
    // The lane's documented default (scripts/run-host-tests.sh), for a direct
    // run of the test binary without the environment set.
    const std::string kDefault = "/Users/haseeb-mir/Downloads/IGI";
    if (std::filesystem::is_directory(kDefault)) return kDefault;
    return Utils::GetExeDirectory();
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

std::vector<uint8_t> BuildMinimalQvm(const std::vector<uint8_t>& code) {
    std::vector<uint8_t> binary(60U + code.size(), 0U);
    binary[0] = 'L';
    binary[1] = 'O';
    binary[2] = 'O';
    binary[3] = 'P';

    auto write_u32 = [&binary](size_t offset, uint32_t value) {
        binary[offset + 0] = static_cast<uint8_t>(value & 0xFFU);
        binary[offset + 1] = static_cast<uint8_t>((value >> 8U) & 0xFFU);
        binary[offset + 2] = static_cast<uint8_t>((value >> 16U) & 0xFFU);
        binary[offset + 3] = static_cast<uint8_t>((value >> 24U) & 0xFFU);
    };

    write_u32(0x04, 8U);                  // LOOP instruction-set identifier
    write_u32(0x08, 5U);                  // IGI1 QVM version
    write_u32(0x0C, 60U);                 // empty identifier table
    write_u32(0x10, 60U);                 // empty identifier pool
    write_u32(0x14, 0U);
    write_u32(0x18, 0U);
    write_u32(0x1C, 60U);                 // empty string table
    write_u32(0x20, 60U);                 // empty string pool
    write_u32(0x24, 0U);
    write_u32(0x28, 0U);
    write_u32(0x2C, 60U);                 // code section
    write_u32(0x30, static_cast<uint32_t>(code.size()));

    std::copy(code.begin(), code.end(), binary.begin() + 60);
    return binary;
}

// Same container, but with a one-entry string pool so a PUSHSI/PUSHII operand can
// reference it.  The header follows the engine's QvmHeader/QvmStrings
// (src/qvm/qvmtypes.h:4-30, read by Qvm_Load 0x4BD220, src/qvm/qvm.c:472-483):
// strOffsets@0x1C, strText@0x20, strTableSize@0x24, strTextSize@0x28, then
// code@0x2C and codeSize@0x30.
//
// The offsets TABLE is not optional: Qvm_Load wires a pool only when the table's
// byte size is non-zero, so a text blob with sz=0 is a container the engine
// cannot read (0 of 1043 retail files have that shape).
std::vector<uint8_t> BuildQvmWithStringPool(const std::vector<uint8_t>& code,
                                            const std::string& pooledString) {
    std::string pool = pooledString;
    pool.push_back('\0');

    constexpr size_t kHeader = 60U;
    constexpr size_t kStringTable = 4U;  // one uint32 offset
    const size_t text = kHeader + kStringTable;

    std::vector<uint8_t> binary(text + pool.size() + code.size(), 0U);
    binary[0] = 'L'; binary[1] = 'O'; binary[2] = 'O'; binary[3] = 'P';

    auto write_u32 = [&binary](size_t offset, uint32_t value) {
        binary[offset + 0] = static_cast<uint8_t>(value & 0xFFU);
        binary[offset + 1] = static_cast<uint8_t>((value >> 8U) & 0xFFU);
        binary[offset + 2] = static_cast<uint8_t>((value >> 16U) & 0xFFU);
        binary[offset + 3] = static_cast<uint8_t>((value >> 24U) & 0xFFU);
    };

    write_u32(0x04, 8U);   // version
    write_u32(0x08, 5U);   // unk08, >= 5 accepted
    write_u32(0x0C, kHeader);   // name pool: offsets table (empty)
    write_u32(0x10, kHeader);   // name pool: text (empty)
    write_u32(0x14, 0U);        // name pool: table bytes
    write_u32(0x18, 0U);        // name pool: text bytes
    write_u32(0x1C, kHeader);   // string pool: offsets table
    write_u32(0x20, static_cast<uint32_t>(text));  // string pool: text
    write_u32(0x24, static_cast<uint32_t>(kStringTable));
    write_u32(0x28, static_cast<uint32_t>(pool.size()));
    write_u32(0x2C, static_cast<uint32_t>(text + pool.size()));
    write_u32(0x30, static_cast<uint32_t>(code.size()));

    write_u32(kHeader, 0U);  // offsets[0] = 0: the pool's first (only) string
    std::copy(pool.begin(), pool.end(), binary.begin() + text);
    std::copy(code.begin(), code.end(), binary.begin() + text + pool.size());
    return binary;
}

// Writes `bytes` to a scratch path, parses it, deletes it.
QVMFile ParseSyntheticQvm(const std::vector<uint8_t>& bytes,
                          const std::string& scratchName) {
    const std::string path = FixturePath(scratchName);
    {
        std::ofstream out(path, std::ios::binary);
        if (!out.is_open()) return QVMFile{};
        out.write(reinterpret_cast<const char*>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
    }
    QVMFile qvm = QVM_Parse(path);
    std::remove(path.c_str());
    return qvm;
}

// Encodes `value` as a QSC double-quoted literal, following the lexer's
// contract (source/level/qsc_lexer.cpp:141-150) -- i.e. the direction a script
// AUTHOR writes, not the direction the decompiler prints.
std::string QscQuote(const std::string& value) {
    std::string out = "\"";
    for (char c : value) {
        if (c == '\\') out += "\\\\";
        else if (c == '"') out += "\\\"";
        else if (c == '\n') out += "\\n";
        else out += c;
    }
    out += "\"";
    return out;
}

// The single StringLit token of `source`, or "" if the source did not lex to
// exactly one string statement.
std::string SoleStringLiteral(const std::string& source, std::string* error) {
    const qsc::LexResult lr = qsc::Lex(source);
    if (!lr.ok) { if (error) *error = "lex: " + lr.error; return {}; }
    for (const qsc::Token& t : lr.tokens) {
        if (t.kind == qsc::TokKind::StringLit) return t.lexeme;
    }
    if (error) *error = "no string literal in: " + source;
    return {};
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

TEST(QvmParserTest, DecodesRetailInlineAndSignedOperands) {
    const std::vector<uint8_t> code = {
        0x07, 'v', 'a', 'n', 'i', 'l', 'l', 'a', 0x00, // PUSHS
        0x0B, 'G', 'O', 'S', 't', 'a', 'r', 't', 0x00, // PUSHI
        0x03, 0xFF,                                     // PUSHB -1
        0x04, 0xFF, 0xFF,                               // PUSHW -1
        0x18, 0x01, 0x00, 0x00, 0x00,                  // CALL with one argument
        0x00, 0x00, 0x00, 0x00,                         // argument code address
        0x14, 0xFB, 0xFF, 0xFF, 0xFF,                   // BRA -5
        0x00                                              // BRK
    };
    const std::string path = FixturePath("_rt_retail_operands.qvm");
    const std::vector<uint8_t> binary = BuildMinimalQvm(code);
    {
        std::ofstream output(path, std::ios::binary);
        ASSERT_TRUE(output.is_open());
        output.write(reinterpret_cast<const char*>(binary.data()),
                     static_cast<std::streamsize>(binary.size()));
    }

    const QVMFile qvm = QVM_Parse(path);
    std::remove(path.c_str());

    ASSERT_TRUE(qvm.valid) << qvm.error;
    ASSERT_EQ(qvm.instructions.size(), 7U);
    EXPECT_EQ(qvm.instructions[0].type, QVMOpType::PUSHS);
    EXPECT_EQ(qvm.instructions[0].inline_text, "vanilla");
    EXPECT_EQ(qvm.instructions[1].type, QVMOpType::PUSHI);
    EXPECT_EQ(qvm.instructions[1].inline_text, "GOStart");
    EXPECT_EQ(qvm.instructions[2].signed_operand, -1);
    EXPECT_EQ(qvm.instructions[3].signed_operand, -1);
    EXPECT_EQ(qvm.instructions[4].type, QVMOpType::CALL);
    EXPECT_EQ(qvm.instructions[4].signed_operand, 1);
    EXPECT_EQ(qvm.instructions[5].signed_operand, -5);
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
    // `std::filesystem::path` + `GetExeDirectory()` instead of
    // `Utils::GetIGIRootPath() + "\\missions\\..."`: `GetIGIRootPath` lives in
    // source/utils.cpp, which the host-native lane cannot build, so the whole
    // suite failed to link there ("unresolved symbol Utils::GetIGIRootPath()").
    // `GetExeDirectory()` is inline in source/utils.h (host-portable), and
    // `GetIGIRootPath` returns the same `IGI_GAME_PATH` when it is set.
    const std::filesystem::path qvmPath =
        std::filesystem::path(CorpusRootString()) / "missions" / "location0" /
        ("level" + std::to_string(level)) / "objects.qvm";

    if (!std::filesystem::exists(qvmPath))
        GTEST_SKIP() << "objects.qvm missing for level " << level << ": " << qvmPath;

    QVMFile qvm = QVM_Parse(qvmPath.string());
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

// ============================================================
//  Defect 1 — `EscapeQSCString` must escape the backslash
//
//  Recorded in docs/igi-re-parity/TRIAGE_REMAINING_ISSUES.md as latent
//  ("a change would have no test that can fail") because no retail string
//  contains a backslash.  The input is synthetic, not unreachable: a QVM whose
//  string pool holds a single backslash is a two-byte pool entry plus a
//  six-byte code section, and before the fix the value did not survive the
//  printed form.  The ambiguous case is a
//  backslash immediately before a quote: the value `\"` printed as `"\\""`,
//  which the lexer reads as one backslash and then a closed literal, so the
//  value was destroyed and the remainder of the line became a stray literal.
//
//  The contract is the reader, not the printer: `qsc::Lex`
//  (source/level/qsc_lexer.cpp:141-150) decodes `\\` -> '\', `\"` -> '"',
//  `\n` -> LF, and `igi::MenuDefinition::FromDecompiledSource`
//  (source/menu/qvm_image.cpp:480-489) inverts `\\` and `\n`.  Both already
//  accepted `\\`; only the printer was wrong.
// ============================================================

namespace {

// PUSHSI <pool index 0> ; BRK
const std::vector<uint8_t> kPushPooledStringThenBrk = {0x08, 0x00, 0x00, 0x00, 0x00, 0x00};

void ExpectPooledStringSurvivesPrinting(const std::string& value, const std::string& scratch) {
    const QVMFile qvm =
        ParseSyntheticQvm(BuildQvmWithStringPool(kPushPooledStringThenBrk, value), scratch);
    ASSERT_TRUE(qvm.valid) << qvm.error;
    ASSERT_EQ(qvm.strings.size(), 1u) << "pool entry count";
    EXPECT_EQ(qvm.strings[0], value) << "the pool must hold the value verbatim";

    const std::string text = QVM_DecompileToString(qvm);
    ASSERT_FALSE(text.empty());
    std::string err;
    EXPECT_EQ(SoleStringLiteral(text, &err), value)
        << err << "\n  value  : " << value << "\n  printed: " << text;
}

} // namespace

TEST(QvmStringEscapeTest, PooledStringWithBackslashSurvivesPrinting) {
    ExpectPooledStringSurvivesPrinting("\\", "_rt_esc_1.qvm");
    ExpectPooledStringSurvivesPrinting("C:\\IGI\\missions\\location0", "_rt_esc_2.qvm");
    ExpectPooledStringSurvivesPrinting("\"", "_rt_esc_3.qvm");
    ExpectPooledStringSurvivesPrinting("line1\nline2", "_rt_esc_4.qvm");
    // The ambiguous case: a backslash immediately before a quote.
    ExpectPooledStringSurvivesPrinting("\\\"", "_rt_esc_5.qvm");
    // Backslash, quote, newline and a backslash directly before the quote.
    ExpectPooledStringSurvivesPrinting("a\\b\"c\nd\\\"e", "_rt_esc_6.qvm");
}

TEST(QvmStringEscapeTest, BackslashBeforeQuotePrintsUnambiguously) {
    const QVMFile qvm =
        ParseSyntheticQvm(BuildQvmWithStringPool(kPushPooledStringThenBrk, "\\\""),
                          "_rt_esc_exact.qvm");
    ASSERT_TRUE(qvm.valid) << qvm.error;

    // value        = '\' '"'
    // escaped      = "\\" "\""      (4 characters)
    // printed form = '"' "\\" "\"" '"' ';' LF
    const std::string expected = std::string("\"") + "\\\\" + "\\\"" + "\"" + ";\n";
    EXPECT_EQ(QVM_DecompileToString(qvm), expected);

    // And it is exactly what the lexer reads back as the original value.
    std::string err;
    EXPECT_EQ(SoleStringLiteral(expected, &err), "\\\"") << err;
}

TEST(QvmStringEscapeTest, InlinePushStringEscapesBackslashToo) {
    // PUSHS "a\"b" (inline, NUL-terminated) ; BRK
    const std::vector<uint8_t> code = {0x07, 'a', '\\', '"', 'b', 0x00, 0x00};
    const QVMFile qvm = ParseSyntheticQvm(BuildMinimalQvm(code), "_rt_esc_inline.qvm");
    ASSERT_TRUE(qvm.valid) << qvm.error;
    ASSERT_EQ(qvm.instructions.size(), 2u);
    EXPECT_EQ(qvm.instructions[0].inline_text, "a\\\"b");

    const std::string text = QVM_DecompileToString(qvm);
    std::string err;
    EXPECT_EQ(SoleStringLiteral(text, &err), "a\\\"b") << err << "\n  printed: " << text;
}

// End-to-end: QSC source -> Lex -> Parse -> Compile -> QVM bytes -> QVM_Parse
// -> QVM_DecompileToString -> Lex.  `QscQuote` encodes the value the way a
// script author writes it (the lexer's contract), so the assertion is not
// circular with respect to the printer under test.
TEST(QvmStringEscapeTest, CompileDecompilePipelinePreservesEscapedStrings) {
    const std::vector<std::string> values = {
        "\\",
        "C:\\IGI\\missions\\location0",
        "\"",
        "line1\nline2",
        "\\\"",
        "a\\b\"c\nd\\\"e",
    };

    for (const std::string& value : values) {
        const std::string src = "SetName(" + QscQuote(value) + ");";
        std::string err;
        const std::string printed = RoundTrip(src, &err, FixturePath("_rt_esc_pipe.qvm"));
        ASSERT_TRUE(err.empty()) << err << " for value " << value;
        ASSERT_FALSE(printed.empty());

        std::string lexErr;
        EXPECT_EQ(SoleStringLiteral(printed, &lexErr), value)
            << lexErr << "\n  source : " << src << "\n  printed: " << printed;

        // The printed text must also still be a well-formed QSC program.
        const qsc::LexResult lr = qsc::Lex(printed);
        EXPECT_TRUE(lr.ok) << lr.error << "\n  printed: " << printed;
        if (lr.ok) {
            const qsc::ParseResult pr = qsc::Parse(lr.tokens);
            EXPECT_TRUE(pr.ok) << pr.error << "\n  printed: " << printed;
        }
    }
}

// ============================================================
//  Defect 2 — operand layout for PUSHA / BLK / JSR
//
//  Recorded in docs/igi-re-parity/TRIAGE_REMAINING_ISSUES.md.  The retail
//  toolchain contradicts itself, so a reader has to pick a side and say which:
//
//    BLK  (47)  interpreter `Qvm_OpBlk` (HOOK 0x4BE980, src/qsc/qvmop.c:338-342)
//               does `vm->pc += 5 + Read32(vm->code + vm->pc + 1)` -- a 4-byte
//               skip count, 5 bytes plus the skipped block.
//               assembler `QvmAsm_OpPlain` (HOOK 0x4C0F50, src/qasm/asm.c:170-176)
//               writes ONE byte; the disassembler steps one byte
//               (src/qasm/dis.c:66-68).  THIS PARSER FOLLOWS THE ASSEMBLER.
//    JSR  (23)  interpreter registers `Qvm_OpError` = `Qvm_OpIllegal`
//               (src/qvm/qvm.c:545; HOOK 0x4BEC90, src/qsc/qvmop.c:491-496):
//               it sets the error and returns WITHOUT advancing pc, and
//               `Qvm_Execute` (src/qvm/qvm.c:326-337) stops on the first error.
//               assembler writes ONE byte.  THIS PARSER FOLLOWS THE ASSEMBLER.
//    PUSHA (6)  interpreter registers `Qvm_OpPush` (src/qvm/qvm.c:538;
//               HOOK 0x4BE8F0, src/qsc/qvmop.c:311-327): 4-byte operand,
//               `vm->pc += 5`.  assembler writes ONE byte (PUSHA is absent from
//               the `QvmAsm_OpPush` registrations, src/qasm/asm.c:46-57).
//               THIS PARSER FOLLOWS THE INTERPRETER.
//
//  BLK / JSR / PUSHA follow the INTERPRETER — the engine's own reader — because
//  that is what decides whether a file is accepted.  BLK carries a 4-byte skip
//  count and advances 5 + skip (`Qvm_OpBlk`, HOOK 0x4BE980, src/qsc/qvmop.c:338-342).
//  JSR is registered as `Qvm_OpError`/`Qvm_OpIllegal` (HOOK 0x4BEC90,
//  src/qsc/qvmop.c:490-496; src/qvm/qvm.c:545), so the walk stops with
//  QVM_ERR_ILLEGAL and the file is refused.  Both are unobservable in the retail
//  corpus (0 occurrences of all three across 1043 files), enforced by
//  QvmCorpusLayoutTest below.
// ============================================================

TEST(QvmOperandLayoutTest, BlkIsFivePlusSkipAndJsrIsRefused) {
    // BLK with skip 0 -> 5 bytes, then BRK.
    {
        const std::vector<uint8_t> code = {0x2F /*BLK*/, 0x00, 0x00, 0x00, 0x00 /*skip=0*/,
                                           0x00 /*BRK*/};
        const QVMFile qvm = ParseSyntheticQvm(BuildMinimalQvm(code), "_rt_layout_1.qvm");
        ASSERT_TRUE(qvm.valid) << qvm.error;
        ASSERT_EQ(qvm.instructions.size(), 2u);
        EXPECT_EQ(qvm.instructions[0].type, QVMOpType::BLK);
        EXPECT_EQ(qvm.instructions[0].size, 5u);   // 5 + skip(0)
        EXPECT_EQ(qvm.instructions[1].type, QVMOpType::BRK);
    }

    // BLK with skip 4 -> 9 bytes: the four skipped bytes are data, not opcodes.
    {
        const std::vector<uint8_t> code = {0x2F, 0x04, 0x00, 0x00, 0x00,
                                           0xAA, 0xBB, 0xCC, 0xDD, 0x00 /*BRK*/};
        const QVMFile qvm = ParseSyntheticQvm(BuildMinimalQvm(code), "_rt_layout_2.qvm");
        ASSERT_TRUE(qvm.valid) << qvm.error;
        ASSERT_EQ(qvm.instructions.size(), 2u);
        EXPECT_EQ(qvm.instructions[0].size, 9u);   // 5 + 4
        EXPECT_EQ(qvm.instructions[1].type, QVMOpType::BRK);
    }

    // JSR is illegal in the engine's own reader, so the file is refused.
    {
        const std::vector<uint8_t> code = {0x17 /*JSR*/, 0x00 /*BRK*/};
        const QVMFile qvm = ParseSyntheticQvm(BuildMinimalQvm(code), "_rt_layout_3.qvm");
        EXPECT_FALSE(qvm.valid);
        EXPECT_EQ(qvm.error_code, 5);              // QVM_ERR_ILLEGAL
    }

    // PUSHA with a 4-byte operand, then BRK — the interpreter's 5-byte form.
    {
        const std::vector<uint8_t> code = {0x06, 0x10, 0x00, 0x00, 0x00, 0x00 /*BRK*/};
        const QVMFile qvm = ParseSyntheticQvm(BuildMinimalQvm(code), "_rt_layout_4.qvm");
        ASSERT_TRUE(qvm.valid) << qvm.error;
        ASSERT_EQ(qvm.instructions.size(), 2u);
        EXPECT_EQ(qvm.instructions[0].type, QVMOpType::PUSHA);
        EXPECT_EQ(qvm.instructions[0].size, 5u);          // interpreter: Qvm_OpPush
        EXPECT_EQ(qvm.instructions[0].operand, 0x10u);    // the 4-byte code offset
        EXPECT_EQ(qvm.instructions[1].type, QVMOpType::BRK);
    }
}

// ============================================================
//  Corpus measurements backing both triage claims
// ============================================================

namespace {

std::filesystem::path QvmCorpusRoot() {
    return std::filesystem::path(CorpusRootString());
}

} // namespace

TEST(QvmCorpusLayoutTest, NoRetailQvmDecodesPushaBlkOrJsr) {
    const std::filesystem::path root = QvmCorpusRoot();
    if (!std::filesystem::is_directory(root)) {
        GTEST_SKIP() << "no corpus at " << root;
    }

    size_t files = 0, parsed = 0, rejected = 0;
    size_t pusha = 0, blk = 0, jsr = 0;
    std::vector<std::string> rejections;

    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        if (!entry.is_regular_file()) continue;
        std::string ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (ext != ".qvm") continue;
        ++files;

        const QVMFile qvm = QVM_Parse(entry.path().string());
        if (!qvm.valid) {
            ++rejected;
            if (rejections.size() < 5) {
                rejections.push_back(entry.path().string() + ": " + qvm.error);
            }
            continue;
        }
        ++parsed;
        for (const QVMInstruction& in : qvm.instructions) {
            if (in.type == QVMOpType::PUSHA) ++pusha;
            else if (in.type == QVMOpType::BLK) ++blk;
            else if (in.type == QVMOpType::JSR) ++jsr;
        }
    }

    std::cout << "[corpus] .qvm files=" << files << " parsed=" << parsed
              << " rejected=" << rejected << " PUSHA=" << pusha << " BLK=" << blk
              << " JSR=" << jsr << "\n";

    EXPECT_GT(files, 1000u) << "the corpus scan found suspiciously few .qvm files";
    for (const std::string& r : rejections) ADD_FAILURE() << "unparseable: " << r;
    EXPECT_EQ(rejected, 0u);
    EXPECT_EQ(pusha, 0u);
    EXPECT_EQ(blk, 0u);
    EXPECT_EQ(jsr, 0u);
}

TEST(QvmCorpusLayoutTest, RetailMenuStringsContainNoBackslash) {
    const std::filesystem::path root = QvmCorpusRoot();
    if (!std::filesystem::is_directory(root / "menusystem")) {
        GTEST_SKIP() << "no menu corpus under " << root;
    }

    struct Expectation {
        const char* name;
        uint32_t strings;
        size_t newlines;
    };
    const Expectation expectations[] = {
        {"ingamemenu.qvm", 139u, 14u},
        {"MAINMENU.QVM", 249u, 28u},
    };

    for (const Expectation& e : expectations) {
        const std::filesystem::path path = root / "menusystem" / e.name;
        ASSERT_TRUE(std::filesystem::exists(path)) << path;
        const QVMFile qvm = QVM_Parse(path.string());
        ASSERT_TRUE(qvm.valid) << path << ": " << qvm.error;

        size_t backslashes = 0, stringsWithNewline = 0;
        for (const std::string& s : qvm.strings) {
            if (s.find('\\') != std::string::npos) ++backslashes;
            if (s.find('\n') != std::string::npos) ++stringsWithNewline;
        }
        std::cout << "[corpus] " << e.name << ": " << qvm.stringCount() << " strings, "
                  << backslashes << " with a backslash, " << stringsWithNewline
                  << " with a newline\n";

        EXPECT_EQ(qvm.stringCount(), e.strings) << e.name;
        EXPECT_EQ(backslashes, 0u) << e.name;
        EXPECT_EQ(stringsWithNewline, e.newlines) << e.name;
    }
}
