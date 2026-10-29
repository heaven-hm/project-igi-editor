// Unit tests for the engine's terrain (QCube) LOD detail scaling.
//
// Oracle: IGI-RE (decompilation of retail IGI.exe).
//   QCube_Begin               HOOK 0x4D2540  src/lod/qcube.c:319-345
//   QCube_SetDetail           HOOK 0x4D2C20  src/lod/qcube.c:567-571
//   gQCubeDetail              DATA 0x54844C  src/lod/qcube.c:578
//   Profile_ApplyPerformance  HOOK 0x406390  src/profile/profile.c:404-416
//   QCube_Walk                HOOK 0x4D18F0  src/lod/qcubewalk.c:327-361
//   TerrainMesh_SetupLighting HOOK 0x50FAE0  src/glowmesh/terrainmesh.c:277-292
//
// See docs/igi-re-parity/TERRAIN_LOD.md for the full trace, including the
// derivation that the editor's hardcoded 163 IS this rule at fov <= 1.

#include <gtest/gtest.h>

#include "level/terrain_lod_settings.h"

#include <cmath>
#include <cstdint>

namespace {

using igi::terrain_lod::BuildTerrainFogTable;
using igi::terrain_lod::DetailValue;
using igi::terrain_lod::kQCubeDetailDefault;
using igi::terrain_lod::kQCubeMinSizeFloor;
using igi::terrain_lod::kTerrainFogTableSize;
using igi::terrain_lod::QCubeMinSizeForFov;
using igi::terrain_lod::TerrainDetailPreset;

// The editor's own field of view, source/common.h:53 FOVY_IN_DEGREE = 70, and
// its zoomed value source/app.cpp:1924 = 40. tan_half_fovx_ is derived from the
// vertical fov and the viewport aspect (source/app.cpp:1946-1954).
double TanHalfFovX(double vertical_fov_degrees, double aspect) {
    return std::tan(vertical_fov_degrees * 3.14159265358979323846 / 360.0) * aspect;
}

// The engine's Float_Log2 is not std::log2 -- it is ~8*log2 for powers of two,
// computed from the exponent field, and returns 0 for non-positive input
// (src/dxinit/spline.c:13-20). The editor ports it as igi::FloatLog2
// (source/runtime/mission_fog.cpp:61); it is re-stated here so this suite stays
// free of the runtime include and of the Windows-only headers it pulls in.
unsigned EngineFloatLog2(float value) {
    if (!(value > 1.0f)) return 0u;
    return static_cast<unsigned>(8.0 * std::log2(static_cast<double>(value)));
}

// One entry of src/glowmesh/terrainmesh.c:278-287, written out longhand so the
// test does not merely re-run the helper's own expression.
float EngineFogEntry(int i, int qcube_min_size, float fog_scale) {
    const int s = 25 - i;
    const uint32_t shifted = 1u << (static_cast<uint32_t>(s) & 31u);
    double d = static_cast<double>(static_cast<int32_t>(shifted)) * qcube_min_size;
    d += d;
    d += static_cast<double>(0x40000000 >> i);

    double e = (static_cast<double>(EngineFloatLog2(static_cast<float>(d))) - 130.0) * fog_scale;
    if (!(e >= 0.0)) e = 0.0;
    if (e > 255.0) e = 255.0;
    return static_cast<float>(e);
}

// --------------------------------------------------------------------------
// The presets (profile.c:412)
// --------------------------------------------------------------------------

TEST(TerrainLodSettingsTest, PresetsAreTheOnlyThreeTheEngineCanRender) {
    // src/profile/profile.c:412 --
    //   QCube_SetDetail(flags & 0x10 ? 0x60 : flags & 0x20 ? 0x81 : 0xA3);
    EXPECT_EQ(DetailValue(TerrainDetailPreset::Low), 0x60);     // 96
    EXPECT_EQ(DetailValue(TerrainDetailPreset::Medium), 0x81);  // 129
    EXPECT_EQ(DetailValue(TerrainDetailPreset::High), 0xA3);    // 163

    // The chain is first-match on 0x10 before 0x20, so 0x10 wins; and the
    // default (neither bit set) is High, not a fourth value.
    EXPECT_EQ(DetailValue(TerrainDetailPreset::High), 163);
}

TEST(TerrainLodSettingsTest, DefaultGlobalAndFloorAreTheEnginesOwnNumbers) {
    // src/lod/qcube.c:578 `DATA(54844C) int gQCubeDetail INIT(115)`
    EXPECT_EQ(kQCubeDetailDefault, 115);
    // src/lod/qcube.c:342 `d < 0x46 ? 0x46 : d`
    EXPECT_EQ(kQCubeMinSizeFloor, 70);
}

// --------------------------------------------------------------------------
// The fov branch (qcube.c:338-342)
// --------------------------------------------------------------------------

TEST(TerrainLodSettingsTest, RetailDefaultFieldOfViewUsesThePresetUnscaled) {
    // gTransContext.fov is tan(half horizontal fov) and retail's default is
    // exactly 1.0: kFovX = 0x1.921fb6p-1 = pi/4, v->fovX = x87_tan(kFovX)
    // (src/viewcam/humanview.c:83,137) -> tan(45 deg) = 1.0.
    //
    // The engine's test is the STRICT `fov > 1.0`, so 1.0 takes the unscaled
    // branch: gQCubeMinSize == gQCubeDetail == 163 for the High preset. This is
    // the case the editor's hardcoded CUBE_ACTIVE_COMPARE = 163 was correct for.
    EXPECT_EQ(QCubeMinSizeForFov(1.0f), 163);
    EXPECT_EQ(QCubeMinSizeForFov(1.0f), DetailValue(TerrainDetailPreset::High));

    // ...and the unscaled branch holds for every fov <= 1, including retail's
    // zoom-in (HumanView_UpdateCamera multiplies fovX by 0.96 per step,
    // src/viewcam/humanview.c:314,340).
    EXPECT_EQ(QCubeMinSizeForFov(0.96f), 163);
    EXPECT_EQ(QCubeMinSizeForFov(0.5f), 163);
    EXPECT_EQ(QCubeMinSizeForFov(0.0f), 163);
}

TEST(TerrainLodSettingsTest, WiderFieldOfViewGivesFinerTerrain) {
    // The scaled branch divides by sqrt(fov) (ci_pow(fov, kHalf),
    // src/lod/qcube.c:341). A larger tangent -> smaller threshold -> finer
    // cells. Monotonic, and strictly below the preset value.
    int previous = QCubeMinSizeForFov(1.0f);
    for (float fov = 1.0f; fov < 4.0f; fov += 0.05f) {
        const int current = QCubeMinSizeForFov(fov);
        EXPECT_LE(current, previous) << "fov=" << fov;
        previous = current;
    }
    EXPECT_LT(QCubeMinSizeForFov(2.0f), 163);

    // sqrt(4) == 2, so a tangent of 4.0 halves the threshold: 163/2 = 81.5, and
    // x87_round (round half to even) takes it to 82.
    EXPECT_EQ(QCubeMinSizeForFov(4.0f), 82);
}

TEST(TerrainLodSettingsTest, TheEditorFieldOfViewAsksForFinerTerrainThanTheConstant) {
    // 16:9 at the editor's 70-degree vertical fov: tan(35 deg) * 16/9 = 1.2448.
    const double tan_16_9 = TanHalfFovX(70.0, 16.0 / 9.0);
    ASSERT_GT(tan_16_9, 1.0);   // otherwise this test would not exercise the rule

    const int scaled = QCubeMinSizeForFov(static_cast<float>(tan_16_9));
    EXPECT_EQ(scaled, 146) << "tan_half_fovx=" << tan_16_9;
    EXPECT_LT(scaled, 163);     // finer than the old hardcoded constant

    // 4:3 at the same vertical fov is a NARROWER horizontal fov: tan(35)*4/3 =
    // 0.9336 < 1.0, so the engine does not scale it at all and the preset value
    // stands. The threshold is therefore aspect-dependent, which is exactly the
    // behaviour the editor's constant could not express.
    const double tan_4_3 = TanHalfFovX(70.0, 4.0 / 3.0);
    ASSERT_LT(tan_4_3, 1.0);
    EXPECT_EQ(QCubeMinSizeForFov(static_cast<float>(tan_4_3)), 163);
    EXPECT_LE(scaled, QCubeMinSizeForFov(static_cast<float>(tan_4_3)));

    // The editor's zoomed fov (40 degrees, source/app.cpp:1924) is below the
    // threshold for every sane aspect, so zooming uses the preset unscaled --
    // the same branch retail's own zoom lands in.
    EXPECT_LT(TanHalfFovX(40.0, 16.0 / 9.0), 1.0);
    EXPECT_EQ(QCubeMinSizeForFov(static_cast<float>(TanHalfFovX(40.0, 16.0 / 9.0))), 163);
}

TEST(TerrainLodSettingsTest, PresetOrderingSurvivesTheFovScale) {
    // Whatever the fov, the three presets must stay ordered: a lower quality
    // preset can never ask for finer terrain than a higher one.
    for (float fov = 0.1f; fov < 6.0f; fov += 0.1f) {
        const int low = QCubeMinSizeForFov(fov, TerrainDetailPreset::Low);
        const int medium = QCubeMinSizeForFov(fov, TerrainDetailPreset::Medium);
        const int high = QCubeMinSizeForFov(fov, TerrainDetailPreset::High);
        EXPECT_LE(low, medium) << "fov=" << fov;
        EXPECT_LE(medium, high) << "fov=" << fov;
    }
}

TEST(TerrainLodSettingsTest, TheFloorClampsAnExtremeFieldOfView) {
    // src/lod/qcube.c:342 clamps to 0x46. 163/sqrt(fov) < 70 needs fov > 5.4.
    EXPECT_EQ(QCubeMinSizeForFov(100.0f), kQCubeMinSizeFloor);
    EXPECT_EQ(QCubeMinSizeForFov(1e6f), kQCubeMinSizeFloor);

    // The Low preset hits the floor sooner: 96/sqrt(4) = 48 < 70.
    EXPECT_EQ(QCubeMinSizeForFov(4.0f, TerrainDetailPreset::Low), kQCubeMinSizeFloor);
    // The High preset does not, at the same fov.
    EXPECT_GT(QCubeMinSizeForFov(4.0f, TerrainDetailPreset::High), kQCubeMinSizeFloor);
}

TEST(TerrainLodSettingsTest, NaNAndNegativeTangentsTakeTheUnscaledBranch) {
    // The engine's comparison is a float `>`: unordered for NaN, false for
    // negatives, so both take `d = gQCubeDetail`. A NaN must not silently
    // change the detail level.
    EXPECT_EQ(QCubeMinSizeForFov(std::nanf("")), 163);
    EXPECT_EQ(QCubeMinSizeForFov(-1.0f), 163);
    EXPECT_EQ(QCubeMinSizeForFov(-1e30f), 163);
}

// --------------------------------------------------------------------------
// The fog table (terrainmesh.c:277-292) -- the one place detail meets fog
// --------------------------------------------------------------------------

TEST(TerrainLodSettingsTest, FogTableHas31EntriesAndIsFullyWritten) {
    float table[kTerrainFogTableSize];
    for (int i = 0; i < kTerrainFogTableSize; ++i) table[i] = -1.0f;

    BuildTerrainFogTable(163, 0.000945f, EngineFloatLog2, table);

    // `for (i = 0, s = 25; s > -6; i++, s--)` -- 25 down to -5 inclusive is 31.
    EXPECT_EQ(kTerrainFogTableSize, 31);
    for (int i = 0; i < kTerrainFogTableSize; ++i) {
        EXPECT_GE(table[i], 0.0f) << "i=" << i;
        EXPECT_LE(table[i], 255.0f) << "i=" << i;
    }
}

TEST(TerrainLodSettingsTest, FogTableMatchesTheEnginesArithmetic) {
    // Independent recomputation of src/glowmesh/terrainmesh.c:278-287.
    const float fog_scale = 0.000945f;
    const int min_size = 163;

    float table[kTerrainFogTableSize];
    BuildTerrainFogTable(min_size, fog_scale, EngineFloatLog2, table);

    for (int i = 0; i < kTerrainFogTableSize; ++i) {
        EXPECT_NEAR(table[i], EngineFogEntry(i, min_size, fog_scale), 1e-4) << "i=" << i;
    }
}

TEST(TerrainLodSettingsTest, FogTableHeadIsStrictlyDecreasingAndTailFloors) {
    float table[kTerrainFogTableSize];
    BuildTerrainFogTable(163, 0.000945f, EngineFloatLog2, table);

    // Entry 0: d = 2*2^25*163 = 1.20125e10, Float_Log2 -> 267, and
    // (267 - 130) * 0.000945 = 0.12946.
    EXPECT_NEAR(table[0], 0.12946f, 1e-4f);

    // While d is large the table steps down by one exponent octave per entry,
    // so the head decreases monotonically. Float_Log2 returns 8 per octave and
    // 8 * 0.000945 = 0.00756 -- the observed step.
    for (int i = 1; i < 18; ++i) {
        EXPECT_LT(table[i], table[i - 1]) << "i=" << i;
    }
    EXPECT_NEAR(table[0] - table[1], 0.00756f, 1e-4f);

    // The bias of 130 means the fog value floors once Float_Log2(d) <= 130,
    // which happens at i == 18 (d = 45824 -> 123).
    for (int i = 18; i < 26; ++i) {
        EXPECT_FLOAT_EQ(table[i], 0.0f) << "i=" << i;
    }
}

TEST(TerrainLodSettingsTest, FogTableTailReproducesTheEnginesX86ShiftWrap) {
    // The engine's loop runs `s` from 25 down to -5 but shifts by `s & 31`
    // (src/glowmesh/terrainmesh.c:278). On x86 `shl` masks the count to 5 bits,
    // so the negative shifts WRAP: s = -1 becomes a shift of 31 and yields
    // 0x80000000, which as a signed int is INT_MIN and makes `d` NEGATIVE. The
    // engine's own table therefore has deterministic-but-odd values in its last
    // five entries (i = 26..30): Float_Log2 of the negative one is 0, and the
    // other four wrap to large shifts and come back UP.
    //
    // This is reproduced rather than "fixed": the table is an engine global and
    // the tail is what retail computes. Pinned so nobody silently changes it.
    float table[kTerrainFogTableSize];
    BuildTerrainFogTable(163, 0.000945f, EngineFloatLog2, table);

    // i = 26 is the negative one: Float_Log2 of a negative returns 0, so the
    // clamped value is 0.
    EXPECT_FLOAT_EQ(table[26], 0.0f);

    // i = 27..30 wrap to shifts 31, 30, 29, 28 and come back up.
    EXPECT_GT(table[27], table[25]);
    EXPECT_GT(table[27], table[26]);
    for (int i = 28; i < kTerrainFogTableSize; ++i) {
        EXPECT_LT(table[i], table[i - 1]) << "i=" << i;
    }
    EXPECT_NEAR(table[27], 0.16632f, 1e-4f);
    EXPECT_NEAR(table[30], 0.14364f, 1e-4f);
}

TEST(TerrainLodSettingsTest, FogTableDependsOnTheDetailLevel) {
    // The dependency that makes the detail preset and the fog one system:
    // TerrainMesh_SetupLighting reads gQCubeMinSize. A finer terrain detail
    // gives a SMALLER cell size, so every depth in the table shifts down and
    // the fog value at a given index drops.
    const int fine_min = QCubeMinSizeForFov(1.25f);
    const int coarse_min = QCubeMinSizeForFov(1.0f);
    ASSERT_LT(fine_min, coarse_min);

    float fine[kTerrainFogTableSize];
    float coarse[kTerrainFogTableSize];
    BuildTerrainFogTable(fine_min, 0.000945f, EngineFloatLog2, fine);
    BuildTerrainFogTable(coarse_min, 0.000945f, EngineFloatLog2, coarse);

    for (int i = 0; i < kTerrainFogTableSize; ++i) {
        EXPECT_LE(fine[i], coarse[i]) << "i=" << i;
    }
    // ...and entries actually differ, so the coupling is real rather than a
    // table that happens to be saturated at both detail levels.
    int differing = 0;
    for (int i = 0; i < kTerrainFogTableSize; ++i) {
        if (fine[i] != coarse[i]) ++differing;
    }
    EXPECT_GT(differing, 0);
}

TEST(TerrainLodSettingsTest, FogTableIsZeroWhenTheLevelHasNoFog) {
    // A density of 0 is a valid "no fog at this keyframe" state and is what
    // every retail level's night keyframe authors; gTerrainFogScale is then 0
    // and every entry must be 0 regardless of the detail level.
    float table[kTerrainFogTableSize];
    BuildTerrainFogTable(163, 0.0f, EngineFloatLog2, table);
    for (int i = 0; i < kTerrainFogTableSize; ++i) {
        EXPECT_FLOAT_EQ(table[i], 0.0f) << "i=" << i;
    }
}

}  // namespace
