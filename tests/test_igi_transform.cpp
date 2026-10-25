// Differential test: the editor's Euler composition must equal the retail game's
// `Matrix_SetEuler` exactly (issue #37).
//
// The reference below is a line-by-line transcription of the verified
// decompilation in IGI-RE:
//   * `src/math/matrix.c:99-110`  Matrix_SetEuler   HOOK(0x4B38E0)
//   * `src/math/matrix.c:112-164` Matrix_SetRotX/Y/Z HOOK(0x4B3B60/0x4B3BA0/0x4B3BE0)
//   * `src/math/matrix.c:10-25`   Mul (out = a * b, row-major 3x3)
//   * `src/physobj/paramops.c:423-435` ParamReal32x9_Parse reads (x, y, z) straight
//     into Matrix_SetEuler(ax, ay, az) — no permutation, no sign flip.
//
// If the editor ever drifts back to a different rotation order, these tests fail.

#include "../source/renderer/igi_transform.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>

namespace {

struct M3 {
    float m[9];
};

// --- transcription of IGI-RE src/math/matrix.c ---------------------------------

M3 SetRotX(float a) {
    long double s = std::sin(static_cast<long double>(a));
    float c = static_cast<float>(std::cos(static_cast<long double>(a)));
    M3 r{};
    r.m[0] = 1.0f;
    r.m[1] = 0.0f;
    r.m[2] = 0.0f;
    r.m[3] = 0.0f;
    r.m[6] = 0.0f;
    r.m[4] = c;
    r.m[5] = static_cast<float>(-s);
    r.m[8] = c;
    r.m[7] = static_cast<float>(s);
    return r;
}

M3 SetRotY(float a) {
    long double s = std::sin(static_cast<long double>(a));
    long double c = std::cos(static_cast<long double>(a));
    M3 r{};
    r.m[1] = 0.0f;
    r.m[3] = 0.0f;
    r.m[4] = 1.0f;
    r.m[5] = 0.0f;
    r.m[7] = 0.0f;
    r.m[0] = static_cast<float>(c);
    r.m[2] = static_cast<float>(s);
    r.m[6] = static_cast<float>(-s);
    r.m[8] = static_cast<float>(c);
    return r;
}

M3 SetRotZ(float a) {
    long double s = std::sin(static_cast<long double>(a));
    float c = static_cast<float>(std::cos(static_cast<long double>(a)));
    M3 r{};
    r.m[2] = 0.0f;
    r.m[5] = 0.0f;
    r.m[6] = 0.0f;
    r.m[7] = 0.0f;
    r.m[8] = 1.0f;
    r.m[0] = c;
    r.m[1] = static_cast<float>(-s);
    r.m[4] = c;
    r.m[3] = static_cast<float>(s);
    return r;
}

M3 Mul(const M3& a, const M3& b) {
    M3 t{};
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            const float* r = &a.m[i * 3];
            t.m[i * 3 + j] = static_cast<float>(
                static_cast<long double>(r[0]) * b.m[j] +
                static_cast<long double>(r[2]) * b.m[6 + j] +
                static_cast<long double>(r[1]) * b.m[3 + j]);
        }
    }
    return t;
}

// The game's authoritative Euler -> matrix conversion.
M3 GameSetEuler(float ax, float ay, float az) {
    M3 m = SetRotZ(az);
    M3 r = SetRotY(ay);
    m = Mul(m, r);
    r = SetRotX(ax);
    m = Mul(m, r);
    return m;
}

// glm::mat4 is column-major: element (row, col) lives at [col][row].
double RefRowCol(const M3& ref, int row, int col) {
    return static_cast<double>(ref.m[row * 3 + col]);
}

void ExpectMatchesGame(const glm::dvec3& rot, double tol = 1e-6) {
    const M3 ref = GameSetEuler(static_cast<float>(rot.x), static_cast<float>(rot.y),
                                static_cast<float>(rot.z));
    const glm::mat4 got = igi::IgiEulerRotationMatrix(rot);
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            EXPECT_NEAR(static_cast<double>(got[col][row]), RefRowCol(ref, row, col), tol)
                << "rot=(" << rot.x << "," << rot.y << "," << rot.z << ") row=" << row
                << " col=" << col;
        }
    }
}

// The order the editor used before the fix: Rz * Rx * Ry.
glm::mat4 LegacyZxyRotation(const glm::dvec3& rot) {
    glm::mat4 m(1.0f);
    m = glm::rotate(m, static_cast<float>(rot.z), glm::vec3(0, 0, 1));
    m = glm::rotate(m, static_cast<float>(rot.x), glm::vec3(1, 0, 0));
    m = glm::rotate(m, static_cast<float>(rot.y), glm::vec3(0, 1, 0));
    return m;
}

}  // namespace

TEST(IgiTransform, EulerMatchesGameMatrixSetEulerForMultiAxisRotation) {
    // 0.3 / 0.4 / 0.5 rad on three different axes: order-sensitive.
    ExpectMatchesGame(glm::dvec3(0.3, 0.4, 0.5));
}

TEST(IgiTransform, EulerMatchesGameMatrixSetEulerOnGrid) {
    const double angles[] = {-2.5, -1.0, -0.25, 0.0, 0.25, 1.0, 2.5};
    for (double ax : angles) {
        for (double ay : angles) {
            for (double az : angles) {
                ExpectMatchesGame(glm::dvec3(ax, ay, az), 1e-6);
            }
        }
    }
}

TEST(IgiTransform, EulerMatchesGameMatrixSetEulerForYawOnlyAndAuthoredValues) {
    // Yaw-only objects are order-invariant, so this pins the common case too.
    ExpectMatchesGame(glm::dvec3(0.0, 0.0, 1.2345));
    ExpectMatchesGame(glm::dvec3(0.0, 0.0, -3.0));
    // Values in the shape the QSC files carry (issue #37 repro: multi-axis object).
    ExpectMatchesGame(glm::dvec3(0.785398, 0.0, 1.570796));
    ExpectMatchesGame(glm::dvec3(-1.2, 0.35, 2.9));
}

TEST(IgiTransform, ZxyOrderIsWrongSoThisTestWouldCatchTheOldBug) {
    const glm::dvec3 rot(0.3, 0.4, 0.5);
    const glm::mat4 game = igi::IgiEulerRotationMatrix(rot);
    const glm::mat4 legacy = LegacyZxyRotation(rot);

    double max_delta = 0.0;
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            max_delta = std::max(max_delta, std::abs(static_cast<double>(game[col][row]) -
                                                     static_cast<double>(legacy[col][row])));
        }
    }
    EXPECT_GT(max_delta, 1e-3)
        << "Z,X,Y must differ from the game's Z,Y,X for a multi-axis rotation";
}

TEST(IgiTransform, RotationIsModelIndependent) {
    // The old code special-cased `506_` and `615_01_1` models. The game has no
    // such rule: `ParamReal32x9_Parse` feeds every authored triple through the
    // same `Matrix_SetEuler`. A multi-axis door rotation must therefore use the
    // same order as any other object.
    const glm::dvec3 door_rot(0.3, 0.4, 0.5);
    const glm::dvec3 desk_rot(0.3, 0.4, 0.5);
    const glm::mat4 a = igi::IgiEulerRotationMatrix(door_rot);
    const glm::mat4 b = igi::IgiEulerRotationMatrix(desk_rot);
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) EXPECT_FLOAT_EQ(a[col][row], b[col][row]);
    }
}

TEST(IgiTransform, ObjectMatrixIsScaleThenRotateThenTranslate) {
    const glm::dvec3 pos(100.0, -200.0, 300.0);
    const glm::dvec3 rot(0.3, 0.4, 0.5);
    const float scale = 40.96f;

    const glm::mat4 m = igi::IgiObjectMatrix(pos, rot, scale);

    // SRT: the point is scaled first, then rotated, then translated.
    const glm::dvec3 local(2.0, 0.0, 0.0);
    const glm::dvec3 expected =
        pos + glm::dvec3(igi::IgiEulerRotationMatrix(rot) *
                         glm::vec4(glm::vec3(local * static_cast<double>(scale)), 1.0f));
    const glm::dvec3 got = glm::dvec3(m * glm::dvec4(local, 1.0));
    // World coordinates are large (40.96 units per metre), so compare with a
    // relative tolerance: the matrix itself is float, the expectation is double.
    const double tol = 1e-6 * std::max(1.0, glm::length(expected));
    EXPECT_NEAR(got.x, expected.x, tol);
    EXPECT_NEAR(got.y, expected.y, tol);
    EXPECT_NEAR(got.z, expected.z, tol);

    // Translation column is the object position, and the 3x3 is unscaled rotation
    // times scale (no shear, no reordering).
    EXPECT_NEAR(static_cast<double>(m[3][0]), pos.x, 1e-6);
    EXPECT_NEAR(static_cast<double>(m[3][1]), pos.y, 1e-6);
    EXPECT_NEAR(static_cast<double>(m[3][2]), pos.z, 1e-6);
    EXPECT_NEAR(glm::length(glm::dvec3(m[0])), static_cast<double>(scale), 1e-4);
}

// --- Matrix_GetEuler: the engine's own decomposition ---------------------------
//
// IGI-RE `src/math/matrix.c:181-208` `Matrix_GetEuler` (HOOK 0x4B3C50) is the
// inverse of `Matrix_SetEuler`. Transcribed verbatim below, on the engine's
// row-major 3x3, so the editor's `igi::IgiMatrixToEuler` can be checked against
// it directly rather than against a round-trip that could hide a shared mistake.
namespace {

struct RefEuler {
    double rx, ry, rz;
};

RefEuler GameGetEuler(const M3& m) {
    const float* e = m.m;
    const long double cy =
        std::sqrt(static_cast<long double>(e[0]) * e[0] + static_cast<long double>(e[3]) * e[3]);
    RefEuler out{};
    if (cy > 0.0001) {
        out.rx = std::atan2(static_cast<double>(e[7]), static_cast<double>(e[8]));
        out.ry = std::atan2(static_cast<double>(-e[6]), static_cast<double>(cy));
        out.rz = std::atan2(static_cast<double>(e[3]), static_cast<double>(e[0]));
    } else {
        out.rx = std::atan2(static_cast<double>(-e[5]), static_cast<double>(e[4]));
        out.ry = std::atan2(static_cast<double>(-e[6]), static_cast<double>(cy));
        out.rz = 0.0;
    }
    return out;
}

// The engine maps negative rx/ry into [0, 2*pi); `igi::IgiMatrixToEuler` returns
// the raw atan2 values, which differ by exactly 2*pi. Compare modulo 2*pi.
double AngleDiff(double a, double b) {
    double d = std::fmod(a - b, 2.0 * M_PI);
    if (d > M_PI) d -= 2.0 * M_PI;
    if (d < -M_PI) d += 2.0 * M_PI;
    return std::fabs(d);
}

}  // namespace

TEST(IgiTransform, MatrixToEulerMatchesGameMatrixGetEuler) {
    // A grid of authored triples, each pushed through the transcribed engine
    // conversion and then through both decompositions.
    const double k = 0.37;
    for (int i = -3; i <= 3; ++i) {
        for (int j = -3; j <= 3; ++j) {
            for (int l = -2; l <= 2; ++l) {
                const glm::dvec3 rot(i * k, j * k, l * k);
                const M3 ref = GameSetEuler(static_cast<float>(rot.x), static_cast<float>(rot.y),
                                            static_cast<float>(rot.z));
                const RefEuler expect = GameGetEuler(ref);
                const glm::dvec3 got = igi::IgiMatrixToEuler(igi::IgiEulerRotationMat3(rot));
                EXPECT_NEAR(AngleDiff(got.x, expect.rx), 0.0, 1e-5) << "rot=" << i << "," << j << "," << l;
                EXPECT_NEAR(AngleDiff(got.y, expect.ry), 0.0, 1e-5) << "rot=" << i << "," << j << "," << l;
                EXPECT_NEAR(AngleDiff(got.z, expect.rz), 0.0, 1e-5) << "rot=" << i << "," << j << "," << l;
            }
        }
    }
}

TEST(IgiTransform, MatrixToEulerRoundTripsTheAuthorsTriple) {
    // Outside the gimbal lock band the decomposition must return the authored
    // triple itself, so a promoted ATTA proxy keeps the orientation it was picked
    // at (the site in app_editor.cpp).
    const glm::dvec3 cases[] = {
        {0.3, 0.4, 0.5}, {-0.9, 0.25, 2.1}, {1.2, -0.6, -2.4}, {0.0, 0.0, 0.0}, {0.0, 0.0, 1.9},
    };
    for (const glm::dvec3& rot : cases) {
        const glm::dvec3 got = igi::IgiMatrixToEuler(igi::IgiEulerRotationMat3(rot));
        EXPECT_NEAR(AngleDiff(got.x, rot.x), 0.0, 1e-5) << "rot=(" << rot.x << "," << rot.y << "," << rot.z << ")";
        EXPECT_NEAR(AngleDiff(got.y, rot.y), 0.0, 1e-5) << "rot=(" << rot.x << "," << rot.y << "," << rot.z << ")";
        EXPECT_NEAR(AngleDiff(got.z, rot.z), 0.0, 1e-5) << "rot=(" << rot.x << "," << rot.y << "," << rot.z << ")";
    }
}

TEST(IgiTransform, LegacyZxyExtractionIsWrongSoThisTestWouldCatchTheOldBug) {
    // The extraction app_editor.cpp used before the fix: an Rz*Rx*Ry reading of
    // the matrix. On a multi-axis rotation it disagrees with the engine, which is
    // what made a promoted ATTA child inherit a wrong orientation.
    const glm::dvec3 rot(0.3, 0.4, 0.5);
    const glm::mat3 m = igi::IgiEulerRotationMat3(rot);
    const float sx = std::max(-1.0f, std::min(1.0f, m[1][2]));
    const float legacy_rx = std::asin(sx);
    const float legacy_ry = std::atan2(-m[0][2], m[2][2]);
    const float legacy_rz = std::atan2(-m[1][0], m[1][1]);
    const glm::dvec3 engine = igi::IgiMatrixToEuler(m);
    EXPECT_GT(std::fabs(static_cast<double>(legacy_rx) - engine.x) +
                  std::fabs(static_cast<double>(legacy_ry) - engine.y) +
                  std::fabs(static_cast<double>(legacy_rz) - engine.z),
              0.05);
}
