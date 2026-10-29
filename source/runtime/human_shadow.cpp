// human_shadow.cpp -- the engine's blob shadow quad. See human_shadow.h for the
// issue mapping (#63), the oracle files and the rule; this file is the rule and
// nothing else.
//
// The original is compiled with `#pragma GCC optimize("excess-precision=standard")`
// (src/shadow/shadow.c:1) and does its geometry in x87 long double, storing floats.
// The port keeps long double for the same expressions so the rounding matches on
// the 32-bit target; the tests pin the values with a float tolerance rather than
// claiming bit equality with a different FPU.
#include "runtime/human_shadow.h"

#include <cmath>

namespace igi {

namespace {

// Vec3f_Cross (src/math/matrix.c:56-62), which is the plain a x b.
void Cross(double out[3], const double a[3], const double b[3]) {
    out[0] = (double)((long double)b[2] * a[1] - (long double)a[2] * b[1]);
    out[1] = (double)((long double)a[2] * b[0] - (long double)b[2] * a[0]);
    out[2] = (double)((long double)a[0] * b[1] - (long double)b[0] * a[1]);
}

// Vec3f_Normalize (src/math/matrix.c:44-54): a zero vector is returned UNCHANGED,
// which is what makes the degenerate (0,0,0) direction produce an all-zero basis
// instead of NaNs. Every component is rounded to float, because the original's
// vector is a Vec3f.
void Normalize(double v[3]) {
    if (v[0] == 0.0 && v[1] == 0.0 && v[2] == 0.0) return;
    const long double inv = 1.0L / sqrtl((long double)v[0] * v[0] + (long double)v[1] * v[1] +
                                         (long double)v[2] * v[2]);
    v[0] = (double)(float)(v[0] * inv);
    v[1] = (double)(float)(v[1] * inv);
    v[2] = (double)(float)(v[2] * inv);
}

}  // namespace

float HumanShadowFade(double height, double maxHeight) {
    // shadow.c:94-100, in the original's order. `t` is long double there, and the
    // first test is the NaN guard: `!(NaN >= kShadowMinFade)` is true.
    long double t = (1.0L - (long double)height / (long double)maxHeight) *
                    (long double)kShadowFade;
    if (!(t >= (long double)kShadowMinFade))
        t = (long double)kShadowMinFade;      // :95-96, also the NaN branch
    else if (t < 0.0L)
        t = 0.0L;                             // :97-98
    else if (!(1.0L >= t))
        t = 1.0L;                             // :99-100
    return (float)t;
}

void HumanShadowColor(float fade, float out[3]) {
    // shadow.c:101-104: colour[i] = gShadowColor[i]; colour[i] = t * colour[i].
    for (int i = 0; i < 3; ++i)
        out[i] = (float)((long double)fade * (long double)kShadowColor[i]);
}

void HumanShadowBasis(const double up[3], float m[9]) {
    // Matrix_SetDirection, src/math/matrix.c:244-277. The order matters: `side` is
    // used UNNORMALIZED by the second cross (:264), while the third cross uses the
    // normalized side (:265) and the unnormalized s; both results are normalized
    // afterwards (:272-273).
    double d[3] = {up[0], up[1], up[2]};
    double ref[3] = {0.0, 0.0, 0.0};
    double side[3], s[3], t[3];
    double nside[3];

    if (d[0] == 0.0 && d[1] == 0.0) {
        ref[0] = 0.0;
        ref[1] = 1.0;                          // :249-252
    } else {
        ref[0] = d[1];
        ref[1] = -d[0];
        ref[2] = 0.0;
        Normalize(ref);                        // :253-257
    }
    ref[2] = 0.0;                              // :258

    Cross(side, ref, d);                       // :259
    nside[0] = side[0];
    nside[1] = side[1];
    nside[2] = side[2];
    Normalize(nside);                          // :263 -> r.m[0..2]

    Cross(s, d, side);                         // :264, unnormalized `side`
    Cross(t, nside, s);                        // :265, normalized side, unnormalized s

    Normalize(s);                              // :272 -> r.m[3..5]
    Normalize(t);                              // :273 -> r.m[6..8]

    for (int i = 0; i < 3; ++i) {
        m[i] = (float)nside[i];
        m[3 + i] = (float)s[i];
        m[6 + i] = (float)t[i];
    }
}

void HumanShadowQuad(const double up[3], float size, float depth, float verts[4][3]) {
    // shadow.c:105-140 verbatim, including which products are float and which are
    // long double (the float ones are `a*`, `c*` and `h`; the rest are long double).
    float m[9];
    HumanShadowBasis(up, m);

    const float h = size;
    const long double n = -(long double)h;
    const float c0 = m[6] * depth;
    const float c1 = m[7] * depth;
    const float c2 = m[8] * depth;
    const float a0 = h * m[3];
    const float a1 = h * m[4];
    const float a2 = h * m[5];
    long double p0 = (long double)h * m[0];
    long double p1 = (long double)h * m[1];
    long double p2 = (long double)h * m[2];
    const long double b0 = p0;
    const long double b1 = p1;
    const long double b2 = p2;

    verts[0][0] = (float)(p0 + a0 + c0);
    verts[0][1] = (float)(p1 + a1 + c1);
    verts[0][2] = (float)(p2 + a2 + c2);

    p0 = n * m[0];
    p1 = n * m[1];
    p2 = n * m[2];
    const long double d0 = p0;
    const long double d1 = p1;
    const long double d2 = p2;
    verts[1][0] = (float)(p0 + a0 + c0);
    verts[1][1] = (float)(p1 + a1 + c1);
    verts[1][2] = (float)(p2 + a2 + c2);

    const long double e0 = n * m[3];
    const long double e1 = n * m[4];
    const long double e2 = n * m[5];
    verts[2][0] = (float)(b0 + e0 + c0);
    verts[2][1] = (float)(b1 + e1 + c1);
    verts[2][2] = (float)(b2 + e2 + c2);
    verts[3][0] = (float)(d0 + e0 + c0);
    verts[3][1] = (float)(d1 + e1 + c1);
    verts[3][2] = (float)(d2 + e2 + c2);
}

HumanShadowTriangles HumanShadowBuildTriangles(const float verts[4][3]) {
    // shadow.c:170-195. poly[0] = (0,1,2) with uv (0,0),(1,0),(0,1);
    // poly[1] = (1,3,2) with uv (1,0),(1,1),(0,1). All three colour pointers of a
    // triangle point at the SAME colour (shadow.c:174, :187), so the quad is flat.
    HumanShadowTriangles tri{};
    const int idx[2][3] = {{0, 1, 2}, {1, 3, 2}};
    const float uv[2][3][2] = {
        {{0.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 1.0f}},
        {{1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}},
    };

    for (int t = 0; t < 2; ++t) {
        for (int v = 0; v < 3; ++v) {
            for (int c = 0; c < 3; ++c)
                tri.pos[t][v][c] = verts[idx[t][v]][c];
            tri.uv[t][v][0] = uv[t][v][0];
            tri.uv[t][v][1] = uv[t][v][1];
        }
    }
    return tri;
}

bool IsHumanShadowOwner(const std::string& type) {
    // Exactly the types whose create handler reaches Human_CreateHandler
    // (human.c:357-358) through TaskType_New's handler-row copy (task.c:397-398).
    //
    // "HumanSoldierFemale" is the editor's defensive name for the female soldier
    // variant (IGI-RE declares the struct at src/soldier/soldiertypes.h:102 and the
    // binary carries its state tables, src/addr.c:456-457) -- it is a
    // HumanSoldier-family soldier, so if the editor ever holds one it is a shadow
    // owner too. No retail level declares it: the 7 shipped levels declare
    // "HumanSoldier", "HumanPlayer", "HumanPlayerInput" and "HumanAI" and nothing else.
    return type == "Human" || type == "HumanSoldier" || type == "HumanSoldierPistol" ||
           type == "HumanSoldierRPG" || type == "HumanPlayer" ||
           type == "HumanSoldierFemale";
}

HumanShadowPlacement BuildHumanShadowPlacement(double x, double y, double placement_z,
                                               bool have_ground, double ground_z) {
    HumanShadowPlacement p{};

    // The engine's ground position is the probe's hit (humanbase/human.c:1844).
    // Without a hit the editor uses the placement's own Z, which is the surface it
    // was authored on (see the header for why this is not the engine's
    // `groundValid = 0` branch).
    const double gz = have_ground ? ground_z : placement_z;
    p.ground_pos[0] = x;
    p.ground_pos[1] = y;
    p.ground_pos[2] = gz;

    // Human::groundDist is a Z distance from the foot to the ground
    // (humanbase/human.c:1801-1802); the editor's placement Z is the foot.
    p.fade = HumanShadowFade(placement_z - gz, (double)kShadowMaxHeight);
    HumanShadowColor(p.fade, p.color);

    // The ground normal. The editor stores none, so it is vertical: IGI is Z-up
    // (humanbase/human.c:1801 compares Z) and Matrix_SetDirection's Z-aligned
    // branch (matrix.c:249-257) makes this the identity basis.
    const double up[3] = {0.0, 0.0, 1.0};
    HumanShadowQuad(up, kShadowQuadSize, kShadowDepth, p.verts);

    p.valid = true;
    return p;
}

}  // namespace igi
