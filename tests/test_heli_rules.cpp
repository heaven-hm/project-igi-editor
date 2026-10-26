// test_heli_rules.cpp - the retail "Heli" vehicle task and "Rotor" magic object,
// pure-logic port (#60).
//
// The oracle for every number here is IGI-RE (read-only). Key citations:
//   heli/heli.c:183-201     Heli_Init 0x4310F0: the 13-parameter ParamTable, the
//                           two ranges, TASKTYPE_HELI
//   heli/heli.c:359-407     Heli_Control 0x431B70: the -1.0f throttle sentinel,
//                           the tick-0 reset
//   heli/heli.c:412-520     Heli_Move 0x431E30: the speed ramp, the smoothed
//                           turn, the three turning forces, the nose pitch
//   heli/heli.c:523-619     Heli_UpdateWheels 0x432550: the wheel probes
//   heli/heli.c:671-681     Heli_GetControlInfo 0x432BE0 + gHeliControls :754
//   heli/heli.c:687-719     Heli_ReadKeys 0x432C10: the throttle ladder
//   plane/heli.c:77-88      Heli_ParseConfig 0x433030: the two HeliConfig lists
//   rotor/rotor.c:110-166   Rotor_DrawPrim 0x42C630: the blade copies and tilt
//   rotor/rotor.c:213-263   Rotor_Run 0x42D440: lift, tilt force, the spin
//   rotor/rotor.c:265-290   Rotor_Move 0x42D680
//   rotor/rotor.c:302-316   Rotor_ParseDef 0x42D9F0
//   physobj/physobjtype.c:64-122  DefinePhysicsObjType 0x4EDD20: where the task
//                           argument list starts
//   editobj/vehconfig.c:16-39     Vehicle_ParseConfig 0x5031F0
//   vehicle/parts.c:284-292       HeliWheel_ParseArgs 0x427240 (the swap)
//
// The corpus half reads the retail OBJECTS.QVM with the editor's own QVM_Parse
// and HELI.QVM with QVM_DecompileToString, then checks the authored declaration
// against HeliParamTable() and every authored instance against ParseHeliParams.

#include "../source/level/qvm_decompiler.h"
#include "../source/level/qvm_parser.h"
#include "../source/runtime/heli_rules.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;

fs::path GameRoot() {
    if (const char* env = std::getenv("IGI_GAME_PATH"); env != nullptr && *env != '\0')
        return fs::path(env);
    return fs::path("/Users/haseeb-mir/Downloads/IGI");
}

// The retail level directories are not consistently cased: LEVEL2..LEVEL14 are
// upper case, but the first mission ships as `level1`.
fs::path LevelDir(int level) {
    const fs::path base = GameRoot() / "MISSIONS" / "location0";
    const fs::path upper = base / ("LEVEL" + std::to_string(level));
    if (fs::is_directory(upper)) return upper;
    return base / ("level" + std::to_string(level));
}

fs::path LevelDirNamed(const std::string& name) {
    return GameRoot() / "MISSIONS" / "location0" / name;
}

// ---------------------------------------------------------------------------
// QVM bytecode -> Heli arg tokens
// ---------------------------------------------------------------------------

bool IsStringPush(QVMOpType type) {
    return type == QVMOpType::PUSHS || type == QVMOpType::PUSHSI ||
           type == QVMOpType::PUSHSIB || type == QVMOpType::PUSHSIW;
}

bool IsIntPush(QVMOpType type) {
    return type == QVMOpType::PUSHI || type == QVMOpType::PUSHII ||
           type == QVMOpType::PUSHIIB || type == QVMOpType::PUSHIIW ||
           type == QVMOpType::PUSHB || type == QVMOpType::PUSHW;
}

bool IsIdentifierPush(QVMOpType type) {
    return type == QVMOpType::PUSHII || type == QVMOpType::PUSHIIB ||
           type == QVMOpType::PUSHIIW;
}

std::string FormatFloat(float value) {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.9g", value);
    return buffer;
}

// Walks one authored Heli call and returns its arguments in the editor's
// argTokens layout: [0]=task id, [1]=type, [2]=name, [3]=parameter 0.
std::vector<std::string> ReadHeliCall(const QVMFile& qvm, std::size_t type_index) {
    std::vector<std::string> tokens;
    tokens.push_back("-1");     // the task id the engine assigns at create time
    tokens.push_back("Heli");   // type

    const std::size_t kExpected = 2 + 1 + static_cast<std::size_t>(igi::kHeliTokenCount);

    for (std::size_t i = type_index + 1;
         i < qvm.instructions.size() && tokens.size() < kExpected; ++i) {
        const QVMInstruction& in = qvm.instructions[i];
        if (in.type == QVMOpType::BRK) continue;

        if (IsStringPush(in.type)) {
            tokens.push_back(in.operand < qvm.strings.size() ? qvm.strings[in.operand]
                                                             : std::string());
            continue;
        }
        if (IsIntPush(in.type)) {
            if (IsIdentifierPush(in.type) && in.operand < qvm.identifiers.size()) {
                tokens.push_back(qvm.identifiers[in.operand]);
            } else {
                tokens.push_back(std::to_string(static_cast<int>(in.operand)));
            }
            continue;
        }
        if (in.type == QVMOpType::PUSH0) { tokens.push_back("0"); continue; }
        if (in.type == QVMOpType::PUSH1) { tokens.push_back("1"); continue; }
        if (in.type == QVMOpType::PUSHF) { tokens.push_back(FormatFloat(in.operand_float)); continue; }
        if (in.type == QVMOpType::MINUS) {
            if (!tokens.empty()) tokens.back() = "-" + tokens.back();
            continue;
        }
        break;  // anything else ends the argument list
    }
    return tokens;
}

// Every authored Heli in the file, in file order. The declaration call pushes
// the type string too, so the two are told apart structurally: after the type
// comes the instance NAME string, and after that a declaration has another
// parameter-name string while an instance has a value.
std::vector<std::vector<std::string>> ExtractHeliCalls(const QVMFile& qvm) {
    std::vector<std::vector<std::string>> out;
    for (std::size_t i = 0; i < qvm.instructions.size(); ++i) {
        const QVMInstruction& in = qvm.instructions[i];
        if (!IsStringPush(in.type)) continue;
        if (in.operand >= qvm.strings.size()) continue;
        if (qvm.strings[in.operand] != "Heli") continue;

        std::size_t name = i + 1;
        while (name < qvm.instructions.size() && qvm.instructions[name].type == QVMOpType::BRK) ++name;
        if (name >= qvm.instructions.size() || !IsStringPush(qvm.instructions[name].type)) continue;

        std::size_t first = name + 1;
        while (first < qvm.instructions.size() && qvm.instructions[first].type == QVMOpType::BRK) ++first;
        if (first >= qvm.instructions.size() || IsStringPush(qvm.instructions[first].type)) continue;

        out.push_back(ReadHeliCall(qvm, i));
    }
    return out;
}

// The parameters a `Task_DeclareParameters("Heli", ...)` call declares, in
// order, as alternating name/type strings (the type name is dropped).
std::vector<std::string> DeclaredHeliParameters(const QVMFile& qvm) {
    std::vector<std::string> out;
    for (std::size_t i = 0; i < qvm.instructions.size(); ++i) {
        const QVMInstruction& in = qvm.instructions[i];
        if (!IsIdentifierPush(in.type)) continue;
        if (in.operand >= qvm.identifiers.size()) continue;
        if (qvm.identifiers[in.operand] != "Task_DeclareParameters") continue;

        std::size_t j = i + 1;
        if (j < qvm.instructions.size() && qvm.instructions[j].type == QVMOpType::CALL) ++j;
        if (j < qvm.instructions.size() && qvm.instructions[j].type == QVMOpType::BRA) ++j;
        std::vector<std::string> pushed;
        for (; j < qvm.instructions.size() && qvm.instructions[j].type != QVMOpType::POP; ++j) {
            if (!IsStringPush(qvm.instructions[j].type)) continue;
            const std::size_t operand = qvm.instructions[j].operand;
            pushed.push_back(operand < qvm.strings.size() ? qvm.strings[operand] : std::string());
        }
        if (pushed.size() >= 3 && pushed[0] == "Heli") {
            out.assign(pushed.begin() + 1, pushed.end());
            return out;
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// objects.qsc text -> the same two things, for the cross-check
// ---------------------------------------------------------------------------

std::string ReadFile(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return {};
    return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

std::string Trim(const std::string& value) {
    const std::size_t first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const std::size_t last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::string Unquote(const std::string& value) {
    const std::string trimmed = Trim(value);
    if (trimmed.size() >= 2 && trimmed.front() == '"' && trimmed.back() == '"') {
        return trimmed.substr(1, trimmed.size() - 2);
    }
    return trimmed;
}

// The comma-separated arguments of the call whose '(' is at `open`, plus the
// index just past its ')'. The decompiler renders a task's CHILDREN inside its
// parent's argument list, and expressions in the arguments contain their own
// parentheses and commas inside string literals, so the scan balances parens
// and skips over quoted strings.
std::vector<std::string> ReadCallArguments(const std::string& text, std::size_t open,
                                           std::size_t* end) {
    std::vector<std::string> args;
    std::size_t close = std::string::npos;
    int depth = 0;
    bool in_string = false;
    for (std::size_t i = open; i < text.size(); ++i) {
        const char c = text[i];
        if (in_string) {
            if (c == '\\') { ++i; continue; }
            if (c == '"') in_string = false;
            continue;
        }
        if (c == '"') { in_string = true; continue; }
        if (c == '(') { ++depth; continue; }
        if (c == ')') {
            --depth;
            if (depth == 0) { close = i; break; }
        }
    }
    if (close == std::string::npos) {
        if (end != nullptr) *end = text.size();
        return args;
    }

    std::size_t cursor = open + 1;
    depth = 0;
    in_string = false;
    for (std::size_t i = cursor; i <= close; ++i) {
        const char c = i < close ? text[i] : ',';
        if (in_string) {
            if (c == '\\') { ++i; continue; }
            if (c == '"') in_string = false;
            continue;
        }
        if (c == '"') { in_string = true; continue; }
        if (c == '(') { ++depth; continue; }
        if (c == ')') { --depth; continue; }
        if (c == ',' && depth == 0) {
            args.push_back(Trim(text.substr(cursor, i - cursor)));
            cursor = i + 1;
        }
    }
    if (end != nullptr) *end = close + 1;
    return args;
}

std::vector<std::vector<std::string>> ParseHeliCallsFromText(const std::string& text) {
    std::vector<std::vector<std::string>> out;
    const std::string needle = "Task_New(";
    std::size_t pos = 0;
    while ((pos = text.find(needle, pos)) != std::string::npos) {
        std::size_t end = 0;
        const std::vector<std::string> args = ReadCallArguments(text, pos + needle.size() - 1, &end);
        if (args.size() >= 3 && Unquote(args[1]) == "Heli") {
            std::vector<std::string> tokens;
            for (const std::string& arg : args) tokens.push_back(Unquote(arg));
            out.push_back(tokens);
        }
        pos += needle.size();
    }
    return out;
}

std::vector<std::string> ParseDeclaredHeliParametersFromText(const std::string& text) {
    const std::string needle = "Task_DeclareParameters(";
    std::size_t pos = 0;
    while ((pos = text.find(needle, pos)) != std::string::npos) {
        std::size_t end = 0;
        const std::vector<std::string> args = ReadCallArguments(text, pos + needle.size() - 1, &end);
        if (args.size() >= 3 && Unquote(args[0]) == "Heli") {
            std::vector<std::string> out;
            for (std::size_t i = 1; i < args.size(); ++i) out.push_back(Unquote(args[i]));
            return out;
        }
        pos = end > pos ? end : pos + needle.size();
    }
    return {};
}

// Every `DefinePhysicsObjType(...)` in a decompiled physics QVM, as its
// argument list.
std::vector<std::vector<std::string>> ParsePhysicsDefsFromText(const std::string& text) {
    std::vector<std::vector<std::string>> out;
    const std::string needle = "DefinePhysicsObjType(";
    std::size_t pos = 0;
    while ((pos = text.find(needle, pos)) != std::string::npos) {
        std::size_t end = 0;
        const std::vector<std::string> args = ReadCallArguments(text, pos + needle.size() - 1, &end);
        if (!args.empty()) {
            std::vector<std::string> tokens;
            for (const std::string& arg : args) tokens.push_back(Unquote(arg));
            out.push_back(tokens);
        }
        pos = end > pos ? end : pos + needle.size();
    }
    return out;
}

// ---------------------------------------------------------------------------

std::vector<std::string> Tokens(std::initializer_list<const char*> values) {
    return std::vector<std::string>(values.begin(), values.end());
}

// An identity 3x3.
void Identity(float m[9]) {
    for (int i = 0; i < 9; ++i) m[i] = (i % 4 == 0) ? 1.0f : 0.0f;
}

// The BELL vehicle config, as the shipped HELI.QVM authors it.
igi::HeliVehicleConfig BellConfig() {
    igi::HeliVehicleConfig cfg{};
    cfg.start_speed_scale = 200.0f;
    for (int i = 0; i < 3; ++i) {
        cfg.axis[i].rate = 50.0f;
        cfg.axis[i].smooth = 0.4f;
    }
    cfg.thrust = 3000.0f;
    cfg.accel = 0.027000000700354576f;
    cfg.accel_slow = 0.0030000000260770321f;
    cfg.part_count = 3;
    cfg.valid = true;
    return cfg;
}

}  // namespace

// ===========================================================================
// The authored parameter table
// ===========================================================================

TEST(HeliRules, ParamTableShapeMatchesHeliInit) {
    const igi::HeliParamDesc* table = igi::HeliParamTable();
    ASSERT_NE(table, nullptr);

    // The retail Task_DeclareParameters line, verbatim
    // (LEVEL2/objects.qsc:62-63).
    const char* kAuthored[igi::kHeliParamCount] = {
        "Position", "Orientation", "Original Thrust", "Speed", "Model",
        "Collision detection", "Force Z", "Open door", "Can fire", "Play sound",
        "ViewCone Alpha (degrees)", "ViewCone Gamma (degrees)", "ViewCone Length (meter)",
    };
    const char* kTypes[igi::kHeliParamCount] = {
        "ObjectPos", "Real32x9", "Real32", "Real32x3", "String16", "bool8", "bool8",
        "VarString", "VarString", "VarString", "Degrees", "Degrees", "Real32",
    };

    int total = 0;
    for (int i = 0; i < igi::kHeliParamCount; ++i) {
        EXPECT_EQ(std::string(table[i].authored_name), kAuthored[i]) << "slot " << i;
        EXPECT_EQ(std::string(table[i].type_name), kTypes[i]) << "slot " << i;
        total += table[i].token_count;
    }
    // ParamTable_New(13) (heli.c:183); the script argument count is 19 because
    // ObjectPos and Real32x9 consume three arguments each and Real32x3 three
    // more (paramops.c:182, :185, :423-435).
    EXPECT_EQ(total, igi::kHeliTokenCount);
}

TEST(HeliRules, ParamTableRangesAreTheEnginesTwo) {
    const igi::HeliParamDesc* table = igi::HeliParamTable();

    // "Original Thrust" carries Param_Range02 (heli.c:188-190) and "ViewCone
    // Length (meter)" carries gHeliRange (heli.c:184-185, :200). Every other
    // row passes NULL.
    for (int i = 0; i < igi::kHeliParamCount; ++i) {
        const bool expected = (i == 2 || i == 12);
        EXPECT_EQ(table[i].has_range, expected) << "slot " << i;
    }
    EXPECT_FLOAT_EQ(table[2].range_min, 0.0f);
    EXPECT_FLOAT_EQ(table[2].range_max, 2.0f);
    EXPECT_FLOAT_EQ(table[12].range_min, 0.0f);
    EXPECT_FLOAT_EQ(table[12].range_max, 1000.0f);
}

TEST(HeliRules, ParamTableEngineFieldsAreTheLoadBearingColumn) {
    const igi::HeliParamDesc* table = igi::HeliParamTable();
    // The authored names are misleading; these are the fields the engine
    // actually binds (heli.c:186-200).
    EXPECT_STREQ(table[2].engine_field, "v.start_speed");
    EXPECT_STREQ(table[3].engine_field, "v.startVel");
    EXPECT_STREQ(table[4].engine_field, "v.name");
    EXPECT_STREQ(table[5].engine_field, "v.collide");
    EXPECT_STREQ(table[6].engine_field, "v.airborne");
    EXPECT_STREQ(table[7].engine_field, "rotor_expr");
    EXPECT_STREQ(table[8].engine_field, "light_expr");
    EXPECT_STREQ(table[9].engine_field, "engine_expr");
}

TEST(HeliRules, ControlTableIsTheFiveGHeliControls) {
    const igi::HeliControlInfo* table = igi::HeliControlTable();
    ASSERT_NE(table, nullptr);
    EXPECT_EQ(igi::kHeliControlCount, 5);
    EXPECT_STREQ(table[0].name, "dAlpha");
    EXPECT_STREQ(table[1].name, "dBeta");
    EXPECT_STREQ(table[2].name, "dGamma");
    EXPECT_STREQ(table[3].name, "Thrust");
    EXPECT_STREQ(table[4].name, "PilotAnim");
    for (int i = 0; i < 3; ++i) {
        EXPECT_FLOAT_EQ(table[i].min, -360.0f);
        EXPECT_FLOAT_EQ(table[i].max, 360.0f);
    }
    EXPECT_FLOAT_EQ(table[3].min, 0.0f);
    EXPECT_FLOAT_EQ(table[3].max, 1.0f);
    EXPECT_FLOAT_EQ(table[4].min, 0.0f);
    EXPECT_FLOAT_EQ(table[4].max, 16.0f);
}

// ===========================================================================
// Rotation helpers
// ===========================================================================

TEST(HeliRules, SetEulerIsRzTimesRyTimesRx) {
    const float ax = 0.31f;
    const float ay = -0.47f;
    const float az = 1.13f;

    float got[9];
    igi::HeliSetEuler(got, ax, ay, az);

    // An independent double-precision reference, written as Rz*Ry*Rx.
    const double cx = std::cos(ax), sx = std::sin(ax);
    const double cy = std::cos(ay), sy = std::sin(ay);
    const double cz = std::cos(az), sz = std::sin(az);
    const double rx[9] = {1, 0, 0, 0, cx, -sx, 0, sx, cx};
    const double ry[9] = {cy, 0, sy, 0, 1, 0, -sy, 0, cy};
    const double rz[9] = {cz, -sz, 0, sz, cz, 0, 0, 0, 1};
    double tmp[9];
    double ref[9];
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            tmp[i * 3 + j] = 0.0;
            for (int k = 0; k < 3; ++k) tmp[i * 3 + j] += rz[i * 3 + k] * ry[k * 3 + j];
        }
    }
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            ref[i * 3 + j] = 0.0;
            for (int k = 0; k < 3; ++k) ref[i * 3 + j] += tmp[i * 3 + k] * rx[k * 3 + j];
        }
    }
    for (int i = 0; i < 9; ++i) EXPECT_NEAR(got[i], ref[i], 1e-6) << "index " << i;
}

TEST(HeliRules, SetEulerOfZeroIsIdentity) {
    float m[9];
    igi::HeliSetEuler(m, 0.0f, 0.0f, 0.0f);
    for (int i = 0; i < 9; ++i) EXPECT_FLOAT_EQ(m[i], (i % 4 == 0) ? 1.0f : 0.0f) << "index " << i;
}

TEST(HeliRules, SetRotXAndRotZMatchTheirDefinitions) {
    float m[9];
    const float a = 0.83f;

    igi::HeliSetRotX(m, a);
    EXPECT_FLOAT_EQ(m[0], 1.0f);
    EXPECT_FLOAT_EQ(m[1], 0.0f);
    EXPECT_FLOAT_EQ(m[2], 0.0f);
    EXPECT_FLOAT_EQ(m[3], 0.0f);
    EXPECT_FLOAT_EQ(m[6], 0.0f);
    EXPECT_NEAR(m[4], std::cos(a), 1e-6);
    EXPECT_NEAR(m[5], -std::sin(a), 1e-6);
    EXPECT_NEAR(m[7], std::sin(a), 1e-6);
    EXPECT_NEAR(m[8], std::cos(a), 1e-6);

    igi::HeliSetRotZ(m, a);
    EXPECT_FLOAT_EQ(m[2], 0.0f);
    EXPECT_FLOAT_EQ(m[5], 0.0f);
    EXPECT_FLOAT_EQ(m[6], 0.0f);
    EXPECT_FLOAT_EQ(m[7], 0.0f);
    EXPECT_FLOAT_EQ(m[8], 1.0f);
    EXPECT_NEAR(m[0], std::cos(a), 1e-6);
    EXPECT_NEAR(m[1], -std::sin(a), 1e-6);
    EXPECT_NEAR(m[3], std::sin(a), 1e-6);
    EXPECT_NEAR(m[4], std::cos(a), 1e-6);
}

TEST(HeliRules, MatrixMulSelectsTheRequestedProductOrder) {
    // mat_mul (rotor.h:159-174) and Mul (matrix.c:10-25) both accumulate all
    // three products in `long double` and round once, so the k order does NOT
    // change the result for ordinary inputs -- it is kept in the port only to
    // mirror the source. What the parameter does control is WHICH product goes
    // in each slot, and that is what this pins: the reference walks an explicit
    // k list instead of the engine's straight-line expression.
    float a[9] = {3.0f, -5.0f, 7.0f, 11.0f, 13.0f, -17.0f, 19.0f, 23.0f, -29.0f};
    float b[9] = {2.0f, -3.0f, 5.0f, 7.0f, 11.0f, -13.0f, 17.0f, -19.0f, 23.0f};

    for (int last0 = 0; last0 < 3; ++last0) {
        for (int last1 = 0; last1 < 3; ++last1) {
            for (int last2 = 0; last2 < 3; ++last2) {
                float got[9];
                igi::HeliMatrixMul(got, a, b, last0, last1, last2);

                const int lasts[3] = {last0, last1, last2};
                for (int i = 0; i < 3; ++i) {
                    const int last = lasts[i];
                    const int order[3] = {last == 0 ? 1 : 0, last == 2 ? 1 : 2, last};
                    for (int j = 0; j < 3; ++j) {
                        long double acc = 0.0L;
                        for (int t = 0; t < 3; ++t) {
                            const int k = order[t];
                            acc += static_cast<long double>(a[i * 3 + k]) * b[k * 3 + j];
                        }
                        EXPECT_FLOAT_EQ(got[i * 3 + j], static_cast<float>(acc))
                            << "last " << last0 << last1 << last2 << " index " << i * 3 + j;
                    }
                }
            }
        }
    }

    // The three orders are the only three distinct k permutations: last = 1
    // reproduces the plain `Mul` order 0, 2, 1 (matrix.c:19-20).
    float plain[9];
    float same[9];
    igi::HeliMatrixMul(plain, a, b, 1, 1, 1);
    igi::HeliMatrixMul(same, a, b, 1, 1, 1);
    for (int i = 0; i < 9; ++i) EXPECT_FLOAT_EQ(plain[i], same[i]) << "index " << i;
}

TEST(HeliRules, MatrixMulIsSafeWhenOutAliasesAnInput) {
    // matrix.c:15 multiplies through a local `Matrix t`, so `Mul(m, m, &r)` is
    // legal; the port must keep that property.
    float m[9] = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 9.0f};
    float expected[9];
    float identity[9];
    Identity(identity);
    igi::HeliMatrixMul(expected, m, identity, 1, 1, 1);
    igi::HeliMatrixMul(m, m, identity, 1, 1, 1);
    for (int i = 0; i < 9; ++i) EXPECT_FLOAT_EQ(m[i], expected[i]) << "index " << i;
}

TEST(HeliRules, FneIsFalseForNan) {
    // heli.h:119-122: `a < b || a > b`, which is false when either side is NaN.
    const float nan = std::numeric_limits<float>::quiet_NaN();
    EXPECT_TRUE(igi::HeliFne(1.0L, 0.0L));
    EXPECT_TRUE(igi::HeliFne(-1.0L, 0.0L));
    EXPECT_FALSE(igi::HeliFne(0.0L, 0.0L));
    EXPECT_FALSE(igi::HeliFne(static_cast<long double>(nan), 0.0L));
}

// ===========================================================================
// Heli_Control
// ===========================================================================

TEST(HeliRules, ThrottleSentinelIsExactlyMinusOne) {
    float axes[5] = {0.0f, 0.0f, 0.0f, -1.0f, -2.0f};
    EXPECT_FALSE(igi::HeliControlThrustRequested(axes));

    axes[3] = -0.99999994f;  // the float just above -1.0f
    EXPECT_TRUE(igi::HeliControlThrustRequested(axes));

    axes[3] = 0.0f;
    EXPECT_TRUE(igi::HeliControlThrustRequested(axes));

    // The test is on the raw bits, so a NaN throttle IS a request.
    axes[3] = std::numeric_limits<float>::quiet_NaN();
    EXPECT_TRUE(igi::HeliControlThrustRequested(axes));
}

TEST(HeliRules, ControlApplyAxesCopiesSteerAndHonoursTheSentinel) {
    igi::HeliMoveState state{};
    state.target_speed = 0.5f;
    const float axes[5] = {0.25f, -0.5f, 0.75f, -1.0f, -2.0f};
    igi::HeliControlApplyAxes(&state, axes);
    EXPECT_FLOAT_EQ(state.steer[0], 0.25f);
    EXPECT_FLOAT_EQ(state.steer[1], -0.5f);
    EXPECT_FLOAT_EQ(state.steer[2], 0.75f);
    EXPECT_FLOAT_EQ(state.target_speed, 0.5f) << "the -1.0f sentinel leaves it alone";

    const float axes2[5] = {0.0f, 0.0f, 0.0f, 1.2f, -2.0f};
    igi::HeliControlApplyAxes(&state, axes2);
    EXPECT_FLOAT_EQ(state.target_speed, 1.2f);
}

TEST(HeliRules, ControlResetRestoresTheAuthoredStart) {
    igi::HeliMoveState state{};
    state.speed = 9.0f;
    state.target_speed = 9.0f;
    state.turn[0] = 1.0f;
    state.steer[0] = 1.0f;
    state.stale = 5.0f;

    const double start_pos[3] = {1.0, 2.0, 3.0};
    const float start_vel[3] = {0.0f, 0.0f, 2.0f};
    float start_matrix[9];
    igi::HeliSetEuler(start_matrix, 0.0f, 0.0f, 0.5f);  // Rz(0.5)
    float axes[5] = {1.0f, 1.0f, 1.0f, 1.5f, 3.0f};
    float vel[3] = {0.0f, 0.0f, 0.0f};

    igi::HeliControlReset(&state, axes, start_pos, start_vel, start_matrix, 1.5f, vel);

    // The caller's array is written back (heli.c:371-375).
    EXPECT_FLOAT_EQ(axes[3], -1.0f);
    EXPECT_FLOAT_EQ(axes[4], -2.0f);
    EXPECT_FLOAT_EQ(axes[0], 0.0f);
    EXPECT_FLOAT_EQ(axes[1], 0.0f);
    EXPECT_FLOAT_EQ(axes[2], 0.0f);

    // speed and target_speed become the authored "Original Thrust"
    // (heli.c:379-380), turn and steer are zeroed (heli.c:381-388).
    EXPECT_FLOAT_EQ(state.speed, 1.5f);
    EXPECT_FLOAT_EQ(state.target_speed, 1.5f);
    for (int i = 0; i < 3; ++i) {
        EXPECT_FLOAT_EQ(state.turn[i], 0.0f) << "turn " << i;
        EXPECT_FLOAT_EQ(state.steer[i], 0.0f) << "steer " << i;
    }
    for (int i = 0; i < 9; ++i) EXPECT_FLOAT_EQ(state.matrix[i], start_matrix[i]) << "m " << i;
    EXPECT_EQ(state.matrix_count, 0);

    // vel = start_matrix * start_vel (heli.c:392-395): Rz(0.5) * (0,0,2) = (0,0,2).
    EXPECT_NEAR(vel[0], 0.0f, 1e-6);
    EXPECT_NEAR(vel[1], 0.0f, 1e-6);
    EXPECT_NEAR(vel[2], 2.0f, 1e-6);
}

TEST(HeliRules, ControlResetTurnsTheStartVelocityIntoWorldSpace) {
    igi::HeliMoveState state{};
    float start_matrix[9];
    igi::HeliSetEuler(start_matrix, 0.0f, 0.0f, 1.5707963267948966f);  // Rz(pi/2)
    const float start_vel[3] = {1.0f, 0.0f, 0.0f};
    float vel[3] = {0.0f, 0.0f, 0.0f};

    igi::HeliControlReset(&state, nullptr, nullptr, start_vel, start_matrix, 1.0f, vel);
    EXPECT_NEAR(vel[0], 0.0f, 1e-5);
    EXPECT_NEAR(vel[1], 1.0f, 1e-5);
    EXPECT_NEAR(vel[2], 0.0f, 1e-5);
}

TEST(HeliRules, ThrottleLadderAndNudges) {
    const float* steps = igi::HeliThrottleSteps();
    ASSERT_NE(steps, nullptr);
    EXPECT_FLOAT_EQ(steps[0], 0.84f);
    EXPECT_FLOAT_EQ(steps[4], 1.0f);
    EXPECT_FLOAT_EQ(steps[9], 1.2f);
    // The ladder is NOT monotone into [0,1]: 0.84 and 1.2 are both authored.
    EXPECT_LT(steps[0], 1.0f);
    EXPECT_GT(steps[9], 1.0f);

    // heli.c:706-719 clamps to [0.8, 1.25], not to the ladder's own range.
    EXPECT_NEAR(igi::HeliThrottleStep(1.0f, 1), 1.01f, 1e-6);
    EXPECT_NEAR(igi::HeliThrottleStep(1.0f, -1), 0.99f, 1e-6);
    EXPECT_FLOAT_EQ(igi::HeliThrottleStep(0.8f, -1), 0.8f);
    EXPECT_FLOAT_EQ(igi::HeliThrottleStep(1.25f, 1), 1.25f);
}

// ===========================================================================
// Heli_Move
// ===========================================================================

TEST(HeliRules, AccelRateSwitchesAtPointEight) {
    igi::HeliVehicleConfig cfg = BellConfig();
    EXPECT_FLOAT_EQ(igi::HeliAccelRate(0.0f, cfg), cfg.accel_slow);
    EXPECT_FLOAT_EQ(igi::HeliAccelRate(0.79f, cfg), cfg.accel_slow);
    EXPECT_FLOAT_EQ(igi::HeliAccelRate(0.8f, cfg), cfg.accel);
    EXPECT_FLOAT_EQ(igi::HeliAccelRate(1.2f, cfg), cfg.accel);
    // `!(speed >= 0.8)` is true for NaN, so a NaN speed takes the slow rate.
    EXPECT_FLOAT_EQ(igi::HeliAccelRate(std::numeric_limits<float>::quiet_NaN(), cfg),
                    cfg.accel_slow);
}

TEST(HeliRules, ApproachSpeedRampsAndClamps) {
    EXPECT_NEAR(igi::HeliApproachSpeed(0.0f, 1.0f, 0.027f), 0.027f, 1e-7);
    EXPECT_FLOAT_EQ(igi::HeliApproachSpeed(0.99f, 1.0f, 0.027f), 1.0f);
    EXPECT_NEAR(igi::HeliApproachSpeed(1.0f, 0.5f, 0.027f), 0.973f, 1e-7);
    EXPECT_FLOAT_EQ(igi::HeliApproachSpeed(0.5f, 0.5f, 0.027f), 0.5f);
    // Below the target the rate is added even when that overshoots a little.
    EXPECT_NEAR(igi::HeliApproachSpeed(0.0f, 0.003f, 0.027f), 0.003f, 1e-7);
}

TEST(HeliRules, ApproachSpeedSnapsOnlyAgainstAZeroThrottle) {
    // heli.c:431: the sub-milli collapse needs BOTH `s < 0.001` and a target of
    // exactly 0.
    EXPECT_FLOAT_EQ(igi::HeliApproachSpeed(0.0035f, 0.0f, 0.003f), 0.0f);
    // Same speed, but the throttle is a non-zero value below the milli
    // threshold: no snap, the 0.0005 survives.
    EXPECT_NEAR(igi::HeliApproachSpeed(0.0035f, 0.0005f, 0.003f), 0.0005f, 1e-9);
    // And a speed that stays above the threshold is untouched.
    EXPECT_NEAR(igi::HeliApproachSpeed(0.0035f, 0.5f, 0.003f), 0.0065f, 1e-7);
}

TEST(HeliRules, SmoothTurnIsAFirstOrderFilter) {
    igi::HeliVehicleConfig cfg = BellConfig();
    const float steer[3] = {1.0f, -1.0f, 0.5f};
    float turn[3] = {0.0f, 0.0f, 0.0f};
    float out[3];

    igi::HeliSmoothTurn(steer, turn, cfg, out);
    // t = (1 - smooth) * steer + smooth * turn, with smooth = 0.4.
    EXPECT_NEAR(out[0], 0.6f, 1e-6);
    EXPECT_NEAR(out[1], -0.6f, 1e-6);
    EXPECT_NEAR(out[2], 0.3f, 1e-6);

    // The second tick feeds the first result back in.
    turn[0] = out[0];
    turn[1] = out[1];
    turn[2] = out[2];
    igi::HeliSmoothTurn(steer, turn, cfg, out);
    EXPECT_NEAR(out[0], 0.6f + 0.4f * 0.6f, 1e-6);
}

TEST(HeliRules, TurnForcesAreTheThreeBodyForces) {
    const igi::HeliVehicleConfig cfg = BellConfig();
    const float turn[3] = {0.1f, 0.2f, 0.3f};
    igi::HeliForce forces[3];
    float stale = -1.0f;

    const int count =
        igi::HeliTurnForces(turn, cfg, 1.0f, 3000.0f, 0, 5.0f, forces, &stale);
    ASSERT_EQ(count, 3);

    // f1 = {0, t0*rate*mass*kYawForce, 0} about {0, 0, 819.2}
    // (heli.c:445, :449).
    EXPECT_FLOAT_EQ(forces[0].force[0], 0.0f);
    EXPECT_NEAR(forces[0].force[1], 0.1f * 50.0f * 3000.0f * -0.8f, 1.0f);
    EXPECT_FLOAT_EQ(forces[0].force[2], 0.0f);
    EXPECT_FLOAT_EQ(forces[0].point[0], 0.0f);
    EXPECT_FLOAT_EQ(forces[0].point[1], 0.0f);
    EXPECT_FLOAT_EQ(forces[0].point[2], 819.2f);

    // f2 = {t1*rate*mass, 0, 0} about {0, 0, 409.6} (heli.c:446, :450).
    EXPECT_NEAR(forces[1].force[0], 0.2f * 50.0f * 3000.0f, 1.0f);
    EXPECT_FLOAT_EQ(forces[1].point[2], 409.6f);

    // f3 = {s * (t2*rate) * mass * kRollForce, 0, 0} about {0, 12288, 0}
    // (heli.c:447, :451).
    EXPECT_NEAR(forces[2].force[0], 1.0f * (0.3f * 50.0f) * 3000.0f * 0.08f, 1.0f);
    EXPECT_FLOAT_EQ(forces[2].point[1], 12288.0f);

    // The reused stack slot holds f2[0] (heli.c:452).
    EXPECT_FLOAT_EQ(stale, forces[1].force[0]);
}

TEST(HeliRules, TurnForceGateNeedsATurnAndAirborneOrFast) {
    const igi::HeliVehicleConfig cfg = BellConfig();
    const float turn[3] = {0.1f, 0.0f, 0.0f};
    const float zero[3] = {0.0f, 0.0f, 0.0f};
    igi::HeliForce forces[3];
    float stale = 0.0f;

    // No turn at all -> no forces.
    EXPECT_EQ(igi::HeliTurnForces(zero, cfg, 1.0f, 3000.0f, 0, 5.0f, forces, &stale), 0);
    // Airborne: the body speed is irrelevant.
    EXPECT_EQ(igi::HeliTurnForces(turn, cfg, 1.0f, 3000.0f, 0, 0.0f, forces, &stale), 3);
    // On the ground and slower than 2 -> gated off.
    EXPECT_EQ(igi::HeliTurnForces(turn, cfg, 1.0f, 3000.0f, 1, 1.9f, forces, &stale), 0);
    // On the ground at exactly 2 -> still gated off (the test is `> 2`).
    EXPECT_EQ(igi::HeliTurnForces(turn, cfg, 1.0f, 3000.0f, 1, 2.0f, forces, &stale), 0);
    EXPECT_EQ(igi::HeliTurnForces(turn, cfg, 1.0f, 3000.0f, 1, 2.1f, forces, &stale), 3);
}

TEST(HeliRules, NosePitchUsesTheHeadingWhenItIsNotDegenerate) {
    // A pure yaw matrix: m[0] = cos, m[3] = sin, m[8] = 1.
    float m[9];
    igi::HeliSetEuler(m, 0.0f, 0.0f, 0.3f);
    bool used_stale = true;
    const long double pitch = igi::HeliNosePitch(m, 0.123f, &used_stale);
    EXPECT_FALSE(used_stale);

    // pitch = 0*m[8] + (m[3]*inv)*m[5] + (m[0]*inv)*m[2], then * kPitchDown
    // because m[8] > 0 (heli.c:468-472).
    const float cx = m[0];
    const long double cy = m[3];
    const long double expected =
        (static_cast<long double>(0.0f) * m[8] + cy * m[5] + cx * m[2]) * -0.7L;
    EXPECT_NEAR(static_cast<double>(pitch), static_cast<double>(expected), 1e-9);
}

TEST(HeliRules, NosePitchSignFollowsM8) {
    float m[9];
    igi::HeliSetEuler(m, 0.0f, 3.14159265358979323846f, 0.0f);  // Ry(pi): m[8] = -1
    EXPECT_LT(m[8], 0.0f);
    bool used_stale = false;
    const long double pitch = igi::HeliNosePitch(m, 0.0f, &used_stale);
    EXPECT_FALSE(used_stale);
    // With m[8] <= 0 the factor is +0.7 (heli.c:472), so a non-zero numerator
    // changes sign relative to the m[8] > 0 case.
    const float cx = m[0];
    const long double cy = m[3];
    const long double expected =
        (static_cast<long double>(0.0f) * m[8] + cy * m[5] + cx * m[2]) * 0.7L;
    EXPECT_NEAR(static_cast<double>(pitch), static_cast<double>(expected), 1e-9);
}

TEST(HeliRules, NosePitchReadsTheReusedStackSlotWhenDegenerate) {
    // heli.c:473-476: m[0] == 0 and m[3] == 0 takes the `else` branch and reads
    // the slot that held f2[0].
    const float m[9] = {0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, -1.0f, 0.0f, 0.0f};
    bool used_stale = false;
    EXPECT_FLOAT_EQ(static_cast<float>(igi::HeliNosePitch(m, 0.25f, &used_stale)), 0.25f);
    EXPECT_TRUE(used_stale);

    // With no turn forces this tick the slot still holds 0, i.e. no pitch.
    used_stale = false;
    EXPECT_FLOAT_EQ(static_cast<float>(igi::HeliNosePitch(m, 0.0f, &used_stale)), 0.0f);
    EXPECT_TRUE(used_stale);
}

TEST(HeliRules, ComposeNoseMatrixIsThePostMultiplyByRotX) {
    const igi::HeliVehicleConfig cfg = BellConfig();
    float m[9];
    igi::HeliSetEuler(m, 0.2f, -0.1f, 0.4f);
    const long double pitch = -0.19762485L;

    float got[9];
    int got_count = -1;
    igi::HeliComposeNoseMatrix(m, 3, cfg, pitch, got, &got_count);

    const float theta = static_cast<float>(pitch * cfg.axis[2].rate * igi::kHeliPitchScale);
    const double c = std::cos(theta);
    const double s = std::sin(theta);
    const double r[9] = {1, 0, 0, 0, c, -s, 0, s, c};
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            double expected = 0.0;
            for (int k = 0; k < 3; ++k) expected += static_cast<double>(m[i * 3 + k]) * r[k * 3 + j];
            EXPECT_NEAR(got[i * 3 + j], expected, 1e-6) << "index " << i * 3 + j;
        }
    }
    EXPECT_EQ(got_count, 4) << "nm.count = matrix.count + r.count + 1 (heli.c:483)";
}

TEST(HeliRules, ComposeNoseMatrixIsIdentityForAZeroPitch) {
    const igi::HeliVehicleConfig cfg = BellConfig();
    float m[9];
    igi::HeliSetEuler(m, 0.3f, 0.2f, -0.1f);
    float got[9];
    int got_count = 0;
    igi::HeliComposeNoseMatrix(m, 0, cfg, 0.0L, got, &got_count);
    for (int i = 0; i < 9; ++i) EXPECT_NEAR(got[i], m[i], 1e-7) << "index " << i;
    EXPECT_EQ(got_count, 1);
}

TEST(HeliRules, RotorSoundParamsFollowTheSpeed) {
    float sp[3] = {0.0f, 0.0f, 0.0f};
    igi::HeliRotorSoundParams(0.5f, sp);
    EXPECT_FLOAT_EQ(sp[1], 0.5f);
    EXPECT_FLOAT_EQ(sp[2], 1.0f);

    igi::HeliRotorSoundParams(0.01f, sp);
    EXPECT_FLOAT_EQ(sp[2], 0.0f) << "`speed > 0.01` is false at exactly 0.01";

    igi::HeliRotorSoundParams(0.0100001f, sp);
    EXPECT_FLOAT_EQ(sp[2], 1.0f);

    igi::HeliRotorSoundParams(0.0f, sp);
    EXPECT_FLOAT_EQ(sp[1], 0.0f);
    EXPECT_FLOAT_EQ(sp[2], 0.0f);
}

TEST(HeliRules, IsRestingIsTheFiveWayAnd) {
    const float w[3] = {0.0f, 0.0f, 0.0f};
    EXPECT_TRUE(igi::HeliIsResting(1, false, 0, 0.0f, 0.1f, w, 0.1f));
    EXPECT_FALSE(igi::HeliIsResting(0, false, 0, 0.0f, 0.1f, w, 0.1f));
    EXPECT_FALSE(igi::HeliIsResting(1, true, 0, 0.0f, 0.1f, w, 0.1f));
    EXPECT_FALSE(igi::HeliIsResting(1, false, 1, 0.0f, 0.1f, w, 0.1f));
    EXPECT_FALSE(igi::HeliIsResting(1, false, 0, 0.1f, 0.1f, w, 0.1f));
    const float spin[3] = {0.5f, 0.0f, 0.0f};
    EXPECT_FALSE(igi::HeliIsResting(1, false, 0, 0.0f, 0.1f, spin, 0.1f));
    EXPECT_TRUE(igi::HeliIsResting(1, false, 0, 0.0f, 0.1f, spin, 0.3f));
}

TEST(HeliRules, MoveStepReproducesTheOrderedTick) {
    igi::HeliVehicleConfig cfg = BellConfig();
    igi::HeliMoveState state{};
    state.speed = 0.0f;
    state.target_speed = 1.0f;
    state.mass = 3000.0f;
    state.body_speed = 0.0f;
    state.touching = 0;
    Identity(state.matrix);
    state.matrix_count = 0;
    state.rest_speed = 68.0f;
    state.rest_spin = 0.001f;

    igi::HeliMoveResult out;
    igi::HeliMoveStep(&state, cfg, &out);

    // Below 0.8 the ramp uses accel_slow (heli.c:416).
    EXPECT_NEAR(out.speed, cfg.accel_slow, 1e-9);
    EXPECT_FLOAT_EQ(state.speed, out.speed);
    // No steer -> no turn forces.
    EXPECT_EQ(out.force_count, 0);
    EXPECT_FALSE(out.turn_forces_added);
    // body.speed is 0, so the nose-pitch block is skipped (heli.c:457).
    EXPECT_FALSE(out.nose_pitch_applied);
    // touching == 0 makes PhysicsObj_IsResting false, so the gate is open.
    EXPECT_TRUE(out.resting_gate_open);
    // `!(0.003 > 0.01)` -> fast = 0 (heli.c:512).
    EXPECT_FLOAT_EQ(out.sound_params[1], out.speed);
    EXPECT_FLOAT_EQ(out.sound_params[2], 0.0f);
}

TEST(HeliRules, MoveStepAppliesTheNosePitchWhenAirborneAndFast) {
    igi::HeliVehicleConfig cfg = BellConfig();
    igi::HeliMoveState state{};
    state.speed = 1.0f;
    state.target_speed = 1.0f;
    state.mass = 3000.0f;
    state.body_speed = 5.0f;
    state.touching = 0;
    Identity(state.matrix);
    state.matrix_count = 2;
    state.steer[0] = 0.1f;
    state.rest_speed = 68.0f;
    state.rest_spin = 0.001f;
    state.force_count = 0;

    igi::HeliMoveResult out;
    igi::HeliMoveStep(&state, cfg, &out);

    EXPECT_EQ(out.force_count, 3);
    EXPECT_TRUE(out.turn_forces_added);
    EXPECT_TRUE(out.nose_pitch_applied);
    EXPECT_FALSE(out.stale_used) << "the identity heading basis is not degenerate";
    // The identity matrix has m[2] == m[5] == 0, so the pitch is 0 and the
    // composed matrix is unchanged apart from its count.
    for (int i = 0; i < 9; ++i) EXPECT_NEAR(out.matrix[i], (i % 4 == 0) ? 1.0f : 0.0f, 1e-6);
    EXPECT_EQ(out.matrix_count, 3);
    // touching == 0 fails PhysicsObj_IsResting on its first test, so the gate
    // stays open regardless of the force count.
    EXPECT_TRUE(out.resting_gate_open);
}

TEST(HeliRules, MoveStepClosesTheRestGateWhenParkedAndIdle) {
    igi::HeliVehicleConfig cfg = BellConfig();
    igi::HeliMoveState state{};
    state.speed = 0.0f;
    state.target_speed = 0.0f;
    state.mass = 3000.0f;
    state.body_speed = 0.0f;
    state.touching = 1;
    state.rest_speed = 68.0f;   // the BELL def's unk2CC
    state.rest_spin = 0.001f;   // and its unk2D0
    Identity(state.matrix);

    igi::HeliMoveResult out;
    igi::HeliMoveStep(&state, cfg, &out);

    EXPECT_EQ(out.force_count, 0);
    EXPECT_FALSE(out.nose_pitch_applied) << "body.speed must exceed 2 (heli.c:457)";
    EXPECT_FALSE(out.resting_gate_open) << "touching, idle, unforced -> resting";
}

TEST(HeliRules, MoveStepKeepsTheStaleSlotAcrossTicks) {
    igi::HeliVehicleConfig cfg = BellConfig();
    igi::HeliMoveState state{};
    state.speed = 1.0f;
    state.target_speed = 1.0f;
    state.mass = 3000.0f;
    state.body_speed = 5.0f;
    state.touching = 0;
    // A degenerate heading basis: m[0] == m[3] == 0.
    const float degenerate[9] = {0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, -1.0f, 0.0f, 0.0f};
    for (int i = 0; i < 9; ++i) state.matrix[i] = degenerate[i];
    state.steer[1] = 0.5f;  // produces f2[0] = turn[1]*rate*mass
    state.rest_speed = 68.0f;

    igi::HeliMoveResult out;
    igi::HeliMoveStep(&state, cfg, &out);
    ASSERT_EQ(out.force_count, 3);
    EXPECT_TRUE(out.stale_used);
    // turn[1] = (1 - 0.4) * 0.5 = 0.3, so f2[0] = 0.3 * 50 * 3000 = 45000.
    EXPECT_NEAR(state.stale, 0.3f * 50.0f * 3000.0f, 1.0f);
    EXPECT_FLOAT_EQ(out.forces[1].force[0], state.stale);
}

// ===========================================================================
// HeliWheel
// ===========================================================================

TEST(HeliRules, WheelParamsAreStoredSwapped) {
    // vehicle/parts.c:284-292: params[1] = arg0 * 4096, params[0] = arg1 * 4096.
    const igi::HeliWheelParams p = igi::ParseHeliWheelParams(Tokens({"2.0", "0.5"}));
    EXPECT_FLOAT_EQ(p.top, 8192.0f);
    EXPECT_FLOAT_EQ(p.len, 2048.0f);
}

TEST(HeliRules, WheelProbeStartsTwoMetresAboveAndRunsTwoMetresDown) {
    igi::HeliWheelParams p{};
    p.len = 2048.0f;
    p.top = 8192.0f;
    const double pos[3] = {1.0, 2.0, 100.0};
    const float off[3] = {4.0f, 5.0f, 6.0f};

    const igi::HeliWheelProbe probe = igi::HeliBuildWheelProbe(p, pos, off);
    EXPECT_DOUBLE_EQ(probe.start_z, 100.0 + 8192.0);
    EXPECT_DOUBLE_EQ(probe.depth, -(2048.0 + 8192.0));
    EXPECT_DOUBLE_EQ(probe.end_z, probe.depth + probe.start_z);
    EXPECT_FLOAT_EQ(probe.offset[0], 4.0f);
    EXPECT_FLOAT_EQ(probe.offset[2], 6.0f);
}

TEST(HeliRules, WheelLiftLandsWhenTheHitIsInsideTheTopProbe) {
    igi::HeliWheelProbe probe{};
    probe.len = 2048.0f;
    probe.top = 8192.0f;
    probe.depth = -(2048.0 + 8192.0);
    probe.offset[0] = 1.0f;
    probe.offset[1] = 2.0f;
    probe.offset[2] = 3.0f;

    const float mass = 3000.0f;
    const igi::HeliWheelLiftResult r = igi::HeliWheelLift(probe, true, 8000.0, mass);
    ASSERT_TRUE(r.applied);
    EXPECT_TRUE(r.landed);
    // u = top + 8192 = 16384; f = ((u - dist)/u + 1) * mass * kLift (heli.c:591).
    const float u = 8192.0f + 8192.0f;
    const float expected = (static_cast<float>(u - 8000.0) / u + 1.0f) * mass * igi::kHeliLift;
    EXPECT_NEAR(r.force[2], expected, 1e-3);
    EXPECT_FLOAT_EQ(r.force[0], 0.0f);
    EXPECT_FLOAT_EQ(r.force[1], 0.0f);
    EXPECT_FLOAT_EQ(r.force[3], 1.0f);
    EXPECT_FLOAT_EQ(r.force[4], 2.0f);
    EXPECT_FLOAT_EQ(r.force[5], 3.0f);
    EXPECT_DOUBLE_EQ(r.max_lift, 16384.0 - 8000.0);
}

TEST(HeliRules, WheelLiftSkipsAHitBeyondTheProbeAndAMissingHit) {
    igi::HeliWheelProbe probe{};
    probe.len = 2048.0f;
    probe.top = 8192.0f;
    probe.depth = -(2048.0 + 8192.0);

    EXPECT_FALSE(igi::HeliWheelLift(probe, false, 8000.0, 3000.0f).applied);
    // |depth| = 10240, so a hit at 20000 is past the probe (heli.c:578-579).
    EXPECT_FALSE(igi::HeliWheelLift(probe, true, 20000.0, 3000.0f).applied);
}

TEST(HeliRules, WheelLiftCompressionBranchNeedsTopNotGreaterThanLen) {
    // heli.c:587 tests `top + 8192 > dist` first, while the guard above it only
    // admits `dist <= len + 8192`. The compression branch is therefore
    // reachable only when `top <= len`.
    igi::HeliWheelProbe probe{};
    probe.len = 4096.0f;
    probe.top = 0.0f;
    probe.depth = -(4096.0 + 8192.0);
    probe.offset[0] = 1.0f;

    const float mass = 3000.0f;
    const igi::HeliWheelLiftResult r = igi::HeliWheelLift(probe, true, 9000.0, mass);
    ASSERT_TRUE(r.applied);
    EXPECT_FALSE(r.landed);
    // pen = len - (dist - 8192) = 4096 - 808 = 3288; force = pen/len*mass*kLift.
    const double pen = 4096.0 - (9000.0 - 8192.0);
    const float expected = static_cast<float>(pen / 4096.0 * mass * igi::kHeliLift);
    EXPECT_NEAR(r.force[2], expected, 1e-2);

    // The BELL wheel has top = 8192 > len = 2048, so it can never reach the
    // compression branch.
    igi::HeliWheelProbe bell{};
    bell.len = 2048.0f;
    bell.top = 8192.0f;
    bell.depth = -(2048.0 + 8192.0);
    for (double dist = 0.0; dist <= 10240.0; dist += 128.0) {
        const igi::HeliWheelLiftResult hit = igi::HeliWheelLift(bell, true, dist, mass);
        if (hit.applied) EXPECT_TRUE(hit.landed) << "dist " << dist;
    }
}

TEST(HeliRules, WheelLiftLandingIsDecidedByTheLastProcessedHit) {
    // heli.c:534 declares `landed = 0` ONCE, outside the loop, and then both
    // heli.c:590 and heli.c:595 assign it inside the per-hit loop, while
    // `max_lift` (heli.c:592-593) only ever grows. So when an earlier hit lands
    // and a later one compresses, the loop finishes with landed == 0 and the
    // correction at heli.c:610-617 is skipped even though max_lift holds a
    // positive value from the earlier hit. The aggregation is modelled here as
    // the engine writes it, because HeliWheelLift is deliberately one iteration.
    igi::HeliWheelProbe probe{};
    probe.len = 4096.0f;   // compression is reachable only because top <= len
    probe.top = 0.0f;
    probe.depth = -(4096.0 + 8192.0);
    const float mass = 3000.0f;

    // `top + 8192 = 8192`, so a hit nearer than that lands and one beyond it
    // takes the compression branch.
    const double near_dist = 8000.0;
    const double far_dist = 9000.0;

    int landed = 0;
    double max_lift = 0.0;
    const double order[2] = {near_dist, far_dist};
    for (const double dist : order) {
        const igi::HeliWheelLiftResult r = igi::HeliWheelLift(probe, true, dist, mass);
        ASSERT_TRUE(r.applied);
        landed = r.landed ? 1 : 0;
        if (r.max_lift > max_lift) max_lift = r.max_lift;
    }
    EXPECT_EQ(landed, 0) << "the compressing hit is last, so heli.c:610 is skipped";
    EXPECT_DOUBLE_EQ(max_lift, 8192.0 - near_dist);

    // Processed in the other order the landing hit is last and the correction
    // does run -- the same set of hits, a different outcome.
    int landed_reversed = 0;
    for (int i = 1; i >= 0; --i) {
        const igi::HeliWheelLiftResult r = igi::HeliWheelLift(probe, true, order[i], mass);
        ASSERT_TRUE(r.applied);
        landed_reversed = r.landed ? 1 : 0;
    }
    EXPECT_EQ(landed_reversed, 1);
}

TEST(HeliRules, WheelLiftDividesByTheAuthoredLengthUnguarded) {
    // heli.c:596 has no zero check on `len`.
    igi::HeliWheelProbe probe{};
    probe.len = 0.0f;
    probe.top = 0.0f;
    probe.depth = -8192.0;

    const igi::HeliWheelLiftResult r = igi::HeliWheelLift(probe, true, 8192.0, 3000.0f);
    ASSERT_TRUE(r.applied);
    EXPECT_FALSE(r.landed);
    EXPECT_TRUE(std::isnan(r.force[2]));
}

TEST(HeliRules, WheelDampUsesTwoRatesForVelocity) {
    float vel[3] = {1.0f, 2.0f, 3.0f};
    float ang[3] = {1.0f, 1.0f, 1.0f};
    igi::HeliWheelDamp(vel, ang);
    EXPECT_NEAR(vel[0], 0.95f, 1e-6);
    EXPECT_NEAR(vel[1], 1.9f, 1e-6);
    EXPECT_NEAR(vel[2], 2.91f, 1e-6);
    for (int i = 0; i < 3; ++i) EXPECT_NEAR(ang[i], 0.98f, 1e-6) << "ang " << i;
}

// ===========================================================================
// Rotor
// ===========================================================================

TEST(HeliRules, PhysicsDefArgIndexIsTwelvePlusSixPerJoint) {
    // physobjtype.c:64-122: arg = 10 + 6 * joints, then the task parser is handed
    // arg + 2.
    EXPECT_EQ(igi::RotorDefArgIndex(0), 12);
    EXPECT_EQ(igi::RotorDefArgIndex(1), 18);
    EXPECT_EQ(igi::RotorDefArgIndex(2), 24);
}

TEST(HeliRules, RotorDefParseConvertsDegreesToRadians) {
    igi::RotorDef def;
    ASSERT_TRUE(igi::ParseRotorDef(Tokens({"0", "1", "10", "5", "0.12", "712_02_1"}), 0, &def));
    EXPECT_EQ(def.lift, 0);
    EXPECT_EQ(def.tilt, 1);
    EXPECT_EQ(def.dir, 10);
    EXPECT_FLOAT_EQ(def.angle_degrees, 5.0f);
    // The engine computes in double and narrows once (rotor.c:310-311), so the
    // result is the float nearest the double product; 1e-7 is a float's own
    // resolution at this magnitude.
    EXPECT_NEAR(def.angle, 5.0 * igi::kRotorDegToRad, 1e-7);
    EXPECT_FLOAT_EQ(def.speed, 0.12f);
    EXPECT_EQ(def.name, "712_02_1");

    // Too short -> refused rather than reading past the end.
    EXPECT_FALSE(igi::ParseRotorDef(Tokens({"0", "1", "10", "5", "0.12"}), 0, &def));
}

TEST(HeliRules, PhysicsDefHeaderLandsOnTheRotorArgs) {
    // The BELL HELI.QVM rotor def, verbatim.
    const std::vector<std::string> args = Tokens({
        "TASKTYPE_ROTOR", "1", "200", "0.5", "0.4", "0.3", "<ROTOR>", "<ROTORDESC>",
        "711_01_1", "2", "0", "0", "6", "0.5", "1", "1", "1", "0", "6", "0.5", "1", "1",
        "0.7", "0.001", "1", "0", "1", "0", "0.1", "711_01_1",
    });
    igi::PhysicsDefHeader header;
    ASSERT_TRUE(igi::ParsePhysicsDefHeader(args, &header));
    EXPECT_EQ(header.task_type, "TASKTYPE_ROTOR");
    EXPECT_EQ(header.type_id, 1);
    EXPECT_FLOAT_EQ(header.mass, 200.0f);
    EXPECT_FLOAT_EQ(header.size[0], 0.5f);
    EXPECT_FLOAT_EQ(header.size[2], 0.3f);
    EXPECT_EQ(header.model, "711_01_1");
    EXPECT_EQ(header.joint_count, 2);
    EXPECT_EQ(header.first_arg, 24);

    igi::RotorDef def;
    ASSERT_TRUE(igi::ParseRotorDef(args, header.first_arg, &def));
    EXPECT_EQ(def.lift, 1);
    EXPECT_EQ(def.tilt, 0);
    EXPECT_EQ(def.dir, 1);
    EXPECT_FLOAT_EQ(def.angle, 0.0f);
    EXPECT_FLOAT_EQ(def.speed, 0.1f);
    EXPECT_EQ(def.name, "711_01_1");
}

TEST(HeliRules, RotorSpinAccumulatesTheOwnerSpeed) {
    igi::RotorDef def{};
    def.dir = 17;
    def.speed = 0.07f;

    float value = 0.0f;
    value = igi::RotorRunValue(value, def, 1.0f);
    EXPECT_NEAR(value, 17.0f * 0.07f, 1e-6);
    value = igi::RotorRunValue(value, def, 2.0f);
    EXPECT_NEAR(value, 17.0f * 0.07f * 3.0f, 1e-6);

    // A stationary owner spins nothing, however the rotor is authored.
    EXPECT_FLOAT_EQ(igi::RotorRunValue(0.0f, def, 0.0f), 0.0f);

    EXPECT_NEAR(igi::RotorSpinAngle(1.0f), igi::kRotorSpin, 1e-6);
    EXPECT_NEAR(igi::RotorBladeStep(def, 2.0f), 0.07f * 2.0f, 1e-7);
}

TEST(HeliRules, RotorBladeTiltClampsToTheAuthoredAngle) {
    igi::RotorDef def{};
    def.angle = 0.5f;
    def.lift = 0;
    def.tilt = 1;

    float m[9];
    Identity(m);
    const float vel[3] = {0.0f, 0.0f, 10.0f};
    const float scale = 200.0f;

    // car_speed 1 -> k = 0.
    const float t1 = igi::RotorBladeTilt(def, 1.0f, m, vel, scale);
    const float expected = -10.0f * igi::kRotorYawTilt * 0.5f / (scale * igi::kRotorMassTilt);
    EXPECT_NEAR(t1, expected, 1e-7);

    // car_speed 1.5 -> k = -0.5, clamped to -0.1.
    const float t2 = igi::RotorBladeTilt(def, 1.5f, m, vel, scale);
    EXPECT_NEAR(t2, 0.1f * 0.5f + expected, 1e-7);

    // car_speed 0 -> k = 1, and the result is clamped to -maxTilt.
    const float t3 = igi::RotorBladeTilt(def, 0.0f, m, vel, scale);
    EXPECT_FLOAT_EQ(t3, -0.5f);

    // A stationary owner has no turn rate, so the tilt is the throttle term.
    const float vel0[3] = {0.0f, 0.0f, 0.0f};
    EXPECT_NEAR(igi::RotorBladeTilt(def, 1.0f, m, vel0, scale), 0.0f, 1e-9);
}

TEST(HeliRules, RotorLiftForceNeedsLiftAndUsesTheUpColumn) {
    igi::RotorDef def{};
    def.lift = 1;
    def.tilt = 0;

    float m[9];
    Identity(m);
    igi::HeliForce f{};

    ASSERT_TRUE(igi::RotorLiftForce(def, 3000.0f, 1.0f, m, &f));
    const float lift = 3000.0f * 1.0f * igi::kRotorLift;
    EXPECT_FLOAT_EQ(f.force[2], lift);
    EXPECT_FLOAT_EQ(f.force[0], 0.0f);
    // up = (0,1,0), fwd = (0,0,1) -> a right angle -> cos ~ 0.
    EXPECT_NEAR(f.force[1], 0.0f, 1e-3);
    EXPECT_FLOAT_EQ(f.point[0], 0.0f);
    EXPECT_FLOAT_EQ(f.point[2], 0.0f);

    // A rotor with lift == 0 adds nothing at all.
    def.lift = 0;
    EXPECT_FALSE(igi::RotorLiftForce(def, 3000.0f, 1.0f, m, &f));
    // And neither does a zero lift product.
    def.lift = 1;
    EXPECT_FALSE(igi::RotorLiftForce(def, 3000.0f, 0.0f, m, &f));
}

TEST(HeliRules, RotorLiftForceTiltsWithTheOwnersAttitude) {
    igi::RotorDef def{};
    def.lift = 1;
    def.tilt = 0;

    float m[9];
    igi::HeliSetRotX(m, 0.5f);
    igi::HeliForce f{};
    ASSERT_TRUE(igi::RotorLiftForce(def, 1000.0f, 1.0f, m, &f));

    const float lift = 1000.0f * igi::kRotorLift;
    // up = (0, cos, sin) -> up[2] zeroed -> (0, cos, 0); fwd = (0, -sin, cos).
    // The angle is acos(-sin) and the force is (0, cos(angle) * lift, lift).
    const double angle = std::acos(-std::sin(0.5));
    EXPECT_NEAR(f.force[1], std::cos(angle) * lift, 1e-2);
    EXPECT_FLOAT_EQ(f.force[2], lift);
}

TEST(HeliRules, RotorTiltForceSignsAndGate) {
    igi::RotorDef def{};
    def.lift = 0;
    def.tilt = 1;
    igi::HeliForce f{};

    ASSERT_TRUE(igi::RotorTiltForce(def, 3000.0f, 1.0f, 0, &f));
    EXPECT_NEAR(f.force[0], -(3000.0f * 1.0f * igi::kRotorTilt), 1e-3);
    EXPECT_FLOAT_EQ(f.point[1], igi::kRotorTiltY);

    // lift-only produces the opposite sign (rotor.c:254-257).
    def.tilt = 0;
    def.lift = 1;
    ASSERT_TRUE(igi::RotorTiltForce(def, 3000.0f, 1.0f, 0, &f));
    EXPECT_NEAR(f.force[0], 3000.0f * 1.0f * igi::kRotorTilt, 1e-3);

    // Both set -> the `if (tilt)` branch wins.
    def.tilt = 1;
    ASSERT_TRUE(igi::RotorTiltForce(def, 3000.0f, 1.0f, 0, &f));
    EXPECT_LT(f.force[0], 0.0f);

    // Gate: a stationary owner, a rotor with neither flag, and a touching
    // helicopter all add nothing.
    EXPECT_FALSE(igi::RotorTiltForce(def, 3000.0f, 0.0f, 0, &f));
    EXPECT_FALSE(igi::RotorTiltForce(def, 3000.0f, 1.0f, 1, &f));
    igi::RotorDef plain{};
    EXPECT_FALSE(igi::RotorTiltForce(plain, 3000.0f, 1.0f, 0, &f));
}

TEST(HeliRules, RotorOffsetPosIsTheOwnerRowTimesTheOffset) {
    float m[9];
    igi::HeliSetEuler(m, 0.0f, 0.0f, 1.5707963267948966f);  // Rz(pi/2)
    const float offset[3] = {10.0f, 0.0f, 0.0f};
    const double pos[3] = {100.0, 200.0, 300.0};
    double out[3];

    igi::RotorOffsetPos(offset, m, pos, out);
    EXPECT_NEAR(out[0], 100.0, 1e-4);
    EXPECT_NEAR(out[1], 210.0, 1e-4);
    EXPECT_NEAR(out[2], 300.0, 1e-4);
}

TEST(HeliRules, RotorLocalMatrixUsesTheMoveSummationOrder) {
    const float owner[9] = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 9.0f};
    float local[9];
    Identity(local);
    float got[9];
    igi::RotorLocalMatrix(got, owner, local);
    float expected[9];
    igi::HeliMatrixMul(expected, owner, local, 1, 0, 0);
    for (int i = 0; i < 9; ++i) EXPECT_FLOAT_EQ(got[i], expected[i]) << "index " << i;
}

TEST(HeliRules, RotorBladeMatrixIsBaseTimesRotXThenRotZ) {
    float base[9];
    igi::HeliSetEuler(base, 0.1f, 0.2f, 0.3f);
    const float angle = 0.7f;
    const float tilt = -0.05f;

    float got[9];
    igi::RotorBladeMatrix(got, base, angle, tilt);

    float rx[9];
    float rz[9];
    float tmp[9];
    float expected[9];
    igi::HeliSetRotX(rx, angle);
    igi::HeliSetRotZ(rz, tilt);
    igi::HeliMatrixMul(tmp, base, rx, 0, 0, 0);
    igi::HeliMatrixMul(expected, tmp, rz, 0, 0, 0);
    for (int i = 0; i < 9; ++i) EXPECT_FLOAT_EQ(got[i], expected[i]) << "index " << i;
}

TEST(HeliRules, RotorBladeMatrixWithNoAngleOrTiltIsTheBase) {
    float base[9];
    igi::HeliSetEuler(base, 0.1f, 0.2f, 0.3f);
    float got[9];
    igi::RotorBladeMatrix(got, base, 0.0f, 0.0f);
    for (int i = 0; i < 9; ++i) EXPECT_NEAR(got[i], base[i], 1e-6) << "index " << i;
}

// ===========================================================================
// The corpus: the shipped QVMs
// ===========================================================================

TEST(HeliRulesCorpus, DeclaredParametersMatchTheParameterTable) {
    const fs::path path = LevelDir(2) / "OBJECTS.QVM";
    if (!fs::is_regular_file(path)) {
        GTEST_SKIP() << "retail IGI not found at " << GameRoot().string();
    }
    const QVMFile qvm = QVM_Parse(path.string());
    const std::vector<std::string> declared = DeclaredHeliParameters(qvm);
    ASSERT_EQ(declared.size(), static_cast<std::size_t>(igi::kHeliParamCount) * 2u);

    const igi::HeliParamDesc* table = igi::HeliParamTable();
    for (int i = 0; i < igi::kHeliParamCount; ++i) {
        EXPECT_EQ(declared[static_cast<std::size_t>(i) * 2], table[i].authored_name) << "slot " << i;
        EXPECT_EQ(declared[static_cast<std::size_t>(i) * 2 + 1], table[i].type_name) << "slot " << i;
    }

    const std::string text = ReadFile(LevelDir(2) / "objects.qsc");
    const std::vector<std::string> from_text = ParseDeclaredHeliParametersFromText(text);
    ASSERT_EQ(from_text.size(), static_cast<std::size_t>(igi::kHeliParamCount) * 2u);
    for (int i = 0; i < igi::kHeliParamCount; ++i) {
        EXPECT_EQ(from_text[static_cast<std::size_t>(i) * 2], table[i].authored_name) << "slot " << i;
        EXPECT_EQ(from_text[static_cast<std::size_t>(i) * 2 + 1], table[i].type_name) << "slot " << i;
    }
}

TEST(HeliRulesCorpus, QvmAgreesWithTheDecompiledObjectsQsc) {
    // LEVEL2, LEVEL4 and level1 ship a decompiled objects.qsc, i.e. a second,
    // human-readable rendering of the same authored data. Comparing the two
    // proves the QVM token reader is aligned with the argument list rather than
    // off by a token.
    //
    // The comparison is over the 22 authored tokens only. The decompiler nests a
    // task's CHILDREN inside its parent's argument list (level1 objects.qsc:913
    // gives the first Heli four trailing Task_New arguments), whereas the QVM
    // stores children as separate tasks, so the text form may legitimately carry
    // extra trailing arguments.
    const std::size_t kAuthored = static_cast<std::size_t>(2 + 1 + igi::kHeliTokenCount);
    int compared = 0;
    for (const char* name : {"level1", "LEVEL2", "LEVEL4"}) {
        const fs::path dir = LevelDirNamed(name);
        if (!fs::is_regular_file(dir / "OBJECTS.QVM") || !fs::is_regular_file(dir / "objects.qsc")) {
            continue;
        }

        const QVMFile qvm = QVM_Parse((dir / "OBJECTS.QVM").string());
        const std::vector<std::vector<std::string>> from_qvm = ExtractHeliCalls(qvm);
        const std::string text = ReadFile(dir / "objects.qsc");
        const std::vector<std::vector<std::string>> from_text = ParseHeliCallsFromText(text);

        ASSERT_EQ(from_qvm.size(), from_text.size()) << name;
        for (std::size_t i = 0; i < from_qvm.size(); ++i) {
            ASSERT_EQ(from_qvm[i].size(), kAuthored) << name << " #" << i;
            ASSERT_GE(from_text[i].size(), kAuthored) << name << " #" << i;
            // The first three tokens are the task id, the type and the name.
            EXPECT_EQ(from_qvm[i][1], from_text[i][1]) << name << " #" << i;
            EXPECT_EQ(from_qvm[i][2], from_text[i][2]) << name << " #" << i;

            igi::HeliParams a;
            igi::HeliParams b;
            ASSERT_TRUE(igi::ParseHeliParams(from_qvm[i], &a)) << name << " #" << i;
            ASSERT_TRUE(igi::ParseHeliParams(from_text[i], &b)) << name << " #" << i;
            EXPECT_DOUBLE_EQ(a.position[0], b.position[0]) << name << " #" << i;
            EXPECT_DOUBLE_EQ(a.position[1], b.position[1]) << name << " #" << i;
            EXPECT_DOUBLE_EQ(a.position[2], b.position[2]) << name << " #" << i;
            EXPECT_FLOAT_EQ(a.orientation[0], b.orientation[0]) << name << " #" << i;
            EXPECT_FLOAT_EQ(a.orientation[1], b.orientation[1]) << name << " #" << i;
            EXPECT_FLOAT_EQ(a.orientation[2], b.orientation[2]) << name << " #" << i;
            EXPECT_FLOAT_EQ(a.original_thrust, b.original_thrust) << name << " #" << i;
            EXPECT_FLOAT_EQ(a.speed[0], b.speed[0]) << name << " #" << i;
            EXPECT_FLOAT_EQ(a.speed[1], b.speed[1]) << name << " #" << i;
            EXPECT_FLOAT_EQ(a.speed[2], b.speed[2]) << name << " #" << i;
            EXPECT_EQ(a.model, b.model) << name << " #" << i;
            EXPECT_EQ(a.collision_detection, b.collision_detection) << name << " #" << i;
            EXPECT_EQ(a.force_z, b.force_z) << name << " #" << i;
            EXPECT_EQ(a.open_door, b.open_door) << name << " #" << i;
            EXPECT_EQ(a.can_fire, b.can_fire) << name << " #" << i;
            EXPECT_EQ(a.play_sound, b.play_sound) << name << " #" << i;
            EXPECT_FLOAT_EQ(a.viewcone_alpha, b.viewcone_alpha) << name << " #" << i;
            EXPECT_FLOAT_EQ(a.viewcone_gamma, b.viewcone_gamma) << name << " #" << i;
            EXPECT_FLOAT_EQ(a.viewcone_length, b.viewcone_length) << name << " #" << i;
        }
        compared += static_cast<int>(from_qvm.size());
    }
    if (compared == 0) GTEST_SKIP() << "retail IGI (with objects.qsc) not found at "
                                    << GameRoot().string();
}

TEST(HeliRulesCorpus, EveryAuthoredHeliInstanceParses) {
    // 14 mission OBJECTS.QVM files; the count is the retail data, not a target.
    const int kExpected[15] = {0, 2, 1, 3, 2, 2, 1, 4, 1, 0, 3, 3, 4, 3, 0};
    int seen = 0;
    int levels = 0;

    for (int level = 1; level <= 14; ++level) {
        const fs::path path = LevelDir(level) / "OBJECTS.QVM";
        if (!fs::is_regular_file(path)) continue;
        ++levels;

        const QVMFile qvm = QVM_Parse(path.string());
        ASSERT_TRUE(qvm.valid) << "LEVEL" << level << ": " << qvm.error;
        const std::vector<std::vector<std::string>> calls = ExtractHeliCalls(qvm);
        EXPECT_EQ(static_cast<int>(calls.size()), kExpected[level]) << "LEVEL" << level;

        for (std::size_t i = 0; i < calls.size(); ++i) {
            // Every authored Heli carries exactly the 19 declared parameters.
            EXPECT_EQ(calls[i].size(), static_cast<std::size_t>(2 + 1 + igi::kHeliTokenCount))
                << "LEVEL" << level << " #" << i;
            igi::HeliParams params;
            EXPECT_TRUE(igi::ParseHeliParams(calls[i], &params))
                << "LEVEL" << level << " #" << i;
            // The engine positions are 12.12 fixed point metres, so a real
            // instance lands in the hundreds of millions of units.
            EXPECT_GT(std::fabs(params.position[0]), 1.0e5) << "LEVEL" << level << " #" << i;
            EXPECT_FALSE(params.model.empty()) << "LEVEL" << level << " #" << i;
            // "Original Thrust" is the authored start speed, inside Param_Range02.
            EXPECT_GE(params.original_thrust, 0.0f) << "LEVEL" << level << " #" << i;
            EXPECT_LE(params.original_thrust, 2.0f) << "LEVEL" << level << " #" << i;
            // "ViewCone Length (meter)" is inside gHeliRange.
            EXPECT_GE(params.viewcone_length, 0.0f) << "LEVEL" << level << " #" << i;
            EXPECT_LE(params.viewcone_length, 1000.0f) << "LEVEL" << level << " #" << i;
        }
        seen += static_cast<int>(calls.size());
    }

    if (levels == 0) GTEST_SKIP() << "retail IGI not found at " << GameRoot().string();
    EXPECT_EQ(levels, 14);
    EXPECT_EQ(seen, 29);
}

TEST(HeliRulesCorpus, LevelTwoInstanceIsTheAuthoredOne) {
    const fs::path path = LevelDir(2) / "OBJECTS.QVM";
    if (!fs::is_regular_file(path)) {
        GTEST_SKIP() << "retail IGI not found at " << GameRoot().string();
    }
    const QVMFile qvm = QVM_Parse(path.string());
    const std::vector<std::vector<std::string>> calls = ExtractHeliCalls(qvm);
    ASSERT_EQ(calls.size(), 1u);

    igi::HeliParams params;
    ASSERT_TRUE(igi::ParseHeliParams(calls[0], &params));
    // LEVEL2/objects.qsc:62-64.
    EXPECT_DOUBLE_EQ(params.position[0], 126942784.0);
    EXPECT_DOUBLE_EQ(params.position[1], -38370560.0);
    EXPECT_DOUBLE_EQ(params.position[2], 175304928.0);
    EXPECT_FLOAT_EQ(params.orientation[0], 0.0f);
    EXPECT_FLOAT_EQ(params.orientation[1], 0.0f);
    EXPECT_NEAR(params.orientation[2], 3.8061590194702148, 1e-6);
    EXPECT_FLOAT_EQ(params.original_thrust, 0.75f);
    EXPECT_FLOAT_EQ(params.speed[0], 0.0f);
    EXPECT_FLOAT_EQ(params.speed[2], 0.0f);
    EXPECT_EQ(params.model, "709_01_1");
    EXPECT_TRUE(params.collision_detection);
    EXPECT_FALSE(params.force_z);
    EXPECT_FLOAT_EQ(params.viewcone_alpha, 180.0f);
    EXPECT_FLOAT_EQ(params.viewcone_gamma, 360.0f);
    EXPECT_FLOAT_EQ(params.viewcone_length, 60.0f);

    // The stored matrix is Rz(3.806...) with no pitch or roll.
    EXPECT_NEAR(params.orientation_matrix[8], 1.0f, 1e-6);
    EXPECT_NEAR(params.orientation_matrix[0], std::cos(params.orientation[2]), 1e-6);
    EXPECT_NEAR(params.orientation_matrix[1], -std::sin(params.orientation[2]), 1e-6);
}

TEST(HeliRulesCorpus, BellPhysicsDefinitionsMatchTheShippedHeliQvm) {
    const fs::path path = GameRoot() / "physicsobj" / "HELIS" / "BELL" / "HELI.QVM";
    if (!fs::is_regular_file(path)) {
        GTEST_SKIP() << "retail IGI not found at " << GameRoot().string();
    }
    const QVMFile qvm = QVM_Parse(path.string());
    ASSERT_TRUE(qvm.valid) << qvm.error;
    const std::vector<std::vector<std::string>> defs = ParsePhysicsDefsFromText(QVM_DecompileToString(qvm));
    ASSERT_GE(defs.size(), 6u);

    igi::PhysicsDefHeader header;
    ASSERT_TRUE(igi::ParsePhysicsDefHeader(defs[0], &header));
    EXPECT_EQ(header.task_type, "TASKTYPE_HELI");
    EXPECT_EQ(header.type_id, 0);
    EXPECT_FLOAT_EQ(header.mass, 3000.0f);
    EXPECT_FLOAT_EQ(header.size[0], 2.2000000476837158f);
    EXPECT_FLOAT_EQ(header.size[1], 7.8000001907348633f);
    EXPECT_FLOAT_EQ(header.size[2], 3.0f);
    EXPECT_EQ(header.model, "709_01_1");
    EXPECT_EQ(header.joint_count, 0);
    EXPECT_EQ(header.first_arg, 12);

    igi::HeliConfig cfg;
    ASSERT_TRUE(igi::ParseHeliConfig(defs[0], header.first_arg, &cfg));
    EXPECT_FLOAT_EQ(cfg.v.start_speed_scale, 200.0f);
    for (int i = 0; i < 3; ++i) {
        EXPECT_FLOAT_EQ(cfg.v.axis[i].rate, 50.0f) << "axis " << i;
        EXPECT_FLOAT_EQ(cfg.v.axis[i].smooth, 0.4f) << "axis " << i;
    }
    EXPECT_FLOAT_EQ(cfg.v.thrust, 3000.0f);
    EXPECT_FLOAT_EQ(cfg.v.accel, 0.027000000700354576f);
    EXPECT_FLOAT_EQ(cfg.v.accel_slow, 0.0030000000260770321f);
    ASSERT_EQ(cfg.v.part_count, 3);
    EXPECT_FLOAT_EQ(cfg.v.parts[0][0], 20.0f);
    EXPECT_FLOAT_EQ(cfg.v.parts[0][1], 1.0f);
    EXPECT_FLOAT_EQ(cfg.v.parts[1][0], 3.0f);
    EXPECT_FLOAT_EQ(cfg.v.parts[1][2], 1.0f);
    EXPECT_FLOAT_EQ(cfg.v.parts[2][0], 50.0f);
    EXPECT_FLOAT_EQ(cfg.v.parts[2][3], 1.0f);
    for (int i = 0; i < 3; ++i) EXPECT_FLOAT_EQ(cfg.v.unk10C[i], 0.0f) << "unk10C " << i;
    // The first list is parsed and never read; only count2/list[3..] is.
    EXPECT_EQ(cfg.count1, 2);
    EXPECT_EQ(cfg.list[0], 0);
    EXPECT_EQ(cfg.list[1], 1);
    EXPECT_EQ(cfg.count2, 0);

    // The three rotors, at the index DefinePhysicsObjType hands the parser.
    const struct {
        int lift, tilt, dir;
        float angle_degrees, speed;
        const char* name;
    } kRotor[3] = {
        {1, 0, 1, 0.0f, 0.1f, "711_01_1"},
        {0, 0, 17, 5.0f, 0.07f, "711_03_1"},
        {0, 1, 10, 0.0f, 0.12f, "712_02_1"},
    };
    for (int i = 0; i < 3; ++i) {
        ASSERT_TRUE(igi::ParsePhysicsDefHeader(defs[static_cast<std::size_t>(i) + 1], &header));
        EXPECT_EQ(header.task_type, "TASKTYPE_ROTOR") << "def " << i + 1;
        EXPECT_EQ(header.first_arg, 24) << "def " << i + 1;
        igi::RotorDef def;
        ASSERT_TRUE(igi::ParseRotorDef(defs[static_cast<std::size_t>(i) + 1], header.first_arg, &def));
        EXPECT_EQ(def.lift, kRotor[i].lift) << "rotor " << i;
        EXPECT_EQ(def.tilt, kRotor[i].tilt) << "rotor " << i;
        EXPECT_EQ(def.dir, kRotor[i].dir) << "rotor " << i;
        EXPECT_FLOAT_EQ(def.angle_degrees, kRotor[i].angle_degrees) << "rotor " << i;
        EXPECT_NEAR(def.angle, kRotor[i].angle_degrees * igi::kRotorDegToRad, 1e-7) << "rotor " << i;
        EXPECT_FLOAT_EQ(def.speed, kRotor[i].speed) << "rotor " << i;
        EXPECT_EQ(def.name, kRotor[i].name) << "rotor " << i;
    }

    // The last two defs are plain physics objects, not rotors.
    ASSERT_TRUE(igi::ParsePhysicsDefHeader(defs[4], &header));
    EXPECT_EQ(header.task_type, "TASKTYPE_GENERICPHYSICSOBJ");
    ASSERT_TRUE(igi::ParsePhysicsDefHeader(defs[5], &header));
    EXPECT_EQ(header.task_type, "TASKTYPE_GENERICPHYSICSOBJ");
}

TEST(HeliRulesCorpus, MilPhysicsDefinitionsMatchTheShippedHeliQvm) {
    // The MIL airframe is the second shipped helicopter and the one that
    // actually populates both HeliConfig lists.
    const fs::path path = GameRoot() / "physicsobj" / "HELIS" / "MIL" / "HELI.QVM";
    if (!fs::is_regular_file(path)) {
        GTEST_SKIP() << "retail IGI not found at " << GameRoot().string();
    }
    const QVMFile qvm = QVM_Parse(path.string());
    ASSERT_TRUE(qvm.valid) << qvm.error;
    const std::vector<std::vector<std::string>> defs = ParsePhysicsDefsFromText(QVM_DecompileToString(qvm));
    ASSERT_GE(defs.size(), 2u);

    igi::PhysicsDefHeader header;
    ASSERT_TRUE(igi::ParsePhysicsDefHeader(defs[0], &header));
    EXPECT_EQ(header.task_type, "TASKTYPE_HELI");
    EXPECT_EQ(header.model, "700_01_1");

    igi::HeliConfig cfg;
    ASSERT_TRUE(igi::ParseHeliConfig(defs[0], header.first_arg, &cfg));
    EXPECT_FLOAT_EQ(cfg.v.start_speed_scale, 200.0f);
    ASSERT_EQ(cfg.v.part_count, 3);
    // unk10C is (60 * 4096, 180 * 0.5 * degToRad, 360 * 0.5 * degToRad).
    EXPECT_FLOAT_EQ(cfg.v.unk10C[0], 60.0f * 4096.0f);
    EXPECT_NEAR(cfg.v.unk10C[1], 90.0f * igi::kHeliDegToRad, 1e-6);
    EXPECT_NEAR(cfg.v.unk10C[2], 180.0f * igi::kHeliDegToRad, 1e-6);
    // count1 = 2, list = {0, 1}; count2 = 6, list[3..8] = 9..14.
    EXPECT_EQ(cfg.count1, 2);
    EXPECT_EQ(cfg.list[0], 0);
    EXPECT_EQ(cfg.list[1], 1);
    EXPECT_EQ(cfg.count2, 6);
    for (int i = 0; i < 6; ++i) EXPECT_EQ(cfg.list[3 + i], 9 + i) << "explode point " << i;

    // The MIL rotor.
    ASSERT_TRUE(igi::ParsePhysicsDefHeader(defs[1], &header));
    EXPECT_EQ(header.task_type, "TASKTYPE_ROTOR");
    igi::RotorDef def;
    ASSERT_TRUE(igi::ParseRotorDef(defs[1], header.first_arg, &def));
    EXPECT_EQ(def.lift, 1);
    EXPECT_EQ(def.tilt, 0);
    EXPECT_EQ(def.dir, 1);
    EXPECT_FLOAT_EQ(def.speed, 0.1f);
    EXPECT_EQ(def.name, "700_03_1");
}
