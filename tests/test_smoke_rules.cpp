// test_smoke_rules.cpp - the retail "Smoke" emitter, pure-logic port (#43).
//
// The oracle for every number here is IGI-RE (read-only). Key citations:
//   fxobj/smoke.c:125-193      Smoke_Init 0x437510: the 21-parameter ParamTable,
//                              the ranges, the four sprites
//   fxobj/smoke.c:212-247      Smoke_Create 0x437B90: the defaults, and
//                              Random_Init(gSmokeRandom)
//   fxobj/smoke.c:298-338      Smoke_Run 0x437DB0: the accumulator and the
//                              per-particle emission
//   fxobj/smoke.c:344-409      Smoke_Changed 0x438000: direction, blend flags,
//                              sprite choice, Particle2D_New arguments
//   graphtask/particle.c:540-634  Particle2D_Add/Emit: what `frames`, `delay`
//                              and the speed arguments mean
//   math/random.c:5-24         Random_Init 0x4B4720, Random_Next 0x4B4740,
//                              Random_Float 0x4B4770
//   math/matrix.c              Matrix_RotateX/RotateZ for the emitter direction
//   physobj/paramops.c:180-186,:423-435  the argument counts per script type
//
// The corpus half reads the retail OBJECTS.QVM with the editor's own QVM_Parse,
// checks the authored declaration against SmokeParamTable(), and cross-checks
// LEVEL1 and LEVEL2 -- the two levels that also ship a decompiled objects.qsc --
// against that human-readable text.

#include "../source/level/qvm_parser.h"
#include "../source/runtime/smoke.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
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

fs::path LevelDir(int level) {
    return GameRoot() / "MISSIONS" / "location0" / ("LEVEL" + std::to_string(level));
}

// ---------------------------------------------------------------------------
// QVM bytecode -> Smoke arg tokens
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

// Walks one authored Smoke call and returns its arguments in the editor's
// argTokens layout: [0]=task id, [1]=type, [2]=name, [3]=parameter 0.
//
// The declaration call (`Task_DeclareParameters("Smoke", ...)`) also pushes the
// type string, so the two are told apart structurally: after the type comes the
// instance NAME string, and after that a declaration has another parameter-name
// string while an instance has a value.
std::vector<std::string> ReadSmokeCall(const QVMFile& qvm, std::size_t type_index) {
    std::vector<std::string> tokens;
    tokens.push_back("-1");   // the task id the engine assigns at create time
    tokens.push_back("Smoke");  // type

    const std::size_t kExpected =
        2 + 1 + static_cast<std::size_t>(igi::kSmokeTokenCount);

    for (std::size_t i = type_index + 1; i < qvm.instructions.size() && tokens.size() < kExpected; ++i) {
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

// Every authored Smoke in the file, in file order.
std::vector<std::vector<std::string>> ExtractSmokeCalls(const QVMFile& qvm) {
    std::vector<std::vector<std::string>> out;
    for (std::size_t i = 0; i < qvm.instructions.size(); ++i) {
        const QVMInstruction& in = qvm.instructions[i];
        if (!IsStringPush(in.type)) continue;
        if (in.operand >= qvm.strings.size()) continue;
        if (qvm.strings[in.operand] != "Smoke") continue;

        std::size_t name = i + 1;
        while (name < qvm.instructions.size() && qvm.instructions[name].type == QVMOpType::BRK) ++name;
        if (name >= qvm.instructions.size() || !IsStringPush(qvm.instructions[name].type)) continue;

        std::size_t first = name + 1;
        while (first < qvm.instructions.size() && qvm.instructions[first].type == QVMOpType::BRK) ++first;
        if (first >= qvm.instructions.size() || IsStringPush(qvm.instructions[first].type)) continue;

        out.push_back(ReadSmokeCall(qvm, i));
    }
    return out;
}

// The parameters a `Task_DeclareParameters("Smoke", ...)` call declares, in
// order, as alternating name/type strings (the type name is dropped).
std::vector<std::string> DeclaredSmokeParameters(const QVMFile& qvm) {
    std::vector<std::string> out;
    for (std::size_t i = 0; i < qvm.instructions.size(); ++i) {
        const QVMInstruction& in = qvm.instructions[i];
        if (!IsIdentifierPush(in.type)) continue;
        if (in.operand >= qvm.identifiers.size()) continue;
        if (qvm.identifiers[in.operand] != "Task_DeclareParameters") continue;

        // CALL, then BRA over the argument block, then the strings, then POP.
        std::size_t j = i + 1;
        if (j < qvm.instructions.size() && qvm.instructions[j].type == QVMOpType::CALL) ++j;
        if (j < qvm.instructions.size() && qvm.instructions[j].type == QVMOpType::BRA) ++j;
        std::vector<std::string> pushed;
        for (; j < qvm.instructions.size() && qvm.instructions[j].type != QVMOpType::POP; ++j) {
            if (!IsStringPush(qvm.instructions[j].type)) continue;
            const std::size_t operand = qvm.instructions[j].operand;
            pushed.push_back(operand < qvm.strings.size() ? qvm.strings[operand] : std::string());
        }
        if (pushed.size() >= 3 && pushed[0] == "Smoke") {
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
// parent's argument list (see LEVEL2 objects.qsc:1042-1046), and expressions in
// the arguments contain their own parentheses and commas inside string literals,
// so the scan balances parens and skips over quoted strings.
std::vector<std::string> ReadCallArguments(const std::string& text, std::size_t open, std::size_t* end) {
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

    // Split the top-level argument list, again respecting nesting and strings.
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

// Every `Task_New(-1, "Smoke", "", ...)` in a decompiled objects.qsc, including
// the ones nested inside a parent container's argument list.
std::vector<std::vector<std::string>> ParseSmokeCallsFromText(const std::string& text) {
    std::vector<std::vector<std::string>> out;
    const std::string needle = "Task_New(";
    std::size_t pos = 0;
    while ((pos = text.find(needle, pos)) != std::string::npos) {
        std::size_t end = 0;
        const std::vector<std::string> args = ReadCallArguments(text, pos + needle.size() - 1, &end);
        if (args.size() >= 3 && Unquote(args[1]) == "Smoke") {
            std::vector<std::string> tokens;
            for (const std::string& arg : args) tokens.push_back(Unquote(arg));
            out.push_back(tokens);
        }
        // Step past this "Task_New(" only, so a call nested in the arguments of
        // a container is still visited.
        pos += needle.size();
    }
    return out;
}

std::vector<std::string> ParseDeclaredSmokeParametersFromText(const std::string& text) {
    const std::string needle = "Task_DeclareParameters(";
    std::size_t pos = 0;
    while ((pos = text.find(needle, pos)) != std::string::npos) {
        std::size_t end = 0;
        const std::vector<std::string> args = ReadCallArguments(text, pos + needle.size() - 1, &end);
        if (args.size() >= 3 && Unquote(args[0]) == "Smoke") {
            std::vector<std::string> out;
            for (std::size_t i = 1; i < args.size(); ++i) out.push_back(Unquote(args[i]));
            return out;
        }
        pos = end > pos ? end : pos + needle.size();
    }
    return {};
}

// ---------------------------------------------------------------------------

igi::SmokeParams SampleParams() {
    // The LEVEL2:1045 authored instance, which exercises every field.
    igi::SmokeParams params;
    params.origin[0] = 126034480.0;
    params.origin[1] = -38145376.0;
    params.origin[2] = 175416512.0;
    params.alpha = 0.0f;
    params.gamma = 0.0f;
    params.count = 100;
    params.radius = 5.0f;
    params.spread = 0.78539800643920898f;
    params.min_velocity = 5.5f;
    params.max_velocity = 7.0f;
    params.colour[0] = 0.30769199132919312f;
    params.colour[1] = 0.17307700216770172f;
    params.colour[2] = 0.0f;
    params.life_time = 1.5f;
    params.fade_time = 1.3999999761581421f;
    params.fade_mode = 0;
    params.sprite_index = 3;
    params.particle_size = 0.5f;
    params.particle_size_delta = 2.6666669845581055f;
    params.min_rotation = 0.0f;
    params.max_rotation = 2.0f;
    params.intensity = 0.05f;
    params.generate_factor = 30.0;
    params.generate_expr.clear();
    params.move_particles = false;
    return params;
}

}  // namespace

// ===========================================================================
// The authored parameter table
// ===========================================================================

TEST(SmokeRules, ParamTableShapeMatchesTheRetailDeclaration) {
    // smoke.c:166 ParamTable_New(21); the argument counts are the script types'
    // (paramops.c:180 RGB -> 3, :182 ObjectPos -> 3, :355-359 the one-argument
    // types), which makes the authored list 25 tokens.
    const igi::SmokeParamDesc* table = igi::SmokeParamTable();
    ASSERT_NE(table, nullptr);

    int tokens = 0;
    int three_token_params = 0;
    for (int i = 0; i < igi::kSmokeParamCount; ++i) {
        tokens += table[i].token_count;
        if (table[i].token_count == 3) ++three_token_params;
    }
    EXPECT_EQ(tokens, igi::kSmokeTokenCount);
    EXPECT_EQ(three_token_params, 2);  // ObjectPos and RGB, and only those
    EXPECT_EQ(igi::kSmokeFirstParam, 3u);  // [0]=id, [1]=type, [2]=name
}

TEST(SmokeRules, ParamTableNamesAreTheRetailDeclaration) {
    // The authored names and types, in ParamTable_Add order (smoke.c:167-187),
    // exactly as the retail declaration prints them.
    const igi::SmokeParamDesc* table = igi::SmokeParamTable();
    const char* kNames[igi::kSmokeParamCount] = {
        "Position", "Alpha", "Gamma", "Number of Particles", "Radius",
        "Maximum Random Angle", "Minimum Velocity", "Maximum Velocity", "Colour",
        "Life Time", "Fade Time", "Fade Mode", "Sprite index", "Particle Size",
        "Particle Size Delta", "Minimum Rotation Speed", "Maximum Rotation Speed",
        "Intensity", "Initial generate factor value", "Generate factor",
        "Move Particles",
    };
    const char* kTypes[igi::kSmokeParamCount] = {
        "ObjectPos", "Angle", "Angle", "Int32", "Real32", "Angle", "Real32",
        "Real32", "RGB", "Real32", "Real32", "Int32", "Int32", "Real32",
        "Real32", "Angle", "Angle", "Real32", "Real64", "VarString", "bool8",
    };
    for (int i = 0; i < igi::kSmokeParamCount; ++i) {
        EXPECT_STREQ(table[i].authored_name, kNames[i]) << "slot " << i;
        EXPECT_STREQ(table[i].type_name, kTypes[i]) << "slot " << i;
    }
}

TEST(SmokeRules, ParamTableRangesAreSmokeInit) {
    // smoke.c:131-154, and which slots get a range at all (smoke.c:167-187 pass
    // NULL for ObjectPos, the spread, the colour, the two rotation speeds, the
    // expression and the bool).
    const igi::SmokeParamDesc* table = igi::SmokeParamTable();
    EXPECT_FLOAT_EQ(table[igi::kSmokeAlpha].range_min, 0.0f);
    EXPECT_FLOAT_EQ(table[igi::kSmokeAlpha].range_max, 3.1415927410125732f);
    EXPECT_FLOAT_EQ(table[igi::kSmokeGamma].range_min, -3.1415927410125732f);
    EXPECT_EQ(table[igi::kSmokeCount].range_min, 1.0f);
    EXPECT_EQ(table[igi::kSmokeCount].range_max, 255.0f);
    EXPECT_FLOAT_EQ(table[igi::kSmokeRadius].range_max, 5.0f);
    EXPECT_FLOAT_EQ(table[igi::kSmokeMinVelocity].range_max, 14.6484375f);
    EXPECT_FLOAT_EQ(table[igi::kSmokeMaxVelocity].range_max, 14.6484375f);
    EXPECT_FLOAT_EQ(table[igi::kSmokeLifeTime].range_max, 4.0f);
    EXPECT_FLOAT_EQ(table[igi::kSmokeFadeTime].range_max, 4.0f);
    EXPECT_FLOAT_EQ(table[igi::kSmokeFadeMode].range_max, 2.0f);
    EXPECT_FLOAT_EQ(table[igi::kSmokeSpriteIndex].range_max, 3.0f);
    EXPECT_FLOAT_EQ(table[igi::kSmokeParticleSize].range_max, 5.0f);
    EXPECT_FLOAT_EQ(table[igi::kSmokeParticleSizeDelta].range_min, -150.0f);
    EXPECT_FLOAT_EQ(table[igi::kSmokeParticleSizeDelta].range_max, 150.0f);
    EXPECT_FLOAT_EQ(table[igi::kSmokeIntensity].range_max, 1.0f);
    EXPECT_FLOAT_EQ(table[igi::kSmokeGenerateFactor].range_max, 150.0f);
    EXPECT_FALSE(table[igi::kSmokeSpread].has_range);
    EXPECT_FALSE(table[igi::kSmokeColour].has_range);
    EXPECT_FALSE(table[igi::kSmokeOrigin].has_range);
    EXPECT_FALSE(table[igi::kSmokeMoveParticles].has_range);
    EXPECT_FALSE(table[igi::kSmokeGenerateExpr].has_range);
}

TEST(SmokeRules, DefaultsAreSmokeCreate) {
    // smoke.c:212-247.
    const igi::SmokeParams defaults;
    EXPECT_DOUBLE_EQ(defaults.origin[0], 0.0);
    EXPECT_DOUBLE_EQ(defaults.origin[1], 0.0);
    EXPECT_DOUBLE_EQ(defaults.origin[2], 0.0);
    EXPECT_FLOAT_EQ(defaults.alpha, 0.0f);
    EXPECT_FLOAT_EQ(defaults.gamma, 0.0f);
    EXPECT_EQ(defaults.count, 100);
    EXPECT_FLOAT_EQ(defaults.spread, 0.78539818525314331f);  // smoke.c:226 pi/2
    EXPECT_FLOAT_EQ(defaults.min_velocity, 4.5f);            // smoke.c:224 speedMax
    EXPECT_FLOAT_EQ(defaults.max_velocity, 6.0f);            // smoke.c:225 speedMin
    EXPECT_FLOAT_EQ(defaults.colour[0], 2.0f);
    EXPECT_FLOAT_EQ(defaults.colour[1], 2.0f);
    EXPECT_FLOAT_EQ(defaults.colour[2], 2.0f);
    EXPECT_FLOAT_EQ(defaults.life_time, 0.5f);               // smoke.c:231 fadeDelay
    EXPECT_FLOAT_EQ(defaults.fade_time, 1.0f);               // smoke.c:230 fadeTime
    EXPECT_EQ(defaults.fade_mode, 0);                        // smoke.c:235 blend
    EXPECT_EQ(defaults.sprite_index, 3);                     // smoke.c:238 sprite
    EXPECT_FLOAT_EQ(defaults.particle_size, 0.5f);           // smoke.c:237 size
    EXPECT_FLOAT_EQ(defaults.particle_size_delta, 2.6666667461395264f);  // smoke.c:236
    EXPECT_FLOAT_EQ(defaults.min_rotation, 0.0f);            // smoke.c:240 unk120
    EXPECT_FLOAT_EQ(defaults.max_rotation, 0.0f);            // smoke.c:239 unk11C
    EXPECT_FLOAT_EQ(defaults.intensity, 1.0f);               // smoke.c:241 level
    EXPECT_DOUBLE_EQ(defaults.generate_factor, 30.0);        // smoke.c:227 rate
    EXPECT_TRUE(defaults.generate_expr.empty());
    EXPECT_FALSE(defaults.move_particles);                   // smoke.c:243 enabled = 0
}

// ===========================================================================
// The random source
// ===========================================================================

TEST(SmokeRules, RandomMatchesTheEngineRecurrence) {
    // Random_Next 0x4B4740 (random.c:12-18) is `c ^= a >> 7; b ^= c; a += b`.
    // The expected values are an independent 32-bit reimplementation of those
    // three lines, not a copy of this port's output.
    igi::SmokeRandom rng;
    EXPECT_EQ(rng.Next(), 0x1085d7b6u);
    EXPECT_EQ(rng.Next(), 0x4a5154afu);
    EXPECT_EQ(rng.Next(), 0x916f73b5u);
    EXPECT_EQ(rng.Next(), 0xca5915d3u);
    EXPECT_EQ(rng.Next(), 0x10e3c300u);
}

TEST(SmokeRules, RandomFloatIsSixteenBitsOver65536) {
    // Random_Float 0x4B4770 (random.c:21-24).
    igi::SmokeRandom rng;
    EXPECT_EQ(rng.Float(), 0.842620849609375L);
    EXPECT_EQ(rng.Float(), 0.3307952880859375L);
    EXPECT_EQ(rng.Float(), 0.4519805908203125L);
}

TEST(SmokeRules, RandomIsResetToTheSameSeed) {
    // Random_Init 0x4B4720 seeds 0xC8FB1A07 / 0x39EA7756 / 0x7FF13CCD
    // (random.c:5-9), and Smoke_Create calls it (smoke.c:246), so two emitters
    // created in the same session emit the same particles.
    igi::SmokeRandom first;
    igi::SmokeRandom second;
    for (int i = 0; i < 16; ++i) EXPECT_EQ(first.Next(), second.Next());
    first.Reset();
    igi::SmokeRandom third;
    EXPECT_EQ(first.Next(), third.Next());
}

// ===========================================================================
// The emission accumulator (Smoke_Run 0x437DB0)
// ===========================================================================

TEST(SmokeRules, AccumulatorFirstTickValues) {
    // smoke.c:319-337 emits first and subtracts second and tests `acc > kRate`
    // strictly, starting from the zero Smoke_Create leaves (smoke.c:228). So the
    // first tick emits ceil(rate/30) - 1 particles, and a rate of exactly 30
    // emits nothing at all on that first tick (see the steady-state tests below).
    struct Case { double rate; int emitted; float remainder; };
    const Case kCases[] = {
        {0.0, 0, 0.0f},
        {0.5, 0, 0.5f},
        {1.0, 0, 1.0f},
        {30.0, 0, 30.0f},      // the accumulator now holds a full tick
        {31.5, 1, 1.5f},
        {45.0, 1, 15.0f},
        {60.0, 1, 30.0f},      // one, not two
        {90.0, 2, 30.0f},
        {120.0, 3, 30.0f},
        {150.0, 4, 30.0f},     // four, not five
        {300.0, 9, 30.0f},
    };
    for (const Case& test : kCases) {
        float acc = 0.0f;
        EXPECT_EQ(igi::SmokeAdvanceAccumulator(test.rate, acc), test.emitted)
            << "rate " << test.rate;
        EXPECT_FLOAT_EQ(acc, test.remainder) << "rate " << test.rate;
    }
}

TEST(SmokeRules, AccumulatorCarriesTheFractionAcrossTicks) {
    // 29.999 is just under one tick's worth, so nothing is emitted until the
    // second tick, when the carried remainder crosses 30.
    float acc = 0.0f;
    EXPECT_EQ(igi::SmokeAdvanceAccumulator(29.999, acc), 0);
    EXPECT_FLOAT_EQ(acc, 29.999000549316406f);
    EXPECT_EQ(igi::SmokeAdvanceAccumulator(29.999, acc), 1);
    EXPECT_FLOAT_EQ(acc, 29.998001098632812f);
    EXPECT_EQ(igi::SmokeAdvanceAccumulator(29.999, acc), 1);
    EXPECT_FLOAT_EQ(acc, 29.99700164794922f);
}

TEST(SmokeRules, AccumulatorTruncatesToFloatLikeTheEngineField) {
    // Smoke.acc is a FLOAT (smoke.c:45) while the local is a long double
    // (smoke.c:300), so the comparison uses the untruncated value and every
    // later one reads the truncated field. A rate whose sum needs more than 24
    // bits of mantissa shows the truncation.
    float acc = 0.0f;
    // 1.0000001 as a double is not representable as a float.
    EXPECT_EQ(igi::SmokeAdvanceAccumulator(1.0000001, acc), 0);
    EXPECT_FLOAT_EQ(acc, 1.0000001f);
    // The stored field is exactly the float value, not the double.
    EXPECT_EQ(acc, static_cast<float>(1.0000001));
}

TEST(SmokeRules, RetailDefaultRateEmitsOnePerTickAfterTheFirst) {
    // Smoke_Create's default rate is exactly 30.0 (smoke.c:227) and the
    // accumulator starts at zero (smoke.c:228). Because the loop tests `acc >
    // kRate` strictly, the first tick is empty and every later tick carries the
    // stored 30 and emits one -- i.e. 30 particles a second, which is what the
    // engine's own comment says (smoke.c:297).
    float acc = 0.0f;
    EXPECT_EQ(igi::SmokeAdvanceAccumulator(30.0, acc), 0);
    EXPECT_FLOAT_EQ(acc, 30.0f);
    int total = 0;
    for (int tick = 1; tick < 300; ++tick) {
        const int emitted = igi::SmokeAdvanceAccumulator(30.0, acc);
        EXPECT_EQ(emitted, 1) << "tick " << tick;
        total += emitted;
    }
    EXPECT_EQ(total, 299);
    EXPECT_FLOAT_EQ(acc, 30.0f);
    EXPECT_FLOAT_EQ(igi::kSmokeSteadyStateDivisor, igi::kSmokeTickRate);
}

TEST(SmokeRules, SteadyStateIsRateOverThirtyPerTick) {
    // The first tick loses one particle when the rate is an exact multiple of
    // 30; from the second tick on the emission is exactly rate / 30.
    struct Case { double rate; int first; int steady; };
    const Case kCases[] = {
        {30.0, 0, 1},
        {60.0, 1, 2},
        {90.0, 2, 3},
        {150.0, 4, 5},
        {300.0, 9, 10},
    };
    for (const Case& test : kCases) {
        float acc = 0.0f;
        EXPECT_EQ(igi::SmokeAdvanceAccumulator(test.rate, acc), test.first)
            << "first tick at rate " << test.rate;
        for (int tick = 0; tick < 32; ++tick) {
            EXPECT_EQ(igi::SmokeAdvanceAccumulator(test.rate, acc), test.steady)
                << "rate " << test.rate << " tick " << tick;
        }
    }
}

TEST(SmokeRules, SubThirtyRatesAccumulateUntilTheyCross) {
    // A rate of 1.0 emits nothing for 30 ticks and then one particle, i.e. one
    // per second; the carried remainder is what makes a fractional rate work.
    // The 31st call is the first whose running total (1 + 30) exceeds 30.
    float acc = 0.0f;
    for (int tick = 0; tick < 30; ++tick) {
        EXPECT_EQ(igi::SmokeAdvanceAccumulator(1.0, acc), 0) << "tick " << tick;
    }
    EXPECT_FLOAT_EQ(acc, 30.0f);
    EXPECT_EQ(igi::SmokeAdvanceAccumulator(1.0, acc), 1);

    // 45 is not a multiple of 30, so it alternates 1, 1, 2, 1, 2, ... and
    // averages 1.5 = 45/30.
    float fractional = 0.0f;
    const int kExpected[8] = {1, 1, 2, 1, 2, 1, 2, 1};
    int total = 0;
    for (int tick = 0; tick < 8; ++tick) {
        const int emitted = igi::SmokeAdvanceAccumulator(45.0, fractional);
        EXPECT_EQ(emitted, kExpected[tick]) << "tick " << tick;
        total += emitted;
    }
    EXPECT_EQ(total, 11);
}

TEST(SmokeRules, RoundIntIsFistpllRoundToNearestEven) {
    // fxobj.h:53-58 is `fistpll` under the default x87 control word, i.e.
    // round-half-to-even, and the x87 "integer indefinite" on overflow or NaN.
    EXPECT_EQ(igi::SmokeRoundInt(0.4L), 0);
    EXPECT_EQ(igi::SmokeRoundInt(0.5L), 0);   // ties to even
    EXPECT_EQ(igi::SmokeRoundInt(1.5L), 2);
    EXPECT_EQ(igi::SmokeRoundInt(2.5L), 2);
    EXPECT_EQ(igi::SmokeRoundInt(-0.5L), 0);
    EXPECT_EQ(igi::SmokeRoundInt(-1.5L), -2);
    EXPECT_EQ(igi::SmokeRoundInt(44.6L), 45);
    // Division by an authored intensity of 0 gives inf, and inf rounds to the
    // indefinite value whose low 32 bits are zero.
    EXPECT_EQ(igi::SmokeRoundInt(std::numeric_limits<long double>::infinity()), 0);
    EXPECT_EQ(igi::SmokeRoundInt(std::numeric_limits<long double>::quiet_NaN()), 0);
}

TEST(SmokeRules, DelayAndFadeFrames) {
    // smoke.c:331-332. `delay` comes from the authored "Life Time"
    // (Smoke.fadeDelay, smoke.c:176) and `frames` from the authored "Fade Time"
    // divided by the authored "Intensity" (Smoke.fadeTime, smoke.c:177).
    igi::SmokeParams params;
    params.life_time = 1.5f;
    params.fade_time = 1.3999999761581421f;
    params.intensity = 0.05f;
    EXPECT_EQ(igi::SmokeDelayFrames(params), 45);   // round(1.5 * 30)
    EXPECT_EQ(igi::SmokeFadeFrames(params), 840);   // round(1.4 * 30 / 0.05)

    params.life_time = 0.1f;
    params.fade_time = 0.5f;
    params.intensity = 0.025f;
    EXPECT_EQ(igi::SmokeDelayFrames(params), 3);
    EXPECT_EQ(igi::SmokeFadeFrames(params), 600);

    // A zero intensity divides by zero: the x87 result is the indefinite
    // integer, i.e. zero frames, which Particle2D_Add turns into a one-frame
    // fade (particle.c:563).
    params.fade_time = 1.0f;
    params.intensity = 0.0f;
    EXPECT_EQ(igi::SmokeFadeFrames(params), 0);
}

// ===========================================================================
// Per-particle emission
// ===========================================================================

TEST(SmokeRules, EmittedParticleUsesThreeDrawsInEngineOrder) {
    // smoke.c:320-330 draws angle, then radius, then the rotation fraction, and
    // the emitted values follow. The first three Random_Float values are
    // 0.842620849609375, 0.3307952880859375 and 0.4519805908203125 (see
    // RandomFloatIsSixteenBitsOver65536).
    igi::SmokeParams params = SampleParams();
    params.radius = 5.0f;
    params.min_rotation = 0.5f;
    params.max_rotation = 1.5f;
    params.intensity = 0.25f;
    params.fade_time = 3.4000000953674316f;
    params.life_time = 1.5f;

    const double emitter[3] = {1000.0, 2000.0, 3000.0};
    igi::SmokeRandom rng;
    igi::SmokeParticle particle;
    igi::SmokeEmitParticles(params, emitter, 1, rng, &particle);

    // angle = (float)(0.842620849609375 * kTwoPi)
    const float angle = 5.294342994689941f;
    // r = (float)(0.3307952880859375 * 5.0 * 4096)
    const float radius = 6774.6875f;
    // x = (1.5 - 0.5) * 0.4519805908203125 + 0.5
    const float rotation = 0.9519805908203125f;

    EXPECT_DOUBLE_EQ(particle.pos[2], 3000.0);  // z is copied, not randomised
    // The engine computes these in long double (smoke.c:328-329); evaluating the
    // same expression in double agrees to within a double ulp of ~6775.
    EXPECT_NEAR(particle.pos[0], 1000.0 + std::cos(static_cast<double>(angle)) * radius, 1e-6);
    EXPECT_NEAR(particle.pos[1], 2000.0 + std::sin(static_cast<double>(angle)) * radius, 1e-6);
    EXPECT_FLOAT_EQ(particle.spin, rotation * igi::kSmokePerFrame);
    EXPECT_FLOAT_EQ(particle.spin, 0.03173268958926201f);
    EXPECT_FLOAT_EQ(particle.level, 0.25f);
    EXPECT_FLOAT_EQ(particle.scale, igi::kSmokeOne);
    EXPECT_EQ(particle.delay_frames, 45);
    EXPECT_EQ(particle.fade_frames, 408);  // round(3.4 * 30 / 0.25)
    // The three draws are consumed, so the next value is the fourth.
    EXPECT_EQ(rng.Float(), 0.0852508544921875L);
}

TEST(SmokeRules, EmittedParticlesStayInsideTheAuthoredDisc) {
    // smoke.c:328-330 puts the particle on a disc of radius `radius` in the
    // emitter's XY plane.
    igi::SmokeParams params = SampleParams();
    params.radius = 3.0f;
    const double emitter[3] = {-100.0, 50.0, 25.0};
    std::vector<igi::SmokeParticle> particles(256);

    igi::SmokeRandom rng;
    igi::SmokeEmitParticles(params, emitter, static_cast<int>(particles.size()), rng, particles.data());

    const double limit = static_cast<double>(params.radius) * igi::kSmokeUnit;
    double min_distance = limit;
    double max_distance = 0.0;
    for (const igi::SmokeParticle& particle : particles) {
        EXPECT_DOUBLE_EQ(particle.pos[2], 25.0);
        const double dx = particle.pos[0] - emitter[0];
        const double dy = particle.pos[1] - emitter[1];
        const double distance = std::sqrt(dx * dx + dy * dy);
        EXPECT_LE(distance, limit + 1e-6);
        min_distance = std::min(min_distance, distance);
        max_distance = std::max(max_distance, distance);
    }
    // A uniform disc reaches close to the rim and close to the centre.
    EXPECT_GT(max_distance, limit * 0.9);
    EXPECT_LT(min_distance, limit * 0.1);
}

TEST(SmokeRules, ZeroRadiusPutsEveryParticleOnTheEmitter) {
    igi::SmokeParams params = SampleParams();
    params.radius = 0.0f;
    const double emitter[3] = {7.0, 8.0, 9.0};
    std::vector<igi::SmokeParticle> particles(64);
    igi::SmokeRandom rng;
    igi::SmokeEmitParticles(params, emitter, static_cast<int>(particles.size()), rng, particles.data());
    for (const igi::SmokeParticle& particle : particles) {
        EXPECT_DOUBLE_EQ(particle.pos[0], 7.0);
        EXPECT_DOUBLE_EQ(particle.pos[1], 8.0);
        EXPECT_DOUBLE_EQ(particle.pos[2], 9.0);
    }
}

TEST(SmokeRules, SpinStaysInsideTheAuthoredRotationRange) {
    // smoke.c:323: x = (max - min) * f + min, and Random_Float never returns 1,
    // so spin is in [min * kPerFrame, max * kPerFrame).
    igi::SmokeParams params = SampleParams();
    params.min_rotation = 0.3f;
    params.max_rotation = 0.6f;
    std::vector<igi::SmokeParticle> particles(512);
    igi::SmokeRandom rng;
    igi::SmokeEmitParticles(params, params.origin, static_cast<int>(particles.size()), rng, particles.data());
    const float low = params.min_rotation * igi::kSmokePerFrame;
    const float high = params.max_rotation * igi::kSmokePerFrame;
    for (const igi::SmokeParticle& particle : particles) {
        EXPECT_GE(particle.spin, low);
        EXPECT_LT(particle.spin, high);
    }
}

TEST(SmokeRules, EmissionIsDeterministicPerSeed) {
    igi::SmokeParams params = SampleParams();
    std::vector<igi::SmokeParticle> first(32);
    std::vector<igi::SmokeParticle> second(32);
    igi::SmokeRandom a;
    igi::SmokeRandom b;
    igi::SmokeEmitParticles(params, params.origin, 32, a, first.data());
    igi::SmokeEmitParticles(params, params.origin, 32, b, second.data());
    for (std::size_t i = 0; i < first.size(); ++i) {
        EXPECT_DOUBLE_EQ(first[i].pos[0], second[i].pos[0]) << i;
        EXPECT_DOUBLE_EQ(first[i].pos[1], second[i].pos[1]) << i;
        EXPECT_FLOAT_EQ(first[i].spin, second[i].spin) << i;
    }
}

TEST(SmokeRules, SpeedArgumentsArePassedInTheEnginesReversedOrder) {
    // smoke.c:333 passes `t->speedMin * kSpeed, t->speedMax * kSpeed`, and slot
    // 6 ("Minimum Velocity") wrote speedMax while slot 7 ("Maximum Velocity")
    // wrote speedMin (smoke.c:173-174). So the first argument is the authored
    // MAXIMUM and the second is the authored MINIMUM.
    igi::SmokeParams params = SampleParams();  // min 5.5, max 7.0
    float a = 0.0f;
    float b = 0.0f;
    igi::SmokeEmitSpeedArguments(params, &a, &b);
    EXPECT_FLOAT_EQ(a, 7.0f * 136.0f);  // 952
    EXPECT_FLOAT_EQ(b, 5.5f * 136.0f);  // 748

    // particle.c:616 interpolates `(b - a) * fraction + a`, which runs backwards
    // here; because the fraction is uniform on [0,1) the sample is still uniform
    // on [a, b].
    EXPECT_FLOAT_EQ(igi::SmokeSampleSpeed(a, b, 0.0L), 952.0f);
    EXPECT_FLOAT_EQ(igi::SmokeSampleSpeed(a, b, 0.4519805908203125L), 859.7959594726562f);
    EXPECT_FLOAT_EQ(igi::SmokeSampleSpeed(a, b, 1.0L), 748.0f);
}

// ===========================================================================
// Direction, blend, sprite and group derivation (Smoke_Changed 0x438000)
// ===========================================================================

TEST(SmokeRules, DirectionIsUpAtZeroPitchAndYaw) {
    // smoke.c:361-365 with the identity matrices gives column 2 of the identity.
    float dir[3] = {9.0f, 9.0f, 9.0f};
    igi::SmokeDirection(0.0f, 0.0f, dir);
    EXPECT_FLOAT_EQ(dir[0], 0.0f);
    EXPECT_FLOAT_EQ(dir[1], 0.0f);
    EXPECT_FLOAT_EQ(dir[2], 1.0f);
}

TEST(SmokeRules, DirectionTiltsByPitchAndSwingsByYaw) {
    // dir = (sin(yaw)sin(pitch), -cos(yaw)sin(pitch), cos(pitch)).
    float dir[3] = {0.0f, 0.0f, 0.0f};
    const float half_pi = 1.5707963267948966f;

    igi::SmokeDirection(half_pi, 0.0f, dir);  // pitch 90 deg: horizontal, -Y
    EXPECT_NEAR(dir[0], 0.0f, 1e-6);
    EXPECT_NEAR(dir[1], -1.0f, 1e-6);
    EXPECT_NEAR(dir[2], 0.0f, 1e-6);

    igi::SmokeDirection(half_pi, half_pi, dir);  // yaw 90 deg: +X
    EXPECT_NEAR(dir[0], 1.0f, 1e-6);
    EXPECT_NEAR(dir[1], 0.0f, 1e-6);
    EXPECT_NEAR(dir[2], 0.0f, 1e-6);

    // LEVEL2:1111 authors Alpha = pi, which points straight down.
    igi::SmokeDirection(3.1415929794311523f, 0.0f, dir);
    EXPECT_NEAR(dir[0], 0.0f, 1e-6);
    EXPECT_NEAR(dir[1], 0.0f, 1e-6);
    EXPECT_NEAR(dir[2], -1.0f, 1e-6);
}

TEST(SmokeRules, BlendFlagsAreTheThreeAuthoredModes) {
    // smoke.c:366-379.
    EXPECT_EQ(igi::SmokeBlendFlags(0), 0x1080000u);
    EXPECT_EQ(igi::SmokeBlendFlags(1), 0x1000008u);
    EXPECT_EQ(igi::SmokeBlendFlags(2), 0x1000010u);
    // The original's default arm passes stack garbage; this port returns 0.
    EXPECT_EQ(igi::SmokeBlendFlags(3), 0u);
    EXPECT_EQ(igi::SmokeBlendFlags(-1), 0u);
}

TEST(SmokeRules, SpriteSlotsAreTheFourRetailSprites) {
    // smoke.c:189-192 load them, smoke.c:380-393 select them.
    EXPECT_STREQ(igi::SmokeSpriteFile(0), "fireball.spr");
    EXPECT_STREQ(igi::SmokeSpriteFile(1), "blood1.spr");
    EXPECT_STREQ(igi::SmokeSpriteFile(2), "smoke2.spr");
    EXPECT_STREQ(igi::SmokeSpriteFile(3), "smoke1.spr");
    EXPECT_STREQ(igi::SmokeSpriteFile(99), "smoke1.spr");  // the default arm
}

TEST(SmokeRules, GroupArgumentsAreTheScaledAuthoredValues) {
    // smoke.c:394-404.
    igi::SmokeParams params = SampleParams();
    params.particle_size = 0.30000001192092896f;
    params.particle_size_delta = 2.6666669845581055f;
    EXPECT_FLOAT_EQ(igi::SmokeGroupSize(params), 1228.800048828125f);  // size * 4096
    float colour[3] = {0.0f, 0.0f, 0.0f};
    igi::SmokeGroupColour(params, colour);
    EXPECT_FLOAT_EQ(colour[0], 19.38459587097168f);  // 0.307692 * 63
    EXPECT_FLOAT_EQ(colour[1], 10.903851509094238f);  // 0.173077 * 63
    EXPECT_FLOAT_EQ(colour[2], 0.0f);
    EXPECT_FLOAT_EQ(igi::SmokeGroupGrow(params), 0.08888890594244003f);  // grow / 30
    EXPECT_FLOAT_EQ(igi::kSmokeParticleLife, 409600.0f);  // Particle2D_New maxDepth
}

TEST(SmokeRules, MovePlacesTheEmitterInItsAnchorFrame) {
    // smoke.c:202-208: v = M * local, then pos = anchor_pos + v.
    const float identity[9] = {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    const double anchor[3] = {100.0, 200.0, 300.0};
    const double local[3] = {1.0, 2.0, 3.0};
    double out[3] = {0.0, 0.0, 0.0};
    igi::SmokeMove(anchor, identity, local, out);
    EXPECT_DOUBLE_EQ(out[0], 101.0);
    EXPECT_DOUBLE_EQ(out[1], 202.0);
    EXPECT_DOUBLE_EQ(out[2], 303.0);

    // A 90-degree rotation about Z maps local +X onto world +Y: v[0] is
    // m[2]*lz + m[1]*ly + m[0]*lx = -2 and v[1] is m[5]*lz + m[4]*ly + m[3]*lx = 1.
    const float rot_z[9] = {0.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    igi::SmokeMove(anchor, rot_z, local, out);
    EXPECT_DOUBLE_EQ(out[0], 100.0 - 2.0);
    EXPECT_DOUBLE_EQ(out[1], 200.0 + 1.0);
    EXPECT_DOUBLE_EQ(out[2], 303.0);
}

TEST(SmokeRules, UpdateLocalInvertsTheParentTransform) {
    // smoke.c:413-439.
    const float identity[9] = {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    const double parent[3] = {10.0, 20.0, 30.0};
    const double origin[3] = {13.0, 24.0, 35.0};
    double local[3] = {0.0, 0.0, 0.0};
    igi::SmokeUpdateLocal(origin, parent, identity, true, local);
    EXPECT_DOUBLE_EQ(local[0], 3.0);
    EXPECT_DOUBLE_EQ(local[1], 4.0);
    EXPECT_DOUBLE_EQ(local[2], 5.0);

    // With no moving ancestor the offset is zero (smoke.c:420-428).
    igi::SmokeUpdateLocal(origin, parent, identity, false, local);
    EXPECT_DOUBLE_EQ(local[0], 0.0);
    EXPECT_DOUBLE_EQ(local[1], 0.0);
    EXPECT_DOUBLE_EQ(local[2], 0.0);
}

TEST(SmokeRules, TranslateIsGatedByMovedAndEnabledOnly) {
    // smoke.c:303-312. "Move Particles" gates the translation of already
    // emitted particles, and nothing else: emission is not gated by it.
    const double pos[3] = {5.0, 6.0, 7.0};
    const double last[3] = {1.0, 2.0, 3.0};
    double delta[3] = {0.0, 0.0, 0.0};

    EXPECT_TRUE(igi::SmokeTranslateDelta(true, true, pos, last, delta));
    EXPECT_DOUBLE_EQ(delta[0], 4.0);
    EXPECT_DOUBLE_EQ(delta[1], 4.0);
    EXPECT_DOUBLE_EQ(delta[2], 4.0);
    EXPECT_FALSE(igi::SmokeTranslateDelta(false, true, pos, last, delta));
    EXPECT_FALSE(igi::SmokeTranslateDelta(true, false, pos, last, delta));
    EXPECT_FALSE(igi::SmokeTranslateDelta(false, false, pos, last, delta));
}

// ===========================================================================
// Parsing the authored call
// ===========================================================================

TEST(SmokeRules, ParseReadsEveryAuthoredParameter) {
    const std::vector<std::string> tokens = {
        "-1", "Smoke", "",
        "126034480", "-38145376", "175416512",   // Position
        "0", "0",                                 // Alpha, Gamma
        "100",                                    // Number of Particles
        "5", "0.78539800643920898",               // Radius, Maximum Random Angle
        "5.5", "7",                               // Minimum/Maximum Velocity
        "0.30769199132919312", "0.17307700216770172", "0",  // Colour
        "1.5", "1.3999999761581421",              // Life Time, Fade Time
        "0", "3",                                 // Fade Mode, Sprite index
        "0.5", "2.6666669845581055",              // Particle Size, Size Delta
        "0", "2",                                 // Min/Max Rotation Speed
        "0.05000000074505806",                    // Intensity
        "30",                                     // Initial generate factor value
        "", "FALSE",                              // Generate factor, Move Particles
    };
    ASSERT_EQ(tokens.size(), igi::kSmokeFirstParam + igi::kSmokeTokenCount);

    igi::SmokeParams params;
    ASSERT_TRUE(igi::ParseSmokeParams(tokens, &params));
    EXPECT_DOUBLE_EQ(params.origin[0], 126034480.0);
    EXPECT_DOUBLE_EQ(params.origin[1], -38145376.0);
    EXPECT_DOUBLE_EQ(params.origin[2], 175416512.0);
    EXPECT_FLOAT_EQ(params.alpha, 0.0f);
    EXPECT_FLOAT_EQ(params.gamma, 0.0f);
    EXPECT_EQ(params.count, 100);
    EXPECT_FLOAT_EQ(params.radius, 5.0f);
    EXPECT_FLOAT_EQ(params.spread, 0.78539800643920898f);
    EXPECT_FLOAT_EQ(params.min_velocity, 5.5f);
    EXPECT_FLOAT_EQ(params.max_velocity, 7.0f);
    EXPECT_FLOAT_EQ(params.colour[0], 0.30769199132919312f);
    EXPECT_FLOAT_EQ(params.colour[1], 0.17307700216770172f);
    EXPECT_FLOAT_EQ(params.colour[2], 0.0f);
    EXPECT_FLOAT_EQ(params.life_time, 1.5f);
    EXPECT_FLOAT_EQ(params.fade_time, 1.3999999761581421f);
    EXPECT_EQ(params.fade_mode, 0);
    EXPECT_EQ(params.sprite_index, 3);
    EXPECT_FLOAT_EQ(params.particle_size, 0.5f);
    EXPECT_FLOAT_EQ(params.particle_size_delta, 2.6666669845581055f);
    EXPECT_FLOAT_EQ(params.min_rotation, 0.0f);
    EXPECT_FLOAT_EQ(params.max_rotation, 2.0f);
    EXPECT_FLOAT_EQ(params.intensity, 0.05000000074505806f);
    EXPECT_DOUBLE_EQ(params.generate_factor, 30.0);
    EXPECT_TRUE(params.generate_expr.empty());
    EXPECT_FALSE(params.move_particles);
}

TEST(SmokeRules, ParseRejectsATruncatedCall) {
    std::vector<std::string> tokens(igi::kSmokeFirstParam + igi::kSmokeTokenCount - 1, "0");
    tokens[0] = "-1";
    tokens[1] = "Smoke";
    igi::SmokeParams params;
    EXPECT_FALSE(igi::ParseSmokeParams(tokens, &params));

    // And it does not leave a half-written struct behind.
    EXPECT_EQ(params.count, 100);
}

TEST(SmokeRules, ParseAcceptsMoveParticlesTrue) {
    // "Move Particles" is the last of the 21 parameters, i.e. the 25th and last
    // authored token (two of the parameters consume three tokens each).
    std::vector<std::string> tokens(igi::kSmokeFirstParam + igi::kSmokeTokenCount, "0");
    tokens[0] = "-1";
    tokens[1] = "Smoke";
    ASSERT_EQ(tokens.back(), "0");
    tokens.back() = "TRUE";
    igi::SmokeParams params;
    ASSERT_TRUE(igi::ParseSmokeParams(tokens, &params));
    EXPECT_TRUE(params.move_particles);
    EXPECT_EQ(igi::kSmokeFirstParam + igi::kSmokeTokenCount, 28u);
}

TEST(SmokeRules, ResolveFiltersToSmokeTasksOnly) {
    std::vector<igi::SmokeTaskObject> objects(2);
    objects[0].type = "Door";
    objects[0].arg_tokens = {"-1", "Door", ""};
    objects[1].type = "Smoke";
    objects[1].arg_tokens.assign(igi::kSmokeFirstParam + igi::kSmokeTokenCount, "0");
    objects[1].arg_tokens[0] = "-1";
    objects[1].arg_tokens[1] = "Smoke";
    const std::vector<igi::SmokeParams> resolved = igi::ResolveSmokeTasks(objects);
    ASSERT_EQ(resolved.size(), 1u);
    EXPECT_EQ(resolved[0].count, 0);  // the all-zero token list parses as authored
}

// ===========================================================================
// The retail corpus
// ===========================================================================

TEST(SmokeRulesCorpus, EveryRetailSmokeParsesAndMatchesTheDeclaration) {
    if (!fs::is_regular_file(LevelDir(1) / "OBJECTS.QVM")) {
        GTEST_SKIP() << "retail IGI not found at " << GameRoot().string()
                     << " (set IGI_GAME_PATH to a retail install to run this test)";
    }

    // The authored per-level instance counts in the shipped QVMs. The level
    // directory for level 1 is lower-case on this install, which the
    // case-insensitive filesystem resolves.
    const int kExpected[15] = {0, 3, 4, 0, 0, 0, 5, 12, 9, 0, 23, 2, 8, 3, 5};
    int total = 0;

    for (int level = 1; level <= 14; ++level) {
        const fs::path path = LevelDir(level) / "OBJECTS.QVM";
        if (!fs::is_regular_file(path)) continue;

        const QVMFile qvm = QVM_Parse(path.string());
        const std::vector<std::vector<std::string>> calls = ExtractSmokeCalls(qvm);

        EXPECT_EQ(static_cast<int>(calls.size()), kExpected[level])
            << "LEVEL" << level << " smoke instance count";

        for (const std::vector<std::string>& tokens : calls) {
            ASSERT_EQ(tokens.size(), igi::kSmokeFirstParam + igi::kSmokeTokenCount)
                << "LEVEL" << level << ": a Smoke call did not yield 25 parameter tokens";
            igi::SmokeParams params;
            ASSERT_TRUE(igi::ParseSmokeParams(tokens, &params)) << "LEVEL" << level;
            ++total;

            // The invariants that hold for all 74 authored instances. The
            // declared ParamTable ranges do NOT all hold -- see
            // DeclaredRangesAreNotEnforcedOnAuthoredData below.
            EXPECT_GE(params.count, 1);
            EXPECT_GE(params.radius, 0.0f);
            EXPECT_LE(params.radius, 5.0f);
            EXPECT_GE(params.particle_size, 0.0f);
            EXPECT_LE(params.particle_size, 5.0f);
            EXPECT_GE(params.intensity, 0.0f);
            EXPECT_LE(params.intensity, 1.0f);
            EXPECT_GE(params.min_velocity, 0.0f);
            EXPECT_GE(params.max_velocity, 0.0f);
            EXPECT_GE(params.fade_mode, 0);
            EXPECT_LE(params.fade_mode, 2);
            EXPECT_GE(params.sprite_index, 0);
            EXPECT_LE(params.sprite_index, 3);
            EXPECT_GE(params.generate_factor, 0.0);
            EXPECT_LE(params.generate_factor, 150.0);
            // The authored speed pair is ordered the way the names say.
            EXPECT_LE(params.min_velocity, params.max_velocity);
            // Every retail emitter uses the fourth sprite slot, smoke1.spr
            // (smoke.c:192), which is also the default arm of the switch.
            EXPECT_EQ(params.sprite_index, 3);
            EXPECT_STREQ(igi::SmokeSpriteFile(params.sprite_index), "smoke1.spr");
        }
    }
    EXPECT_EQ(total, 74);
}

TEST(SmokeRulesCorpus, DeclaredRangesAreNotEnforcedOnAuthoredData) {
    // A finding, not an expectation of the port: the ranges Smoke_Init registers
    // (smoke.c:131-154) are editor metadata and the shipping levels contain
    // values outside them. Counted rather than asserted per instance so the
    // numbers are visible if the corpus changes.
    if (!fs::is_regular_file(LevelDir(2) / "OBJECTS.QVM")) {
        GTEST_SKIP() << "retail IGI not found at " << GameRoot().string();
    }

    int count_over = 0;      // declared max 255, authored up to 600
    int pitch_over = 0;      // declared max 0x1.921fb6p+1, authored 1 ulp above
    int yaw_over = 0;        // same
    int speed_over = 0;      // declared max 14.6484375, authored 1 ulp above
    int instances = 0;

    for (int level = 1; level <= 14; ++level) {
        const fs::path path = LevelDir(level) / "OBJECTS.QVM";
        if (!fs::is_regular_file(path)) continue;
        const QVMFile qvm = QVM_Parse(path.string());
        for (const std::vector<std::string>& tokens : ExtractSmokeCalls(qvm)) {
            igi::SmokeParams params;
            ASSERT_TRUE(igi::ParseSmokeParams(tokens, &params)) << "LEVEL" << level;
            ++instances;
            if (params.count > 255) ++count_over;
            if (params.alpha > 3.1415927410125732f) ++pitch_over;
            if (params.gamma > 3.1415927410125732f) ++yaw_over;
            if (params.min_velocity > 14.6484375f || params.max_velocity > 14.6484375f) ++speed_over;
        }
    }
    EXPECT_EQ(instances, 74);
    EXPECT_EQ(count_over, 13);
    EXPECT_EQ(pitch_over, 4);
    EXPECT_EQ(yaw_over, 2);
    EXPECT_EQ(speed_over, 9);

    // The authored "pi" is the float one ulp above the engine's range max, which
    // is what makes the pitch/yaw violations a rounding artefact rather than a
    // different angle.
    EXPECT_FLOAT_EQ(3.1415929794311523f, std::nextafter(3.1415927410125732f,
                                                       std::numeric_limits<float>::infinity()));
}

TEST(SmokeRulesCorpus, Level2SmokeValuesAreExactlyTheAuthoredOnes) {
    const fs::path path = LevelDir(2) / "OBJECTS.QVM";
    if (!fs::is_regular_file(path)) {
        GTEST_SKIP() << "retail IGI not found at " << GameRoot().string();
    }
    const QVMFile qvm = QVM_Parse(path.string());
    const std::vector<std::vector<std::string>> calls = ExtractSmokeCalls(qvm);
    ASSERT_EQ(calls.size(), 4u);

    std::vector<igi::SmokeParams> params;
    for (const std::vector<std::string>& tokens : calls) {
        igi::SmokeParams parsed;
        ASSERT_TRUE(igi::ParseSmokeParams(tokens, &parsed));
        params.push_back(parsed);
    }

    // objects.qsc:1044 - the fireball emitter (Fade Mode 2, Sprite index 3).
    EXPECT_DOUBLE_EQ(params[0].origin[0], 126014848.0);
    EXPECT_DOUBLE_EQ(params[0].origin[1], -38164756.0);
    EXPECT_DOUBLE_EQ(params[0].origin[2], 175416512.0);
    EXPECT_FLOAT_EQ(params[0].alpha, 0.0f);
    EXPECT_FLOAT_EQ(params[0].gamma, 0.0f);
    EXPECT_EQ(params[0].count, 170);
    EXPECT_FLOAT_EQ(params[0].radius, 1.0f);
    EXPECT_FLOAT_EQ(params[0].spread, 1.0f);
    EXPECT_FLOAT_EQ(params[0].min_velocity, 5.5f);
    EXPECT_FLOAT_EQ(params[0].max_velocity, 7.0f);
    EXPECT_FLOAT_EQ(params[0].colour[0], 2.0f);
    EXPECT_FLOAT_EQ(params[0].colour[1], 2.0f);
    EXPECT_FLOAT_EQ(params[0].colour[2], 2.0f);
    EXPECT_FLOAT_EQ(params[0].life_time, 1.5f);
    EXPECT_FLOAT_EQ(params[0].fade_time, 3.4000000953674316f);
    EXPECT_EQ(params[0].fade_mode, 2);
    EXPECT_EQ(params[0].sprite_index, 3);
    EXPECT_FLOAT_EQ(params[0].particle_size, 0.30000001192092896f);
    EXPECT_FLOAT_EQ(params[0].particle_size_delta, 2.6666669845581055f);
    EXPECT_FLOAT_EQ(params[0].min_rotation, 0.0f);
    EXPECT_FLOAT_EQ(params[0].max_rotation, 2.0f);
    EXPECT_FLOAT_EQ(params[0].intensity, 0.25f);
    EXPECT_DOUBLE_EQ(params[0].generate_factor, 30.0);
    EXPECT_FALSE(params[0].move_particles);

    // objects.qsc:1045 - a brown dust emitter, and the one this module uses as
    // its SampleParams. Its speed pair is authored 5.5 / 7.0 and its intensity
    // 0.05, which is what makes its fade 840 frames long.
    EXPECT_DOUBLE_EQ(params[1].origin[0], 126034480.0);
    EXPECT_DOUBLE_EQ(params[1].origin[1], -38145376.0);
    EXPECT_DOUBLE_EQ(params[1].origin[2], 175416512.0);
    EXPECT_EQ(params[1].count, 100);
    EXPECT_FLOAT_EQ(params[1].radius, 5.0f);
    EXPECT_FLOAT_EQ(params[1].spread, 0.78539800643920898f);
    EXPECT_FLOAT_EQ(params[1].colour[0], 0.30769199132919312f);
    EXPECT_FLOAT_EQ(params[1].colour[1], 0.17307700216770172f);
    EXPECT_FLOAT_EQ(params[1].colour[2], 0.0f);
    EXPECT_FLOAT_EQ(params[1].life_time, 1.5f);
    EXPECT_FLOAT_EQ(params[1].fade_time, 1.3999999761581421f);
    EXPECT_EQ(params[1].fade_mode, 0);
    EXPECT_FLOAT_EQ(params[1].particle_size, 0.5f);
    EXPECT_FLOAT_EQ(params[1].min_rotation, 0.0f);
    EXPECT_FLOAT_EQ(params[1].max_rotation, 2.0f);
    EXPECT_FLOAT_EQ(params[1].intensity, 0.05000000074505806f);
    EXPECT_EQ(igi::SmokeDelayFrames(params[1]), 45);
    EXPECT_EQ(igi::SmokeFadeFrames(params[1]), 840);

    // objects.qsc:1111 - the level's fast emitter: it authors a rate of 150, so
    // its first tick emits four particles and every later tick five.
    EXPECT_FLOAT_EQ(params[3].alpha, 3.1415929794311523f);
    EXPECT_FLOAT_EQ(params[3].gamma, 0.0f);
    EXPECT_EQ(params[3].count, 200);
    EXPECT_FLOAT_EQ(params[3].radius, 2.5f);
    EXPECT_FLOAT_EQ(params[3].min_velocity, 4.5f);
    EXPECT_FLOAT_EQ(params[3].max_velocity, 6.0f);
    EXPECT_FLOAT_EQ(params[3].life_time, 0.10000000149011612f);
    EXPECT_FLOAT_EQ(params[3].fade_time, 0.5f);
    EXPECT_EQ(params[3].fade_mode, 1);
    EXPECT_FLOAT_EQ(params[3].particle_size, 0.75f);
    EXPECT_FLOAT_EQ(params[3].min_rotation, 0.5f);
    EXPECT_FLOAT_EQ(params[3].max_rotation, 1.5f);
    EXPECT_FLOAT_EQ(params[3].intensity, 0.02500000037252903f);
    EXPECT_DOUBLE_EQ(params[3].generate_factor, 150.0);
    EXPECT_EQ(igi::SmokeDelayFrames(params[3]), 3);
    EXPECT_EQ(igi::SmokeFadeFrames(params[3]), 600);

    float acc = 0.0f;
    EXPECT_EQ(igi::SmokeAdvanceAccumulator(params[3].generate_factor, acc), 4);
    EXPECT_EQ(igi::SmokeAdvanceAccumulator(params[3].generate_factor, acc), 5);

    // ... while the three rate-30.0 emitters in the same level emit nothing on
    // their first tick and one particle on every later one.
    for (int i = 0; i < 3; ++i) {
        float slow = 0.0f;
        EXPECT_DOUBLE_EQ(params[i].generate_factor, 30.0);
        EXPECT_EQ(igi::SmokeAdvanceAccumulator(params[i].generate_factor, slow), 0);
        EXPECT_EQ(igi::SmokeAdvanceAccumulator(params[i].generate_factor, slow), 1);
    }
}

TEST(SmokeRulesCorpus, Level1SmokeValuesAreExactlyTheAuthoredOnes) {
    const fs::path path = LevelDir(1) / "OBJECTS.QVM";
    if (!fs::is_regular_file(path)) {
        GTEST_SKIP() << "retail IGI not found at " << GameRoot().string();
    }
    const QVMFile qvm = QVM_Parse(path.string());
    const std::vector<std::vector<std::string>> calls = ExtractSmokeCalls(qvm);
    ASSERT_EQ(calls.size(), 3u);

    igi::SmokeParams params;
    ASSERT_TRUE(igi::ParseSmokeParams(calls[0], &params));

    // objects.qsc:903 (and :975, which is identical).
    EXPECT_DOUBLE_EQ(params.origin[0], 24614592.0);
    EXPECT_DOUBLE_EQ(params.origin[1], -55795096.0);
    EXPECT_DOUBLE_EQ(params.origin[2], 174417712.0);
    EXPECT_FLOAT_EQ(params.alpha, 1.7802360057830811f);
    EXPECT_FLOAT_EQ(params.gamma, -1.6755160093307495f);
    EXPECT_EQ(params.count, 300);
    EXPECT_FLOAT_EQ(params.radius, 2.0f);
    EXPECT_FLOAT_EQ(params.spread, 0.78539800643920898f);
    EXPECT_FLOAT_EQ(params.min_velocity, 4.5f);
    EXPECT_FLOAT_EQ(params.max_velocity, 6.0f);
    EXPECT_FLOAT_EQ(params.colour[0], 2.0f);
    EXPECT_FLOAT_EQ(params.life_time, 1.5f);
    EXPECT_FLOAT_EQ(params.fade_time, 2.0f);
    EXPECT_EQ(params.fade_mode, 0);
    EXPECT_EQ(params.sprite_index, 3);
    EXPECT_FLOAT_EQ(params.particle_size, 0.69999998807907104f);
    EXPECT_FLOAT_EQ(params.min_rotation, 0.30000001192092896f);
    EXPECT_FLOAT_EQ(params.max_rotation, 0.60000002384185791f);
    EXPECT_FLOAT_EQ(params.intensity, 0.10000000149011612f);
    EXPECT_DOUBLE_EQ(params.generate_factor, 30.0);

    // Its direction is a real tilt, not straight up.
    float dir[3] = {0.0f, 0.0f, 0.0f};
    igi::SmokeDirection(params.alpha, params.gamma, dir);
    EXPECT_NEAR(dir[0], std::sin(params.gamma) * std::sin(params.alpha), 1e-6);
    EXPECT_NEAR(dir[1], -std::cos(params.gamma) * std::sin(params.alpha), 1e-6);
    EXPECT_NEAR(dir[2], std::cos(params.alpha), 1e-6);
    EXPECT_NEAR(std::sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]), 1.0, 1e-6);
}

TEST(SmokeRulesCorpus, QvmAgreesWithTheDecompiledObjectsQsc) {
    // LEVEL1 and LEVEL2 also ship a decompiled objects.qsc, i.e. a second,
    // human-readable rendering of the same authored data. Comparing the two
    // proves the QVM token reader is aligned with the argument list rather than
    // off by a token.
    for (int level : {1, 2}) {
        const fs::path dir = LevelDir(level);
        if (!fs::is_regular_file(dir / "OBJECTS.QVM") || !fs::is_regular_file(dir / "objects.qsc")) {
            GTEST_SKIP() << "retail IGI (with objects.qsc) not found at " << GameRoot().string();
        }

        const QVMFile qvm = QVM_Parse((dir / "OBJECTS.QVM").string());
        const std::vector<std::vector<std::string>> from_qvm = ExtractSmokeCalls(qvm);
        const std::string text = ReadFile(dir / "objects.qsc");
        const std::vector<std::vector<std::string>> from_text = ParseSmokeCallsFromText(text);

        ASSERT_EQ(from_qvm.size(), from_text.size()) << "LEVEL" << level;
        for (std::size_t i = 0; i < from_qvm.size(); ++i) {
            ASSERT_EQ(from_qvm[i].size(), from_text[i].size()) << "LEVEL" << level << " #" << i;
            igi::SmokeParams a;
            igi::SmokeParams b;
            ASSERT_TRUE(igi::ParseSmokeParams(from_qvm[i], &a));
            ASSERT_TRUE(igi::ParseSmokeParams(from_text[i], &b));
            EXPECT_DOUBLE_EQ(a.origin[0], b.origin[0]) << "LEVEL" << level << " #" << i;
            EXPECT_DOUBLE_EQ(a.origin[1], b.origin[1]) << "LEVEL" << level << " #" << i;
            EXPECT_DOUBLE_EQ(a.origin[2], b.origin[2]) << "LEVEL" << level << " #" << i;
            EXPECT_FLOAT_EQ(a.alpha, b.alpha);
            EXPECT_FLOAT_EQ(a.gamma, b.gamma);
            EXPECT_EQ(a.count, b.count);
            EXPECT_FLOAT_EQ(a.radius, b.radius);
            EXPECT_FLOAT_EQ(a.spread, b.spread);
            EXPECT_FLOAT_EQ(a.min_velocity, b.min_velocity);
            EXPECT_FLOAT_EQ(a.max_velocity, b.max_velocity);
            EXPECT_FLOAT_EQ(a.colour[0], b.colour[0]);
            EXPECT_FLOAT_EQ(a.colour[1], b.colour[1]);
            EXPECT_FLOAT_EQ(a.colour[2], b.colour[2]);
            EXPECT_FLOAT_EQ(a.life_time, b.life_time);
            EXPECT_FLOAT_EQ(a.fade_time, b.fade_time);
            EXPECT_EQ(a.fade_mode, b.fade_mode);
            EXPECT_EQ(a.sprite_index, b.sprite_index);
            EXPECT_FLOAT_EQ(a.particle_size, b.particle_size);
            EXPECT_FLOAT_EQ(a.particle_size_delta, b.particle_size_delta);
            EXPECT_FLOAT_EQ(a.min_rotation, b.min_rotation);
            EXPECT_FLOAT_EQ(a.max_rotation, b.max_rotation);
            EXPECT_FLOAT_EQ(a.intensity, b.intensity);
            EXPECT_DOUBLE_EQ(a.generate_factor, b.generate_factor);
            EXPECT_EQ(a.generate_expr, b.generate_expr);
            EXPECT_EQ(a.move_particles, b.move_particles);
        }
    }
}

TEST(SmokeRulesCorpus, DeclaredParametersMatchTheParameterTable) {
    // The authored declaration lives in the QVM bytecode as the argument block
    // of Task_DeclareParameters, so the port's table can be checked against the
    // shipping data rather than against a hand-copied list.
    const fs::path path = LevelDir(2) / "OBJECTS.QVM";
    if (!fs::is_regular_file(path)) {
        GTEST_SKIP() << "retail IGI not found at " << GameRoot().string();
    }
    const QVMFile qvm = QVM_Parse(path.string());
    const std::vector<std::string> declared = DeclaredSmokeParameters(qvm);
    ASSERT_EQ(declared.size(), static_cast<std::size_t>(igi::kSmokeParamCount) * 2u);

    const igi::SmokeParamDesc* table = igi::SmokeParamTable();
    for (int i = 0; i < igi::kSmokeParamCount; ++i) {
        EXPECT_EQ(declared[static_cast<std::size_t>(i) * 2], table[i].authored_name) << "slot " << i;
        EXPECT_EQ(declared[static_cast<std::size_t>(i) * 2 + 1], table[i].type_name) << "slot " << i;
    }

    // And the same declaration as the decompiled text.
    const std::string text = ReadFile(LevelDir(2) / "objects.qsc");
    const std::vector<std::string> from_text = ParseDeclaredSmokeParametersFromText(text);
    ASSERT_EQ(from_text.size(), static_cast<std::size_t>(igi::kSmokeParamCount) * 2u);
    for (int i = 0; i < igi::kSmokeParamCount; ++i) {
        EXPECT_EQ(from_text[static_cast<std::size_t>(i) * 2], table[i].authored_name) << "slot " << i;
        EXPECT_EQ(from_text[static_cast<std::size_t>(i) * 2 + 1], table[i].type_name) << "slot " << i;
    }
}
