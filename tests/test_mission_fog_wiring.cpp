// test_mission_fog_wiring.cpp - the ONE fog path, and the proof that wiring it
// changed no number.
//
// BEFORE (commit ebf9f815, source/level/level.cpp:452-543): Level::LoadFogInfo
// carried its own inline parse of the level's authored GlobalLightKeyframe --
// find every "GlobalLightKeyframe" in the QSC, read "Fog color terrain" from QSC
// args 7..9 and "Fog density terrain" from arg 10, keep the keyframe with the
// smallest Time, and turn the density into a linear-fog distance with
// `(1.0f / density) * 488.28125f`.
//
// AFTER: LoadFogInfo calls igi::ResolveAuthoredFogFromQsc (source/level/fog_qsc.h)
// -> igi::ResolveMissionFog (source/runtime/mission_fog.cpp) ->
// igi::LinearFogFarMetresForAuthoredDensity.
//
// This suite reproduces the OLD algorithm verbatim as LegacyFogFromQsc (used by
// the Windows-lane half below) and, for the host-native half, its token-space
// image LegacyFogFromTokens, and requires the NEW path to produce BIT-IDENTICAL
// values on the retail corpus. A future change to any fog number fails here.
//
// The oracle for every rule involved is IGI-RE; the citations are in
// source/runtime/mission_fog.h and source/level/fog_qsc.h.

// gtest first: source/common.h (pulled in by level_common.h) uses std::exception,
// FILE and va_list without including <exception>/<cstdio>/<cstdarg> itself, which
// the Windows PCH supplies and the host lane does not. The other host-portable
// suites that include level_common.h do the same.
#include <gtest/gtest.h>

#include "../source/level/fog_qsc.h"
#include "../source/level/level_common.h"
#include "support/mission_fog_corpus.h"
#include "support/temp_directory.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#if defined(_WIN32)
// The end-to-end half needs QSC::Load, which calls File_LoadText/File_FreeBuf in
// source/common.cpp -- a Windows-only translation unit (`#error "unsupported
// platform"` on every other target), so a host-native binary cannot link it. The
// host half below therefore drives the same rules through the QVM bytecode walk,
// which is cross-checked against the two shipped decompiled objects.qsc files.
#include "../source/level/qvm_decompiler.h"
#include "../source/utils.h"
#endif

namespace {

using namespace test_support;

// The pre-refactor defaults, and the two values LoadFogInfo hands to SetupFog.
struct FogValues {
    bool authored = false;
    float color[3] = {0.15f, 0.15f, 0.15f};
    float far_meters = 30000.0f;
};

std::string FloatBits(float value) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "0x%08x", bits);
    return buffer;
}

// Bit-for-bit, not EXPECT_FLOAT_EQ's four-ulp tolerance: this refactor claims to
// change no number at all, and one ulp in the density is a different fog.
void ExpectSameFog(const FogValues& legacy, const FogValues& wired, const std::string& where) {
    EXPECT_EQ(legacy.authored, wired.authored) << where;
    EXPECT_EQ(FloatBits(legacy.color[0]), FloatBits(wired.color[0])) << where << " fog colour R";
    EXPECT_EQ(FloatBits(legacy.color[1]), FloatBits(wired.color[1])) << where << " fog colour G";
    EXPECT_EQ(FloatBits(legacy.color[2]), FloatBits(wired.color[2])) << where << " fog colour B";
    EXPECT_EQ(FloatBits(legacy.far_meters), FloatBits(wired.far_meters)) << where << " fog far (m)";
    // And the value the call site actually hands over: level.cpp converts to
    // world units with WORLD_UNITS_PER_METER (common.h:40) before SetupFog.
    EXPECT_EQ(FloatBits(legacy.far_meters * 4096.0f), FloatBits(wired.far_meters * 4096.0f))
        << where << " fog far (world units)";
}

// ---------------------------------------------------------------------------
// The "before": Level::LoadFogInfo as it stood at ebf9f815, copied verbatim
// except for the renderer hand-off (render_res_loader->SetupFog) and the
// glm::vec4 wrapper, neither of which computes a fog number.
//
// Windows lane only: it reads a parsed QSC, and QSC::Load needs
// source/common.cpp, which is a Windows-only translation unit.
// ---------------------------------------------------------------------------
#if defined(_WIN32)
FogValues LegacyFogFromQsc(const QSC& qsc_objects) {
    FogValues out;

    const QSC::func_s* qsc_funcs[1024];
    int num_func = qsc_objects.FindFuncByStr("GlobalLightKeyframe", qsc_funcs);

    bool haveBest = false;
    float bestTime = 0.0f;
    float bestFogColor[3] = {out.color[0], out.color[1], out.color[2]};
    float bestFogFarMeters = out.far_meters;

    for (int fi = 0; fi < num_func; ++fi) {
        const QSC::func_s* f = qsc_funcs[fi];

        float curFogColor[3] = {out.color[0], out.color[1], out.color[2]};
        float curFogDensity = 0.0f;
        float curTime = 0.0f;
        bool haveDensity = false;

        int arg_idx = 0;
        const QSC::arg_s* a = f->args_;
        while (a) {
            if (a->type_ == QSC::arg_s::type_t::DBL) {
                switch (arg_idx) {
                case 7: curFogColor[0] = (float)a->dbl_; break;
                case 8: curFogColor[1] = (float)a->dbl_; break;
                case 9: curFogColor[2] = (float)a->dbl_; break;
                case 10: curFogDensity = (float)a->dbl_; haveDensity = true; break;
                }
                curTime = (float)a->dbl_;
            }
            a = a->next_;
            arg_idx++;
        }

        if (haveDensity && (!haveBest || curTime < bestTime)) {
            haveBest = true;
            bestTime = curTime;
            bestFogColor[0] = curFogColor[0];
            bestFogColor[1] = curFogColor[1];
            bestFogColor[2] = curFogColor[2];
            bestFogFarMeters = (curFogDensity > 0.0f) ? (1.0f / curFogDensity) * 488.28125f : out.far_meters;
        }
    }

    if (haveBest) {
        out.authored = true;
        out.color[0] = bestFogColor[0];
        out.color[1] = bestFogColor[1];
        out.color[2] = bestFogColor[2];
        out.far_meters = bestFogFarMeters;
    }
    return out;
}
#endif  // _WIN32

// The token-space image of the loop above, for the host-native half. It is the
// same algorithm with "the argument is a DBL" read as "the token parses as a
// float", which is exactly how the token vectors are built: the QVM walk emits
// "TRUE"/"FALSE"/strings for non-numeric arguments and a float-exact decimal for
// every numeric one, and ParseKeyframeCallsFromText does the same for .qsc text.
// So for retail data the two are the same function; the Windows-lane test below
// pins that by comparing this image against the verbatim arg-level copy.
bool TokenIsNumber(const std::string& token) {
    if (token.empty()) return false;
    char* end = nullptr;
    std::strtof(token.c_str(), &end);
    return end != token.c_str() && *end == '\0';
}

FogValues LegacyFogFromTokens(const std::vector<std::vector<std::string>>& keyframes) {
    FogValues out;

    bool haveBest = false;
    float bestTime = 0.0f;
    float bestFogColor[3] = {out.color[0], out.color[1], out.color[2]};
    float bestFogFarMeters = out.far_meters;

    for (const std::vector<std::string>& tokens : keyframes) {
        float curFogColor[3] = {out.color[0], out.color[1], out.color[2]};
        float curFogDensity = 0.0f;
        float curTime = 0.0f;
        bool haveDensity = false;

        for (std::size_t arg_idx = 0; arg_idx < tokens.size(); ++arg_idx) {
            if (!TokenIsNumber(tokens[arg_idx])) continue;
            switch (arg_idx) {
            case 7: curFogColor[0] = std::strtof(tokens[arg_idx].c_str(), nullptr); break;
            case 8: curFogColor[1] = std::strtof(tokens[arg_idx].c_str(), nullptr); break;
            case 9: curFogColor[2] = std::strtof(tokens[arg_idx].c_str(), nullptr); break;
            case 10: curFogDensity = std::strtof(tokens[arg_idx].c_str(), nullptr); haveDensity = true; break;
            }
            curTime = std::strtof(tokens[arg_idx].c_str(), nullptr);
        }

        if (haveDensity && (!haveBest || curTime < bestTime)) {
            haveBest = true;
            bestTime = curTime;
            bestFogColor[0] = curFogColor[0];
            bestFogColor[1] = curFogColor[1];
            bestFogColor[2] = curFogColor[2];
            bestFogFarMeters = (curFogDensity > 0.0f) ? (1.0f / curFogDensity) * 488.28125f : out.far_meters;
        }
    }

    if (haveBest) {
        out.authored = true;
        out.color[0] = bestFogColor[0];
        out.color[1] = bestFogColor[1];
        out.color[2] = bestFogColor[2];
        out.far_meters = bestFogFarMeters;
    }
    return out;
}

// ---------------------------------------------------------------------------
// The "after": exactly what Level::LoadFogInfo does now.
// ---------------------------------------------------------------------------
FogValues WiredFogFromTokens(const std::vector<std::vector<std::string>>& keyframes) {
    FogValues out;

    const igi::MissionFog fog = igi::ResolveMissionFog(KeyframesAsFogTasks(keyframes));
    if (fog.authored) {
        out.authored = true;
        out.color[0] = fog.sets[0].fog[0];
        out.color[1] = fog.sets[0].fog[1];
        out.color[2] = fog.sets[0].fog[2];
        out.far_meters = igi::LinearFogFarMetresForAuthoredDensity(fog.sets[0].density, out.far_meters);
    }
    return out;
}

// The same, from a parsed QSC (the editor's real input).
#if defined(_WIN32)
FogValues WiredFogFromQsc(const QSC& qsc_objects) {
    FogValues out;

    const igi::MissionFog fog = igi::ResolveAuthoredFogFromQsc(qsc_objects);
    if (fog.authored) {
        out.authored = true;
        out.color[0] = fog.sets[0].fog[0];
        out.color[1] = fog.sets[0].fog[1];
        out.color[2] = fog.sets[0].fog[2];
        out.far_meters = igi::LinearFogFarMetresForAuthoredDensity(fog.sets[0].density, out.far_meters);
    }
    return out;
}
#endif  // _WIN32

// The 54-argument chain a retail GlobalLightKeyframe parses to, built by hand so
// the bridge's positional layout can be tested without QSC::Load.
struct ArgChain {
    std::vector<QSC::arg_s> args;
    QSC::func_s func{};
};

ArgChain MakeKeyframeArgs(float density, float time, float r, float g, float b) {
    ArgChain chain;
    chain.args.resize(54);

    for (std::size_t i = 0; i < chain.args.size(); ++i) {
        QSC::arg_s& arg = chain.args[i];
        arg.next_ = (i + 1 < chain.args.size()) ? &chain.args[i + 1] : nullptr;
        arg.is_float_ = true;
        arg.type_ = QSC::arg_s::type_t::DBL;
        arg.dbl_ = 0.0;
    }

    // [0]=task id, [1]=type, [2]=instance name: the three leading Task_New args.
    chain.args[0].dbl_ = -1.0;
    chain.args[1].type_ = QSC::arg_s::type_t::STR;
    chain.args[1].str_ = "GlobalLightKeyframe";
    chain.args[2].type_ = QSC::arg_s::type_t::STR;
    chain.args[2].str_ = "";

    // [3]=PushButton (a BOOL in the file), [4..6]=ambient terrain,
    // [7..9]=Fog color terrain, [10]=Fog density terrain, [11]=Link setting.
    chain.args[3].type_ = QSC::arg_s::type_t::BOOL;
    chain.args[3].bool_ = 0;
    chain.args[7].dbl_ = static_cast<double>(r);
    chain.args[8].dbl_ = static_cast<double>(g);
    chain.args[9].dbl_ = static_cast<double>(b);
    chain.args[10].dbl_ = static_cast<double>(density);
    chain.args[11].dbl_ = -1.0;
    chain.args[53].dbl_ = static_cast<double>(time);

    chain.func.func_name_ = "Task_New";
    chain.func.args_ = chain.args.data();
    return chain;
}

}  // namespace

// ---------------------------------------------------------------------------
// The constant, and the arithmetic that must not move
// ---------------------------------------------------------------------------

TEST(MissionFogWiringTest, FogDistanceConstantIsTheEngineEFoldingDistance) {
    // IGI-RE: 1 / (gWorldScale * gTerrainFogScale) = 1 / (0.001 * density * 0.0005)
    // = 2e6 / density world units (terraind3d.c:164, direct3d.c:532,
    // globallight.c:115,125), and 2e6 / 4096 units-per-metre = 488.28125 m.
    EXPECT_EQ(igi::kEFoldingMetresPerAuthoredDensity, 488.28125f);
    EXPECT_EQ(igi::kEditorWorldUnitsPerMetre, 4096.0f);
    EXPECT_EQ(igi::kEFoldingMetresPerAuthoredDensity * igi::kEditorWorldUnitsPerMetre, 2000000.0f);

    // The float-product form of the same derivation is one ulp LOW, which is why
    // the header keeps the exact literal: recomputing it from the constants would
    // silently move every level's fog distance. This assertion is the tripwire.
    const float from_constants =
        1.0f / (igi::kWorldScale * igi::kGlobalLightFogScale * igi::kEditorWorldUnitsPerMetre);
    EXPECT_EQ(FloatBits(from_constants), FloatBits(488.2812f));
    EXPECT_NE(FloatBits(from_constants), FloatBits(igi::kEFoldingMetresPerAuthoredDensity));
}

TEST(MissionFogWiringTest, FarDistanceIsThePreRefactorExpressionForEveryDensity) {
    // (1.0f / density) * 488.28125f on real retail densities. The bit patterns
    // are what the pre-refactor expression produced; a "cleaner" formula with the
    // same real value (for example ExponentialFogEFoldingDistance(scale) / kFixed,
    // which goes through long double) does NOT reproduce them exactly.
    struct Case { float density; float far_meters; };
    const Case cases[] = {
        {1.8899999856948853f, 258.349884033203125f},    // LEVEL1
        {0.18000000715255737f, 2712.673583984375f},     // LEVEL2/6/10
        {4.7899999618530273f, 101.93762969970703125f},  // LEVEL3
        {9.5f, 51.3980255126953125f},                   // LEVEL14
    };
    for (const Case& c : cases) {
        EXPECT_EQ(FloatBits(igi::LinearFogFarMetresForAuthoredDensity(c.density, 30000.0f)),
                  FloatBits(c.far_meters))
            << "density " << c.density;
    }

    // A density of 0 is the engine's "no fog at this keyframe" state (every
    // retail level's Time=100 keyframe), and keeps the caller's default.
    EXPECT_EQ(igi::LinearFogFarMetresForAuthoredDensity(0.0f, 30000.0f), 30000.0f);
    EXPECT_EQ(igi::LinearFogFarMetresForAuthoredDensity(-1.0f, 30000.0f), 30000.0f);
}

// ---------------------------------------------------------------------------
// The bridge: QSC argument chain -> mission_fog tokens
// ---------------------------------------------------------------------------

TEST(MissionFogWiringTest, BridgeTokenisesTheArgumentChainPositionally) {
    ArgChain chain = MakeKeyframeArgs(1.8899999856948853f, 12.0f, 0.57692301273345947f,
                                      0.53846198320388794f, 0.48076900839805603f);

    const igi::FogTaskObject object = igi::FogTaskObjectFromFunc(chain.func, "GlobalLightKeyframe");
    EXPECT_EQ(object.type, "GlobalLightKeyframe");

    // One token per argument, in order: declared parameter k is token k+3.
    ASSERT_EQ(object.arg_tokens.size(), 54u);
    EXPECT_EQ(object.arg_tokens[1], "\"GlobalLightKeyframe\"");  // type (STR)
    EXPECT_EQ(object.arg_tokens[2], "\"\"");                     // instance name (STR)
    EXPECT_EQ(object.arg_tokens[3], "FALSE");                    // PushButton (BOOL)
    EXPECT_EQ(object.arg_tokens[7], "0.576923013");              // Fog color terrain R
    EXPECT_EQ(object.arg_tokens[8], "0.538461983");              // Fog color terrain G
    EXPECT_EQ(object.arg_tokens[9], "0.480769008");              // Fog color terrain B
    EXPECT_EQ(object.arg_tokens[10], "1.88999999");              // Fog density terrain
    EXPECT_EQ(object.arg_tokens[11], "-1");                      // Link setting terrain
    EXPECT_EQ(object.arg_tokens[53], "12");                      // Time

    // And the tokens round-trip to the exact authored floats: %.9g is
    // FLT_DECIMAL_DIG, so strtof gives back the same float the pre-refactor code
    // got from `(float)a->dbl_`.
    const igi::MissionFog fog = igi::ResolveMissionFog({object});
    ASSERT_TRUE(fog.authored);
    EXPECT_EQ(FloatBits(fog.sets[0].density), FloatBits(1.8899999856948853f));
    EXPECT_EQ(FloatBits(fog.sets[0].fog[0]), FloatBits(0.57692301273345947f));
    EXPECT_EQ(FloatBits(fog.sets[0].fog[1]), FloatBits(0.53846198320388794f));
    EXPECT_EQ(FloatBits(fog.sets[0].fog[2]), FloatBits(0.48076900839805603f));
    EXPECT_EQ(FloatBits(fog.time_seconds), FloatBits(12.0f));
}

TEST(MissionFogWiringTest, KeyframeSelectionIsTheSmallestTime) {
    // The decompiler does not always emit keyframes in chronological order, so
    // "the first one" is not the level's default state (level 3 authors its
    // Time=100 night keyframe first).
    const std::vector<std::vector<std::string>> keyframes = {
        KeyframeTokens(0.0f, 100.0f),        // night
        KeyframeTokens(1.89f, 12.0f),        // day
    };

    const FogValues legacy = LegacyFogFromTokens(keyframes);
    const FogValues wired = WiredFogFromTokens(keyframes);
    ExpectSameFog(legacy, wired, "two keyframes");

    ASSERT_TRUE(wired.authored);
    EXPECT_EQ(FloatBits(wired.far_meters), FloatBits((1.0f / 1.89f) * igi::kEFoldingMetresPerAuthoredDensity));
}

TEST(MissionFogWiringTest, LevelWithoutAKeyframeKeepsTheDefaultsOnBothPaths) {
    const FogValues legacy = LegacyFogFromTokens({});
    const FogValues wired = WiredFogFromTokens({});

    ExpectSameFog(legacy, wired, "level with no keyframe");
    EXPECT_FALSE(wired.authored);
    EXPECT_EQ(FloatBits(wired.color[0]), FloatBits(0.15f));
    EXPECT_EQ(FloatBits(wired.far_meters), FloatBits(30000.0f));
}

// ---------------------------------------------------------------------------
// The retail corpus, host-native half: all 14 levels through the QVM walk
// ---------------------------------------------------------------------------

TEST(MissionFogWiringCorpusTest, AllFourteenLevelsResolveIdenticallyBeforeAndAfter) {
    if (!fs::is_regular_file(LevelDir(1) / "OBJECTS.QVM")) {
        GTEST_SKIP() << "retail IGI not found at " << GameRoot().string()
                     << " (set IGI_GAME_PATH to point at it)";
    }

    std::printf("\n  level   density     legacy far (m)        wired far (m)       fog RGB\n");
    for (int level = 1; level <= 14; ++level) {
        const fs::path qvm_path = LevelDir(level) / "OBJECTS.QVM";
        ASSERT_TRUE(fs::is_regular_file(qvm_path)) << qvm_path.string();

        const QVMFile qvm = QVM_Parse(qvm_path.string());
        ASSERT_TRUE(qvm.valid) << qvm_path.string() << ": " << qvm.error;

        const std::vector<std::vector<std::string>> keyframes = ExtractKeyframes(qvm);
        ASSERT_FALSE(keyframes.empty()) << qvm_path.string() << ": no GlobalLightKeyframe";
        for (const std::vector<std::string>& tokens : keyframes) {
            ASSERT_EQ(tokens.size(), 54u) << "level " << level << ": the walk must reach Time";
        }

        const FogValues legacy = LegacyFogFromTokens(keyframes);
        const FogValues wired = WiredFogFromTokens(keyframes);

        ExpectSameFog(legacy, wired, "level " + std::to_string(level));

        // Every retail level authors a positive daytime density, so neither path
        // may be falling back to the defaults.
        ASSERT_TRUE(wired.authored) << "level " << level;
        ASSERT_TRUE(legacy.authored) << "level " << level;
        EXPECT_GT(wired.far_meters, 1.0f) << "level " << level;

        const float density =
            igi::ResolveMissionFog(KeyframesAsFogTasks(keyframes)).sets[0].density;
        std::printf("  %5d   %9.4f   %20.6f   %20.6f   (%.4f, %.4f, %.4f)\n", level, density,
                    legacy.far_meters, wired.far_meters, wired.color[0], wired.color[1],
                    wired.color[2]);
    }
}

TEST(MissionFogWiringCorpusTest, ShippedDecompiledTextResolvesIdenticallyBeforeAndAfter) {
    // LEVEL1 and LEVEL4 also ship a human-readable objects.qsc, so this drives the
    // same comparison from authored text that did not come from the editor's own
    // decompiler.
    for (int level : {1, 4}) {
        const fs::path qsc_path = LevelDir(level) / "objects.qsc";
        if (!fs::is_regular_file(qsc_path)) {
            GTEST_SKIP() << "retail IGI (with objects.qsc) not found at " << GameRoot().string();
        }

        const std::vector<std::vector<std::string>> keyframes =
            ParseKeyframeCallsFromText(ReadFile(qsc_path));
        ASSERT_FALSE(keyframes.empty()) << "level " << level;

        const FogValues legacy = LegacyFogFromTokens(keyframes);
        const FogValues wired = WiredFogFromTokens(keyframes);
        ExpectSameFog(legacy, wired, "shipped objects.qsc, level " + std::to_string(level));
        EXPECT_TRUE(wired.authored) << "level " << level;
    }
}

TEST(MissionFogWiringCorpusTest, Level1WiredFogIsTheShippedNumbers) {
    const fs::path qvm_path = LevelDir(1) / "OBJECTS.QVM";
    if (!fs::is_regular_file(qvm_path)) {
        GTEST_SKIP() << "retail IGI not found at " << GameRoot().string();
    }

    const QVMFile qvm = QVM_Parse(qvm_path.string());
    ASSERT_TRUE(qvm.valid) << qvm.error;

    const igi::MissionFog fog = igi::ResolveMissionFog(KeyframesAsFogTasks(ExtractKeyframes(qvm)));
    ASSERT_TRUE(fog.authored);
    EXPECT_EQ(FloatBits(fog.sets[0].density), FloatBits(1.8899999856948853f));
    EXPECT_EQ(FloatBits(fog.sets[0].fog[0]), FloatBits(0.57692301273345947f));
    EXPECT_EQ(FloatBits(fog.sets[0].fog[1]), FloatBits(0.53846198320388794f));
    EXPECT_EQ(FloatBits(fog.sets[0].fog[2]), FloatBits(0.48076900839805603f));
    EXPECT_EQ(FloatBits(fog.time_seconds), FloatBits(12.0f));

    // The value the renderer is handed: 258.34988 m (the engine e-folds at
    // 2e6 / 1.89 / 4096 m), converted to world units at the call site.
    const float far_meters = igi::LinearFogFarMetresForAuthoredDensity(fog.sets[0].density, 30000.0f);
    EXPECT_EQ(FloatBits(far_meters), FloatBits(258.349884033203125f));
    EXPECT_EQ(FloatBits(far_meters * 4096.0f), FloatBits(258.349884033203125f * 4096.0f));
}

// ---------------------------------------------------------------------------
// The editor's real load path, in the Windows lane only:
//   objects.qvm -> QVM_Decompile -> objects.qsc text -> QSC::Load -> the bridge
// ---------------------------------------------------------------------------

#if defined(_WIN32)
TEST(MissionFogWiringQscTest, DecompiledLevelsResolveIdenticallyBeforeAndAfter) {
    if (!fs::is_regular_file(LevelDir(1) / "OBJECTS.QVM")) {
        GTEST_SKIP() << "retail IGI not found at " << GameRoot().string()
                     << " (set IGI_GAME_PATH to point at it)";
    }

    test_support::TempDirectory temp;

    std::printf("\n  level   keyframes   legacy far (m)        wired far (m)\n");
    for (int level = 1; level <= 14; ++level) {
        const fs::path qvm_path = LevelDir(level) / "OBJECTS.QVM";
        ASSERT_TRUE(fs::is_regular_file(qvm_path)) << qvm_path.string();

        const QVMFile qvm = QVM_Parse(qvm_path.string());
        ASSERT_TRUE(qvm.valid) << qvm_path.string() << ": " << qvm.error;

        // Exactly Level::DecompileObjects: decompile to editor/qed/temp, trim, load.
        const fs::path qsc_path = temp.path() / ("level" + std::to_string(level) + ".qsc");
        ASSERT_TRUE(QVM_Decompile(qvm, qsc_path.string())) << qsc_path.string();
        Utils::TrimFileInPlace(qsc_path.string());

        QSC qsc;
        qsc.Load(qsc_path.string().c_str());
        ASSERT_FALSE(qsc.HadOverflow()) << "level " << level;

        // The bridge's own view of the level, against the QVM walk the host half
        // uses: if these ever disagree, the host half is testing the wrong data.
        const std::vector<igi::FogTaskObject> bridged = igi::FogTaskObjectsFromQsc(qsc);
        const std::vector<std::vector<std::string>> walked = ExtractKeyframes(qvm);
        ASSERT_EQ(bridged.size(), walked.size()) << "level " << level;
        for (std::size_t k = 0; k < bridged.size(); ++k) {
            ASSERT_EQ(bridged[k].arg_tokens.size(), walked[k].size())
                << "level " << level << " keyframe " << k;
            for (std::size_t i = 3; i < walked[k].size(); ++i) {
                EXPECT_TRUE(SameToken(bridged[k].arg_tokens[i], walked[k][i]))
                    << "level " << level << " keyframe " << k << " token " << i << ": bridge \""
                    << bridged[k].arg_tokens[i] << "\" vs walk \"" << walked[k][i] << "\"";
            }
        }

        const FogValues legacy = LegacyFogFromQsc(qsc);
        const FogValues wired = WiredFogFromQsc(qsc);
        ExpectSameFog(legacy, wired, "level " + std::to_string(level));

        ASSERT_TRUE(wired.authored) << "level " << level;
        ASSERT_TRUE(legacy.authored) << "level " << level;
        EXPECT_GT(wired.far_meters, 1.0f) << "level " << level;

        std::printf("  %5d   %9zu   %20.6f   %20.6f\n", level, bridged.size(), legacy.far_meters,
                    wired.far_meters);
    }
}

TEST(MissionFogWiringQscTest, ShippedDecompiledTextResolvesIdenticallyBeforeAndAfter) {
    for (int level : {1, 4}) {
        const fs::path qsc_path = LevelDir(level) / "objects.qsc";
        if (!fs::is_regular_file(qsc_path)) {
            GTEST_SKIP() << "retail IGI (with objects.qsc) not found at " << GameRoot().string();
        }

        QSC qsc;
        qsc.Load(qsc_path.string().c_str());

        const FogValues legacy = LegacyFogFromQsc(qsc);
        const FogValues wired = WiredFogFromQsc(qsc);
        ExpectSameFog(legacy, wired, "shipped objects.qsc, level " + std::to_string(level));
        EXPECT_TRUE(wired.authored) << "level " << level;
    }
}
#endif  // _WIN32
