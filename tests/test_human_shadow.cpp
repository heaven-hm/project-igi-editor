// test_human_shadow.cpp -- the engine's blob shadow quad
// (source/runtime/human_shadow.h), pinned against IGI-RE.
//
// ORACLE (read-only, /Users/haseeb-mir/Documents/Code/IGI-RE):
//   Shadow_MagicObjEnterHandler HOOK 0x4E09D0 (src/shadow/shadow.c:84-146) -- the
//   visibility gate, the fade, the clamp chain, the colour and the four corners;
//   Shadow_New HOOK 0x4E0CC0 (src/shadow/shadow.c:154-198) -- the two triangles and
//   their UVs; Matrix_SetDirection HOOK 0x4B4010 (src/math/matrix.c:244-277) -- the
//   ground basis; Human_CreateHandler HOOK 0x45EB30 (src/soldier/human.c:337-373) --
//   the only caller of Shadow_New, and the two constants it passes.
//
// The same functions are verified against the retail binary by IGI-RE's own
// emulator suite (IGI-RE tests/test_shadow_shadow.py, which runs the reconstruction
// and the original side by side at 0x4E09D0 over 6 heights x 5 directions x 2
// rounding modes), so these tests pin a rule that has been checked against the
// shipped executable -- not just against a reading of the decompilation.
#include "runtime/human_shadow.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

constexpr float kEps = 1e-5f;

// ── The engine's constants, read straight off the DATA lines ────────────────

TEST(HumanShadowConstants, TheFadeConstantsAreTheAliasedData) {
    // src/statgun/mp5sd.c:120-122 DATA(5336CC) kZoomIn INIT(0.8f), ALIAS(kShadowFade, ...)
    EXPECT_FLOAT_EQ(igi::kShadowFade, 0.8f);
    // src/heli/wheel.c:285-286 DATA(5336A4) kThreeTenths INIT(0.3f), ALIAS(kShadowMinFade, ...)
    EXPECT_FLOAT_EQ(igi::kShadowMinFade, 0.3f);
    // src/glass/shatter.c:579-581 DATA(533480) kTen INIT(10.0f), ALIAS(kShadowDepth, ...)
    EXPECT_FLOAT_EQ(igi::kShadowDepth, 10.0f);
    // src/shadow/shadow.c:201-202 INIT({0.9f, 0.9f, 0.9f, 0.0f})
    EXPECT_FLOAT_EQ(igi::kShadowColor[0], 0.9f);
    EXPECT_FLOAT_EQ(igi::kShadowColor[1], 0.9f);
    EXPECT_FLOAT_EQ(igi::kShadowColor[2], 0.9f);
}

TEST(HumanShadowConstants, TheTwoPlacementConstantsKeepTheirLiteralForm) {
    // src/soldier/human.c:189 `static const float kShadowSize = 4096000.0f;`
    EXPECT_FLOAT_EQ(igi::kShadowMaxHeight, 4096000.0f);
    // src/soldier/human.c:190 `static const float kShadowHeight = 0x1.ccccccp+10f;`
    EXPECT_FLOAT_EQ(igi::kShadowQuadSize, 0x1.ccccccp+10f);
    EXPECT_FLOAT_EQ(igi::kShadowQuadSize, 1843.2f);
}

// The two constants are named backwards for what they do, and the numbers say so:
// Shadow_New's 7th argument lands in `sh->maxHeight`, the fade DENOMINATOR
// (shadow.c:166 + :94), and its 8th in `sh->size`, the quad HALF-extent
// (shadow.c:167 + :106). Read in the engine's own unit (kFixed = 4096 units/m,
// src/math/mathvars.h:7) they are 1000 m and 0.45 m, i.e. a fade that never
// engages and a 0.9 m blob -- not the other way round.
TEST(HumanShadowConstants, TheMetricReadingOfTheTwoConstants) {
    EXPECT_NEAR(igi::kShadowQuadSize / igi::kShadowUnitsPerMetre, 0.45, 1e-4);
    EXPECT_NEAR(igi::kShadowMaxHeight / igi::kShadowUnitsPerMetre, 1000.0, 1e-3);
    // A 0.45 m half-extent is the only reading that fits a human: the alternative
    // (4096000 as the size) would be a 2 km shadow.
    EXPECT_LT(igi::kShadowQuadSize, 4096.0f);
    EXPECT_GT(igi::kShadowMaxHeight, 4096.0f * 100.0f);
}

// ── The fade curve, shadow.c:94-100 ─────────────────────────────────────────

TEST(HumanShadowFade, OnTheGroundIsTheUnclampedCurveValue) {
    // t = (1 - 0/4096000) * 0.8
    EXPECT_FLOAT_EQ(igi::HumanShadowFade(0.0, igi::kShadowMaxHeight), 0.8f);
}

TEST(HumanShadowFade, FadesLinearlyWithHeight) {
    const double M = igi::kShadowMaxHeight;
    EXPECT_NEAR(igi::HumanShadowFade(0.25 * M, M), 0.75 * 0.8, 1e-6);
    EXPECT_NEAR(igi::HumanShadowFade(0.50 * M, M), 0.50 * 0.8, 1e-6);
    // 0.625*M is exactly where the raw curve reaches kShadowMinFade:
    // (1 - 0.625)*0.8 = 0.3.
    EXPECT_NEAR(igi::HumanShadowFade(0.625 * M, M), igi::kShadowMinFade, 1e-6);
}

TEST(HumanShadowFade, ClampsAtTheMinimum) {
    const double M = igi::kShadowMaxHeight;
    // Above 0.625*M the raw curve is below 0.3, so the first branch bites.
    EXPECT_FLOAT_EQ(igi::HumanShadowFade(0.7 * M, M), igi::kShadowMinFade);
    EXPECT_FLOAT_EQ(igi::HumanShadowFade(M, M), igi::kShadowMinFade);
    EXPECT_FLOAT_EQ(igi::HumanShadowFade(100.0 * M, M), igi::kShadowMinFade);
    // The clamp point is 0.625 * 4096000 units = 2560000 units = 625 m.
    EXPECT_NEAR(0.625 * M / igi::kShadowUnitsPerMetre, 625.0, 1e-6);
}

TEST(HumanShadowFade, ClampsAtTheTopWhenTheHeightIsNegative) {
    const double M = igi::kShadowMaxHeight;
    // A foot below the ground: (1 + 0.5)*0.8 = 1.2 -> the third branch clamps to 1.
    EXPECT_FLOAT_EQ(igi::HumanShadowFade(-0.5 * M, M), 1.0f);
    EXPECT_FLOAT_EQ(igi::HumanShadowFade(-M, M), 1.0f);
    // The middle branch (t < 0) is dead code in the original: t < 0 requires
    // height > maxHeight, which the first branch has already clamped. Recorded
    // rather than "fixed": the port keeps the original's order.
    EXPECT_FLOAT_EQ(igi::HumanShadowFade(2.0 * M, M), igi::kShadowMinFade);
}

TEST(HumanShadowFade, NaNAndInfinitiesTakeTheFirstBranch) {
    const double M = igi::kShadowMaxHeight;
    const double nan = std::nan("");
    // `!(NaN >= kShadowMinFade)` is true, so NaN lands on the MINIMUM, not on 1.
    EXPECT_FLOAT_EQ(igi::HumanShadowFade(nan, M), igi::kShadowMinFade);
    // +inf height -> t = -inf -> the minimum; -inf height -> t = +inf -> 1.
    EXPECT_FLOAT_EQ(igi::HumanShadowFade(INFINITY, M), igi::kShadowMinFade);
    EXPECT_FLOAT_EQ(igi::HumanShadowFade(-INFINITY, M), 1.0f);
}

TEST(HumanShadowFade, AZeroOrNegativeDenominatorIsStillWellDefined) {
    // 0/0 = NaN -> the first branch.
    EXPECT_FLOAT_EQ(igi::HumanShadowFade(0.0, 0.0), igi::kShadowMinFade);
    // 1/0 = +inf -> t = -inf -> the first branch.
    EXPECT_FLOAT_EQ(igi::HumanShadowFade(1.0, 0.0), igi::kShadowMinFade);
    // A negative denominator flips the ratio: (1 - 1/-1) * 0.8 = 1.6 -> clamped to 1.
    EXPECT_FLOAT_EQ(igi::HumanShadowFade(1.0, -1.0), 1.0f);
}

// The honest reading of the retail constants: the curve is flat to 0.2% at any
// height a soldier can reach, so the fade is a safety net rather than a visible
// effect. Pinned so a future change to kShadowMaxHeight cannot pass unnoticed.
TEST(HumanShadowFade, TheRetailFadeIsEffectivelyConstant) {
    const double jump = 1.8 * igi::kShadowUnitsPerMetre;  // 1.8 m = 7372.8 units
    const float at_jump = igi::HumanShadowFade(jump, igi::kShadowMaxHeight);
    EXPECT_NEAR(at_jump, 0.79856f, 1e-5f);
    EXPECT_LT(std::fabs(at_jump - 0.8f), 0.002f);
    // A 100 m fall is 409600 units = a tenth of the denominator, so it still only
    // reaches 0.72 -- an order of magnitude above the 0.3 clamp.
    EXPECT_NEAR(igi::HumanShadowFade(100.0 * igi::kShadowUnitsPerMetre, igi::kShadowMaxHeight),
                0.72f, 1e-4f);
}

// ── The colour, shadow.c:101-104 ────────────────────────────────────────────

TEST(HumanShadowColor, IsTheGreyConstantScaledByTheFade) {
    float c[3] = {0.0f, 0.0f, 0.0f};
    igi::HumanShadowColor(0.8f, c);
    EXPECT_NEAR(c[0], 0.72, kEps);
    EXPECT_NEAR(c[1], 0.72, kEps);
    EXPECT_NEAR(c[2], 0.72, kEps);
}

TEST(HumanShadowColor, StaysGreyAtEveryFade) {
    const float fades[] = {0.0f, 0.3f, 0.5f, 0.8f, 1.0f};
    for (float f : fades) {
        float c[3] = {0.0f, 0.0f, 0.0f};
        igi::HumanShadowColor(f, c);
        EXPECT_FLOAT_EQ(c[0], c[1]);
        EXPECT_FLOAT_EQ(c[1], c[2]);
        EXPECT_NEAR(c[0], 0.9f * f, kEps);
    }
}

TEST(HumanShadowColor, TheClampedFadesGiveTheClampedColours) {
    float c[3] = {0.0f, 0.0f, 0.0f};
    igi::HumanShadowColor(igi::kShadowMinFade, c);
    EXPECT_NEAR(c[0], 0.27, kEps);
    igi::HumanShadowColor(1.0f, c);
    EXPECT_NEAR(c[0], 0.9, kEps);
}

// ── The ground basis, Matrix_SetDirection (matrix.c:244-277) ────────────────

TEST(HumanShadowBasis, AZUpNormalIsTheIdentity) {
    const double up[3] = {0.0, 0.0, 1.0};
    float m[9] = {};
    igi::HumanShadowBasis(up, m);
    const float ident[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    for (int i = 0; i < 9; ++i) EXPECT_FLOAT_EQ(m[i], ident[i]) << "m[" << i << "]";
}

TEST(HumanShadowBasis, TheThirdAxisIsTheNormalItself) {
    // For a unit direction the original's t = side x s is the direction again, so
    // the kShadowDepth lift is along the ground normal, whatever the slope.
    const double ups[3][3] = {{0.0, 0.0, 1.0}, {0.0, 0.0, -1.0}, {0.5773502691896258, 0.5773502691896258, 0.5773502691896258}};
    for (const auto& up : ups) {
        float m[9] = {};
        igi::HumanShadowBasis(up, m);
        EXPECT_NEAR(m[6], up[0], 1e-6);
        EXPECT_NEAR(m[7], up[1], 1e-6);
        EXPECT_NEAR(m[8], up[2], 1e-6);
    }
}

TEST(HumanShadowBasis, TheFirstTwoAxesAreOrthonormalAndInPlane) {
    const double up[3] = {1.0, 2.0, 3.0};
    const double len = std::sqrt(14.0);
    const double n[3] = {up[0] / len, up[1] / len, up[2] / len};
    float m[9] = {};
    igi::HumanShadowBasis(up, m);

    const double a[3] = {m[0], m[1], m[2]};
    const double b[3] = {m[3], m[4], m[5]};
    const double c[3] = {m[6], m[7], m[8]};
    const auto dot = [](const double p[3], const double q[3]) {
        return p[0] * q[0] + p[1] * q[1] + p[2] * q[2];
    };
    EXPECT_NEAR(dot(a, a), 1.0, 1e-6);
    EXPECT_NEAR(dot(b, b), 1.0, 1e-6);
    EXPECT_NEAR(dot(c, c), 1.0, 1e-6);
    EXPECT_NEAR(dot(a, b), 0.0, 1e-6);
    EXPECT_NEAR(dot(a, c), 0.0, 1e-6);
    EXPECT_NEAR(dot(b, c), 0.0, 1e-6);
    // The first axis is perpendicular to the horizontal reference the original
    // builds from (d.y, -d.x, 0) (matrix.c:253-257) -- it is in the ground plane,
    // but not necessarily horizontal itself.
    const double ref[3] = {up[1], -up[0], 0.0};
    EXPECT_NEAR(dot(a, ref), 0.0, 1e-6);
}

TEST(HumanShadowBasis, ScalesWithTheNormalBecauseTheOriginalNeverNormalizesIt) {
    // A non-unit normal gives the same basis: the first cross is normalized and the
    // third axis comes out normalized too. (The original does use the UNNORMALIZED
    // side for the second cross, :264 -- pinned here by equality on a scaled input.)
    const double unit_up[3] = {0.0, 0.0, 1.0};
    const double big_up[3] = {0.0, 0.0, 5.0};
    float a[9] = {}, b[9] = {};
    igi::HumanShadowBasis(unit_up, a);
    igi::HumanShadowBasis(big_up, b);
    for (int i = 0; i < 9; ++i) EXPECT_NEAR(a[i], b[i], 1e-6) << "m[" << i << "]";
}

TEST(HumanShadowBasis, TheDegenerateZeroNormalIsAZeroBasisNotNaN) {
    // Vec3f_Normalize returns a zero vector unchanged (matrix.c:48-49), so a zero
    // direction collapses the quad instead of producing NaNs. IGI-RE's own emulator
    // suite covers dir = (0,0,0) at 0x4E09D0 for exactly this reason.
    const double up[3] = {0.0, 0.0, 0.0};
    float m[9] = {};
    igi::HumanShadowBasis(up, m);
    for (int i = 0; i < 9; ++i) EXPECT_FLOAT_EQ(m[i], 0.0f) << "m[" << i << "]";
}

// ── The quad, shadow.c:106-140 ──────────────────────────────────────────────

TEST(HumanShadowQuad, FlatGroundIsAPlusMinusSquareLiftedByTheDepth) {
    const double up[3] = {0.0, 0.0, 1.0};
    const float h = igi::kShadowQuadSize;
    const float d = igi::kShadowDepth;
    float v[4][3] = {};
    igi::HumanShadowQuad(up, h, d, v);

    EXPECT_FLOAT_EQ(v[0][0], h);   EXPECT_FLOAT_EQ(v[0][1], h);   EXPECT_FLOAT_EQ(v[0][2], d);
    EXPECT_FLOAT_EQ(v[1][0], -h);  EXPECT_FLOAT_EQ(v[1][1], h);   EXPECT_FLOAT_EQ(v[1][2], d);
    EXPECT_FLOAT_EQ(v[2][0], h);   EXPECT_FLOAT_EQ(v[2][1], -h);  EXPECT_FLOAT_EQ(v[2][2], d);
    EXPECT_FLOAT_EQ(v[3][0], -h);  EXPECT_FLOAT_EQ(v[3][1], -h);  EXPECT_FLOAT_EQ(v[3][2], d);
}

TEST(HumanShadowQuad, TheRetailDepthIsUnderThreeMillimetres) {
    // 10 units at 4096 units/m.
    EXPECT_NEAR(igi::kShadowDepth / igi::kShadowUnitsPerMetre, 0.002441, 1e-6);
}

TEST(HumanShadowQuad, TheQuadIsCentredOnTheOwnersGroundPosition) {
    // The four corners are LOCAL to the PolyList position, which
    // Shadow_MagicObjEnterHandler sets to *sh->pos = the owner's ground position
    // (shadow.c:141). So their mean is the ground position plus the depth lift.
    const double up[3] = {0.3, -0.2, 0.9};
    float v[4][3] = {};
    igi::HumanShadowQuad(up, igi::kShadowQuadSize, igi::kShadowDepth, v);

    const double len = std::sqrt(0.3 * 0.3 + 0.2 * 0.2 + 0.9 * 0.9);
    for (int axis = 0; axis < 3; ++axis) {
        const double mean = (v[0][axis] + v[1][axis] + v[2][axis] + v[3][axis]) / 4.0;
        EXPECT_NEAR(mean, igi::kShadowDepth * (up[axis] / len), 1e-3) << "axis " << axis;
    }
}

TEST(HumanShadowQuad, TheCornersAreOppositePairsInTheGroundPlane) {
    const double up[3] = {0.3, -0.2, 0.9};
    float v[4][3] = {};
    igi::HumanShadowQuad(up, igi::kShadowQuadSize, igi::kShadowDepth, v);

    const double len = std::sqrt(0.3 * 0.3 + 0.2 * 0.2 + 0.9 * 0.9);
    const double c[3] = {igi::kShadowDepth * up[0] / len, igi::kShadowDepth * up[1] / len,
                         igi::kShadowDepth * up[2] / len};
    // v0 + v3 == 2c and v1 + v2 == 2c: the quad is a parallelogram centred on c.
    for (int axis = 0; axis < 3; ++axis) {
        EXPECT_NEAR(v[0][axis] + v[3][axis], 2.0 * c[axis], 1e-3) << "axis " << axis;
        EXPECT_NEAR(v[1][axis] + v[2][axis], 2.0 * c[axis], 1e-3) << "axis " << axis;
    }
    // Every edge is 2*size long: |v0 - v1| and |v1 - v3|.
    const auto dist = [&](const float p[3], const float q[3]) {
        return std::sqrt(std::pow(p[0] - q[0], 2.0) + std::pow(p[1] - q[1], 2.0) +
                         std::pow(p[2] - q[2], 2.0));
    };
    EXPECT_NEAR(dist(v[0], v[1]), 2.0 * igi::kShadowQuadSize, 1e-2);
    EXPECT_NEAR(dist(v[1], v[3]), 2.0 * igi::kShadowQuadSize, 1e-2);
    EXPECT_NEAR(dist(v[0], v[3]), 2.0 * igi::kShadowQuadSize * std::sqrt(2.0), 1e-2);
}

TEST(HumanShadowQuad, EveryCornerLiesInTheGroundPlane) {
    const double up[3] = {0.0, 0.0, 1.0};
    float v[4][3] = {};
    igi::HumanShadowQuad(up, 100.0f, 10.0f, v);
    for (int i = 0; i < 4; ++i) EXPECT_FLOAT_EQ(v[i][2], 10.0f) << "corner " << i;
}

TEST(HumanShadowQuad, ADownwardNormalLiftsTheQuadDownwards) {
    // m[6..8] is the direction itself, so an inverted ground normal inverts the
    // lift. (The engine never feeds one: groundUp is a surface normal.)
    const double up[3] = {0.0, 0.0, -1.0};
    float v[4][3] = {};
    igi::HumanShadowQuad(up, 100.0f, 10.0f, v);
    for (int i = 0; i < 4; ++i) EXPECT_FLOAT_EQ(v[i][2], -10.0f) << "corner " << i;
}

TEST(HumanShadowQuad, AZeroNormalCollapsesTheQuadAtTheDepthZeroPoint) {
    const double up[3] = {0.0, 0.0, 0.0};
    float v[4][3] = {};
    igi::HumanShadowQuad(up, igi::kShadowQuadSize, igi::kShadowDepth, v);
    for (int i = 0; i < 4; ++i)
        for (int axis = 0; axis < 3; ++axis) EXPECT_FLOAT_EQ(v[i][axis], 0.0f);
}

TEST(HumanShadowQuad, AZeroSizeIsAPointAtTheDepthLift) {
    const double up[3] = {0.0, 0.0, 1.0};
    float v[4][3] = {};
    igi::HumanShadowQuad(up, 0.0f, igi::kShadowDepth, v);
    for (int i = 0; i < 4; ++i) {
        EXPECT_FLOAT_EQ(v[i][0], 0.0f);
        EXPECT_FLOAT_EQ(v[i][1], 0.0f);
        EXPECT_FLOAT_EQ(v[i][2], igi::kShadowDepth);
    }
}

// ── The two triangles and their UVs, shadow.c:170-195 ───────────────────────

TEST(HumanShadowTriangles, AreTheEnginePairWithTheEngineUVs) {
    const double up[3] = {0.0, 0.0, 1.0};
    float v[4][3] = {};
    igi::HumanShadowQuad(up, igi::kShadowQuadSize, igi::kShadowDepth, v);
    const igi::HumanShadowTriangles tri = igi::HumanShadowBuildTriangles(v);

    ASSERT_EQ(tri.count, 2);
    // poly[0] = (verts[0], verts[1], verts[2]) with uv (0,0),(1,0),(0,1).
    const int idx[2][3] = {{0, 1, 2}, {1, 3, 2}};
    for (int t = 0; t < 2; ++t)
        for (int i = 0; i < 3; ++i)
            for (int axis = 0; axis < 3; ++axis)
                EXPECT_FLOAT_EQ(tri.pos[t][i][axis], v[idx[t][i]][axis])
                    << "tri " << t << " vert " << i << " axis " << axis;

    EXPECT_FLOAT_EQ(tri.uv[0][0][0], 0.0f); EXPECT_FLOAT_EQ(tri.uv[0][0][1], 0.0f);
    EXPECT_FLOAT_EQ(tri.uv[0][1][0], 1.0f); EXPECT_FLOAT_EQ(tri.uv[0][1][1], 0.0f);
    EXPECT_FLOAT_EQ(tri.uv[0][2][0], 0.0f); EXPECT_FLOAT_EQ(tri.uv[0][2][1], 1.0f);
    EXPECT_FLOAT_EQ(tri.uv[1][0][0], 1.0f); EXPECT_FLOAT_EQ(tri.uv[1][0][1], 0.0f);
    EXPECT_FLOAT_EQ(tri.uv[1][1][0], 1.0f); EXPECT_FLOAT_EQ(tri.uv[1][1][1], 1.0f);
    EXPECT_FLOAT_EQ(tri.uv[1][2][0], 0.0f); EXPECT_FLOAT_EQ(tri.uv[1][2][1], 1.0f);
}

TEST(HumanShadowTriangles, TheTwoTrianglesCoverTheQuadExactlyOnce) {
    // Both triangles of the engine's pair are wound the SAME way (the quad is a
    // fan across the v1-v2 diagonal), and their doubled areas sum to twice the
    // quad's area: 2 * (2h * 2h) for area2 == 2 * area.
    const double up[3] = {0.0, 0.0, 1.0};
    float v[4][3] = {};
    igi::HumanShadowQuad(up, 100.0f, 10.0f, v);
    const igi::HumanShadowTriangles tri = igi::HumanShadowBuildTriangles(v);
    const auto area2 = [](const float a[3], const float b[3], const float c[3]) {
        return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]);
    };
    const float a0 = area2(tri.pos[0][0], tri.pos[0][1], tri.pos[0][2]);
    const float a1 = area2(tri.pos[1][0], tri.pos[1][1], tri.pos[1][2]);
    EXPECT_NEAR(a0, 4.0f * 100.0f * 100.0f, 1e-2);
    EXPECT_NEAR(a1, 4.0f * 100.0f * 100.0f, 1e-2);
    EXPECT_NEAR(std::fabs(a0) + std::fabs(a1), 2.0f * (200.0f * 200.0f), 1e-2);
}

// ── Placement ───────────────────────────────────────────────────────────────

TEST(HumanShadowPlacement, SitsAtTheGroundPositionSizedByTheConstants) {
    const igi::HumanShadowPlacement p =
        igi::BuildHumanShadowPlacement(1234.5, -678.25, 4096.0, true, 4096.0);
    ASSERT_TRUE(p.valid);
    EXPECT_DOUBLE_EQ(p.ground_pos[0], 1234.5);
    EXPECT_DOUBLE_EQ(p.ground_pos[1], -678.25);
    EXPECT_DOUBLE_EQ(p.ground_pos[2], 4096.0);
    // On the ground: the full 0.8 fade and the 0.72 grey.
    EXPECT_FLOAT_EQ(p.fade, 0.8f);
    EXPECT_NEAR(p.color[0], 0.72, kEps);
    // And the retail quad: +-1843.2 units, lifted 10 along the normal.
    EXPECT_FLOAT_EQ(p.verts[0][0], igi::kShadowQuadSize);
    EXPECT_FLOAT_EQ(p.verts[0][1], igi::kShadowQuadSize);
    EXPECT_FLOAT_EQ(p.verts[0][2], igi::kShadowDepth);
}

TEST(HumanShadowPlacement, UsesTheSuppliedGroundHitForThePosition) {
    // The engine's groundPos is the probe's hit (humanbase/human.c:1844), not the
    // owner's own position: a placement 1.8 m above the floor puts the shadow ON
    // the floor.
    const double floor = 100.0 * igi::kShadowUnitsPerMetre;
    const double foot = floor + 1.8 * igi::kShadowUnitsPerMetre;
    const igi::HumanShadowPlacement p = igi::BuildHumanShadowPlacement(10.0, 20.0, foot, true, floor);
    ASSERT_TRUE(p.valid);
    EXPECT_DOUBLE_EQ(p.ground_pos[2], floor);
    // Height above ground 7372.8 units -> the barely-faded retail value.
    EXPECT_NEAR(p.fade, 0.79856f, 1e-5f);
    EXPECT_NEAR(p.color[0], 0.9 * 0.79856, 1e-4);
}

TEST(HumanShadowPlacement, WithoutAGroundHitThePlacementIsTheGround) {
    // The editor's mapping of `groundValid = 0` (humanbase/human.c:1840-1843): the
    // collision world is not available, so the authored Z -- the surface the editor
    // placed the object on -- is the ground, and the height above it is zero.
    const igi::HumanShadowPlacement p =
        igi::BuildHumanShadowPlacement(5.0, 6.0, 789.0, false, 0.0);
    ASSERT_TRUE(p.valid);
    EXPECT_DOUBLE_EQ(p.ground_pos[2], 789.0);
    EXPECT_FLOAT_EQ(p.fade, 0.8f);
}

TEST(HumanShadowPlacement, AFallenOwnerStillFadesByTheFormula) {
    // 700 m up: past the 625 m clamp point, so the minimum fade and the 0.27 grey.
    const double ground = 0.0;
    const double high = 700.0 * igi::kShadowUnitsPerMetre;
    const igi::HumanShadowPlacement p = igi::BuildHumanShadowPlacement(0.0, 0.0, high, true, ground);
    EXPECT_FLOAT_EQ(p.fade, igi::kShadowMinFade);
    EXPECT_NEAR(p.color[0], 0.27, kEps);
}

TEST(HumanShadowPlacement, TheQuadIsLocalToTheGroundPosition) {
    // The drawn quad is ground_pos + verts: the corners never carry the world
    // position themselves (shadow.c:141 sets the PolyList position instead).
    const igi::HumanShadowPlacement p =
        igi::BuildHumanShadowPlacement(100000.0, 200000.0, 3000.0, true, 3000.0);
    for (int i = 0; i < 4; ++i) {
        EXPECT_LE(std::fabs(p.verts[i][0]), igi::kShadowQuadSize);
        EXPECT_LE(std::fabs(p.verts[i][1]), igi::kShadowQuadSize);
    }
}

// ── The visibility gate, shadow.c:92 ────────────────────────────────────────

TEST(HumanShadowGate, NeedsBothTheVisibleAndTheEnabledFlag) {
    EXPECT_TRUE(igi::HumanShadowDrawn(1, 1));
    EXPECT_FALSE(igi::HumanShadowDrawn(0, 1));   // Human_HideShadow, viewcam/human.c:782
    EXPECT_FALSE(igi::HumanShadowDrawn(1, 0));   // groundValid = 0, humanbase/human.c:1841
    EXPECT_FALSE(igi::HumanShadowDrawn(0, 0));
}

// ── Which types own a shadow, task.c:397-398 + human.c:357 ──────────────────

TEST(HumanShadowOwners, EveryHumanDerivedTypeHasOne) {
    EXPECT_TRUE(igi::IsHumanShadowOwner("Human"));            // human.c:246
    EXPECT_TRUE(igi::IsHumanShadowOwner("HumanSoldier"));     // soldier.c:718
    EXPECT_TRUE(igi::IsHumanShadowOwner("HumanPlayer"));      // humanplayer.c:1091
    EXPECT_TRUE(igi::IsHumanShadowOwner("HumanSoldierPistol"));  // pistol.c:550
    EXPECT_TRUE(igi::IsHumanShadowOwner("HumanSoldierRPG"));  // rpg.c:593
}

TEST(HumanShadowOwners, HumanAINeedsNoShadowBecauseItIsNotHumanDerived) {
    // aiaction.c:194 -- `TaskType_New("HumanAI", ..., Task_GetRootType(), 0)`, so it
    // never inherits Human_CreateHandler. The AI soldiers in the levels are
    // HumanSoldier tasks with a HumanAI child; the HumanAI controller itself is not
    // a human body and Shadow_New has exactly one caller.
    EXPECT_FALSE(igi::IsHumanShadowOwner("HumanAI"));
}

TEST(HumanShadowOwners, NothingElseOwnsOne) {
    const char* others[] = {"", "Guard", "Door", "Car", "Heli", "Plane", "Train",
                            "ExplodeObject", "Building", "EditRigidObj", "Gun",
                            "HumanPlayerInput", "HumanCamera", "Sector", "Smoke"};
    for (const char* t : others) EXPECT_FALSE(igi::IsHumanShadowOwner(t)) << t;
}

// ── The oracle itself ───────────────────────────────────────────────────────

// The constants above are copies of IGI-RE DATA lines. This pins the copies to the
// files, so a change on either side fails here instead of silently drifting. The
// oracle is an absolute path outside this repository: the test SKIPS when it is not
// present, which is why it must not be the only check (the value tests above run
// everywhere).
class HumanShadowOracle : public ::testing::Test {
protected:
    static std::string ReadOracle(const std::string& relative) {
        const char* env = std::getenv("IGI_RE_PATH");
        const std::string root = env && *env ? env : "/Users/haseeb-mir/Documents/Code/IGI-RE";
        std::ifstream in(root + "/" + relative, std::ios::binary);
        if (!in) return std::string();
        return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    }
};

TEST_F(HumanShadowOracle, TheFadeAndColourConstantsAreStillInShadowC) {
    const std::string src = ReadOracle("src/shadow/shadow.c");
    if (src.empty()) GTEST_SKIP() << "IGI-RE not present (set IGI_RE_PATH)";
    EXPECT_NE(src.find("INIT({0.9f, 0.9f, 0.9f, 0.0f})"), std::string::npos)
        << "gShadowColor is no longer 0.9,0.9,0.9 in shadow.c:201";
    // The fade formula and the clamp chain.
    EXPECT_NE(src.find("(kOneD - (long double)*sh->height / sh->maxHeight) * kShadowFade"), std::string::npos);
    EXPECT_NE(src.find("if (!(t >= kShadowMinFade))"), std::string::npos);
    EXPECT_NE(src.find("else if (t < kZero)"), std::string::npos);
    EXPECT_NE(src.find("else if (!(kOne >= t))"), std::string::npos);
}

TEST_F(HumanShadowOracle, ThePlacementConstantsAreStillInHumanC) {
    const std::string src = ReadOracle("src/soldier/human.c");
    if (src.empty()) GTEST_SKIP() << "IGI-RE not present (set IGI_RE_PATH)";
    EXPECT_NE(src.find("kShadowSize = 4096000.0f"), std::string::npos);
    EXPECT_NE(src.find("kShadowHeight = 0x1.ccccccp+10f"), std::string::npos);
    EXPECT_NE(src.find("Game_AddTexture(\"shadow1.tex\")"), std::string::npos);
    // Shadow_New is called from Human_CreateHandler with (kShadowSize, kShadowHeight)
    // in that order, and Human_CreateHandler is the only caller.
    EXPECT_NE(src.find("Shadow_New(h, h->groundPos, &h->unk7E8, &h->groundValid, &h->groundDist, "
                       "h->groundUp, kShadowSize,"),
              std::string::npos);
}

TEST_F(HumanShadowOracle, TheAliasedFadeConstantsAreStillTheValuesWeUse) {
    const std::string mp5 = ReadOracle("src/statgun/mp5sd.c");
    const std::string wheel = ReadOracle("src/heli/wheel.c");
    const std::string shatter = ReadOracle("src/glass/shatter.c");
    if (mp5.empty() || wheel.empty() || shatter.empty())
        GTEST_SKIP() << "IGI-RE not present (set IGI_RE_PATH)";
    EXPECT_NE(mp5.find("DATA(5336CC) float kZoomIn INIT(0.8f)"), std::string::npos);
    EXPECT_NE(mp5.find("ALIAS(kShadowFade, kZoomIn, 0)"), std::string::npos);
    EXPECT_NE(wheel.find("DATA(5336A4) const float kThreeTenths INIT(0.3f)"), std::string::npos);
    EXPECT_NE(wheel.find("ALIAS(kShadowMinFade, kThreeTenths, 0)"), std::string::npos);
    EXPECT_NE(shatter.find("DATA(533480) float kTen INIT(10.0f)"), std::string::npos);
    EXPECT_NE(shatter.find("ALIAS(kShadowDepth, kTen, 0)"), std::string::npos);
}

TEST_F(HumanShadowOracle, TheBlendAndAlphaSourceAreStillTheOnesPorted) {
    const std::string poly = ReadOracle("src/dxinit/polylist.c");
    if (poly.empty()) GTEST_SKIP() << "IGI-RE not present (set IGI_RE_PATH)";
    // The default (flags == 0) branch: ALPHABLENDENABLE 1, SRCBLEND 5 = SRCALPHA,
    // DESTBLEND 6 = INVSRCALPHA.
    EXPECT_NE(poly.find("SET_RS(0x1B, 1);"), std::string::npos);
    EXPECT_NE(poly.find("SET_RS(0x13, 5);"), std::string::npos);
    EXPECT_NE(poly.find("SET_RS(0x14, 6);"), std::string::npos);
    // A textured triangle takes its ALPHA from the texture (ALPHAOP SELECTARG1),
    // and the RGB is MODULATE(texture, diffuse).
    EXPECT_NE(poly.find("SET_TSS(0, 4, 2);"), std::string::npos);
    EXPECT_NE(poly.find("SET_TSS(0, 1, 4);"), std::string::npos);
}

}  // namespace
