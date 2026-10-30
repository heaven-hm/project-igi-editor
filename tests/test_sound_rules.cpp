// test_sound_rules.cpp - the authored sound-emitter model (#70).
//
// The oracle for every number here is IGI-RE (read-only). Key citations:
//   condsound.c:73-93      ConditionalSound Init/Create and the ParamTable order
//   condsound.c:121-136    Run: the condition edge and the once gate
//   condsound.c:150-189    Play/Stop/UpdatePlaying, the stream branch
//   ambientarea.c:28-54    the struct, the ParamTable order, kHalfSize/kFadeStep
//   ambientarea.c:110-179  stop_sound, random_delay, Run (the fade integrator)
//   ambientarea.c:198-222  Changed: the sound list split
//   voice.c:584-611        Voice_SetParams: the distance fade and the pan
//   voice.c:709-724        SoundParams_Init: minDist 0, maxDist 1.0
//   sounddef.c:65-81       SoundDefSound_Play: the maxDist*4096 hard cull
//   sounddef.c:206-226     the DefineSound argument order and first-match Find
//   soundsys.c:217-246     a sound without flag 1 lasts one frame
//   soundsys.c:545-578     Sound_Play: name lookup, NULL on empty
//   game.c:441-442         common sounds load before mission sounds
//   paramops.c:133-160     kParamTypes: Real32x9/Real64x3 are Param_Three
//   paramops.c:423-435     ParamReal32x9_Parse: three Euler angles
//   paramtype.c:102-122    String32 is 0x20 bytes, String256 is 0x100
//   matrix.c:9-21,99-110   Mul and Matrix_SetEuler (Rz * Ry * Rx)
//   random.c:22-26         Random_Float is [0,1) with 16-bit granularity
//   missile.h:111-117      round_int is fistpll: round-half-to-EVEN
//
// The corpus half reads the retail OBJECTS.QVM and SOUNDS.QVM with the editor's
// own QVM_Parse and cross-checks level1/LEVEL2/LEVEL4 - the only levels that
// also ship a decompiled objects.qsc - against that human-readable text. It
// skips (GTEST_SKIP) when IGI_GAME_PATH is absent.

#include "../source/level/qvm_parser.h"
#include "../source/runtime/sound_emitter.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;

fs::path GameRoot() {
    if (const char* env = std::getenv("IGI_GAME_PATH"); env != nullptr && *env != '\0')
        return fs::path(env);
    return fs::path("/Users/haseeb-mir/Downloads/IGI");
}

// The retail tree spells level 1 "level1" and the rest "LEVEL2".."LEVEL14", and
// on a case-insensitive filesystem the upper-case form resolves for all of them.
// Try both so the suite also runs on a case-sensitive one.
fs::path LevelDir(int level) {
    const fs::path base = GameRoot() / "MISSIONS" / "location0";
    const fs::path upper = base / ("LEVEL" + std::to_string(level));
    if (fs::exists(upper)) return upper;
    return base / ("level" + std::to_string(level));
}

// ---------------------------------------------------------------------------
// QVM bytecode -> authored argument tokens
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

// PUSHIIB/PUSHIIW/PUSHII index the QVM identifier table, which for a retail
// OBJECTS.QVM is exactly ["Task_DeclareParameters","Task_New","TRUE","FALSE"]:
// `PUSHIIB 3` is FALSE.
bool IsIdentifierPush(QVMOpType type) {
    return type == QVMOpType::PUSHII || type == QVMOpType::PUSHIIB ||
           type == QVMOpType::PUSHIIW;
}

std::string FormatFloat(float value) {
    char buffer[64];
    // %.9g round-trips a float exactly, so the token re-parses to the same value.
    std::snprintf(buffer, sizeof(buffer), "%.9g", value);
    return buffer;
}

// Reads up to `want` value tokens starting at `start`. CALL and BRA are skipped
// because a nested Task_New header can sit between a call's arguments; the
// caller always asks for the exact authored count, so this cannot run past the
// end of an argument list in well-formed bytecode.
std::vector<std::string> ReadArgs(const QVMFile& qvm, std::size_t start, std::size_t want) {
    std::vector<std::string> tokens;
    for (std::size_t i = start; i < qvm.instructions.size() && tokens.size() < want; ++i) {
        const QVMInstruction& in = qvm.instructions[i];
        if (in.type == QVMOpType::BRK || in.type == QVMOpType::CALL ||
            in.type == QVMOpType::BRA) {
            continue;
        }
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
        if (in.type == QVMOpType::PUSH0) {
            tokens.push_back("0");
            continue;
        }
        if (in.type == QVMOpType::PUSH1) {
            tokens.push_back("1");
            continue;
        }
        if (in.type == QVMOpType::PUSHF) {
            tokens.push_back(FormatFloat(in.operand_float));
            continue;
        }
        if (in.type == QVMOpType::MINUS) {
            if (!tokens.empty()) tokens.back() = "-" + tokens.back();
            continue;
        }
        break;
    }
    return tokens;
}

std::size_t SkipBrk(const QVMFile& qvm, std::size_t index) {
    while (index < qvm.instructions.size() && qvm.instructions[index].type == QVMOpType::BRK) {
        ++index;
    }
    return index;
}

// One authored Task_New call.
struct Instance {
    std::string name;
    std::vector<std::string> args;  // parameter values only, no id/type/name
};

// Every instance of `type` in a QVM, in file order.
//
// The Task_DeclareParameters call pushes the type string too, so the two are
// told apart by the first two declared name/type PAIRS, which the declaration
// repeats verbatim and which no authored instance reproduces. For
// ConditionalSound the simpler "three strings in a row" test would misclassify
// an instance, because its first two arguments are strings (the condition and
// the sound name).
std::vector<Instance> ExtractInstances(const QVMFile& qvm, const std::string& type,
                                      std::size_t want_args, const char* first_name,
                                      const char* first_type, const char* second_name,
                                      const char* second_type) {
    std::vector<Instance> out;
    for (std::size_t i = 0; i < qvm.instructions.size(); ++i) {
        const QVMInstruction& in = qvm.instructions[i];
        if (!IsStringPush(in.type)) continue;
        if (in.operand >= qvm.strings.size()) continue;
        if (qvm.strings[in.operand] != type) continue;

        const std::size_t name = SkipBrk(qvm, i + 1);
        if (name >= qvm.instructions.size() || !IsStringPush(qvm.instructions[name].type)) {
            continue;
        }
        const std::vector<std::string> head = ReadArgs(qvm, name, 4);
        if (head.size() == 4 && head[0] == first_name && head[1] == first_type &&
            head[2] == second_name && head[3] == second_type) {
            continue;  // the declaration
        }

        Instance instance;
        instance.name = qvm.strings[qvm.instructions[name].operand];
        instance.args = ReadArgs(qvm, name + 1, want_args);
        if (instance.args.size() != want_args) continue;
        out.push_back(std::move(instance));
    }
    return out;
}

std::vector<Instance> ExtractConditionalSounds(const QVMFile& qvm) {
    return ExtractInstances(qvm, "ConditionalSound", 8, "Condition", "VarString", "SoundDef",
                            "String32");
}

std::vector<Instance> ExtractAmbientAreas(const QVMFile& qvm) {
    return ExtractInstances(qvm, "AmbientArea", 12, "Position", "ObjectPos", "Orientation",
                            "Real32x9");
}

// The 13 DefineSound arguments, in SoundDefSound_Write order
// (src/define/sounddef.c:206-218). The DefineSound identifier is followed by a
// CALL and a BRA before the arguments, which ReadArgs skips.
std::vector<std::vector<std::string>> ExtractSoundDefinitions(const QVMFile& qvm) {
    std::vector<std::vector<std::string>> out;
    for (std::size_t i = 0; i < qvm.instructions.size(); ++i) {
        const QVMInstruction& in = qvm.instructions[i];
        if (!IsIdentifierPush(in.type)) continue;
        if (in.operand >= qvm.identifiers.size()) continue;
        if (qvm.identifiers[in.operand] != "DefineSound") continue;
        std::vector<std::string> args = ReadArgs(qvm, i + 1, 13);
        if (args.size() == 13) out.push_back(std::move(args));
    }
    return out;
}

QVMFile LoadQvm(const fs::path& path) {
    QVMFile qvm;
    if (!fs::exists(path)) return qvm;
    return QVM_Parse(path.string());
}

// The definitions a level sees: "LOCAL:common/sounds/" is loaded before
// "MISSION:sounds/" (src/game/game.c:441-442), so COMMON goes in first and
// first-wins makes its definitions shadow same-named mission ones.
igi::SoundDefinitionRegistry LoadRegistry(int level) {
    igi::SoundDefinitionRegistry registry;
    const fs::path common = GameRoot() / "COMMON" / "SOUNDS" / "SOUNDS.QVM";
    const fs::path mission = LevelDir(level) / "SOUNDS" / "SOUNDS.QVM";
    for (const fs::path& path : {common, mission}) {
        const QVMFile qvm = LoadQvm(path);
        if (!qvm.valid) continue;
        for (const std::vector<std::string>& args : ExtractSoundDefinitions(qvm)) {
            igi::SoundDefinition definition;
            if (igi::ParseSoundDefinition(args, definition)) registry.Add(definition);
        }
    }
    return registry;
}

// ---------------------------------------------------------------------------
// objects.qsc text -> the same calls, for the cross-check
// ---------------------------------------------------------------------------

std::string ReadFile(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return {};
    return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

// Splits the argument list of every `Task_New(..., "<type>", ...)` call in a
// decompiled .qsc, honouring quoted strings and nested parentheses. The result
// is in the same layout as the QVM walk: [0]=id, [1]=type, [2]=name, [3]=param 0.
std::vector<std::vector<std::string>> ParseTaskNewCalls(const std::string& text,
                                                        const std::string& type) {
    std::vector<std::vector<std::string>> out;
    const std::string needle = "Task_New(";
    std::size_t pos = text.find(needle);

    while (pos != std::string::npos) {
        const std::size_t open = pos + needle.size() - 1;
        std::size_t index = open + 1;
        int depth = 1;
        bool in_string = false;
        bool escape = false;
        for (; index < text.size(); ++index) {
            const char c = text[index];
            if (escape) {
                escape = false;
                continue;
            }
            if (in_string && c == '\\') {
                escape = true;
                continue;
            }
            if (c == '"') {
                in_string = !in_string;
                continue;
            }
            if (in_string) continue;
            if (c == '(') ++depth;
            if (c == ')' && --depth == 0) break;
        }
        if (index >= text.size()) break;

        const std::string body = text.substr(open + 1, index - open - 1);
        std::vector<std::string> args;
        std::string current;
        depth = 0;
        in_string = false;
        escape = false;
        for (char c : body) {
            if (escape) {
                current.push_back(c);
                escape = false;
                continue;
            }
            if (in_string && c == '\\') {
                current.push_back(c);
                escape = true;
                continue;
            }
            if (c == '"') {
                in_string = !in_string;
                current.push_back(c);
                continue;
            }
            if (!in_string && c == '(') ++depth;
            if (!in_string && c == ')') --depth;
            if (!in_string && depth == 0 && c == ',') {
                args.push_back(current);
                current.clear();
                continue;
            }
            current.push_back(c);
        }
        if (!current.empty()) args.push_back(current);

        for (std::string& arg : args) {
            const std::size_t b = arg.find_first_not_of(" \t\r\n");
            const std::size_t e = arg.find_last_not_of(" \t\r\n");
            arg = (b == std::string::npos) ? std::string() : arg.substr(b, e - b + 1);
            if (arg.size() >= 2 && arg.front() == '"' && arg.back() == '"') {
                arg = arg.substr(1, arg.size() - 2);
                // The .qsc writes a newline inside a condition as the two
                // characters `\n`; the compiled QVM string holds a real 0x0A.
                // Level 1 alone has 35 such conditions.
                std::string unescaped;
                for (std::size_t i = 0; i < arg.size(); ++i) {
                    if (arg[i] != '\\' || i + 1 >= arg.size()) {
                        unescaped.push_back(arg[i]);
                        continue;
                    }
                    switch (arg[++i]) {
                        case 'n': unescaped.push_back('\n'); break;
                        case 't': unescaped.push_back('\t'); break;
                        case 'r': unescaped.push_back('\r'); break;
                        case '"': unescaped.push_back('"'); break;
                        case '\\': unescaped.push_back('\\'); break;
                        default:
                            unescaped.push_back('\\');
                            unescaped.push_back(arg[i]);
                            break;
                    }
                }
                arg = std::move(unescaped);
            }
        }

        if (args.size() > 1 && args[1] == type) out.push_back(args);
        pos = text.find(needle, pos + needle.size());
    }
    return out;
}

// A retail level's authored AmbientArea, keyed by name.
const igi::AmbientAreaResolved* FindArea(const std::vector<igi::AmbientAreaResolved>& areas,
                                        const std::string& name) {
    for (const igi::AmbientAreaResolved& area : areas) {
        if (area.authored_name == name) return &area;
    }
    return nullptr;
}

// The corpus is read once per process; each helper below re-parses because the
// QVM files are small and this keeps the failure output local.
bool CorpusPresent() { return fs::exists(GameRoot() / "MISSIONS" / "location0"); }

}  // namespace

// ---------------------------------------------------------------------------
// The parameter schema
// ---------------------------------------------------------------------------

TEST(SoundRulesTest, ConditionalSoundSchemaIsTheParamTableOrder) {
    int count = 0;
    const igi::SoundParamSpec* params = igi::ConditionalSoundParamSpecs(&count);
    ASSERT_EQ(count, 6);
    ASSERT_EQ(count, igi::kConditionalSoundParamCount);

    // condsound.c:87-92, with the authored names from LEVEL1/objects.qsc:78.
    EXPECT_STREQ(params[0].authored_name, "Condition");
    EXPECT_STREQ(params[0].engine_type, "VarString");
    EXPECT_STREQ(params[1].authored_name, "SoundDef");
    EXPECT_STREQ(params[1].engine_type, "String32");
    EXPECT_STREQ(params[2].authored_name, "Position");
    EXPECT_STREQ(params[2].engine_type, "ObjectPos");
    EXPECT_STREQ(params[3].authored_name, "Simple");
    EXPECT_STREQ(params[3].engine_type, "bool8");
    EXPECT_STREQ(params[4].authored_name, "OneShot");
    EXPECT_STREQ(params[4].engine_type, "bool8");
    EXPECT_STREQ(params[5].authored_name, "Relative to microphone");
    EXPECT_STREQ(params[5].engine_type, "bool8");

    // The struct field the decompilation calls `loop` is the LAST parameter and
    // is authored as "Relative to microphone" (condsound.c:92): the name is a
    // misnomer for Sound_SetGlobal.
    EXPECT_EQ(params[5].type, igi::SoundParamType::Bool8);
    EXPECT_EQ(igi::kConditionalSoundRelativeOffset, 0xB3);
}

TEST(SoundRulesTest, AmbientAreaSchemaIsTheParamTableOrder) {
    int count = 0;
    const igi::SoundParamSpec* params = igi::AmbientAreaParamSpecs(&count);
    ASSERT_EQ(count, 6);
    ASSERT_EQ(count, igi::kAmbientAreaParamCount);

    // ambientarea.c:74-79, with the authored names from LEVEL1/objects.qsc:55.
    EXPECT_STREQ(params[0].authored_name, "Position");
    EXPECT_STREQ(params[0].engine_type, "ObjectPos");
    EXPECT_STREQ(params[1].authored_name, "Orientation");
    EXPECT_STREQ(params[1].engine_type, "Real32x9");
    EXPECT_STREQ(params[2].authored_name, "Size");
    EXPECT_STREQ(params[2].engine_type, "Real64x3");
    EXPECT_STREQ(params[3].authored_name, "Min Delay");
    EXPECT_STREQ(params[3].engine_type, "Real64");
    EXPECT_STREQ(params[4].authored_name, "Random wait");
    EXPECT_STREQ(params[4].engine_type, "Real64");
    EXPECT_STREQ(params[5].authored_name, "SoundDef");
    EXPECT_STREQ(params[5].engine_type, "String256");
}

TEST(SoundRulesTest, ArgCountsComeFromTheParamTypeNotItsName) {
    // kParamTypes: Real32x9 and Real64x3 are Param_Three, which sets *n = 3
    // (paramops.c:143,146,152 and :398-402); ObjectPos aliases Real64x3
    // (paramops.c:183). Everything else is Param_One (paramops.c:355-359).
    EXPECT_EQ(igi::SoundParamScriptArgs(igi::SoundParamType::ObjectPos), 3);
    EXPECT_EQ(igi::SoundParamScriptArgs(igi::SoundParamType::Real64x3), 3);
    EXPECT_EQ(igi::SoundParamScriptArgs(igi::SoundParamType::Real32x9), 3);
    EXPECT_EQ(igi::SoundParamScriptArgs(igi::SoundParamType::VarString), 1);
    EXPECT_EQ(igi::SoundParamScriptArgs(igi::SoundParamType::String32), 1);
    EXPECT_EQ(igi::SoundParamScriptArgs(igi::SoundParamType::String256), 1);
    EXPECT_EQ(igi::SoundParamScriptArgs(igi::SoundParamType::Real64), 1);
    EXPECT_EQ(igi::SoundParamScriptArgs(igi::SoundParamType::Bool8), 1);

    // 1 + 1 + 3 + 1 + 1 + 1 and 3 + 3 + 3 + 1 + 1 + 1. The retail QVM's Task_New
    // call headers carry 11 and 15, i.e. these plus id, type and name.
    EXPECT_EQ(igi::ConditionalSoundScriptArgs(), 8);
    EXPECT_EQ(igi::AmbientAreaScriptArgs(), 12);
}

// ---------------------------------------------------------------------------
// round_int is round-half-to-even, not a truncation
// ---------------------------------------------------------------------------

TEST(SoundRulesTest, RoundHalfEvenIsFistpllNotTruncation) {
    // missile.h:111-117: `fistpll` stores with the current rounding mode, which
    // is round-to-nearest with ties to even. Truncation would give 0, 1, 2.
    EXPECT_DOUBLE_EQ(igi::RoundHalfEven(0.5), 0.0);
    EXPECT_DOUBLE_EQ(igi::RoundHalfEven(1.5), 2.0);
    EXPECT_DOUBLE_EQ(igi::RoundHalfEven(2.5), 2.0);
    EXPECT_DOUBLE_EQ(igi::RoundHalfEven(3.5), 4.0);
    EXPECT_DOUBLE_EQ(igi::RoundHalfEven(-0.5), 0.0);
    EXPECT_DOUBLE_EQ(igi::RoundHalfEven(-1.5), -2.0);
    EXPECT_DOUBLE_EQ(igi::RoundHalfEven(-2.5), -2.0);
    // Below and above the tie.
    EXPECT_DOUBLE_EQ(igi::RoundHalfEven(0.49), 0.0);
    EXPECT_DOUBLE_EQ(igi::RoundHalfEven(0.51), 1.0);
    EXPECT_DOUBLE_EQ(igi::RoundHalfEven(2.6), 3.0);
}

TEST(SoundRulesTest, ConditionCoercionGoesThroughASignedChar) {
    // `cs->isRun = (char)round_int(VarString_EvalReal(...))` (condsound.c:125),
    // then `if (!cs->isRun)`. The cast wraps, so 256 is false and 255 is true.
    EXPECT_FALSE(igi::ConditionHolds(0.0));
    EXPECT_TRUE(igi::ConditionHolds(1.0));
    EXPECT_TRUE(igi::ConditionHolds(-1.0));
    EXPECT_TRUE(igi::ConditionHolds(0.6));   // round_int -> 1
    EXPECT_FALSE(igi::ConditionHolds(0.4));  // round_int -> 0
    EXPECT_FALSE(igi::ConditionHolds(0.5));  // the tie goes to even: 0
    EXPECT_TRUE(igi::ConditionHolds(1.5));   // the tie goes to even: 2
    EXPECT_TRUE(igi::ConditionHolds(127.0));
    EXPECT_TRUE(igi::ConditionHolds(255.0));   // (char)255 == -1
    EXPECT_FALSE(igi::ConditionHolds(256.0));  // (char)256 == 0
    EXPECT_FALSE(igi::ConditionHolds(-256.0));
    EXPECT_TRUE(igi::ConditionHolds(257.0));
}

// ---------------------------------------------------------------------------
// Matrix_SetEuler
// ---------------------------------------------------------------------------

TEST(SoundRulesTest, MatrixFromEulerIsRzRyRx) {
    // matrix.c:99-110: Rz, then * Ry, then * Rx, row-major m[i*3+j].
    float m[9] = {};
    const double identity[3] = {0.0, 0.0, 0.0};
    igi::MatrixFromEuler(identity, m);
    EXPECT_FLOAT_EQ(m[0], 1.0f);
    EXPECT_FLOAT_EQ(m[1], 0.0f);
    EXPECT_FLOAT_EQ(m[2], 0.0f);
    EXPECT_FLOAT_EQ(m[3], 0.0f);
    EXPECT_FLOAT_EQ(m[4], 1.0f);
    EXPECT_FLOAT_EQ(m[5], 0.0f);
    EXPECT_FLOAT_EQ(m[6], 0.0f);
    EXPECT_FLOAT_EQ(m[7], 0.0f);
    EXPECT_FLOAT_EQ(m[8], 1.0f);

    // 90 degrees about Z: Rz = [[0,-1,0],[1,0,0],[0,0,1]]. The sine terms are
    // exact; the cosine terms are 0 only up to the transcendental. x87 fcos and
    // the host's cosl can differ in the last bits, so they are compared with a
    // tolerance rather than for equality.
    const double quarter_turn[3] = {0.0, 0.0, 1.57079632679489661923};
    igi::MatrixFromEuler(quarter_turn, m);
    EXPECT_FLOAT_EQ(m[1], -1.0f);
    EXPECT_FLOAT_EQ(m[3], 1.0f);
    EXPECT_FLOAT_EQ(m[8], 1.0f);
    EXPECT_NEAR(m[0], 0.0f, 1e-6f);
    EXPECT_NEAR(m[4], 0.0f, 1e-6f);
    EXPECT_NEAR(m[2], 0.0f, 1e-6f);
    EXPECT_NEAR(m[5], 0.0f, 1e-6f);
    EXPECT_NEAR(m[6], 0.0f, 1e-6f);
    EXPECT_NEAR(m[7], 0.0f, 1e-6f);

    // 90 degrees about X: Rx = [[1,0,0],[0,0,-1],[0,1,0]].
    const double about_x[3] = {1.57079632679489661923, 0.0, 0.0};
    igi::MatrixFromEuler(about_x, m);
    EXPECT_FLOAT_EQ(m[0], 1.0f);
    EXPECT_FLOAT_EQ(m[5], -1.0f);
    EXPECT_FLOAT_EQ(m[7], 1.0f);
    EXPECT_NEAR(m[4], 0.0f, 1e-6f);
    EXPECT_NEAR(m[8], 0.0f, 1e-6f);
}

// ---------------------------------------------------------------------------
// The AmbientArea sound list
// ---------------------------------------------------------------------------

TEST(SoundRulesTest, AmbientSoundListSplitsOnSpaceSemicolonAndComma) {
    // ambientarea.c:207: `c == ' ' || c == ';' || c == ',' || c == 0`.
    const std::vector<std::string> one = igi::SplitAmbientSoundList("machinery_01");
    ASSERT_EQ(one.size(), 1u);
    EXPECT_EQ(one[0], "machinery_01");

    const std::vector<std::string> three =
        igi::SplitAmbientSoundList("dog_01 dog_02 dog_03");
    ASSERT_EQ(three.size(), 3u);
    EXPECT_EQ(three[0], "dog_01");
    EXPECT_EQ(three[2], "dog_03");

    const std::vector<std::string> mixed = igi::SplitAmbientSoundList("a;b,c d");
    ASSERT_EQ(mixed.size(), 4u);
    EXPECT_EQ(mixed[0], "a");
    EXPECT_EQ(mixed[1], "b");
    EXPECT_EQ(mixed[2], "c");
    EXPECT_EQ(mixed[3], "d");

    // Empty words are dropped because `inWord` gates the copy (ambientarea.c:208).
    EXPECT_TRUE(igi::SplitAmbientSoundList("").empty());
    EXPECT_TRUE(igi::SplitAmbientSoundList("   ").empty());
    EXPECT_TRUE(igi::SplitAmbientSoundList(",,;").empty());
    EXPECT_EQ(igi::SplitAmbientSoundList(" a  b ").size(), 2u);
}

// ---------------------------------------------------------------------------
// Parsing an authored instance
// ---------------------------------------------------------------------------

TEST(SoundRulesTest, ParseConditionalSoundFromRetailShapedTokens) {
    // LEVEL1/objects.qsc:562-563.
    const std::vector<std::string> tokens = {
        "-1",  "ConditionalSound", "AlarmControl_98", "AlarmControl_98.isAlarm",
        "alarmsystem_working", "24461340", "-56116196", "174456944", "FALSE", "FALSE", "FALSE"};
    igi::ConditionalSoundEmitter emitter;
    ASSERT_TRUE(igi::ParseConditionalSound(tokens, emitter));
    EXPECT_EQ(emitter.condition, "AlarmControl_98.isAlarm");
    EXPECT_EQ(emitter.sound_def, "alarmsystem_working");
    EXPECT_DOUBLE_EQ(emitter.position[0], 24461340.0);
    EXPECT_DOUBLE_EQ(emitter.position[1], -56116196.0);
    EXPECT_DOUBLE_EQ(emitter.position[2], 174456944.0);
    EXPECT_FALSE(emitter.simple);
    EXPECT_FALSE(emitter.one_shot);
    EXPECT_FALSE(emitter.relative_to_microphone);

    // A cutscene voiceover: relative to the microphone (LEVEL1/objects.qsc:1079).
    const std::vector<std::string> stream = {
        "-1", "ConditionalSound", "cut", "CutScene_1200.nTick < 11.5*GAME_FREQUENCY",
        "m1_heligen01", "0", "0", "0", "FALSE", "FALSE", "TRUE"};
    ASSERT_TRUE(igi::ParseConditionalSound(stream, emitter));
    EXPECT_TRUE(emitter.relative_to_microphone);
    EXPECT_FALSE(emitter.simple);

    // Too short: refused, and the output is untouched.
    igi::ConditionalSoundEmitter untouched;
    untouched.sound_def = "keep";
    EXPECT_FALSE(igi::ParseConditionalSound({"-1", "ConditionalSound", "x"}, untouched));
    EXPECT_EQ(untouched.sound_def, "keep");
}

TEST(SoundRulesTest, ParseAmbientAreaFromRetailShapedTokens) {
    // LEVEL1/objects.qsc:1183-1189: "machinery room 02", 24689856.0,
    // -56311688.0, 174413136.0, 0, 0, 0, 9.0, 5.0, 4.0, 0, 0, "machinery_01".
    const std::vector<std::string> tokens = {"96",
                                             "AmbientArea",
                                             "machinery room 02",
                                             "24689856",
                                             "-56311688",
                                             "174413136",
                                             "0",
                                             "0",
                                             "0",
                                             "9",
                                             "5",
                                             "4",
                                             "0",
                                             "0",
                                             "machinery_01"};
    igi::AmbientAreaResolved area;
    ASSERT_TRUE(igi::ParseAmbientArea(tokens, area));
    EXPECT_EQ(area.authored_name, "machinery room 02");
    EXPECT_DOUBLE_EQ(area.authored.position[0], 24689856.0);
    EXPECT_DOUBLE_EQ(area.authored.position[2], 174413136.0);
    EXPECT_DOUBLE_EQ(area.authored.size[0], 9.0);
    EXPECT_DOUBLE_EQ(area.authored.size[1], 5.0);
    EXPECT_DOUBLE_EQ(area.authored.size[2], 4.0);
    EXPECT_DOUBLE_EQ(area.authored.min_delay, 0.0);
    EXPECT_DOUBLE_EQ(area.authored.random_wait, 0.0);
    ASSERT_EQ(area.sounds.size(), 1u);
    EXPECT_EQ(area.sounds[0], "machinery_01");
    // Orientation 0,0,0 -> the identity (matrix.c:99-110).
    EXPECT_FLOAT_EQ(area.matrix[0], 1.0f);
    EXPECT_FLOAT_EQ(area.matrix[4], 1.0f);
    EXPECT_FLOAT_EQ(area.matrix[8], 1.0f);
    // Create sets size 1,1,1 and volume 0 (ambientarea.c:87-97).
    EXPECT_DOUBLE_EQ(igi::kAmbientAreaVolumeOffset, 0x78);

    // LEVEL4/objects.qsc:674: three names, min 20, random 20.
    const std::vector<std::string> three = {"-1",
                                            "AmbientArea",
                                            "dogs",
                                            "112822272",
                                            "-56167508",
                                            "175286944",
                                            "0",
                                            "0",
                                            "0",
                                            "5000",
                                            "5000",
                                            "500",
                                            "20",
                                            "20",
                                            "dog_01 dog_02 dog_03"};
    ASSERT_TRUE(igi::ParseAmbientArea(three, area));
    ASSERT_EQ(area.sounds.size(), 3u);
    EXPECT_EQ(area.sounds[2], "dog_03");
    EXPECT_DOUBLE_EQ(area.authored.min_delay, 20.0);
    EXPECT_DOUBLE_EQ(area.authored.random_wait, 20.0);

    igi::AmbientAreaResolved untouched;
    EXPECT_FALSE(igi::ParseAmbientArea({"-1", "AmbientArea", "x", "1"}, untouched));
}

// ---------------------------------------------------------------------------
// Voice_SetParams: the distance fade
// ---------------------------------------------------------------------------

TEST(SoundRulesTest, DistanceFadeIsLinearBetweenMinAndMax) {
    // voice.c:584-600, with both distances scaled by 4096 (voice.c:79).
    // m1_heliby01 is volume 0.8, minDist 0, maxDist 200 (level1 SOUNDS.QVM).
    const double k200 = 200.0 * 4096.0;
    EXPECT_DOUBLE_EQ(igi::SoundAttenuatedVolume(0.0, 0.0, 200.0, 0.8), 0.8);
    EXPECT_DOUBLE_EQ(igi::SoundAttenuatedVolume(k200 * 0.25, 0.0, 200.0, 0.8), 0.8 * 0.75);
    EXPECT_DOUBLE_EQ(igi::SoundAttenuatedVolume(k200 * 0.5, 0.0, 200.0, 0.8), 0.8 * 0.5);
    EXPECT_DOUBLE_EQ(igi::SoundAttenuatedVolume(k200, 0.0, 200.0, 0.8), 0.0);
    // Beyond maxDist the volume is hard zeroed, not extrapolated negative.
    EXPECT_DOUBLE_EQ(igi::SoundAttenuatedVolume(k200 + 1.0, 0.0, 200.0, 0.8), 0.0);
    EXPECT_DOUBLE_EQ(igi::SoundAttenuatedVolume(1e12, 0.0, 200.0, 0.8), 0.0);

    // Inside minDist the volume is left alone: wind_1 is minDist 10000,
    // maxDist 11000, volume 0.75 (COMMON/SOUNDS/SOUNDS.QVM:271).
    const double kMin = 10000.0 * 4096.0;
    const double kMax = 11000.0 * 4096.0;
    EXPECT_DOUBLE_EQ(igi::SoundAttenuatedVolume(0.0, 10000.0, 11000.0, 0.75), 0.75);
    EXPECT_DOUBLE_EQ(igi::SoundAttenuatedVolume(kMin, 10000.0, 11000.0, 0.75), 0.75);
    EXPECT_DOUBLE_EQ(igi::SoundAttenuatedVolume(kMin - 1.0, 10000.0, 11000.0, 0.75), 0.75);
    // A quarter of the way through the 1000 m fade band.
    EXPECT_DOUBLE_EQ(igi::SoundAttenuatedVolume(kMin + (kMax - kMin) * 0.25, 10000.0, 11000.0,
                                                0.75),
                     0.75 * 0.75);
    EXPECT_DOUBLE_EQ(igi::SoundAttenuatedVolume(kMax, 10000.0, 11000.0, 0.75), 0.0);

    // The fade with the volume factored out.
    EXPECT_DOUBLE_EQ(igi::SoundDistanceFade(k200 * 0.5, 0.0, 200.0), 0.5);
    EXPECT_DOUBLE_EQ(igi::SoundDistanceFade(k200 * 2.0, 0.0, 200.0), 0.0);

    // A zero-width band divides by zero at exactly minDist. The engine's
    // `if (!(k >= 0))` turns the NaN into 0 (voice.c:595-597), so the sound is
    // silenced rather than passed through.
    EXPECT_DOUBLE_EQ(igi::SoundAttenuatedVolume(5.0 * 4096.0, 5.0, 5.0, 1.0), 0.0);
    // Just outside the band it is still the hard cull that applies.
    EXPECT_DOUBLE_EQ(igi::SoundAttenuatedVolume(5.0 * 4096.0 + 1.0, 5.0, 5.0, 1.0), 0.0);
    // And just inside minDist the volume survives untouched.
    EXPECT_DOUBLE_EQ(igi::SoundAttenuatedVolume(5.0 * 4096.0 - 1.0, 5.0, 5.0, 1.0), 1.0);
}

TEST(SoundRulesTest, AudibleRadiusCullIsInclusiveOfMaxDist) {
    // sounddef.c:65-71: return unless r*r >= DistanceSq with r = maxDist * 4096,
    // compared as squares. m1_heliby01 is maxDist 200.
    const double r = 200.0 * 4096.0;
    EXPECT_TRUE(igi::SoundWithinAudibleRadius(r, 200.0));
    EXPECT_TRUE(igi::SoundWithinAudibleRadius(r - 1.0, 200.0));
    EXPECT_FALSE(igi::SoundWithinAudibleRadius(r + 1.0, 200.0));
    EXPECT_TRUE(igi::SoundWithinAudibleRadius(0.0, 200.0));

    // heli_by_01, dog_01, wind_fx_01 and ventilation_01 are maxDist 1 metre, so
    // they are inaudible from anywhere except right on top of the listener.
    // AmbientArea repositions them onto the listener (ambientarea.c:170), which
    // is what makes them usable at all.
    EXPECT_TRUE(igi::SoundWithinAudibleRadius(4096.0, 1.0));
    EXPECT_FALSE(igi::SoundWithinAudibleRadius(4097.0, 1.0));
}

TEST(SoundRulesTest, PanIsTheListenerRightAxisOverTheDistanceSum) {
    // voice.c:601-611. r = right . delta, then pan = r / dist * (dist / (max+min)),
    // so the two dist factors cancel and pan is r / (max+min).
    const double delta[3] = {100.0, 0.0, 0.0};
    const double right[3] = {1.0, 0.0, 0.0};
    EXPECT_DOUBLE_EQ(igi::SoundPan(delta, right, 100.0, 0.0, 200.0),
                     100.0 / (200.0 * 4096.0));
    // The cancellation: doubling the delta doubles the pan, and the distance
    // along the same axis does not appear.
    const double delta2[3] = {200.0, 0.0, 0.0};
    EXPECT_DOUBLE_EQ(igi::SoundPan(delta2, right, 200.0, 0.0, 200.0),
                     200.0 / (200.0 * 4096.0));

    // A sound at the listener's own position has no pan (voice.c:610).
    const double zero[3] = {0.0, 0.0, 0.0};
    EXPECT_DOUBLE_EQ(igi::SoundPan(zero, right, 0.0, 0.0, 200.0), 0.0);

    // Far off to the right the pan saturates at +1 (voice.c:605-608).
    // Named `far_right`, not `far`: `windef.h` defines `far` as an empty macro
    // for 16-bit compatibility, so `const double far[3]` is not declarable in a
    // translation unit that includes <windows.h>.  The host lane (clang/macOS)
    // never sees that macro, so only the Windows gate can catch this.
    const double far_right[3] = {1e7, 0.0, 0.0};
    EXPECT_DOUBLE_EQ(igi::SoundPan(far_right, right, 1e7, 0.0, 200.0), 1.0);
    const double far_left[3] = {-1e7, 0.0, 0.0};
    EXPECT_DOUBLE_EQ(igi::SoundPan(far_left, right, 1e7, 0.0, 200.0), -1.0);

    // A sound behind the listener has no pan either: the right axis is
    // orthogonal to the delta.
    const double behind[3] = {0.0, 100.0, 0.0};
    EXPECT_DOUBLE_EQ(igi::SoundPan(behind, right, 100.0, 0.0, 200.0), 0.0);
}

// ---------------------------------------------------------------------------
// AmbientArea containment
// ---------------------------------------------------------------------------

TEST(SoundRulesTest, AmbientAreaContainmentIsStrictAndScaledBy4096) {
    // ambientarea.c:142-143. size is in metres and is scaled by kHalfSize =
    // 4096 (ambientarea.c:51-52), so size 9,5,4 is an 18 x 10 x 8 metre box.
    const double size[3] = {9.0, 5.0, 4.0};
    const float identity[9] = {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    const double origin[3] = {0.0, 0.0, 0.0};

    const double centre[3] = {0.0, 0.0, 0.0};
    EXPECT_TRUE(igi::AmbientAreaContainsListener(identity, origin, size, centre));

    // 1 metre short of the x face is inside; exactly on it is not.
    const double near_x[3] = {9.0 * 4096.0 - 1.0, 0.0, 0.0};
    EXPECT_TRUE(igi::AmbientAreaContainsListener(identity, origin, size, near_x));
    const double on_x[3] = {9.0 * 4096.0, 0.0, 0.0};
    EXPECT_FALSE(igi::AmbientAreaContainsListener(identity, origin, size, on_x));
    const double on_neg_x[3] = {-9.0 * 4096.0, 0.0, 0.0};
    EXPECT_FALSE(igi::AmbientAreaContainsListener(identity, origin, size, on_neg_x));
    const double on_y[3] = {0.0, 5.0 * 4096.0, 0.0};
    EXPECT_FALSE(igi::AmbientAreaContainsListener(identity, origin, size, on_y));
    const double on_z[3] = {0.0, 0.0, 4.0 * 4096.0};
    EXPECT_FALSE(igi::AmbientAreaContainsListener(identity, origin, size, on_z));
    const double past_z[3] = {0.0, 0.0, 4.0 * 4096.0 + 1.0};
    EXPECT_FALSE(igi::AmbientAreaContainsListener(identity, origin, size, past_z));

    // The box moves with its Position: the delta is listener - position.
    const double offset[3] = {100000.0, 0.0, 0.0};
    EXPECT_TRUE(igi::AmbientAreaContainsListener(identity, offset, size, offset));
    const double offset_near_x[3] = {100000.0 + 8.0 * 4096.0, 0.0, 0.0};
    EXPECT_TRUE(igi::AmbientAreaContainsListener(identity, offset, size, offset_near_x));
    const double offset_on_x[3] = {100000.0 + 9.0 * 4096.0, 0.0, 0.0};
    EXPECT_FALSE(igi::AmbientAreaContainsListener(identity, offset, size, offset_on_x));

    // The decomposed pair agrees with the end-to-end test away from the faces.
    double local[3];
    igi::AmbientAreaLocalPoint(identity, origin, near_x, local);
    EXPECT_TRUE(igi::AmbientAreaContains(size, local));
    EXPECT_DOUBLE_EQ(local[0], 9.0 * 4096.0 - 1.0);
}

TEST(SoundRulesTest, AmbientAreaContainmentUsesTheInverseRotation) {
    // ambientarea.c:134-140 rotates the world delta by the TRANSPOSE of the
    // authored matrix: lx = m[6]*dz + m[3]*dy + m[0]*dx.
    //
    // A box with half extents 10,1,1 turned 90 degrees about Z has its local x
    // axis along world +y, so a listener 9 m along world +y is inside.
    const double euler[3] = {0.0, 0.0, 1.57079632679489661923};
    float matrix[9];
    igi::MatrixFromEuler(euler, matrix);

    const double size[3] = {10.0, 1.0, 1.0};
    const double origin[3] = {0.0, 0.0, 0.0};
    const double along_world_y[3] = {0.0, 9.0 * 4096.0, 0.0};
    EXPECT_TRUE(igi::AmbientAreaContainsListener(matrix, origin, size, along_world_y));
    const double past_world_y[3] = {0.0, 11.0 * 4096.0, 0.0};
    EXPECT_FALSE(igi::AmbientAreaContainsListener(matrix, origin, size, past_world_y));
    // ... and 9 m along world +x is now outside, because that is the local y
    // axis and its half extent is 1 m.
    const double along_world_x[3] = {9.0 * 4096.0, 0.0, 0.0};
    EXPECT_FALSE(igi::AmbientAreaContainsListener(matrix, origin, size, along_world_x));
    const double short_world_x[3] = {0.5 * 4096.0, 0.0, 0.0};
    EXPECT_TRUE(igi::AmbientAreaContainsListener(matrix, origin, size, short_world_x));
}

TEST(SoundRulesTest, AmbientSoundPickRoundsAndCanLeaveTheList) {
    // ambientarea.c:164: round_int(Random_Float * count), with Random_Float in
    // [0,1) (random.c:22-26). round, not floor, so index `count` is reachable
    // and the last valid name is under-selected.
    EXPECT_EQ(igi::AmbientSoundIndexForRandom(0.0, 3), 0);
    EXPECT_EQ(igi::AmbientSoundIndexForRandom(0.1, 3), 0);   // 0.3 -> 0
    EXPECT_EQ(igi::AmbientSoundIndexForRandom(0.2, 3), 1);   // 0.6 -> 1
    EXPECT_EQ(igi::AmbientSoundIndexForRandom(0.5, 3), 2);   // 1.5 -> 2 (ties to even)
    EXPECT_EQ(igi::AmbientSoundIndexForRandom(0.6, 3), 2);   // 1.8 -> 2
    EXPECT_EQ(igi::AmbientSoundIndexForRandom(0.9, 3), 3);   // 2.7 -> 3: out of range
    EXPECT_EQ(igi::AmbientSoundIndexForRandom(0.9999, 3), 3);
    EXPECT_EQ(igi::AmbientSoundIndexForRandom(0.9999, 1), 1);
    // The engine then calls Sound_Play on an empty name and gets NULL
    // (soundsys.c:552), so the frame is simply silent.
    EXPECT_EQ(igi::AmbientSoundIndexForRandom(0.9999, 5), 5);
}

TEST(SoundRulesTest, AmbientDelayIsSecondsTimesThirty) {
    // ambientarea.c:120-123: round_int((Random_Float * randDelay + minDelay) * 30).
    EXPECT_EQ(igi::AmbientDelayFrames(0.0, 20.0, 20.0), 600);
    EXPECT_EQ(igi::AmbientDelayFrames(0.5, 20.0, 20.0), 900);
    EXPECT_EQ(igi::AmbientDelayFrames(0.9999, 20.0, 20.0), 1200);
    EXPECT_EQ(igi::AmbientDelayFrames(0.5, 0.0, 0.0), 0);
    // LEVEL1's "machinery room 02" authors 0 and 0, so it never waits.
    EXPECT_EQ(igi::AmbientDelayFrames(0.9, 0.0, 0.0), 0);
    // A negative delay counts up instead of down (ambientarea.c:176-177), which
    // is why the validator rejects it.
    EXPECT_LT(igi::AmbientDelayFrames(0.0, -1.0, 0.0), 0);
}

// ---------------------------------------------------------------------------
// AmbientArea_Run: the fade integrator
// ---------------------------------------------------------------------------

namespace {

igi::AmbientAreaResolved MakeArea(double sx, double sy, double sz, double min_delay,
                                  double random_wait, const std::string& sounds) {
    igi::AmbientAreaResolved area;
    area.authored.size[0] = sx;
    area.authored.size[1] = sy;
    area.authored.size[2] = sz;
    area.authored.min_delay = min_delay;
    area.authored.random_wait = random_wait;
    area.authored.sound_def = sounds;
    area.sounds = igi::SplitAmbientSoundList(sounds);
    area.matrix[0] = 1.0f;
    area.matrix[4] = 1.0f;
    area.matrix[8] = 1.0f;
    return area;
}

}  // namespace

TEST(SoundRulesTest, AmbientVolumeFadesInAndOutByFiveHundredthsPerFrame) {
    // ambientarea.c:53,132,144,147: kFadeStep = 0.05 per frame, so a 30 fps
    // level takes 20 frames to cross the full range.
    const igi::AmbientAreaResolved area = MakeArea(9.0, 5.0, 4.0, 0.0, 0.0, "machinery_01");
    const double inside[3] = {0.0, 0.0, 0.0};
    // Far outside every face: 1e6 units is 244 m, well past the 9 m half extent.
    const double outside[3] = {1e6, 0.0, 0.0};

    igi::AmbientAreaState state;
    EXPECT_DOUBLE_EQ(state.volume, 0.0);  // Create sets volume 0 (ambientarea.c:97)

    const igi::AmbientAreaTick first =
        igi::TickAmbientArea(state, area, inside, false, 0.0, 0.1, 0.0);
    EXPECT_TRUE(first.inside);
    EXPECT_DOUBLE_EQ(first.volume, 0.05);
    EXPECT_FALSE(first.stop_sound);  // volume is not 0, so no stop branch
    // The area has no sound yet and the delay is 0, so a sound starts at once.
    EXPECT_TRUE(first.start_sound);
    EXPECT_EQ(first.sound_index, 0);

    // 19 more inside frames reach full volume exactly (the clamp catches the
    // accumulated rounding).
    for (int i = 0; i < 19; ++i) {
        igi::TickAmbientArea(state, area, inside, false, 0.0, 0.1, 0.0);
    }
    EXPECT_DOUBLE_EQ(state.volume, 1.0);

    // Leaving the box fades out at the same rate; after 20 frames the volume is
    // clamped to exactly 0.0 and the stop branch re-rolls the delay.
    for (int i = 0; i < 19; ++i) {
        const igi::AmbientAreaTick tick =
            igi::TickAmbientArea(state, area, outside, false, 0.0, 0.1, 0.0);
        EXPECT_FALSE(tick.inside);
        EXPECT_GT(state.volume, 0.0);
    }
    const igi::AmbientAreaTick last =
        igi::TickAmbientArea(state, area, outside, false, 0.0, 0.1, 0.0);
    EXPECT_DOUBLE_EQ(last.volume, 0.0);
    EXPECT_FALSE(last.inside);

    // A 1/40th-of-the-range step: the fade is a linear ramp, not a curve.
    igi::AmbientAreaState ramp;
    igi::TickAmbientArea(ramp, area, inside, false, 0.0, 0.1, 0.0);
    igi::TickAmbientArea(ramp, area, inside, false, 0.0, 0.1, 0.0);
    EXPECT_DOUBLE_EQ(ramp.volume, 0.1);
    igi::TickAmbientArea(ramp, area, inside, false, 0.0, 0.1, 0.0);
    EXPECT_DOUBLE_EQ(ramp.volume, 0.15);
}

TEST(SoundRulesTest, AmbientSoundStopsAndRerollsItsDelayWhenSilent) {
    // ambientarea.c:156-158: `if (a->volume == 0.0 || (a->sound && !*a->sound))`
    // re-rolls the delay and calls stop_sound.
    const igi::AmbientAreaResolved area = MakeArea(9.0, 5.0, 4.0, 20.0, 20.0, "dog_01 dog_02");
    const double outside[3] = {1e9, 0.0, 0.0};

    igi::AmbientAreaState state;
    state.has_sound = true;  // a sound is playing
    state.volume = 0.0;
    const igi::AmbientAreaTick tick =
        igi::TickAmbientArea(state, area, outside, false, 0.0, 0.5, 0.5);
    EXPECT_TRUE(tick.stop_sound);
    EXPECT_FALSE(state.has_sound);
    // The branch re-rolls the delay and the tick's tail decrements it, so a
    // silent area always sits at one less than a fresh roll (ambientarea.c:157,
    // 176-177): round((0.5 * 20 + 20) * 30) == 900, then 899.
    EXPECT_EQ(state.delay, 899);
    igi::TickAmbientArea(state, area, outside, false, 0.0, 0.5, 0.5);
    // Still silent: the branch runs again, so the delay is re-rolled to 900 and
    // decremented to 899 once more. It can only count down while the area is
    // inside with a non-zero volume and no live sound.
    EXPECT_EQ(state.delay, 899);

    // A sound whose task died is treated the same way (ambientarea.c:156).
    igi::AmbientAreaState dead;
    dead.has_sound = true;
    dead.sound_alive = false;
    dead.volume = 1.0;
    const igi::AmbientAreaTick dead_tick =
        igi::TickAmbientArea(dead, area, outside, false, 0.0, 0.0, 0.0);
    EXPECT_TRUE(dead_tick.stop_sound);
    EXPECT_FALSE(dead.has_sound);
    // min delay 20, random 20, Random_Float 0 -> 600, decremented to 599.
    EXPECT_EQ(dead.delay, 599);
}

TEST(SoundRulesTest, AmbientDelayGatesTheStartAndThenCountsDown) {
    // ambientarea.c:163-174: the pick is only reached when the area is inside,
    // has no sound and volume != 0; the start is then gated on `!a->delay`.
    const igi::AmbientAreaResolved area = MakeArea(9.0, 5.0, 4.0, 20.0, 20.0, "dog_01 dog_02");
    const double inside[3] = {0.0, 0.0, 0.0};

    igi::AmbientAreaState state;
    state.volume = 1.0;
    state.delay = 3;
    for (int i = 0; i < 3; ++i) {
        const igi::AmbientAreaTick tick =
            igi::TickAmbientArea(state, area, inside, false, 0.0, 0.1, 0.0);
        EXPECT_FALSE(tick.start_sound);
    }
    EXPECT_EQ(state.delay, 0);
    const igi::AmbientAreaTick started =
        igi::TickAmbientArea(state, area, inside, false, 0.0, 0.1, 0.0);
    EXPECT_TRUE(started.start_sound);
    EXPECT_EQ(started.sound_index, 0);  // round(0.1 * 2) = 0
    // The host is expected to record that a sound is now live.
    state.has_sound = true;
    const igi::AmbientAreaTick playing =
        igi::TickAmbientArea(state, area, inside, false, 0.0, 0.1, 0.0);
    EXPECT_TRUE(playing.update_playing);
    EXPECT_FALSE(playing.start_sound);
    EXPECT_DOUBLE_EQ(playing.volume, 1.0);
}

TEST(SoundRulesTest, AmbientConditionBranchFadesOutButIsUnreachableInRetail) {
    // ambientarea.c:131-132 is the only reader of the `cond` field, and `cond`
    // is NOT in the ParamTable (ambientarea.c:73-79) nor authored by any of the
    // 162 retail instances, so VarString_HasExpr is always false. When it is
    // forced on, a false condition fades the volume out regardless of position.
    const igi::AmbientAreaResolved area = MakeArea(9.0, 5.0, 4.0, 0.0, 0.0, "machinery_01");
    const double inside[3] = {0.0, 0.0, 0.0};

    igi::AmbientAreaState state;
    state.volume = 1.0;
    const igi::AmbientAreaTick tick =
        igi::TickAmbientArea(state, area, inside, true, 0.0, 0.1, 0.0);
    EXPECT_DOUBLE_EQ(tick.volume, 0.95);
    EXPECT_FALSE(tick.inside);  // the containment test is skipped entirely

    // With the condition true the normal path resumes.
    const igi::AmbientAreaTick back =
        igi::TickAmbientArea(state, area, inside, true, 1.0, 0.1, 0.0);
    EXPECT_TRUE(back.inside);
    EXPECT_DOUBLE_EQ(back.volume, 1.0);  // 0.95 + 0.05, clamped
}

// ---------------------------------------------------------------------------
// ConditionalSound_Run: the condition edge
// ---------------------------------------------------------------------------

TEST(SoundRulesTest, ConditionalSoundIsARisingEdgeGate) {
    // condsound.c:121-136.
    igi::ConditionalSoundEmitter emitter;
    emitter.condition = "AlarmControl_98.isAlarm";
    emitter.sound_def = "alarmsystem_working";

    igi::ConditionalSoundState state;
    // The condition is false: no sound to stop, nothing plays.
    igi::ConditionalSoundTick tick = igi::TickConditionalSound(state, emitter, true, 0.0);
    EXPECT_FALSE(tick.start_sound);
    EXPECT_FALSE(tick.stop_sound);
    EXPECT_FALSE(tick.is_playing);

    // False -> true: play once.
    tick = igi::TickConditionalSound(state, emitter, true, 1.0);
    EXPECT_TRUE(tick.start_sound);
    EXPECT_FALSE(tick.stop_sound);
    EXPECT_TRUE(tick.is_playing);
    EXPECT_TRUE(state.has_sound);
    EXPECT_TRUE(state.played);

    // Still true: neither stop nor a second play.
    tick = igi::TickConditionalSound(state, emitter, true, 1.0);
    EXPECT_FALSE(tick.start_sound);
    EXPECT_FALSE(tick.stop_sound);
    EXPECT_TRUE(tick.is_playing);

    // True -> false: stop.
    tick = igi::TickConditionalSound(state, emitter, true, 0.0);
    EXPECT_TRUE(tick.stop_sound);
    EXPECT_FALSE(tick.start_sound);
    EXPECT_FALSE(tick.is_playing);
    EXPECT_FALSE(state.has_sound);

    // Still false: nothing left to stop, so Stop returns early (condsound.c:140).
    tick = igi::TickConditionalSound(state, emitter, true, 0.0);
    EXPECT_FALSE(tick.stop_sound);

    // Rising edge again.
    tick = igi::TickConditionalSound(state, emitter, true, 1.0);
    EXPECT_TRUE(tick.start_sound);
    EXPECT_TRUE(state.has_sound);

    // ConditionalSound_Reset (condsound.c:257-262) stops and clears both edges.
    igi::ResetConditionalSound(state);
    EXPECT_FALSE(state.has_sound);
    EXPECT_FALSE(state.is_playing);
    EXPECT_EQ(state.is_run, 0);
    EXPECT_EQ(state.is_last_run, 0);
    // With both edges cleared the next true tick is a rising edge again.
    tick = igi::TickConditionalSound(state, emitter, true, 1.0);
    EXPECT_TRUE(tick.start_sound);
}

TEST(SoundRulesTest, ConditionalSoundWithoutAConditionNeverRuns) {
    // condsound.c:123: without an expression VarString_HasExpr is false, so
    // isRun is never written and stays at its Create value of 0. The sound is
    // stopped every tick and never started, whatever the caller passes.
    igi::ConditionalSoundEmitter emitter;
    emitter.sound_def = "alarmsystem_working";

    igi::ConditionalSoundState state;
    for (int i = 0; i < 3; ++i) {
        const igi::ConditionalSoundTick tick =
            igi::TickConditionalSound(state, emitter, false, 1.0);
        EXPECT_FALSE(tick.start_sound);
        EXPECT_EQ(state.is_run, 0);
    }
    EXPECT_TRUE(igi::ValidateConditionalSound(emitter, nullptr).size() >= 1);
}

TEST(SoundRulesTest, OneShotPlaysOnceAndIsNeverStopped) {
    // condsound.c:128-129: `if (cs->once && cs->played) return;` sits BEFORE the
    // stop/play decision, so once a one-shot has played the condition is
    // ignored for the rest of the mission - including a false condition.
    igi::ConditionalSoundEmitter emitter;
    emitter.condition = "x";
    emitter.one_shot = true;

    igi::ConditionalSoundState state;
    igi::ConditionalSoundTick tick = igi::TickConditionalSound(state, emitter, true, 1.0);
    EXPECT_TRUE(tick.start_sound);
    EXPECT_TRUE(state.played);

    tick = igi::TickConditionalSound(state, emitter, true, 0.0);
    EXPECT_FALSE(tick.stop_sound);
    EXPECT_TRUE(state.has_sound);
    EXPECT_TRUE(tick.is_playing);

    tick = igi::TickConditionalSound(state, emitter, true, 1.0);
    EXPECT_FALSE(tick.start_sound);
    EXPECT_FALSE(tick.stop_sound);
}

TEST(SoundRulesTest, SimpleStreamsAreRequestedEveryFrameAndNeverStopped) {
    // condsound.c:155-165: `cs->stream` takes the SoundHandle path and leaves
    // cs->sound NULL, so ConditionalSound_Stop is a no-op for it. The stream
    // lasts one frame unless it is re-requested (soundsys.c:217-218), which is
    // what UpdatePlaying does (condsound.c:181-184).
    igi::ConditionalSoundEmitter emitter;
    emitter.condition = "x";
    emitter.simple = true;

    igi::ConditionalSoundState state;
    igi::ConditionalSoundTick tick = igi::TickConditionalSound(state, emitter, true, 1.0);
    EXPECT_TRUE(tick.start_sound);
    EXPECT_FALSE(state.has_sound);  // no positioned task
    EXPECT_TRUE(state.is_playing);

    tick = igi::TickConditionalSound(state, emitter, true, 1.0);
    EXPECT_TRUE(tick.request_stream);
    EXPECT_FALSE(tick.start_sound);
    EXPECT_FALSE(tick.stop_sound);

    // The condition drops. The stream is STILL re-requested: UpdatePlaying runs
    // before the condition test (condsound.c:126-127) and ConditionalSound_Stop
    // returns immediately because cs->sound is NULL (condsound.c:140-141), so
    // nothing ever clears isPlaying. A Simple emitter therefore outlives its
    // condition - the condition only gates the first start.
    tick = igi::TickConditionalSound(state, emitter, true, 0.0);
    EXPECT_FALSE(tick.stop_sound);
    EXPECT_TRUE(tick.request_stream);
    EXPECT_TRUE(tick.is_playing);

    // It ends only when the streamed voice itself ends, which the host reports
    // through stream_alive. Then the re-request stops and isPlaying clears.
    state.stream_alive = false;
    tick = igi::TickConditionalSound(state, emitter, true, 0.0);
    EXPECT_FALSE(tick.request_stream);
    EXPECT_FALSE(tick.is_playing);

    // A later rising edge starts it again.
    state.stream_alive = true;
    tick = igi::TickConditionalSound(state, emitter, true, 1.0);
    EXPECT_TRUE(tick.start_sound);
    EXPECT_FALSE(state.has_sound);
}

TEST(SoundRulesTest, RelativeToMicrophoneIsTheGlobalFlagNotALoopFlag) {
    // condsound.c:170-173: `if (cs->loop) Sound_SetGlobal(sound, 1); else
    // Sound_SetPos(sound, cs->params);`. The field is authored as "Relative to
    // microphone" (LEVEL1/objects.qsc:78) and its consumer is the
    // `!(v->flags & 4)` test in Voice_SetParams (voice.c:586), so it means
    // "heard at any distance", not "loop". Only the non-relative path uses
    // Position, which is why the cutscene stream emitters author 0,0,0.
    const std::vector<std::string> positional = {
        "-1", "ConditionalSound", "a", "cond", "snd", "1", "2", "3", "FALSE", "FALSE", "FALSE"};
    igi::ConditionalSoundEmitter emitter;
    ASSERT_TRUE(igi::ParseConditionalSound(positional, emitter));
    EXPECT_FALSE(emitter.relative_to_microphone);

    const std::vector<std::string> global = {
        "-1", "ConditionalSound", "a", "cond", "snd", "0", "0", "0", "FALSE", "FALSE", "TRUE"};
    ASSERT_TRUE(igi::ParseConditionalSound(global, emitter));
    EXPECT_TRUE(emitter.relative_to_microphone);

    EXPECT_EQ(igi::kVoiceFlagPositional, 0x2u);
    EXPECT_EQ(igi::kVoiceFlagGlobal, 0x4u);
    EXPECT_EQ(igi::kVoiceFlagLoop, 0x10u);
}

// ---------------------------------------------------------------------------
// Sound definitions
// ---------------------------------------------------------------------------

TEST(SoundRulesTest, SoundDefinitionRoundTripsItsThirteenArguments) {
    // sounddef.c:206-218: name, sample, x, y, z, volume, minDist, maxDist,
    // volumeIndex, pitchIndex, sampleIndex, unk124, loop.
    const std::vector<std::string> tokens = {"m1_heliby01", "M1_HELIBY01", "0",          "0",
                                            "0",          "0.800000012", "0",         "200",
                                            "-1",         "-1",          "-1",        "0",
                                            "FALSE"};
    igi::SoundDefinition definition;
    ASSERT_TRUE(igi::ParseSoundDefinition(tokens, definition));
    EXPECT_EQ(definition.name, "m1_heliby01");
    EXPECT_EQ(definition.sample, "M1_HELIBY01");
    EXPECT_FLOAT_EQ(static_cast<float>(definition.volume), 0.8f);
    EXPECT_FLOAT_EQ(static_cast<float>(definition.min_dist), 0.0f);
    EXPECT_FLOAT_EQ(static_cast<float>(definition.max_dist), 200.0f);
    EXPECT_EQ(definition.volume_index, -1);
    EXPECT_FALSE(definition.loop);

    EXPECT_FALSE(igi::ParseSoundDefinition({"only", "twelve", "args"}, definition));
}

TEST(SoundRulesTest, RegistryFirstMatchWinsLikeSoundDefSoundFind) {
    // sounddef.c:221-226 only accepts a match while `!a->found`, so the first
    // definition of a name wins - the order COMMON is loaded in (game.c:441).
    igi::SoundDefinition first;
    first.name = "machinery_01";
    first.volume = 0.35;
    first.max_dist = 25.0;
    igi::SoundDefinition second;
    second.name = "machinery_01";
    second.volume = 0.5;
    second.max_dist = 65.0;

    igi::SoundDefinitionRegistry registry;
    registry.Add(first);
    registry.Add(second);
    ASSERT_EQ(registry.size(), 2u);
    const igi::SoundDefinition* found = registry.Find("machinery_01");
    ASSERT_NE(found, nullptr);
    EXPECT_DOUBLE_EQ(found->max_dist, 25.0);
    EXPECT_EQ(registry.Find("nothing"), nullptr);
}

// ---------------------------------------------------------------------------
// Validation
// ---------------------------------------------------------------------------

TEST(SoundRulesTest, ValidationNamesTheEnginesOwnHazards) {
    // ConditionalSound: no condition and no definition.
    igi::ConditionalSoundEmitter emitter;
    std::vector<igi::SoundIssue> issues = igi::ValidateConditionalSound(emitter, nullptr);
    ASSERT_EQ(issues.size(), 2u);
    EXPECT_EQ(issues[0].kind, igi::SoundIssueKind::EmptyCondition);
    EXPECT_EQ(issues[1].kind, igi::SoundIssueKind::EmptySoundDef);
    EXPECT_STREQ(igi::SoundIssueKindName(issues[0].kind), "empty-condition");

    // A 32-byte name is at the Param_String32 boundary (paramtype.c:102-108).
    emitter.condition = "x";
    emitter.sound_def = std::string(31, 'a');
    EXPECT_TRUE(igi::ValidateConditionalSound(emitter, nullptr).empty());
    emitter.sound_def = std::string(32, 'a');
    issues = igi::ValidateConditionalSound(emitter, nullptr);
    ASSERT_EQ(issues.size(), 1u);
    EXPECT_EQ(issues[0].kind, igi::SoundIssueKind::NameTooLong);

    // AmbientArea: a ninth name overruns names[8] (ambientarea.c:38,212).
    igi::AmbientAreaResolved area;
    area.authored.sound_def = "a b c d e f g h i";
    area.sounds = igi::SplitAmbientSoundList(area.authored.sound_def);
    ASSERT_EQ(area.sounds.size(), 9u);
    issues = igi::ValidateAmbientArea(area, nullptr);
    bool saw_too_many = false;
    for (const igi::SoundIssue& issue : issues) {
        if (issue.kind == igi::SoundIssueKind::TooManySounds) saw_too_many = true;
    }
    EXPECT_TRUE(saw_too_many);

    // Eight is fine.
    area.authored.sound_def = "a b c d e f g h";
    area.sounds = igi::SplitAmbientSoundList(area.authored.sound_def);
    issues = igi::ValidateAmbientArea(area, nullptr);
    for (const igi::SoundIssue& issue : issues) {
        EXPECT_NE(issue.kind, igi::SoundIssueKind::TooManySounds);
    }

    // A 32-byte word overruns the 0x20-byte slot (ambientarea.c:209-210).
    igi::AmbientAreaResolved long_word;
    long_word.authored.sound_def = std::string(32, 'w');
    long_word.sounds = igi::SplitAmbientSoundList(long_word.authored.sound_def);
    issues = igi::ValidateAmbientArea(long_word, nullptr);
    bool saw_long_word = false;
    for (const igi::SoundIssue& issue : issues) {
        if (issue.kind == igi::SoundIssueKind::SoundWordTooLong) saw_long_word = true;
    }
    EXPECT_TRUE(saw_long_word);

    // The list itself overruns sounds[0x20] from 32 bytes on.
    igi::AmbientAreaResolved long_list;
    long_list.authored.sound_def = std::string(40, 'x');
    long_list.sounds = igi::SplitAmbientSoundList(long_list.authored.sound_def);
    issues = igi::ValidateAmbientArea(long_list, nullptr);
    bool saw_long_list = false;
    for (const igi::SoundIssue& issue : issues) {
        if (issue.kind == igi::SoundIssueKind::SoundListTooLong) saw_long_list = true;
    }
    EXPECT_TRUE(saw_long_list);

    // No names, a non-positive half extent and a negative delay.
    igi::AmbientAreaResolved bad;
    bad.authored.size[1] = 0.0;
    bad.authored.min_delay = -1.0;
    issues = igi::ValidateAmbientArea(bad, nullptr);
    std::vector<igi::SoundIssueKind> kinds;
    for (const igi::SoundIssue& issue : issues) kinds.push_back(issue.kind);
    EXPECT_NE(std::find(kinds.begin(), kinds.end(), igi::SoundIssueKind::NoSounds), kinds.end());
    EXPECT_NE(std::find(kinds.begin(), kinds.end(), igi::SoundIssueKind::NonPositiveSize),
              kinds.end());
    EXPECT_NE(std::find(kinds.begin(), kinds.end(), igi::SoundIssueKind::NegativeDelay),
              kinds.end());
}

// ---------------------------------------------------------------------------
// The retail corpus
// ---------------------------------------------------------------------------

namespace {

// Instances per level, pinned from the retail OBJECTS.QVM. These are the counts
// the QVM really contains, and for level1/LEVEL2/LEVEL4 they are also the counts
// of Task_New calls in the shipped objects.qsc.
struct LevelCounts {
    int level;
    int conditional_sounds;
    int ambient_areas;
};

const LevelCounts kRetailCounts[] = {
    {1, 21, 21},  {2, 25, 6},   {3, 33, 18},  {4, 18, 7},   {5, 18, 8},
    {6, 19, 17},  {7, 31, 9},   {8, 26, 9},   {9, 16, 13},  {10, 53, 15},
    {11, 19, 8},  {12, 46, 5},  {13, 16, 11}, {14, 43, 15},
};

constexpr int kTotalConditionalSounds = 384;
constexpr int kTotalAmbientAreas = 162;

// The retail levels that also ship a decompiled objects.qsc.
const int kLevelsWithText[] = {1, 2, 4};

}  // namespace

TEST(SoundRulesCorpus, RetailEmittersExtractParseAndValidate) {
    if (!CorpusPresent()) GTEST_SKIP() << "no retail corpus at " << GameRoot();

    int total_cs = 0;
    int total_aa = 0;
    int total_references = 0;
    int max_names = 0;
    int max_word_bytes = 0;
    int max_list_bytes = 0;
    std::vector<std::string> unresolved;

    for (const LevelCounts& counts : kRetailCounts) {
        const QVMFile qvm = LoadQvm(LevelDir(counts.level) / "OBJECTS.QVM");
        ASSERT_TRUE(qvm.valid) << "OBJECTS.QVM did not parse for level " << counts.level;

        const std::vector<Instance> cs = ExtractConditionalSounds(qvm);
        const std::vector<Instance> aa = ExtractAmbientAreas(qvm);
        EXPECT_EQ(static_cast<int>(cs.size()), counts.conditional_sounds)
            << "ConditionalSound count for level " << counts.level;
        EXPECT_EQ(static_cast<int>(aa.size()), counts.ambient_areas)
            << "AmbientArea count for level " << counts.level;
        total_cs += static_cast<int>(cs.size());
        total_aa += static_cast<int>(aa.size());

        const igi::SoundDefinitionRegistry registry = LoadRegistry(counts.level);
        ASSERT_GT(registry.size(), 0u) << "no sound definitions for level " << counts.level;

        for (const Instance& instance : cs) {
            igi::ConditionalSoundEmitter emitter;
            ASSERT_TRUE(igi::ParseConditionalSound(
                {"-1", "ConditionalSound", instance.name, instance.args[0], instance.args[1],
                 instance.args[2], instance.args[3], instance.args[4], instance.args[5],
                 instance.args[6], instance.args[7]},
                emitter))
                << instance.name;
            // Every retail ConditionalSound authors a condition and a definition.
            EXPECT_FALSE(emitter.condition.empty()) << instance.name;
            EXPECT_FALSE(emitter.sound_def.empty()) << instance.name;
            ++total_references;

            // The validator's unresolved-definition report must agree with a
            // direct registry lookup.
            int reported = 0;
            for (const igi::SoundIssue& issue :
                 igi::ValidateConditionalSound(emitter, &registry)) {
                if (issue.kind == igi::SoundIssueKind::UnresolvedSoundDef) ++reported;
            }
            if (!registry.Find(emitter.sound_def)) {
                unresolved.push_back("L" + std::to_string(counts.level) + ":" +
                                     emitter.sound_def);
                EXPECT_EQ(reported, 1) << instance.name;
            } else {
                EXPECT_EQ(reported, 0) << instance.name;
            }
        }

        for (const Instance& instance : aa) {
            igi::AmbientAreaResolved area;
            std::vector<std::string> tokens = {"-1", "AmbientArea", instance.name};
            tokens.insert(tokens.end(), instance.args.begin(), instance.args.end());
            ASSERT_TRUE(igi::ParseAmbientArea(tokens, area)) << instance.name;

            // Every retail AmbientArea is structurally safe: at most 5 names,
            // no word over 19 bytes, no list over 29 bytes, no zero or negative
            // half extent and no negative delay.
            max_names = std::max(max_names, static_cast<int>(area.sounds.size()));
            max_list_bytes =
                std::max(max_list_bytes, static_cast<int>(area.authored.sound_def.size()));
            EXPECT_LE(static_cast<int>(area.sounds.size()), igi::kAmbientSoundSlots)
                << instance.name;
            EXPECT_LT(static_cast<int>(area.authored.sound_def.size()),
                      igi::kAmbientSoundSlotCapacity)
                << instance.name;
            for (const std::string& word : area.sounds) {
                max_word_bytes = std::max(max_word_bytes, static_cast<int>(word.size()));
                EXPECT_LT(static_cast<int>(word.size()), igi::kAmbientSoundSlotCapacity)
                    << instance.name;
            }
            for (int axis = 0; axis < 3; ++axis) {
                EXPECT_GT(area.authored.size[axis], 0.0) << instance.name;
            }
            EXPECT_GE(area.authored.min_delay, 0.0) << instance.name;
            EXPECT_GE(area.authored.random_wait, 0.0) << instance.name;

            int expected_missing = 0;
            for (const std::string& word : area.sounds) {
                ++total_references;
                if (!registry.Find(word)) {
                    unresolved.push_back("L" + std::to_string(counts.level) + ":" + word);
                    ++expected_missing;
                }
            }
            int reported = 0;
            for (const igi::SoundIssue& issue : igi::ValidateAmbientArea(area, &registry)) {
                if (issue.kind == igi::SoundIssueKind::UnresolvedSoundDef) ++reported;
            }
            EXPECT_EQ(reported, expected_missing) << instance.name;
        }
    }

    EXPECT_EQ(total_cs, kTotalConditionalSounds);
    EXPECT_EQ(total_aa, kTotalAmbientAreas);
    // 384 ConditionalSound definitions plus the 260 names spread over the 162
    // AmbientArea lists.
    EXPECT_EQ(total_references, 644);
    // The observed maxima across all 162 areas: "thunder_1 thunder_2 thunder_3"
    // is the longest list and "alarmsystem_working" the longest word. All three
    // sit inside the engine's unchecked limits (names[8], char[0x20] slots,
    // sounds[0x20]), which is why retail never corrupts itself.
    EXPECT_EQ(max_names, 5);
    EXPECT_EQ(max_word_bytes, 19);
    EXPECT_EQ(max_list_bytes, 29);

    // Exactly 14 of the 644 authored sound references dangle. Every one of them
    // is a real authoring defect in the retail data - a typo (hammer_1 for
    // hammer_01, wind_fx_1 for wind_fx_01), a plural slip (dogs_01 for dog_01)
    // or a definition that was never written (detected_hear, helicopter_cut) -
    // and Sound_Play returns NULL for each, so the emitter is silent.
    std::vector<std::string> expected_unresolved = {
        "L3:dogs_01",   "L3:dogs_02",        "L3:dogs_03",        "L6:detected_hear",
        "L8:gondola_fx_01", "L8:gondola_fx_06", "L10:anya_test_01", "L10:m10o_heliblfd",
        "L10:hammer_1", "L10:car_1",         "L11:wind_fx_1",     "L11:wind_fx_2",
        "L12:helicopter_cut", "L12:helicopter_cut",
    };
    std::sort(unresolved.begin(), unresolved.end());
    std::sort(expected_unresolved.begin(), expected_unresolved.end());
    EXPECT_EQ(unresolved, expected_unresolved);
}

// One authored value, normalised so that a QVM float and its .qsc decimal text
// compare equal: numbers go through float, everything else stays verbatim.
//
// The comparison is at the authored format's precision -- 7 significant digits,
// which is what ArgTokenFromArg re-serialises with "to match the source format"
// (source/level/fog_qsc.h:52) -- not at %.9g.  The two installed artifacts are
// not bit-identical: level 1's "wind behind" half extent is 32.158935546875
// (float 0x4200A2C0) at objects.qsc:1184 but 0x4200A2C1 = 32.158939361572266
// at OBJECTS.QVM+166087, one ULP apart.  At authored precision the two agree; a
// wrong token or argument position still changes the value by far more than the
// seventh digit.
std::string CanonicalArg(const std::string& token) {
    char* end = nullptr;
    const float value = std::strtof(token.c_str(), &end);
    if (end != token.c_str() && *end == '\0') {
        char buffer[64];
        std::snprintf(buffer, sizeof(buffer), "%.7g", static_cast<double>(value));
        return buffer;
    }
    return token;
}

std::string CanonicalCall(const std::string& name, const std::vector<std::string>& args) {
    std::string key = name;
    for (const std::string& arg : args) {
        key.push_back('\x1f');
        key += CanonicalArg(arg);
    }
    return key;
}

TEST(SoundRulesCorpus, QvmAndQscAgreeOnTheAuthoredCalls) {
    if (!CorpusPresent()) GTEST_SKIP() << "no retail corpus at " << GameRoot();

    for (int level : kLevelsWithText) {
        const fs::path dir = LevelDir(level);
        const std::string text = ReadFile(dir / "objects.qsc");
        if (text.empty()) GTEST_SKIP() << "no objects.qsc for level " << level;

        const QVMFile qvm = LoadQvm(dir / "OBJECTS.QVM");
        ASSERT_TRUE(qvm.valid);

        // ConditionalSound instance names repeat inside a level ("base amb" is
        // authored twice in level1), so the two sources are compared as a sorted
        // multiset of name-plus-arguments rather than matched by name.
        for (const std::pair<const char*, std::size_t> types :
             {std::make_pair("ConditionalSound", std::size_t{8}),
              std::make_pair("AmbientArea", std::size_t{12})}) {
            const std::vector<std::vector<std::string>> from_text =
                ParseTaskNewCalls(text, types.first);
            const std::vector<Instance> from_qvm =
                std::strcmp(types.first, "ConditionalSound") == 0
                    ? ExtractConditionalSounds(qvm)
                    : ExtractAmbientAreas(qvm);

            std::vector<std::string> text_keys;
            for (const std::vector<std::string>& call : from_text) {
                ASSERT_GE(call.size(), 3 + types.second) << types.first << " " << call[2];
                text_keys.push_back(CanonicalCall(
                    call[2], std::vector<std::string>(call.begin() + 3,
                                                      call.begin() + 3 + types.second)));
            }
            std::vector<std::string> qvm_keys;
            for (const Instance& instance : from_qvm) {
                qvm_keys.push_back(CanonicalCall(instance.name, instance.args));
            }

            std::sort(text_keys.begin(), text_keys.end());
            std::sort(qvm_keys.begin(), qvm_keys.end());
            EXPECT_EQ(text_keys, qvm_keys) << types.first << " in level " << level;
        }
    }
}

TEST(SoundRulesCorpus, Level1AmbientAreasCarryTheirAuthoredGeometry) {
    if (!CorpusPresent()) GTEST_SKIP() << "no retail corpus at " << GameRoot();

    const QVMFile qvm = LoadQvm(LevelDir(1) / "OBJECTS.QVM");
    ASSERT_TRUE(qvm.valid);
    const std::vector<Instance> instances = ExtractAmbientAreas(qvm);
    ASSERT_EQ(instances.size(), 21u);

    std::vector<igi::AmbientAreaResolved> areas;
    for (const Instance& instance : instances) {
        igi::AmbientAreaResolved area;
        std::vector<std::string> tokens = {"-1", "AmbientArea", instance.name};
        tokens.insert(tokens.end(), instance.args.begin(), instance.args.end());
        ASSERT_TRUE(igi::ParseAmbientArea(tokens, area));
        area.authored_name = instance.name;
        areas.push_back(std::move(area));
    }

    // LEVEL1/objects.qsc:1183-1189.
    const igi::AmbientAreaResolved* machinery = FindArea(areas, "machinery room 02");
    ASSERT_NE(machinery, nullptr);
    EXPECT_DOUBLE_EQ(machinery->authored.position[0], 24689856.0);
    EXPECT_DOUBLE_EQ(machinery->authored.position[1], -56311688.0);
    EXPECT_DOUBLE_EQ(machinery->authored.position[2], 174413136.0);
    EXPECT_DOUBLE_EQ(machinery->authored.size[0], 9.0);
    EXPECT_DOUBLE_EQ(machinery->authored.size[1], 5.0);
    EXPECT_DOUBLE_EQ(machinery->authored.size[2], 4.0);
    EXPECT_DOUBLE_EQ(machinery->authored.min_delay, 0.0);
    EXPECT_DOUBLE_EQ(machinery->authored.random_wait, 0.0);
    ASSERT_EQ(machinery->sounds.size(), 1u);
    EXPECT_EQ(machinery->sounds[0], "machinery_01");

    // The 9 x 5 x 4 half extents are an 18 x 10 x 8 metre box, so a listener at
    // the centre is inside and one 10 m up is not.
    const float identity[9] = {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    EXPECT_TRUE(igi::AmbientAreaContainsListener(identity, machinery->authored.position,
                                                 machinery->authored.size,
                                                 machinery->authored.position));
    const double ten_metres_up[3] = {machinery->authored.position[0],
                                     machinery->authored.position[1],
                                     machinery->authored.position[2] + 10.0 * 4096.0};
    EXPECT_FALSE(igi::AmbientAreaContainsListener(identity, machinery->authored.position,
                                                  machinery->authored.size, ten_metres_up));

    // A wide wind area with a non-integral half extent (LEVEL1/objects.qsc:1184).
    const igi::AmbientAreaResolved* wind = FindArea(areas, "wind behind");
    ASSERT_NE(wind, nullptr);
    EXPECT_FLOAT_EQ(static_cast<float>(wind->authored.size[0]), 32.1589355f);
    EXPECT_DOUBLE_EQ(wind->authored.size[1], 100.0);
    EXPECT_DOUBLE_EQ(wind->authored.size[2], 20.0);
    EXPECT_EQ(wind->sounds[0], "wind_1");

    // The only level1 area that uses the full eight-slot budget is nowhere near
    // it: the maximum is three names.
    std::size_t max_names = 0;
    for (const igi::AmbientAreaResolved& area : areas) {
        max_names = std::max(max_names, area.sounds.size());
    }
    EXPECT_EQ(max_names, 3u);
}

TEST(SoundRulesCorpus, Level1And2And4ConditionalSoundsCarryTheirConditions) {
    if (!CorpusPresent()) GTEST_SKIP() << "no retail corpus at " << GameRoot();

    // LEVEL1/objects.qsc:562-563.
    {
        const QVMFile qvm = LoadQvm(LevelDir(1) / "OBJECTS.QVM");
        ASSERT_TRUE(qvm.valid);
        const std::vector<Instance> instances = ExtractConditionalSounds(qvm);
        ASSERT_EQ(instances.size(), 21u);

        bool saw_alarm = false;
        bool saw_global_cutscene = false;
        for (const Instance& instance : instances) {
            igi::ConditionalSoundEmitter emitter;
            std::vector<std::string> tokens = {"-1", "ConditionalSound", instance.name};
            tokens.insert(tokens.end(), instance.args.begin(), instance.args.end());
            ASSERT_TRUE(igi::ParseConditionalSound(tokens, emitter));

            if (emitter.sound_def == "alarmsystem_working") {
                // Two level1 instances use this definition; the second one is at
                // a different position but shares the condition and the flags.
                EXPECT_EQ(emitter.condition, "AlarmControl_98.isAlarm");
                EXPECT_FALSE(emitter.relative_to_microphone);
                EXPECT_FALSE(emitter.simple);
                EXPECT_FALSE(emitter.one_shot);
                if (emitter.position[0] == 24461340.0) {
                    saw_alarm = true;
                    EXPECT_DOUBLE_EQ(emitter.position[1], -56116196.0);
                    EXPECT_DOUBLE_EQ(emitter.position[2], 174456944.0);
                }
            }
            if (emitter.relative_to_microphone) {
                saw_global_cutscene = true;
                // The global path never reads Position (condsound.c:170-173), and
                // every retail instance that sets it authors 0,0,0.
                EXPECT_DOUBLE_EQ(emitter.position[0], 0.0);
                EXPECT_DOUBLE_EQ(emitter.position[1], 0.0);
                EXPECT_DOUBLE_EQ(emitter.position[2], 0.0);
                EXPECT_FALSE(emitter.simple);
            }
        }
        EXPECT_TRUE(saw_alarm);
        EXPECT_TRUE(saw_global_cutscene);
    }

    // LEVEL2/objects.qsc:979 and LEVEL4/objects.qsc:554,651-654.
    for (int level : {2, 4}) {
        const QVMFile qvm = LoadQvm(LevelDir(level) / "OBJECTS.QVM");
        ASSERT_TRUE(qvm.valid);
        const std::vector<Instance> instances = ExtractConditionalSounds(qvm);
        EXPECT_GT(instances.size(), 0u);
        for (const Instance& instance : instances) {
            igi::ConditionalSoundEmitter emitter;
            std::vector<std::string> tokens = {"-1", "ConditionalSound", instance.name};
            tokens.insert(tokens.end(), instance.args.begin(), instance.args.end());
            ASSERT_TRUE(igi::ParseConditionalSound(tokens, emitter));
            EXPECT_FALSE(emitter.condition.empty());
            EXPECT_FALSE(emitter.sound_def.empty());
        }
    }
}

TEST(SoundRulesCorpus, RetailSoundDefinitionsGiveTheRealAudibleRadii) {
    if (!CorpusPresent()) GTEST_SKIP() << "no retail corpus at " << GameRoot();

    // COMMON is loaded before the mission (game.c:441-442) and first-match wins
    // (sounddef.c:221-226), so COMMON's machinery_01 shadows the mission's.
    igi::SoundDefinitionRegistry registry;
    const QVMFile common = LoadQvm(GameRoot() / "COMMON" / "SOUNDS" / "SOUNDS.QVM");
    ASSERT_TRUE(common.valid);
    for (const std::vector<std::string>& args : ExtractSoundDefinitions(common)) {
        igi::SoundDefinition definition;
        ASSERT_TRUE(igi::ParseSoundDefinition(args, definition));
        registry.Add(definition);
    }
    const QVMFile mission = LoadQvm(LevelDir(1) / "SOUNDS" / "SOUNDS.QVM");
    ASSERT_TRUE(mission.valid);
    for (const std::vector<std::string>& args : ExtractSoundDefinitions(mission)) {
        igi::SoundDefinition definition;
        ASSERT_TRUE(igi::ParseSoundDefinition(args, definition));
        registry.Add(definition);
    }

    // COMMON/SOUNDS/SOUNDS.QVM:338 and :360 both define machinery_01; the first
    // one is what Sound_Play finds.
    const igi::SoundDefinition* machinery = registry.Find("machinery_01");
    ASSERT_NE(machinery, nullptr);
    EXPECT_FLOAT_EQ(static_cast<float>(machinery->volume), 0.35f);
    EXPECT_FLOAT_EQ(static_cast<float>(machinery->min_dist), 0.0f);
    EXPECT_FLOAT_EQ(static_cast<float>(machinery->max_dist), 25.0f);
    EXPECT_TRUE(machinery->loop);

    // COMMON:271 wind_1 has a 1000 m wide fade band that starts at 10 km.
    const igi::SoundDefinition* wind = registry.Find("wind_1");
    ASSERT_NE(wind, nullptr);
    EXPECT_FLOAT_EQ(static_cast<float>(wind->min_dist), 10000.0f);
    EXPECT_FLOAT_EQ(static_cast<float>(wind->max_dist), 11000.0f);
    EXPECT_FLOAT_EQ(static_cast<float>(wind->volume), 0.75f);
    EXPECT_TRUE(wind->loop);

    // The short-radius definitions that AmbientArea depends on: 1 metre.
    for (const char* name : {"heli_by_01", "dog_01", "wind_fx_01", "ventilation_01"}) {
        const igi::SoundDefinition* definition = registry.Find(name);
        ASSERT_NE(definition, nullptr) << name;
        EXPECT_FLOAT_EQ(static_cast<float>(definition->max_dist), 1.0f) << name;
    }

    // level1's own definitions.
    const igi::SoundDefinition* heligen = registry.Find("m1_heligen01");
    ASSERT_NE(heligen, nullptr);
    EXPECT_FLOAT_EQ(static_cast<float>(heligen->max_dist), 1000.0f);
    EXPECT_FLOAT_EQ(static_cast<float>(heligen->volume), 0.5f);
    EXPECT_TRUE(heligen->loop);

    const igi::SoundDefinition* heliby = registry.Find("m1_heliby01");
    ASSERT_NE(heliby, nullptr);
    EXPECT_FLOAT_EQ(static_cast<float>(heliby->max_dist), 200.0f);
    EXPECT_FLOAT_EQ(static_cast<float>(heliby->volume), 0.8f);
    EXPECT_FALSE(heliby->loop);

    // The audible radius really is the definition's, not the emitter's: the
    // machinery_01 emitter in level1 has a 9 x 5 x 4 box and the definition
    // reaches 25 m, while the emitter's own Position plays no part in the
    // falloff at all (ambientarea.c:161,170 puts the sound on the listener).
    EXPECT_TRUE(igi::SoundWithinAudibleRadius(25.0 * 4096.0, machinery->max_dist));
    EXPECT_FALSE(igi::SoundWithinAudibleRadius(25.0 * 4096.0 + 1.0, machinery->max_dist));
    // Halfway through the 25 m band the level is exactly half the authored
    // volume. The authored volume is the float 0.349999994, so the product is
    // 0.174999997, not the double 0.175.
    const float machinery_volume = static_cast<float>(machinery->volume);
    EXPECT_FLOAT_EQ(static_cast<float>(igi::SoundAttenuatedVolume(
                        12.5 * 4096.0, machinery->min_dist, machinery->max_dist,
                        machinery->volume)),
                    machinery_volume * 0.5f);
}

TEST(SoundRulesCorpus, EveryRetailSoundDefinitionParses) {
    if (!CorpusPresent()) GTEST_SKIP() << "no retail corpus at " << GameRoot();

    int definitions = 0;
    std::vector<std::string> anomalies;
    std::vector<fs::path> files{GameRoot() / "COMMON" / "SOUNDS" / "SOUNDS.QVM"};
    for (int level = 1; level <= 14; ++level) {
        files.push_back(LevelDir(level) / "SOUNDS" / "SOUNDS.QVM");
    }
    for (const fs::path& path : files) {
        const QVMFile qvm = LoadQvm(path);
        if (!qvm.valid) continue;
        for (const std::vector<std::string>& args : ExtractSoundDefinitions(qvm)) {
            igi::SoundDefinition definition;
            ASSERT_TRUE(igi::ParseSoundDefinition(args, definition)) << path;
            // Seven of the 722 retail definitions are degenerate: three
            // empty-named RADARWIND_1/RADARWIND_1/RADARENGINE_1 entries in
            // COMMON (repeated verbatim in LEVEL8) and one COMMON entry,
            // alarmsystem_destroyed, with an empty sample. Both are
            // unreachable: Sound_Play rejects an empty name
            // (src/sound/soundsys.c:552) and an empty sample cannot be
            // loaded. Everything else is well-formed.
            if (definition.name.empty() || definition.sample.empty()) {
                anomalies.push_back(path.parent_path().parent_path().filename().string() + ":" +
                                    definition.name + ":" + definition.sample);
            } else {
                EXPECT_GE(definition.max_dist, 0.0);
                EXPECT_GE(definition.min_dist, 0.0);
            }
            ++definitions;
        }
    }
    // COMMON ships 361 definitions and the 14 missions add 361 more.
    EXPECT_EQ(definitions, 722);

    std::vector<std::string> expected = {
        "COMMON::RADARWIND_1",     "COMMON::RADARWIND_1", "COMMON::RADARENGINE_1",
        "COMMON:alarmsystem_destroyed:", "LEVEL8::RADARWIND_1", "LEVEL8::RADARWIND_1",
        "LEVEL8::RADARENGINE_1",
    };
    std::sort(anomalies.begin(), anomalies.end());
    std::sort(expected.begin(), expected.end());
    EXPECT_EQ(anomalies, expected);
}
