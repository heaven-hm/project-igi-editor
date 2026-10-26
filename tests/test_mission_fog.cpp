// test_mission_fog.cpp - per-mission authored fog (#66).
//
// The oracle for every number here is IGI-RE (read-only). Key citations:
//   globallight.c:115,117-140,264-285   the fog colour/density ParamTable, publish()
//   light.c:55-72,172-175               Fog_Compute and its constants
//   spline.c:13-20                      Float_Log2 - a bit trick, NOT log2
//   terraind3d.c:164,292-302            the exponential terrain fog and FOGDENSITY
//   terrainmesh.c:282,302-320,960       the lightmap fog table and gTerrainFogScale
//   flatwater.c:421                     the water variant
//   d3d/direct3d.c:532                  gWorldScale = 0.001f
//   haze/haze.c:42-53,196-240           HazeLayer fields and Haze_GetFog
//   file/vec.c:140-181                  Vec3_FastLength
//
// The corpus half reads the retail OBJECTS.QVM with the editor's own QVM_Parse
// and cross-checks LEVEL1 and LEVEL4 - the only two levels that also ship a
// decompiled objects.qsc - against that human-readable text.

#include "../source/level/qvm_parser.h"
#include "../source/runtime/mission_fog.h"

#include <gtest/gtest.h>

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

constexpr float kFixed = 4096.0f;  // src/math/mathvars.h:7 - game units per metre

fs::path GameRoot() {
    if (const char* env = std::getenv("IGI_GAME_PATH"); env != nullptr && *env != '\0')
        return fs::path(env);
    return fs::path("/Users/haseeb-mir/Downloads/IGI");
}

fs::path LevelDir(int level) {
    return GameRoot() / "MISSIONS" / "location0" / ("LEVEL" + std::to_string(level));
}

// ---------------------------------------------------------------------------
// QVM bytecode -> GlobalLightKeyframe arg tokens
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
// OBJECTS.QVM is exactly ["Task_DeclareParameters","Task_New","TRUE","FALSE"].
// That is how a boolean literal reaches the task: `PUSHIIB 3` is FALSE.
bool IsIdentifierPush(QVMOpType type) {
    return type == QVMOpType::PUSHII || type == QVMOpType::PUSHIIB ||
           type == QVMOpType::PUSHIIW;
}

std::string FormatFloat(float value) {
    char buffer[64];
    // %.9g round-trips a float exactly; the token is re-parsed with strtof by
    // mission_fog.cpp, so this keeps the byte-for-byte value.
    std::snprintf(buffer, sizeof(buffer), "%.9g", value);
    return buffer;
}

// Walks one authored GlobalLightKeyframe call and returns its arguments in the
// editor's argTokens layout: [0]=task id, [1]=type, [2]=name, [3]=parameter 0.
//
// The declaration call (`Task_DeclareParameters("GlobalLightKeyframe", ...)`)
// also pushes the type string, so the two are told apart structurally: after the
// type comes the instance NAME string, and after that a declaration has another
// parameter-name string while an instance has a value.
std::vector<std::string> ReadKeyframeCall(const QVMFile& qvm, std::size_t type_index) {
    std::vector<std::string> tokens;
    tokens.push_back("-1");                      // task id, see below
    tokens.push_back("GlobalLightKeyframe");     // type

    const std::size_t kExpected = 2 + 1 + static_cast<std::size_t>(igi::kGlobalLightKeyframeParams);

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
        // Anything else ends the call's argument list.
        break;
    }
    return tokens;
}

// Every authored GlobalLightKeyframe in the file, in file order.
std::vector<std::vector<std::string>> ExtractKeyframes(const QVMFile& qvm) {
    std::vector<std::vector<std::string>> out;
    for (std::size_t i = 0; i < qvm.instructions.size(); ++i) {
        const QVMInstruction& in = qvm.instructions[i];
        if (!IsStringPush(in.type)) continue;
        if (in.operand >= qvm.strings.size()) continue;
        if (qvm.strings[in.operand] != "GlobalLightKeyframe") continue;

        std::size_t name = i + 1;
        while (name < qvm.instructions.size() && qvm.instructions[name].type == QVMOpType::BRK) ++name;
        if (name >= qvm.instructions.size() || !IsStringPush(qvm.instructions[name].type)) continue;

        std::size_t first = name + 1;
        while (first < qvm.instructions.size() && qvm.instructions[first].type == QVMOpType::BRK) ++first;
        if (first >= qvm.instructions.size() || IsStringPush(qvm.instructions[first].type)) continue;

        out.push_back(ReadKeyframeCall(qvm, i));
    }
    return out;
}

// ---------------------------------------------------------------------------
// objects.qsc text -> the same tokens, for the cross-check
// ---------------------------------------------------------------------------

std::string ReadFile(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return {};
    return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

// Splits the argument list of every `Task_New(..., "GlobalLightKeyframe", ...)`
// call in a decompiled .qsc, honouring quoted strings and nested parentheses.
std::vector<std::vector<std::string>> ParseKeyframeCallsFromText(const std::string& text) {
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
            if (escape) { escape = false; continue; }
            if (in_string && c == '\\') { escape = true; continue; }
            if (c == '"') { in_string = !in_string; continue; }
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
            if (escape) { current.push_back(c); escape = false; continue; }
            if (in_string && c == '\\') { current.push_back(c); escape = true; continue; }
            if (c == '"') { in_string = !in_string; current.push_back(c); continue; }
            if (!in_string && c == '(') ++depth;
            if (!in_string && c == ')') --depth;
            if (!in_string && depth == 0 && c == ',') { args.push_back(current); current.clear(); continue; }
            current.push_back(c);
        }
        if (!current.empty()) args.push_back(current);

        for (std::string& arg : args) {
            const std::size_t b = arg.find_first_not_of(" \t\r\n");
            const std::size_t e = arg.find_last_not_of(" \t\r\n");
            arg = (b == std::string::npos) ? std::string() : arg.substr(b, e - b + 1);
            if (arg.size() >= 2 && arg.front() == '"' && arg.back() == '"') {
                arg = arg.substr(1, arg.size() - 2);
            }
        }

        if (args.size() > 1 && args[1] == "GlobalLightKeyframe") out.push_back(args);
        // The decompiled .qsc is a nested Task_New tree, so a parent call's body
        // contains its children. Resume just past this call's own name rather
        // than past its matching paren, otherwise every nested call is skipped.
        pos = text.find(needle, pos + needle.size());
    }
    return out;
}

// True when both tokens denote the same authored value: equal as float when both
// parse as numbers, equal as strings otherwise (TRUE/FALSE, names).
bool SameToken(const std::string& a, const std::string& b) {
    char* end_a = nullptr;
    char* end_b = nullptr;
    const float fa = std::strtof(a.c_str(), &end_a);
    const float fb = std::strtof(b.c_str(), &end_b);
    const bool numeric_a = end_a != a.c_str() && *end_a == '\0';
    const bool numeric_b = end_b != b.c_str() && *end_b == '\0';
    if (numeric_a && numeric_b) return fa == fb;
    return a == b;
}

}  // namespace

// ---------------------------------------------------------------------------
// Float_Log2 is a bit trick, not a logarithm
// ---------------------------------------------------------------------------

TEST(MissionFogTest, FloatLog2IsTheBitTrickAndNotLog2) {
    // spline.c:13-20. (bits - 0x3F800000) >> 20 on the float's own bits.
    EXPECT_EQ(igi::FloatLog2(1.0f), 0u);      // 0x3F800000 is the boundary
    EXPECT_EQ(igi::FloatLog2(2.0f), 8u);      // 1 ulp of the exponent field
    EXPECT_EQ(igi::FloatLog2(4096.0f), 96u);  // 1 metre, kFixed = 4096
    EXPECT_EQ(igi::FloatLog2(1048576.0f), 160u);  // 2^20

    // Exact powers of two cannot tell the bit trick from 8*log2, because the
    // trick *is* that approximation. Values between powers can, and they are the
    // ones that matter: the result is 8*exponent + floor(8*mantissa), i.e. the
    // mantissa is treated linearly instead of logarithmically, so it differs
    // from 8*log2 by up to ~1.68 - about 1.3% of the 130 bias.
    EXPECT_EQ(igi::FloatLog2(1.1f), 0u);   // 8*log2(1.1) = 1.099 -> 1
    EXPECT_EQ(igi::FloatLog2(2.2f), 8u);   // 8*log2(2.2) = 9.10  -> 9
    // Worst case, just below the mantissa=0.5 boundary: the trick floors 3.9992
    // to 3 where 8*log2 says 4.68.
    EXPECT_EQ(igi::FloatLog2(1.4999f), 3u);

    // x <= 1 and every negative value return 0. The engine's compare is signed,
    // which is what makes a negative float return 0 rather than ~2048.
    EXPECT_EQ(igi::FloatLog2(0.5f), 0u);
    EXPECT_EQ(igi::FloatLog2(0.0f), 0u);
    EXPECT_EQ(igi::FloatLog2(-1.0f), 0u);
    EXPECT_EQ(igi::FloatLog2(-4096.0f), 0u);

    // ~8*log2(x) elsewhere, and the field saturates near 1023. The shift floors,
    // so 2^30 is exactly 240 while 1.07e9 - just under 2^30 - is 239.
    EXPECT_EQ(igi::FloatLog2(1073741824.0f), 240u);  // 2^30
    EXPECT_EQ(igi::FloatLog2(1.07e9f), 239u);
}

// ---------------------------------------------------------------------------
// The three falloff variants, and the exponential that actually renders
// ---------------------------------------------------------------------------

TEST(MissionFogTest, ExponentialFogMatchesTheEngineCoefficient) {
    // terraind3d.c:164 with gWorldScale = 0.001 (direct3d.c:532).
    // LEVEL1 authors density 1.89, so gTerrainFogScale = 1.89 * 0.0005 = 0.000945.
    const float scale = 1.89f * igi::kGlobalLightFogScale;
    EXPECT_NEAR(scale, 0.000945f, 1e-9f);

    // transmittance = exp(-depth * 0.001 * 0.000945)
    EXPECT_NEAR(igi::ExponentialFogTransmittance(0.0f, scale), 1.0f, 1e-6f);
    const float one_metre = kFixed;
    EXPECT_NEAR(igi::ExponentialFogTransmittance(one_metre, scale),
                std::exp(-1.0f * igi::kWorldScale * scale * one_metre), 1e-6f);

    // Visible by 100 m, heavy by 1 km - the answer to the open question.
    const float at_100m = igi::ExponentialFogAmount(100.0f * kFixed, scale);
    const float at_300m = igi::ExponentialFogAmount(300.0f * kFixed, scale);
    const float at_1km = igi::ExponentialFogAmount(1000.0f * kFixed, scale);
    EXPECT_NEAR(at_100m, 0.3210f, 0.002f);
    EXPECT_NEAR(at_300m, 0.6870f, 0.002f);
    EXPECT_NEAR(at_1km, 0.9793f, 0.002f);

    // Monotonic, and a zero scale means no fog at all.
    EXPECT_GT(at_300m, at_100m);
    EXPECT_GT(at_1km, at_300m);
    EXPECT_EQ(igi::ExponentialFogAmount(1.0e9f, 0.0f), 0.0f);
}

TEST(MissionFogTest, EFoldingDistanceIsTheEngineFogDistance) {
    // 1 / (kWorldScale * scale) game units. For an authored density of 1.0:
    //   scale = 0.0005  ->  2 000 000 units  ->  488.28125 m at kFixed = 4096.
    const float unit_scale = 1.0f * igi::kGlobalLightFogScale;
    EXPECT_NEAR(igi::ExponentialFogEFoldingDistance(unit_scale), 2000000.0f, 1.0f);
    EXPECT_NEAR(igi::ExponentialFogEFoldingDistance(unit_scale) / kFixed, 488.28125f, 0.001f);

    // LEVEL1's authored 1.89 gives 258.35 m.
    const float level1_scale = 1.89f * igi::kGlobalLightFogScale;
    EXPECT_NEAR(igi::ExponentialFogEFoldingDistance(level1_scale) / kFixed, 258.35f, 0.05f);

    // The editor's existing `(1.0f / density) * 120.0f` is the same 1/density
    // shape with an invented constant; the engine's is 4.069x larger.
    EXPECT_NEAR(488.28125f / 120.0f, 4.0690f, 0.0001f);

    // Degenerate inputs are not distances.
    EXPECT_EQ(igi::ExponentialFogEFoldingDistance(0.0f), 0.0f);
    EXPECT_EQ(igi::ExponentialFogDistanceForFraction(unit_scale, 0.0f), 0.0f);
    EXPECT_EQ(igi::ExponentialFogDistanceForFraction(unit_scale, 1.0f), 0.0f);
}

TEST(MissionFogTest, FogComputeIsTheLog2ShapeDividedBy65536) {
    // light.c:55-72. depth is scaled by fov*0.83 + 0.17 when that is below 1.
    const float scale = 1.89f * igi::kGlobalLightFogScale;

    // fov 1.0 -> f = 1.0, so depth is untouched.
    const float fov_one = 1.0f * igi::kFovScale + igi::kFovBase;
    EXPECT_NEAR(fov_one, 1.0f, 1e-6f);

    // At 2^20 units Float_Log2 is 160, so (160 - 130) * scale / 65536.
    const float expected = (160.0f - 130.0f) * scale * igi::kFogScaleOneOver65536;
    EXPECT_NEAR(igi::FogComputeAmount(0.0f, 1048576.0f, scale, 1.0f), expected, 1e-9f);
    // Which is ~4.3e-7: this variant cannot reach a visible range at 1.89.
    EXPECT_LT(expected, 1e-6f);

    // Below the bias, and with a zero scale, the result clamps to 0.
    EXPECT_EQ(igi::FogComputeAmount(0.0f, 4096.0f, scale, 1.0f), 0.0f);
    EXPECT_EQ(igi::FogComputeAmount(0.0f, 1048576.0f, 0.0f, 1.0f), 0.0f);

    // The haze term is added before the clamp and is scaled by 1/65536, so a
    // caller-supplied 65536 is exactly 1.0 (i.e. fully fogged).
    // (The log2 term still subtracts a hair, 130 * scale / 65536 = 1.9e-6.)
    EXPECT_NEAR(igi::FogComputeAmount(65536.0f, 0.0f, scale, 1.0f), 1.0f, 1e-5f);
    // A fov that makes f < 1 shortens the effective depth, which for a depth
    // below the bias can only keep the result at 0; check a depth above it.
    const float wide = igi::FogComputeAmount(0.0f, 1048576.0f * 4.0f, scale, 0.5f);
    const float narrow = igi::FogComputeAmount(0.0f, 1048576.0f * 4.0f, scale, 1.0f);
    EXPECT_LT(wide, narrow);
}

TEST(MissionFogTest, LightmapTableNeedsADensityHundredsOfTimesLarger) {
    // terrainmesh.c:282 - an 0..255 lightmap value, not a 0..1 factor.
    const float level1_scale = 1.89f * igi::kGlobalLightFogScale;
    // At 2^30 units Float_Log2 is 240, so (240 - 130) * 0.000945 = 0.104.
    EXPECT_NEAR(igi::LightmapFogValue(1073741824.0f, level1_scale), 0.10395f, 1e-4f);
    // Out of 255 that is invisible: the table is effectively all-zero.
    EXPECT_LT(igi::LightmapFogValue(1073741824.0f, level1_scale), 1.0f);

    // It needs an authored density of ~1400 to reach even 77 of 255 at 2^30.
    const float huge = 1400.0f * igi::kGlobalLightFogScale;
    EXPECT_NEAR(igi::LightmapFogValue(1073741824.0f, huge), 77.0f, 0.5f);

    EXPECT_EQ(igi::LightmapFogValue(4096.0f, huge), 0.0f);   // below the bias
    EXPECT_EQ(igi::LightmapFogValue(1.0e30f, 1.0e6f), 255.0f);  // clamped
}

TEST(MissionFogTest, FlatWaterCarriesTheMissingGain) {
    // flatwater.c:421 - the same log2 shape with an extra k2000 * k256th.
    EXPECT_FLOAT_EQ(igi::kFlatWaterFogGain, 7.8125f);

    const float level1_scale = 1.89f * igi::kGlobalLightFogScale;
    // At 2^20 (256 m): (160 - 130) * 0.000945 * 7.8125 = 0.2215.
    EXPECT_NEAR(igi::FlatWaterFogAmount(1048576.0f, level1_scale), 0.2215f, 1e-3f);
    // At 2^30 (~262 km, i.e. far beyond any level) it saturates near 0.81.
    EXPECT_NEAR(igi::FlatWaterFogAmount(1073741824.0f, level1_scale), 0.8121f, 1e-3f);

    // This is the only one of the three log2 variants that is in range at the
    // authored densities, which is why the module documents them separately.
    EXPECT_GT(igi::FlatWaterFogAmount(1048576.0f, level1_scale), 0.1f);
    EXPECT_LT(igi::FogComputeAmount(0.0f, 1048576.0f, level1_scale, 1.0f), 1e-6f);
    EXPECT_LT(igi::LightmapFogValue(1048576.0f, level1_scale), 1.0f);

    // A depth just past the onset whose mantissa exposes the bit trick: at
    // 144179 units Float_Log2 is 136, while a real 8*log2 would say 137. That is
    // a 17% difference in the fog factor, so the rendered path cannot silently
    // swap in std::log2.
    EXPECT_EQ(igi::FloatLog2(144179.0f), 136u);
    EXPECT_NEAR(igi::FlatWaterFogAmount(144179.0f, level1_scale), 0.0443f, 1e-3f);

    EXPECT_EQ(igi::FlatWaterFogAmount(4096.0f, level1_scale), 0.0f);
    EXPECT_EQ(igi::FlatWaterFogAmount(1.0e30f, 1.0e6f), 1.0f);
}

TEST(MissionFogTest, FastLengthIsTheEngineEstimate) {
    // file/vec.c:140-181: max + (sqrt2-1)*mid + (sqrt3-sqrt2)*min. It is exact
    // on axis-aligned and equal-component vectors.
    const float axis[3] = {3.0f, 0.0f, 0.0f};
    EXPECT_FLOAT_EQ(igi::FastLength3(axis), 3.0f);
    const float diagonal[3] = {1.0f, 1.0f, 0.0f};
    EXPECT_NEAR(igi::FastLength3(diagonal), std::sqrt(2.0f), 1e-5f);
    const float cube[3] = {1.0f, 1.0f, 1.0f};
    EXPECT_NEAR(igi::FastLength3(cube), std::sqrt(3.0f), 1e-5f);

    // Signs do not matter, and an over-estimate is expected off the specials.
    const float negative[3] = {-1.0f, -1.0f, -1.0f};
    EXPECT_FLOAT_EQ(igi::FastLength3(negative), igi::FastLength3(cube));
    // Off the specials it over-estimates: (1,2,3) gives 4.1463 for sqrt(14) =
    // 3.7417, i.e. +10.8%. It never under-estimates.
    const float mixed[3] = {1.0f, 2.0f, 3.0f};
    EXPECT_NEAR(igi::FastLength3(mixed), 3.0f + 0.41421357f * 2.0f + 0.31783724f, 1e-5f);
    EXPECT_GT(igi::FastLength3(mixed), std::sqrt(14.0f));
    EXPECT_LT(igi::FastLength3(mixed), std::sqrt(14.0f) * 1.12f);
}

// ---------------------------------------------------------------------------
// ResolveMissionFog
// ---------------------------------------------------------------------------

namespace {

// Builds a keyframe token vector with 51 parameters in the declared order.
std::vector<std::string> KeyframeTokens(float fog_density, float time,
                                        float fog_r = 0.5f, float fog_g = 0.5f, float fog_b = 0.5f) {
    std::vector<std::string> t;
    t.push_back("-1");
    t.push_back("GlobalLightKeyframe");
    t.push_back("");
    t.push_back("FALSE");                 // 0  Link All Sliders
    for (int set = 0; set < 5; ++set) {
        t.push_back("0");                 // ambient R
        t.push_back("0");                 // ambient G
        t.push_back("0");                 // ambient B
        t.push_back(std::to_string(fog_r));
        t.push_back(std::to_string(fog_g));
        t.push_back(std::to_string(fog_b));
        t.push_back(std::to_string(fog_density));
        t.push_back(std::to_string(set - 1));  // link
    }
    t.push_back("0.5"); t.push_back("0.55"); t.push_back("0.6");  // 41..43 Sky color
    t.push_back("0"); t.push_back("0"); t.push_back("0");         // 44..46 Water ambient
    t.push_back("1"); t.push_back("1"); t.push_back("1");         // 47..49 Water color
    t.push_back(std::to_string(time));                            // 50 Time
    return t;
}

}  // namespace

TEST(MissionFogTest, ResolverReadsTheDeclaredParameterOrder) {
    std::vector<igi::FogTaskObject> objects;
    objects.push_back({"GlobalLightKeyframe", KeyframeTokens(1.89f, 12.0f, 0.576923f, 0.538462f, 0.480769f)});

    const igi::MissionFog fog = igi::ResolveMissionFog(objects);
    ASSERT_TRUE(fog.authored);
    EXPECT_EQ(fog.keyframe_count, 1);
    EXPECT_EQ(fog.keyframe_index, 0);
    EXPECT_FLOAT_EQ(fog.time_seconds, 12.0f);

    EXPECT_FLOAT_EQ(fog.sets[0].fog[0], 0.576923f);
    EXPECT_FLOAT_EQ(fog.sets[0].fog[1], 0.538462f);
    EXPECT_FLOAT_EQ(fog.sets[0].fog[2], 0.480769f);
    EXPECT_FLOAT_EQ(fog.sets[0].density, 1.89f);
    EXPECT_FLOAT_EQ(fog.sets[0].link, -1.0f);
    EXPECT_FLOAT_EQ(fog.terrain_fog_scale(), 1.89f * 0.0005f);

    // The later sets must not be read out of the terrain block.
    EXPECT_FLOAT_EQ(fog.sets[4].link, 3.0f);
    EXPECT_FLOAT_EQ(fog.sky_color[0], 0.5f);
    EXPECT_FLOAT_EQ(fog.sky_color[2], 0.6f);
    EXPECT_FLOAT_EQ(fog.water_color[0], 1.0f);
}

TEST(MissionFogTest, ResolverPicksTheSmallestTimeNotTheFirstKeyframe) {
    // The decompiler does not always emit keyframes in chronological order, and
    // Level::LoadFogInfo selects the smallest Time for the same reason.
    std::vector<igi::FogTaskObject> objects;
    objects.push_back({"GlobalLightKeyframe", KeyframeTokens(0.0f, 100.0f)});   // night
    objects.push_back({"GlobalLightKeyframe", KeyframeTokens(1.2f, 12.0f)});    // day

    const igi::MissionFog fog = igi::ResolveMissionFog(objects);
    ASSERT_TRUE(fog.authored);
    EXPECT_EQ(fog.keyframe_count, 2);
    EXPECT_EQ(fog.keyframe_index, 1);
    EXPECT_FLOAT_EQ(fog.time_seconds, 12.0f);
    EXPECT_FLOAT_EQ(fog.sets[0].density, 1.2f);
}

TEST(MissionFogTest, ResolverIgnoresOtherTasksAndShortKeyframes) {
    std::vector<igi::FogTaskObject> objects;
    objects.push_back({"GlobalLight", {"4", "GlobalLight", "GlobalLight", "36.5"}});
    objects.push_back({"GlobalLightKeyframe", {"-1", "GlobalLightKeyframe", ""}});  // truncated
    objects.push_back({"RainEffect", {"4", "RainEffect", "Rain"}});

    const igi::MissionFog fog = igi::ResolveMissionFog(objects);
    EXPECT_FALSE(fog.authored);
    EXPECT_EQ(fog.keyframe_count, 1);   // counted, but not usable
    EXPECT_EQ(fog.keyframe_index, -1);
}

TEST(MissionFogTest, TerrainScaleIsClampedToTheEngineCeiling) {
    // terrainmesh.c:302-320 clamps gTerrainFogScale to 0..16384.
    std::vector<igi::FogTaskObject> objects;
    objects.push_back({"GlobalLightKeyframe", KeyframeTokens(1.0e9f, 1.0f)});
    const igi::MissionFog fog = igi::ResolveMissionFog(objects);
    ASSERT_TRUE(fog.authored);
    EXPECT_FLOAT_EQ(fog.sets[0].density, 1.0e9f);              // authored value kept
    EXPECT_FLOAT_EQ(fog.terrain_fog_scale(), igi::kTerrainFogScaleMax);

    // 16384 / 0.0005 = 32 768 000 is the largest density that matters.
    std::vector<igi::FogTaskObject> negative;
    negative.push_back({"GlobalLightKeyframe", KeyframeTokens(-5.0f, 1.0f)});
    EXPECT_FLOAT_EQ(igi::ResolveMissionFog(negative).terrain_fog_scale(), 0.0f);
}

// ---------------------------------------------------------------------------
// Haze_GetFog
// ---------------------------------------------------------------------------

TEST(MissionFogTest, HazeBoxesContributeDensityAndColourAlongTheRay) {
    // haze.c:196-240. The eye is inside the box, looking up out of it.
    igi::HazeBox box;
    box.density = 2.0f;
    box.bottom = 0.0f;
    box.top = 1000.0f;
    box.color[0] = 0.25f;
    box.color[1] = 0.5f;
    box.color[2] = 0.75f;
    box.visible = true;

    const float eye[3] = {0.0f, 0.0f, 500.0f};
    const float point[3] = {0.0f, 0.0f, 900.0f};
    float color[3] = {0.0f, 0.0f, 0.0f};
    const float density = igi::HazeGetFog(&box, 1, eye, point, color);

    // The whole 400-unit segment lies inside the box, so v = len and the colour
    // is the box's own.
    EXPECT_NEAR(density, 400.0f * 2.0f, 0.5f);
    EXPECT_NEAR(color[0], 0.25f, 1e-4f);
    EXPECT_NEAR(color[1], 0.5f, 1e-4f);
    EXPECT_NEAR(color[2], 0.75f, 1e-4f);

    // Invisible boxes do not participate.
    box.visible = false;
    float hidden[3] = {9.0f, 9.0f, 9.0f};
    EXPECT_FLOAT_EQ(igi::HazeGetFog(&box, 1, eye, point, hidden), 0.0f);

    // Neither does a ray that never enters the box.
    box.visible = true;
    const float above[3] = {0.0f, 0.0f, 2000.0f};
    EXPECT_FLOAT_EQ(igi::HazeGetFog(&box, 1, above, above, color), 0.0f);
}

TEST(MissionFogTest, TwoHazeBoxesAverageByDensity) {
    igi::HazeBox boxes[2];
    for (int i = 0; i < 2; ++i) {
        boxes[i].density = 1.0f;
        boxes[i].bottom = 0.0f;
        boxes[i].top = 1000.0f;
        boxes[i].visible = true;
    }
    boxes[0].color[0] = 0.0f;
    boxes[0].color[1] = 0.0f;
    boxes[0].color[2] = 0.0f;
    boxes[1].color[0] = 1.0f;
    boxes[1].color[1] = 1.0f;
    boxes[1].color[2] = 1.0f;

    const float eye[3] = {0.0f, 0.0f, 500.0f};
    const float point[3] = {0.0f, 0.0f, 900.0f};
    float color[3] = {0.0f, 0.0f, 0.0f};
    igi::HazeGetFog(boxes, 2, eye, point, color);

    // Equal densities and equal segment lengths -> a plain mean.
    EXPECT_NEAR(color[0], 0.5f, 1e-4f);
    EXPECT_NEAR(color[1], 0.5f, 1e-4f);
    EXPECT_NEAR(color[2], 0.5f, 1e-4f);
}

// ---------------------------------------------------------------------------
// Retail corpus: the 14-level fog inventory from OBJECTS.QVM
// ---------------------------------------------------------------------------

TEST(MissionFogCorpusTest, AllFourteenLevelsHaveAuthoredFog) {
    if (!fs::is_regular_file(LevelDir(1) / "OBJECTS.QVM")) {
        GTEST_SKIP() << "retail IGI not found at " << GameRoot().string()
                     << " (set IGI_GAME_PATH to point at it)";
    }

    std::printf("\n  level  density   scale       fog RGB                    time   e-fold(m)  @100m  @300m  @1km\n");
    for (int level = 1; level <= 14; ++level) {
        const fs::path path = LevelDir(level) / "OBJECTS.QVM";
        ASSERT_TRUE(fs::is_regular_file(path)) << path.string();

        const QVMFile qvm = QVM_Parse(path.string());
        ASSERT_TRUE(qvm.valid) << path.string() << ": " << qvm.error;

        const std::vector<std::vector<std::string>> keyframes = ExtractKeyframes(qvm);
        ASSERT_FALSE(keyframes.empty()) << path.string() << ": no GlobalLightKeyframe";

        std::vector<igi::FogTaskObject> objects;
        for (const std::vector<std::string>& tokens : keyframes) {
            objects.push_back({"GlobalLightKeyframe", tokens});
        }

        const igi::MissionFog fog = igi::ResolveMissionFog(objects);
        ASSERT_TRUE(fog.authored) << "level " << level;

        const float scale = fog.terrain_fog_scale();
        const float efolding = igi::ExponentialFogEFoldingDistance(scale) / kFixed;
        const float at_100 = igi::ExponentialFogAmount(100.0f * kFixed, scale) * 100.0f;
        const float at_300 = igi::ExponentialFogAmount(300.0f * kFixed, scale) * 100.0f;
        const float at_1km = igi::ExponentialFogAmount(1000.0f * kFixed, scale) * 100.0f;

        std::printf("  %5d  %7.3f  %10.8f  (%.4f, %.4f, %.4f)  %5.1f  %9.1f  %4.1f%%  %4.1f%%  %4.1f%%\n",
                    level, fog.sets[0].density, scale, fog.sets[0].fog[0], fog.sets[0].fog[1],
                    fog.sets[0].fog[2], fog.time_seconds, efolding, at_100, at_300, at_1km);

        // Every retail level authors a positive density, so every level must
        // produce a finite, non-zero fog distance and a visible fog amount.
        EXPECT_GT(fog.sets[0].density, 0.0f) << "level " << level;
        EXPECT_GT(scale, 0.0f) << "level " << level;
        EXPECT_GT(efolding, 1.0f) << "level " << level;
        EXPECT_GT(at_100, 1.0f) << "level " << level;

        // The colour is a normalised RGB triple.
        for (int c = 0; c < 3; ++c) {
            EXPECT_GE(fog.sets[0].fog[c], 0.0f) << "level " << level;
            EXPECT_LE(fog.sets[0].fog[c], 1.0f) << "level " << level;
        }

        // Every level authors two keyframes: a daytime one and a Time=100 night
        // state with density 0. The resolver must have picked the daytime one.
        EXPECT_EQ(fog.keyframe_count, 2) << "level " << level;
        EXPECT_LT(fog.time_seconds, 100.0f) << "level " << level;
    }
}

TEST(MissionFogCorpusTest, Level1AndLevel4MatchTheDecompiledText) {
    // LEVEL1 and LEVEL4 are the only levels that ship a decompiled objects.qsc
    // next to the binary OBJECTS.QVM, so they are the only ones where the
    // bytecode walk above can be checked against human-readable authored data.
    for (int level : {1, 4}) {
        const fs::path dir = LevelDir(level);
        const fs::path qsc = dir / "objects.qsc";
        if (!fs::is_regular_file(qsc) || !fs::is_regular_file(dir / "OBJECTS.QVM")) {
            GTEST_SKIP() << "retail IGI (with objects.qsc) not found at " << GameRoot().string();
        }

        const QVMFile qvm = QVM_Parse((dir / "OBJECTS.QVM").string());
        ASSERT_TRUE(qvm.valid) << qvm.error;

        const std::vector<std::vector<std::string>> from_binary = ExtractKeyframes(qvm);
        const std::vector<std::vector<std::string>> from_text =
            ParseKeyframeCallsFromText(ReadFile(qsc));

        ASSERT_FALSE(from_binary.empty()) << "level " << level;
        ASSERT_EQ(from_binary.size(), from_text.size()) << "level " << level;

        for (std::size_t k = 0; k < from_binary.size(); ++k) {
            // The task id is synthesised for the binary walk, so compare from the
            // type token onwards; everything after it is authored data.
            ASSERT_EQ(from_binary[k].size(), from_text[k].size())
                << "level " << level << " keyframe " << k;
            EXPECT_EQ(from_binary[k][1], from_text[k][1]) << "level " << level;
            for (std::size_t i = 2; i < from_binary[k].size(); ++i) {
                EXPECT_TRUE(SameToken(from_binary[k][i], from_text[k][i]))
                    << "level " << level << " keyframe " << k << " token " << i
                    << ": binary \"" << from_binary[k][i] << "\" vs text \""
                    << from_text[k][i] << "\"";
            }
        }
    }
}

TEST(MissionFogCorpusTest, Level1FogMatchesTheShippedNumbers) {
    const fs::path path = LevelDir(1) / "OBJECTS.QVM";
    if (!fs::is_regular_file(path)) {
        GTEST_SKIP() << "retail IGI not found at " << GameRoot().string();
    }

    const QVMFile qvm = QVM_Parse(path.string());
    ASSERT_TRUE(qvm.valid) << qvm.error;

    std::vector<igi::FogTaskObject> objects;
    for (const std::vector<std::string>& tokens : ExtractKeyframes(qvm)) {
        objects.push_back({"GlobalLightKeyframe", tokens});
    }
    const igi::MissionFog fog = igi::ResolveMissionFog(objects);
    ASSERT_TRUE(fog.authored);

    // LEVEL1 objects.qsc line 82: fog colour (0.57692301273345947,
    // 0.53846198320388794, 0.48076900839805603), density 1.8899999856948853,
    // Time 12.0. Pinning the full-precision values keeps the bytecode walk
    // honest: a mis-aligned token index would land on a neighbouring field.
    EXPECT_FLOAT_EQ(fog.sets[0].fog[0], 0.57692301273345947f);
    EXPECT_FLOAT_EQ(fog.sets[0].fog[1], 0.53846198320388794f);
    EXPECT_FLOAT_EQ(fog.sets[0].fog[2], 0.48076900839805603f);
    EXPECT_FLOAT_EQ(fog.sets[0].density, 1.8899999856948853f);
    EXPECT_FLOAT_EQ(fog.time_seconds, 12.0f);

    // publish(): gTerrainFogScale = density * 0.0005 = 0.000945.
    EXPECT_FLOAT_EQ(fog.terrain_fog_scale(), 0.00094499996f);

    // And the resulting engine fog distance for LEVEL1's haze.
    EXPECT_NEAR(igi::ExponentialFogEFoldingDistance(fog.terrain_fog_scale()) / kFixed,
                258.35f, 0.05f);
}
