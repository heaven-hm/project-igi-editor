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

#include "support/mission_fog_corpus.h"

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

}  // namespace

using namespace test_support;

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

TEST(MissionFogTest, FogIntensityScaleIsOneAtTheLevelDefault) {
    // The Fog Intensity spinner is the editor's OWN preview control -- IGI-RE has
    // no fog-intensity field anywhere -- so the one value that reproduces retail is
    // 100, and a level must start there (App::LoadLevel resets it; the default was
    // 200, i.e. TWICE retail's density, because the exponent is doubled).
    EXPECT_FLOAT_EQ(igi::FogIntensityDistanceScale(igi::kFogIntensityLevelDefault), 1.0f);
    EXPECT_EQ(igi::kFogIntensityLevelDefault, 100);

    // The level-start identity, stated the way the shader sees it: at 100 the
    // coefficient Renderer::ApplyFogIntensity writes is the engine's own
    // gTerrainFogScale (the value TerrainMesh_SetFog stored, terrainmesh.c:302-320),
    // so the exponent is `depth * gTerrainFogScale` with no multiplier at all.
    constexpr float kModelScaleDown = 0.001f;   // source/common.h:54, == gWorldScale
    for (float density : {0.115f, 0.12f, 0.4f, 1.2f, 1.89f, 3.0f, 4.79f, 9.5f}) {
        const float far_metres =
            igi::LinearFogFarMetresForAuthoredDensity(density, 30000.0f) *
            igi::FogIntensityDistanceScale(igi::kFogIntensityLevelDefault);
        const float coefficient =
            1.0f / (far_metres * igi::kEditorWorldUnitsPerMetre * kModelScaleDown);
        EXPECT_NEAR(coefficient, density * igi::kGlobalLightFogScale,
                    density * igi::kGlobalLightFogScale * 1e-5f)
            << "density " << density;
    }

    // The curve itself: 100 = authored, 200 = twice the density (half the
    // distance), 300 = four times, and the low end approaches 2x the distance.
    EXPECT_FLOAT_EQ(igi::FogIntensityDistanceScale(200), 0.5f);
    EXPECT_FLOAT_EQ(igi::FogIntensityDistanceScale(300), 0.25f);
    EXPECT_FLOAT_EQ(igi::FogIntensityDistanceScale(0), 2.0f);
    EXPECT_FLOAT_EQ(igi::FogIntensityDistanceScale(50), 2.0f * std::sqrt(0.5f));

    // Monotone: a larger percentage is never a longer distance (never thinner fog).
    for (int i = 0; i < 1000; i += 25) {
        EXPECT_GE(igi::FogIntensityDistanceScale(i), igi::FogIntensityDistanceScale(i + 25))
            << "intensity " << i;
    }

    // The spinner's range is 0..1000 and the result is always a usable divisor
    // (Renderer::ApplyFogIntensity divides by it).
    EXPECT_FLOAT_EQ(igi::FogIntensityDistanceScale(-1), igi::FogIntensityDistanceScale(0));
    EXPECT_FLOAT_EQ(igi::FogIntensityDistanceScale(99999), igi::FogIntensityDistanceScale(1000));
    for (int i = -10; i <= 1010; i += 10) {
        const float scale = igi::FogIntensityDistanceScale(i);
        EXPECT_TRUE(std::isfinite(scale)) << "intensity " << i;
        EXPECT_GE(scale, 0.001f) << "intensity " << i;
    }
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
// KeyframeTokens (a 51-parameter keyframe in the declared order) lives in
// tests/support/mission_fog_corpus.h so the wiring suite builds the same shape.

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

TEST(MissionFogCorpusTest, LevelStartFogMatchesTheEngineOnEveryLevel) {
    // The level-start contract, per shipped level: what the engine stores and
    // exponentiates, and what the editor hands its shader at the Fog Intensity the
    // level now starts at (100 == the authored value, igi::kFogIntensityLevelDefault).
    //
    //   engine:  gTerrainFogScale = authored "Fog density terrain" * 0.0005, clamped
    //            0..16384 by TerrainMesh_SetFog (globallight.c:115,125;
    //            terrainmesh.c:302-320), exponentiated as
    //            1/exp(vz * gWorldScale * gTerrainFogScale) (terraind3d.c:164-167).
    //   editor:  coefficient = 1 / (far_world * RENDERER_MODEL_SCALE_DOWN) with
    //            far_world = (488.28125 / density) * 4096 world units
    //            (Renderer::ApplyFogIntensity, source/renderer/renderer.cpp).
    //
    // The two must be the same number: at intensity 100 the editor's coefficient IS
    // gTerrainFogScale, which is why the exponential is identical term for term.
    // The `@200%` column is what the OLD default (200) produced -- twice the
    // exponent, i.e. twice retail's fog -- so a reader can see the defect's size.
    if (!fs::is_regular_file(LevelDir(1) / "OBJECTS.QVM")) {
        GTEST_SKIP() << "retail IGI not found at " << GameRoot().string()
                     << " (set IGI_GAME_PATH to point at it)";
    }

    constexpr float kModelScaleDown = 0.001f;   // source/common.h:54 == gWorldScale
    std::printf("\n  level  authored   engine scale   e-fold(m)   editor far@100%%   editor coeff@100%%"
                "   ratio   coeff@200%%   fog@100m  @300m   @1km\n");
    int levels = 0;
    for (int level = 1; level <= 14; ++level) {
        const fs::path path = LevelDir(level) / "OBJECTS.QVM";
        if (!fs::is_regular_file(path)) continue;
        ++levels;

        const QVMFile qvm = QVM_Parse(path.string());
        ASSERT_TRUE(qvm.valid) << "LEVEL" << level << ": " << qvm.error;
        const igi::MissionFog fog =
            igi::ResolveMissionFog(KeyframesAsFogTasks(ExtractKeyframes(qvm)));
        ASSERT_TRUE(fog.authored) << "LEVEL" << level;

        const float density = fog.sets[0].density;
        const float engine_scale = fog.terrain_fog_scale();
        const float efolding_m = igi::ExponentialFogEFoldingDistance(engine_scale) / kFixed;

        // What Level::LoadFogInfo -> Renderer::SetupFog hands over at 100%.
        const float far_m = igi::LinearFogFarMetresForAuthoredDensity(density, 30000.0f) *
                            igi::FogIntensityDistanceScale(igi::kFogIntensityLevelDefault);
        const float far_world = far_m * igi::kEditorWorldUnitsPerMetre;
        const float coeff_100 = 1.0f / (far_world * kModelScaleDown);
        const float coeff_200 = coeff_100 / igi::FogIntensityDistanceScale(200);

        const float at_100m = igi::ExponentialFogAmount(100.0f * kFixed, engine_scale) * 100.0f;
        const float at_300m = igi::ExponentialFogAmount(300.0f * kFixed, engine_scale) * 100.0f;
        const float at_1km = igi::ExponentialFogAmount(1000.0f * kFixed, engine_scale) * 100.0f;

        std::printf("  %5d  %8.3f  %12.8f  %9.1f  %15.1f  %17.9f  %6.3f  %10.6f  %7.1f%%  %5.1f%%  %5.1f%%\n",
                    level, density, engine_scale, efolding_m, far_m, coeff_100,
                    coeff_100 / engine_scale, coeff_200, at_100m, at_300m, at_1km);

        // THE LEVEL-START CONTRACT: at the intensity a level starts at, the editor's
        // shader coefficient is the engine's own gTerrainFogScale.
        EXPECT_NEAR(coeff_100, engine_scale, engine_scale * 1e-5f) << "LEVEL" << level;
        EXPECT_GT(efolding_m, 1.0f) << "LEVEL" << level;
        EXPECT_GT(at_100m, 1.0f) << "LEVEL" << level;
    }
    EXPECT_EQ(levels, 14) << "the corpus must be all fourteen shipped levels";
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

// ===========================================================================
// The OBJECT pass's fog, and the two shader-facing conversions
//
// "the fog ... should be exactly how the level is": retail fogs the terrain with
// GlobalLight set 0 and the objects with set 1, so the editor needs BOTH, and each
// shader needs the authored number in ITS OWN units. These tests pin the three
// things that decide what is drawn:
//   1. which set each pass reads (kObjectLightSet, proved by address arithmetic);
//   2. the authored `link` inheritance the engine resolves before publish();
//   3. the two conversions: the object's e-folding distance in world units, and the
//      terrain's exponent per unit of clip-space depth.
// ===========================================================================

namespace {

// A keyframe whose five sets carry DISTINCT values, so a test can tell which set a
// reader used. Parameter layout: see globallight.c:264-282 and the corpus helper's
// KeyframeTokens (which gives every set the same values and therefore cannot).
std::vector<std::string> KeyframeTokensPerSet(float terrain_density, float object_density,
                                              float time) {
    std::vector<std::string> t;
    t.push_back("-1");
    t.push_back("GlobalLightKeyframe");
    t.push_back("");
    t.push_back("FALSE");                       // 0  Link All Sliders
    for (int set = 0; set < 5; ++set) {
        const float density = (set == 0) ? terrain_density
                             : (set == 1) ? object_density
                                          : 0.0f;
        t.push_back(std::to_string(0.1f * static_cast<float>(set)));   // ambient R
        t.push_back(std::to_string(0.2f * static_cast<float>(set)));   // ambient G
        t.push_back(std::to_string(0.3f * static_cast<float>(set)));   // ambient B
        t.push_back(std::to_string(0.01f * static_cast<float>(set + 1)));  // fog R
        t.push_back(std::to_string(0.02f * static_cast<float>(set + 1)));  // fog G
        t.push_back(std::to_string(0.03f * static_cast<float>(set + 1)));  // fog B
        t.push_back(std::to_string(density));                          // density
        t.push_back("-1");                                            // link: none
    }
    t.push_back("0.5"); t.push_back("0.55"); t.push_back("0.6");  // 41..43 Sky color
    t.push_back("0"); t.push_back("0"); t.push_back("0");         // 44..46 Water ambient
    t.push_back("1"); t.push_back("1"); t.push_back("1");         // 47..49 Water color
    t.push_back(std::to_string(time));                            // 50 Time
    return t;
}

}  // namespace

TEST(ObjectFogTest, EachPassReadsItsOwnAuthoredSet) {
    // gFogColor is gFogColors[1] and gFogDensity is gFogDensities[1] (address
    // arithmetic in mission_fog.h), while the terrain reads set 0 through
    // TerrainMesh_SetFog/TerrainMesh_SetAmbient (globallight.c:124-125).
    std::vector<igi::FogTaskObject> objects;
    objects.push_back({"GlobalLightKeyframe", KeyframeTokensPerSet(2.0f, 8.0f, 12.0f)});

    const igi::MissionFog fog = igi::ResolveMissionFog(objects);
    ASSERT_TRUE(fog.authored);

    // Terrain: set 0, clamped to the 16384 ceiling terrainmesh.c:302-320 applies.
    EXPECT_FLOAT_EQ(fog.sets[0].density, 2.0f);
    EXPECT_FLOAT_EQ(fog.terrain_fog_scale(), 2.0f * igi::kGlobalLightFogScale);
    EXPECT_FLOAT_EQ(fog.terrain_fog_color()[0], 0.01f);
    EXPECT_FLOAT_EQ(fog.terrain_fog_color()[2], 0.03f);

    // Objects: set 1, and NOT clamped -- the ceiling is gTerrainFogScale's only.
    EXPECT_FLOAT_EQ(fog.sets[igi::kObjectLightSet].density, 8.0f);
    EXPECT_FLOAT_EQ(fog.object_fog_scale(), 8.0f * igi::kGlobalLightFogScale);
    EXPECT_FLOAT_EQ(fog.object_fog_color()[0], 0.02f);
    EXPECT_FLOAT_EQ(fog.object_fog_color()[1], 0.04f);
    EXPECT_FLOAT_EQ(fog.object_fog_color()[2], 0.06f);

    // The object scale is deliberately unclamped: 1e9 * 0.0005 exceeds 16384 and
    // the object value must survive it (only the terrain value is capped).
    std::vector<igi::FogTaskObject> huge;
    huge.push_back({"GlobalLightKeyframe", KeyframeTokensPerSet(1.0e9f, 1.0e9f, 1.0f)});
    const igi::MissionFog big = igi::ResolveMissionFog(huge);
    ASSERT_TRUE(big.authored);
    EXPECT_FLOAT_EQ(big.terrain_fog_scale(), igi::kTerrainFogScaleMax);
    EXPECT_FLOAT_EQ(big.object_fog_scale(), 1.0e9f * igi::kGlobalLightFogScale);
}

TEST(ObjectFogTest, LinkSettingsAreResolvedBeforeAnythingReadsTheSets) {
    // GlobalLightKeyframe_ParamsChanged HOOK 0x5282C0 (globallight.c:392-403):
    //     for (n = 5; n; n--)
    //         for (i = 0; i < 5; i++)
    //             if (link in 0..4) sets[i].color = sets[link].color;
    // A level that authors `link` and leaves the linked set at zero relies on it,
    // and publish() then hands the RESOLVED colours to both passes.
    std::vector<std::string> t = KeyframeTokensPerSet(4.0f, 0.0f, 12.0f);

    // set 1 links to set 0, set 2 links to set 1, ... a chain, each with its own
    // (wrong) explicit colour and density. Parameter k for set i is at token 3 + k,
    // and set i's "Link setting" is the 8th field of its block.
    const int base[5] = {1, 9, 17, 25, 33};
    for (int set = 1; set < 5; ++set) {
        t[3 + base[set] + 7] = std::to_string(set - 1);   // link -> the previous set
        t[3 + base[set] + 6] = "123.0";                   // a placeholder density
        for (int c = 0; c < 3; ++c) t[3 + base[set] + 3 + c] = "0.9";  // placeholder fog
    }
    t[3 + base[0] + 7] = "-1";   // "Link setting terrain"

    std::vector<igi::FogTaskObject> objects;
    objects.push_back({"GlobalLightKeyframe", t});
    const igi::MissionFog fog = igi::ResolveMissionFog(objects);
    ASSERT_TRUE(fog.authored);

    // Every set now carries set 0's terrain colour and density, through the chain.
    for (int set = 0; set < 5; ++set) {
        EXPECT_FLOAT_EQ(fog.sets[set].density, 4.0f) << "set " << set;
        EXPECT_FLOAT_EQ(fog.sets[set].fog[0], 0.01f) << "set " << set;
        EXPECT_FLOAT_EQ(fog.sets[set].ambient[2], 0.0f) << "set " << set;
    }
    // The link values themselves stay as authored (the engine never rewrites them).
    EXPECT_EQ(fog.sets[3].link, 2);

    // A NEGATIVE link is "none" and must leave the set's own values alone.
    std::vector<igi::FogTaskObject> unlinked;
    unlinked.push_back({"GlobalLightKeyframe", KeyframeTokensPerSet(2.0f, 8.0f, 12.0f)});
    const igi::MissionFog plain = igi::ResolveMissionFog(unlinked);
    ASSERT_TRUE(plain.authored);
    EXPECT_FLOAT_EQ(plain.sets[1].density, 8.0f);
    EXPECT_FLOAT_EQ(plain.sets[4].density, 0.0f);
}

TEST(ObjectFogTest, TerrainShaderCoefficientIsTheEngineTerrainFogScale) {
    // The terrain fog pass measures depth as `1.0 / gl_FragCoord.w`, i.e. in units
    // of world * RENDERER_MODEL_SCALE_DOWN (0.001f) -- the engine's gWorldScale.
    // The engine's exponent is
    //     depth_world * gWorldScale * gTerrainFogScale
    //   = (depth_world * gWorldScale) * gTerrainFogScale
    //   = depth_clip * gTerrainFogScale
    // so the coefficient the shader must be given IS gTerrainFogScale, and the
    // reciprocal relation Renderer::ApplyFogIntensity uses has to reproduce it.
    //
    // Renderer::ApplyFogIntensity writes 1 / (far_world * RENDERER_MODEL_SCALE_DOWN)
    // where far_world is the e-folding distance in world units.
    constexpr float kModelScaleDown = 0.001f;   // source/common.h:54
    for (float density : {0.115f, 0.12f, 0.4f, 1.2f, 1.89f, 3.0f, 4.79f, 9.5f}) {
        const float far_metres = igi::LinearFogFarMetresForAuthoredDensity(density, 30000.0f);
        const float far_world = far_metres * igi::kEditorWorldUnitsPerMetre;
        const float coefficient = 1.0f / (far_world * kModelScaleDown);
        const float engine_scale = density * igi::kGlobalLightFogScale;
        // Within a float ulp: the two are algebraically the same number
        // (488.28125 * 4096 * 0.001 == 2000, and 1/2000 == 0.0005).
        EXPECT_NEAR(coefficient, engine_scale, engine_scale * 1e-5f) << "density " << density;
    }

    // The identity that makes it exact, stated as the engine states it:
    // 1 / (kWorldScale * kGlobalLightFogScale) = 2 000 000 world units.
    // (The tolerance is a float ulp at 2000: 0.001f is not exactly 1/1000.)
    EXPECT_NEAR(igi::kEFoldingMetresPerAuthoredDensity * igi::kEditorWorldUnitsPerMetre *
                    kModelScaleDown,
                2000.0f, 0.001f);
}

TEST(ObjectFogTest, FogSetRangeMatchesTheExponentialAtBothEdges) {
    // Fog_SetRange HOOK 0x49B2D0 (src/dxinit/d3drender.c:108-131) solves for the
    // D3D linear ramp that passes through the engine's exponential at the near and
    // far side of a sphere around the object:
    //     (end - n) / (end - start) == 1 / exp(kWorldScale * n * density)
    //     (end - f) / (end - start) == 1 / exp(kWorldScale * f * density)
    // That is the property being tested -- not the code's own output.
    //
    // `density` is gFogDensity: the AUTHORED density times kGlobalLightFogScale
    // (globallight.c:115,133-134), which is what publish() stores and what
    // MissionFog::object_fog_scale() returns. Using the authored number here would
    // inflate the exponent by 2000x.
    struct Case { float depth; float radius; float authored; };
    const Case cases[] = {
        {200000.0f, 20000.0f, 1.89f},     // ~49 m away, a 5 m object, LEVEL1's haze
        {1000000.0f, 100000.0f, 0.12f},   // ~244 m away, LEVEL5's thin haze
        {40000.0f, 4000.0f, 9.5f},        // ~10 m away, LEVEL14's thick haze
        {600000.0f, 30000.0f, 0.79f},     // ~146 m away, LEVEL7's haze
    };
    for (const Case& c : cases) {
        const float density = c.authored * igi::kGlobalLightFogScale;
        const igi::FogRange range = igi::FogSetRangeForDepth(c.depth, c.radius, density);
        ASSERT_FALSE(igi::FogRangeIsDegenerate(range)) << "depth " << c.depth;
        EXPECT_LT(range.start, range.end);

        const float n = c.depth - c.radius;
        const float f = c.depth + c.radius;
        const float span = range.end - range.start;
        // D3D's linear vertex fog factor, then the engine's transmittance at each
        // edge: kWorldScale converts game units to the space Fog_SetRange writes in.
        const float ramp_n = (range.end - n * igi::kWorldScale) / span;
        const float ramp_f = (range.end - f * igi::kWorldScale) / span;
        const float exp_n = 1.0f / std::exp(igi::kWorldScale * n * density);
        const float exp_f = 1.0f / std::exp(igi::kWorldScale * f * density);
        EXPECT_NEAR(ramp_n, exp_n, 2e-3f) << "near edge, density " << density;
        EXPECT_NEAR(ramp_f, exp_f, 2e-3f) << "far edge, density " << density;
        // The ramp runs the right way: the near edge retains more light than the far.
        EXPECT_GT(ramp_n, ramp_f);
    }
}

TEST(ObjectFogTest, FogSetRangeIsDegenerateWhenTheRampCollapses) {
    // The engine's own branch: `if (eq(diffl, 0) || eq(a, 0)) { start = end = 0; }`.
    // A zero density makes a == b == 1, so diff == 0 and there is no ramp to build.
    EXPECT_TRUE(igi::FogRangeIsDegenerate(igi::FogSetRangeForDepth(200000.0f, 20000.0f, 0.0f)));
    const igi::FogRange none = igi::FogSetRangeForDepth(200000.0f, 20000.0f, 0.0f);
    EXPECT_FLOAT_EQ(none.start, 0.0f);
    EXPECT_FLOAT_EQ(none.end, 0.0f);

    // A valid ramp is NOT reported degenerate, so the flag cannot be vacuous.
    const float level1 = 1.89f * igi::kGlobalLightFogScale;
    EXPECT_FALSE(igi::FogRangeIsDegenerate(igi::FogSetRangeForDepth(200000.0f, 20000.0f, level1)));

    // And the flag is the only safe gate for a span a caller might divide by: a
    // hand-built non-finite range is rejected even though end > start holds.
    igi::FogRange infinite;
    infinite.start = -std::numeric_limits<float>::infinity();
    infinite.end = 2050.0f;
    EXPECT_TRUE(igi::FogRangeIsDegenerate(infinite));
    EXPECT_TRUE(igi::FogRangeIsDegenerate(igi::FogRange{}));   // {0,0}
}

TEST(ObjectFogCorpusTest, EveryLevelAuthorsBothTheTerrainAndTheObjectFog) {
    if (!fs::is_regular_file(LevelDir(1) / "OBJECTS.QVM")) {
        GTEST_SKIP() << "retail IGI not found at " << GameRoot().string()
                     << " (set IGI_GAME_PATH to a retail install to run this test)";
    }

    int levels = 0;
    for (int level = 1; level <= 14; ++level) {
        const fs::path path = LevelDir(level) / "OBJECTS.QVM";
        if (!fs::is_regular_file(path)) continue;

        const QVMFile qvm = QVM_Parse(path.string());
        ASSERT_TRUE(qvm.valid) << "LEVEL" << level;
        const std::vector<std::vector<std::string>> keyframes = ExtractKeyframes(qvm);
        ASSERT_FALSE(keyframes.empty()) << "LEVEL" << level;

        const igi::MissionFog fog = igi::ResolveMissionFog(KeyframesAsFogTasks(keyframes));
        ASSERT_TRUE(fog.authored) << "LEVEL" << level;
        ++levels;

        // The selected keyframe is the smallest-Time one, which is also the one
        // app_level.cpp must light with (GlobalLight_SetTime(0.0f) -> hi keyframe).
        float min_time = keyframes[0].size() > 53 ? std::strtof(keyframes[0][53].c_str(), nullptr)
                                                  : 0.0f;
        for (const std::vector<std::string>& kf : keyframes) {
            if (kf.size() <= 53) continue;
            const float time = std::strtof(kf[53].c_str(), nullptr);
            if (time < min_time) min_time = time;
        }
        EXPECT_FLOAT_EQ(fog.time_seconds, min_time) << "LEVEL" << level;

        // Both passes have a usable authored fog: the terrain set 0 and the object
        // set 1. A zero here would mean the level renders with no fog at all on
        // that pass, which is exactly the defect this wiring fixes for objects.
        EXPECT_GT(fog.sets[0].density, 0.0f) << "LEVEL" << level << " terrain density";
        EXPECT_GT(fog.sets[igi::kObjectLightSet].density, 0.0f)
            << "LEVEL" << level << " object-category-1 density";
        EXPECT_GT(fog.terrain_fog_scale(), 0.0f) << "LEVEL" << level;
        EXPECT_GT(fog.object_fog_scale(), 0.0f) << "LEVEL" << level;

        // The object e-folding distance the editor hands the object shader, in
        // metres, and the terrain one: both must be finite and inside the range the
        // authored densities imply (51.4 m .. 4069 m across the corpus).
        const float object_metres = igi::LinearFogFarMetresForAuthoredDensity(
            fog.sets[igi::kObjectLightSet].density, 30000.0f);
        EXPECT_GT(object_metres, 1.0f) << "LEVEL" << level;
        EXPECT_LT(object_metres, 30000.0f) << "LEVEL" << level;

        // The two values Level::LoadFogInfo actually hands over, plus the UBO
        // coefficient Renderer::ApplyFogIntensity derives from the terrain one.
        // `terrain_setup_far` is SetupFog's `fog_far` argument; the coefficient is
        // the number the terrain fragment shader multiplies its view depth by.
        const float terrain_metres = igi::LinearFogFarMetresForAuthoredDensity(
            fog.sets[0].density, 30000.0f);
        const float terrain_setup_far = terrain_metres * igi::kEditorWorldUnitsPerMetre;
        const float object_setup_far = object_metres * igi::kEditorWorldUnitsPerMetre;
        const float ubo_coefficient = 1.0f / (terrain_setup_far * 0.001f);
        EXPECT_GT(ubo_coefficient, 0.0f) << "LEVEL" << level;
        EXPECT_NEAR(ubo_coefficient, fog.terrain_fog_scale(), fog.terrain_fog_scale() * 1e-5f)
            << "LEVEL" << level;

        std::printf("  level %2d  terrain d=%-8.3f e=%-8.1f m  SetupFog far=%-12.0f coeff=%.9f"
                    "  object d=%-8.3f e=%-8.1f m  SetupObjectFog far=%-12.0f\n",
                    level, fog.sets[0].density, terrain_metres, terrain_setup_far,
                    ubo_coefficient, fog.sets[igi::kObjectLightSet].density, object_metres,
                    object_setup_far);
    }
    EXPECT_EQ(levels, 14) << "the corpus must be all fourteen shipped levels";
}

TEST(ObjectFogCorpusTest, TheFirstKeyframeInDocumentOrderIsTheSmallestTime) {
    // app_level.cpp used to light with the FIRST GlobalLightKeyframe child in
    // document order. That is only the engine's keyframe (GlobalLight_SetTime(0.0f)
    // -> the smallest Time, globallight.c:337 + :186-192) if the level happens to
    // author them chronologically. This test records whether it does, for every
    // shipped level, so the two rules can never silently diverge again.
    if (!fs::is_regular_file(LevelDir(1) / "OBJECTS.QVM")) {
        GTEST_SKIP() << "retail IGI not found at " << GameRoot().string()
                     << " (set IGI_GAME_PATH to a retail install to run this test)";
    }

    for (int level = 1; level <= 14; ++level) {
        const fs::path path = LevelDir(level) / "OBJECTS.QVM";
        if (!fs::is_regular_file(path)) continue;

        const QVMFile qvm = QVM_Parse(path.string());
        const std::vector<std::vector<std::string>> keyframes = ExtractKeyframes(qvm);
        ASSERT_GE(keyframes.size(), 1u) << "LEVEL" << level;

        ASSERT_GT(keyframes[0].size(), 53u) << "LEVEL" << level;
        const float first_time = std::strtof(keyframes[0][53].c_str(), nullptr);

        float min_time = first_time;
        int min_count = 1;
        for (std::size_t i = 1; i < keyframes.size(); ++i) {
            if (keyframes[i].size() <= 53) continue;
            const float time = std::strtof(keyframes[i][53].c_str(), nullptr);
            if (time < min_time) { min_time = time; min_count = 1; }
            else if (time == min_time) { ++min_count; }
        }

        std::printf("  level %2d: %zu keyframe(s), first Time=%.1f, smallest Time=%.1f%s\n",
                    level, keyframes.size(), first_time, min_time,
                    first_time == min_time ? "" : "  <-- document order is NOT chronological");

        // The rule the engine's GlobalLight_SetTime(0.0f) resolves has to have ONE
        // answer: if two keyframes shared the smallest Time, GlobalLight_Apply's
        // `hi` would be whichever the child list reaches first, and the editor's
        // resolver would be free to pick the other.
        EXPECT_EQ(min_count, 1) << "LEVEL" << level
                                << ": the smallest-Time keyframe is not unique";
    }
}

