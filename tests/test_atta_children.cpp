/******************************************************************************
 * @file    test_atta_children.cpp
 * @brief   Host-native tests for the ATTA attachment rules in
 *          `source/renderer/atta_children.h`.
 *
 * The oracle is IGI-RE (the verified decompilation of the retail IGI.exe); each
 * test names the function it transcribes. The fixture records in
 * `RealHeliAttachmentRecord` are the exact bytes of the shipped
 * `709_01_1.mef` (level 1, the Black-Hawk-class hull) ATTA entries 0 and 1,
 * read at the offsets the record layout documents.
 *****************************************************************************/
#include <gtest/gtest.h>

#include "renderer/atta_children.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

// ─── Literal transcription of the IGI-RE oracle ─────────────────────────────
// `src/magicobj/rigidcoll.c:399-450` (RigidDynCubeObj_CreateMagicObjs) and the
// identical product in `src/shadow/sector.c:545-630`. `M` is the parent's
// row-major 3x3, `o` its position, `attPos`/`a` the raw ATTA record fields.
void IgiReChildFrame(const double M[9], const double o[3],
                     const float attPos[3], const float a[9],
                     double outPos[3], double outP[9]) {
    // arg.pos = M * att->pos + o->pos
    for (int i = 0; i < 3; ++i) {
        outPos[i] = M[i * 3 + 0] * attPos[0] + M[i * 3 + 1] * attPos[1] +
                    M[i * 3 + 2] * attPos[2] + o[i];
    }

    // R: rows are the orthonormalized columns of `a` (W, E, F).
    const double c0 = (double)a[7] * a[3] - (double)a[4] * a[6];
    const double c1 = -((double)a[7] * a[0] - (double)a[1] * a[6]);
    const double c2 = (double)a[4] * a[0] - (double)a[1] * a[3];

    double W[3] = {a[0], a[3], a[6]};
    const double wlen = std::sqrt(W[0] * W[0] + W[1] * W[1] + W[2] * W[2]);
    if (wlen != 0.0) { W[0] /= wlen; W[1] /= wlen; W[2] /= wlen; }

    double e[3] = {c1 * a[6] - c2 * a[3],
                   -(c0 * a[6] - c2 * a[0]),
                   c0 * a[3] - c1 * a[0]};
    const double elen = std::sqrt(e[0] * e[0] + e[1] * e[1] + e[2] * e[2]);
    if (elen != 0.0) { e[0] /= elen; e[1] /= elen; e[2] /= elen; }

    double F[3] = {e[2] * W[1] - e[1] * W[2],
                   -(e[2] * W[0] - e[0] * W[2]),
                   e[1] * W[0] - e[0] * W[1]};
    const double flen = std::sqrt(F[0] * F[0] + F[1] * F[1] + F[2] * F[2]);
    if (flen != 0.0) { F[0] /= flen; F[1] /= flen; F[2] /= flen; }

    const double R[9] = {W[0], W[1], W[2], e[0], e[1], e[2], F[0], F[1], F[2]};

    // P = M * R (row-major)
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            outP[i * 3 + j] = M[i * 3 + 0] * R[0 * 3 + j] +
                              M[i * 3 + 1] * R[1 * 3 + j] +
                              M[i * 3 + 2] * R[2 * 3 + j];
        }
    }
}

// GLM stores columns; a row-major `M` with position `o` becomes this mat4.
glm::mat4 ParentFromRowMajor(const double M[9], const double o[3]) {
    glm::mat4 m(1.0f);
    for (int c = 0; c < 3; ++c) {
        m[c] = glm::vec4(static_cast<float>(M[c]), static_cast<float>(M[3 + c]),
                         static_cast<float>(M[6 + c]), 0.0f);
    }
    m[3] = glm::vec4(static_cast<float>(o[0]), static_cast<float>(o[1]),
                     static_cast<float>(o[2]), 1.0f);
    return m;
}

double Element(const glm::mat4& m, int row, int col) {
    return static_cast<double>(m[col][row]);
}

void ExpectMatchesIgiRe(const glm::mat4& parent, const double M[9], const double o[3],
                        const float pos[3], const float a[9]) {
    const glm::mat4 child = igi::IgiAttaChildWorld(parent, pos, a);
    double wantPos[3], wantP[9];
    IgiReChildFrame(M, o, pos, a, wantPos, wantP);

    for (int i = 0; i < 3; ++i) {
        // The oracle is double and the helper float; positions reach ~3.4e4 raw
        // units, so compare with a relative tolerance.
        EXPECT_NEAR(Element(child, i, 3), wantPos[i], 1e-6 * (1.0 + std::abs(wantPos[i])))
            << "axis " << i;
    }
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            EXPECT_NEAR(Element(child, i, j), wantP[i * 3 + j], 1e-5)
                << "row " << i << " col " << j;
        }
    }
}

// A 30 deg rotation about Z, row-major.
const double kParentM[9] = {
    0.8660254037844387, -0.5,               0.0,
    0.5,                0.8660254037844387, 0.0,
    0.0,                0.0,                1.0,
};
const double kParentO[3] = {1000.0, 2000.0, 300.0};

} // namespace

// ─── Record layout ──────────────────────────────────────────────────────────
// IGI-RE `src/model/modeltypes.h:26` ("ATTA, 0x44 bytes each"), the 0x44 stride
// in `src/lod/mesh3d.c:493-497`, and the field order of `MagicAttach`
// (`src/magicobj/rigidcoll.c:359-366`).
TEST(AttaChildren, RecordLayoutIs68BytesWithIgiReFieldOffsets) {
    EXPECT_EQ(igi::kAttaRecordSize, 0x44u);
    EXPECT_EQ(igi::kAttaNameOffset, 0x00u);
    EXPECT_EQ(igi::kAttaPosOffset, 0x10u);
    EXPECT_EQ(igi::kAttaMatOffset, 0x1Cu);
    EXPECT_EQ(igi::kAttaTailOffset, 0x40u);

    // Decode a synthetic record at the documented offsets.
    unsigned char rec[igi::kAttaRecordSize];
    std::memset(rec, 0, sizeof(rec));
    std::memcpy(rec + igi::kAttaNameOffset, "711_01_1", 8);
    const float pos[3] = {1.0f, 2.0f, 3.0f};
    const float m[9] = {1, 2, 3, 4, 5, 6, 7, 8, 9};
    std::memcpy(rec + igi::kAttaPosOffset, pos, sizeof(pos));
    std::memcpy(rec + igi::kAttaMatOffset, m, sizeof(m));
    const int32_t tail = 7;
    std::memcpy(rec + igi::kAttaTailOffset, &tail, sizeof(tail));

    EXPECT_STREQ(reinterpret_cast<const char*>(rec + igi::kAttaNameOffset), "711_01_1");
    float got[3];
    std::memcpy(got, rec + igi::kAttaPosOffset, sizeof(got));
    EXPECT_EQ(got[0], 1.0f);
    EXPECT_EQ(got[1], 2.0f);
    EXPECT_EQ(got[2], 3.0f);
    int32_t gotTail = 0;
    std::memcpy(&gotTail, rec + igi::kAttaTailOffset, sizeof(gotTail));
    EXPECT_EQ(gotTail, 7);
}

// ─── Child local rotation ───────────────────────────────────────────────────
// IGI-RE builds R with the orthonormalized COLUMNS of `m` as its ROWS
// (`src/magicobj/rigidcoll.c:419-436`), which for an orthonormal basis is
// `transpose(m)`.
TEST(AttaChildren, LocalRotationIsTheTransposeOfTheStoredBasis) {
    const float m[9] = {0.8660254037844387f, -0.5f, 0.0f,
                        0.5f, 0.8660254037844387f, 0.0f,
                        0.0f, 0.0f, 1.0f};
    // Independent build of the stored basis as a mathematical matrix A, then
    // transpose it. The rule under test is L == transpose(A).
    glm::mat3 A(1.0f);
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) A[c][r] = m[r * 3 + c];
    }
    const glm::mat3 want = glm::transpose(A);
    const glm::mat3 rot = igi::IgiAttaLocalRotation(m);
    for (int c = 0; c < 3; ++c) {
        for (int r = 0; r < 3; ++r) {
            EXPECT_NEAR(rot[c][r], want[c][r], 1e-6) << "row " << r << " col " << c;
        }
    }
    // The fixture really is a rotation, so the transpose is its inverse.
    for (int c = 0; c < 3; ++c) {
        for (int r = 0; r < 3; ++r) {
            EXPECT_NEAR((rot * A)[c][r], (c == r) ? 1.0f : 0.0f, 1e-6);
        }
    }
}

// ─── Parent/child composition ───────────────────────────────────────────────
TEST(AttaChildren, ChildWorldMatchesIgiReFormulaForSyntheticRecords) {
    const float identity[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    const float posA[3] = {0.0f, 0.0f, 8894.7919921875f};
    ExpectMatchesIgiRe(ParentFromRowMajor(kParentM, kParentO), kParentM, kParentO, posA, identity);

    // A tilted basis (45 deg about X) exercises the non-identity path.
    const float tilted[9] = {1.0f, 0.0f, 0.0f,
                             0.0f, 0.70710678f, -0.70710678f,
                             0.0f, 0.70710678f, 0.70710678f};
    const float posB[3] = {-503.39837646484375f, -34107.7578125f, 7310.171875f};
    ExpectMatchesIgiRe(ParentFromRowMajor(kParentM, kParentO), kParentM, kParentO, posB, tilted);

    // Identity parent: the child sits at the raw ATTA offset.
    const double identityM[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    ExpectMatchesIgiRe(ParentFromRowMajor(identityM, kParentO), identityM, kParentO, posB, tilted);
}

// ─── Real retail fixture ────────────────────────────────────────────────────
// ATTA records 0 and 1 of the shipped level-1 `709_01_1.mef` (the hull the
// MAGICOBJ.QVM rotor definitions 711_01_1 / 712_01_1 attach to). Values are the
// exact 32-bit floats read at the documented offsets.
namespace {

struct RealAttaRecord {
    const char* name;
    float pos[3];
    float m[9];
};

const RealAttaRecord kMainRotor = {
    "711_01_1",
    {3.464876101588743e-07f, -0.10687528550624847f, 8894.7919921875f},
    {-2.64476994971119e-07f, 9.636929831913221e-08f, -1.0f,
     -1.0f, 1.7870600288460992e-07f, 2.54985991432477e-07f,
     1.4509700463349873e-07f, 1.0f, 2.1308099462657992e-07f},
};

const RealAttaRecord kTailRotor = {
    "712_01_1",
    {-503.39837646484375f, -34107.7578125f, 7310.171875f},
    {1.0f, 0.0f, -8.742279788975793e-08f,
     -7.105429898699844e-15f, 1.0f, -8.742279788975793e-08f,
     8.742279788975793e-08f, 8.742279788975793e-08f, 1.0f},
};

// The editor's helper transposes the stored basis instead of Gram-Schmidt
// orthonormalizing it; that is only valid when the retail basis is already
// orthonormal, so assert that property of the shipped data.
void ExpectOrthonormalBasis(const float m[9]) {
    for (int c = 0; c < 3; ++c) {
        const double x = m[0 * 3 + c], y = m[1 * 3 + c], z = m[2 * 3 + c];
        EXPECT_NEAR(std::sqrt(x * x + y * y + z * z), 1.0, 1e-6) << "column " << c;
    }
    for (int a = 0; a < 3; ++a) {
        for (int b = a + 1; b < 3; ++b) {
            const double dot = (double)m[0 * 3 + a] * m[0 * 3 + b] +
                               (double)m[1 * 3 + a] * m[1 * 3 + b] +
                               (double)m[2 * 3 + a] * m[2 * 3 + b];
            EXPECT_NEAR(dot, 0.0, 1e-6) << "columns " << a << "," << b;
        }
    }
}

} // namespace

TEST(AttaChildren, RetailHeliRotorRecordsAreOrthonormalAndMatchIgiRe) {
    ExpectOrthonormalBasis(kMainRotor.m);
    ExpectOrthonormalBasis(kTailRotor.m);

    const double identityM[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    const glm::mat4 parent = ParentFromRowMajor(identityM, kParentO);

    ExpectMatchesIgiRe(parent, identityM, kParentO, kMainRotor.pos, kMainRotor.m);
    ExpectMatchesIgiRe(parent, identityM, kParentO, kTailRotor.pos, kTailRotor.m);
}

// The main rotor's own local X axis is the vertical shaft: `IgiAttaLocalRotation`
// column 0 for the shipped record is ~(0, 0, -1), which is why the game's
// `base * RotX(a)` spin (`src/rotor/rotor.c:145-150`) turns the disc.
TEST(AttaChildren, RetailMainRotorLocalXAxisIsTheVerticalShaft) {
    const glm::mat3 rot = igi::IgiAttaLocalRotation(kMainRotor.m);
    EXPECT_NEAR(rot[0][0], 0.0, 1e-5);
    EXPECT_NEAR(rot[0][1], 0.0, 1e-5);
    EXPECT_NEAR(rot[0][2], -1.0, 1e-5);

    // The tail rotor's local X is lateral (the heli frame's +X).
    const glm::mat3 tail = igi::IgiAttaLocalRotation(kTailRotor.m);
    EXPECT_NEAR(tail[0][0], 1.0, 1e-5);
    EXPECT_NEAR(tail[0][1], 0.0, 1e-5);
    EXPECT_NEAR(tail[0][2], 0.0, 1e-5);
}

// ─── Rotor spin ─────────────────────────────────────────────────────────────
// IGI-RE `src/rotor/rotor.c:139-152`: `copy.matrix = base * RotX(a)` — a
// post-multiplied rotation about the part's own local X, leaving its local X
// axis and its position untouched.
TEST(AttaChildren, RotorSpinPostMultipliesLocalXAndKeepsShaftAndPosition) {
    glm::mat4 base = ParentFromRowMajor(kParentM, kParentO);
    const glm::mat4 spun = igi::IgiRotorSpinMatrix(base, 1.5707963267948966f);

    // Position and local X column are unchanged.
    for (int i = 0; i < 4; ++i) {
        EXPECT_NEAR(spun[3][i], base[3][i], 1e-6) << "translation component " << i;
    }
    for (int i = 0; i < 3; ++i) {
        EXPECT_NEAR(spun[0][i], base[0][i], 1e-6) << "local X component " << i;
    }

    // RotX(+90 deg) maps local Y -> local Z and local Z -> -local Y.
    for (int i = 0; i < 3; ++i) {
        EXPECT_NEAR(spun[1][i], base[2][i], 1e-6) << "local Y component " << i;
        EXPECT_NEAR(spun[2][i], -base[1][i], 1e-6) << "local Z component " << i;
    }
}

// ─── The world frame is NOT the transpose: a negative control ───────────────
// `IgiReChildFrame` states the oracle's world frame as the matrix product
// `P = M * R`. That is the correct reading, but expressing it as a product makes
// it re-derivable — and the obvious-looking re-derivation is WRONG. The nine
// assignments in `src/magicobj/rigidcoll.c:440-448` index BOTH operands flat
// (`P[0] = R[3]*M[1] + R[6]*M[2] + R[0]*M[0]`), which reads like a transpose, and
// the sibling product in `src/shadow/sector.c:610-630` is easy to conflate with
// it. "Correcting" the product into `R^T * M^T` (equivalently `(M*R)^T`) yields
// the INVERSE rotation: every ATTA child — rotors, wheels, doors, hatches — would
// be drawn wrongly rotated while its position stayed correct, which is exactly
// the shape of the "attached part is upside down" class of report.
//
// So this test pins the transpose as a NEGATIVE control. The basis used is the
// shipped `709_01_1.mef` main rotor, which is not symmetric, so the control has
// teeth: if someone transposes `IgiAttaChildWorld`'s product, this fails loudly
// instead of quietly changing every attachment's orientation.
TEST(AttaChildren, ChildWorldFrameIsNotTheTransposeOfTheOracleProduct) {
    double wantPos[3], wantP[9];
    IgiReChildFrame(kParentM, kParentO, kMainRotor.pos, kMainRotor.m, wantPos, wantP);

    const glm::mat4 child = igi::IgiAttaChildWorld(
        ParentFromRowMajor(kParentM, kParentO), kMainRotor.pos, kMainRotor.m);

    // The helper must still match the oracle (no regression from this test).
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            EXPECT_NEAR(Element(child, i, j), wantP[i * 3 + j], 1e-5) << "row " << i << " col " << j;
        }
    }

    // The control only means something if the two differ for this record.
    double maxTransposeDelta = 0.0;
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            maxTransposeDelta = std::max(maxTransposeDelta,
                                         std::abs(wantP[i * 3 + j] - wantP[j * 3 + i]));
        }
    }
    EXPECT_GT(maxTransposeDelta, 0.1)
        << "the shipped main-rotor basis is (near) symmetric, so this control cannot "
           "distinguish the frame from its transpose";

    // Compare as WHOLE matrices, not element by element: a diagonal entry of a
    // rotation is shared with its transpose, so per-element "must differ"
    // assertions are false for (0,0), (1,1), (2,2) on a near-diagonal basis.
    double maxDistanceFromOracle = 0.0;
    double maxDistanceFromTranspose = 0.0;
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            maxDistanceFromOracle = std::max(maxDistanceFromOracle,
                                             std::abs(Element(child, i, j) - wantP[i * 3 + j]));
            maxDistanceFromTranspose = std::max(maxDistanceFromTranspose,
                                                std::abs(Element(child, i, j) - wantP[j * 3 + i]));
        }
    }
    EXPECT_LT(maxDistanceFromOracle, 1e-5)
        << "the helper must reproduce the oracle's P = M * R (rigidcoll.c:440-448)";
    EXPECT_GT(maxDistanceFromTranspose, 0.1)
        << "the helper matches the TRANSPOSE of the oracle's world frame; "
           "rigidcoll.c:440-448 is P = M * R, not (M*R)^T";
}

// A rotor's children inherit the UNSPUN frame: `src/rotor/rotor.c:180-181`
// passes `o->obj.matrix` to the child draw, not the spun drawing copy.
TEST(AttaChildren, RotorSpinDoesNotLeakIntoTheChildFrame) {
    const float identity[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    const float bladePos[3] = {0.0f, 0.0f, 0.0f};
    const glm::mat4 hub = igi::IgiAttaChildWorld(
        ParentFromRowMajor(kParentM, kParentO), kMainRotor.pos, kMainRotor.m);
    const glm::mat4 blade = igi::IgiAttaChildWorld(hub, bladePos, identity);

    // The blade is placed in the hub's frame, so it sits exactly at the hub.
    for (int i = 0; i < 3; ++i) {
        EXPECT_NEAR(blade[3][i], hub[3][i], 1e-3) << "component " << i;
    }
    // Its basis is the hub's unspun basis (the identity ATTA record adds nothing).
    for (int c = 0; c < 3; ++c) {
        for (int r = 0; r < 3; ++r) {
            EXPECT_NEAR(blade[c][r], hub[c][r], 1e-5) << "col " << c << " row " << r;
        }
    }
}

// The vehicle-preview root and the ATTA composition must use ONE convention.
//
// `renderer_objects.cpp` feeds the helicopter flight preview's engine matrix
// through `igi::IgiEngineMatrixToGlm4` and then hands that mat4 to
// `IgiAttaChildWorld` as the parent frame (the rotors hang off the preview pose,
// because `Heli_Move` broadcasts the move event with `&h->v.pos`/`&h->v.matrix`
// every tick — src/heli/heli.c:502-507, and `Rotor_Move` re-places the rotor
// from the owner's matrix, src/rotor/rotor.c:265-290).  If that conversion were
// the transpose of the engine's matrix, the hull would be drawn with the inverse
// of the rotation its own children are composed against.
//
// The oracle is the engine's own product: the image of the local X axis under
// `Matrix.m` is its first COLUMN, (m[0], m[3], m[6]) — `Mesh3D_GetWorldVertex`
// HOOK 0x4D0A50 (src/lod/mesh3d.c:434-436), `part_place`
// (src/vehicle/parts.c:81-90), `Heli_Control` (src/heli/heli.c:390-392) and
// `Direct3D_MakeMatrix` HOOK 0x4950F0 (src/d3d/direct3d.c:1211-1237, whose D3D
// row-vector matrix takes that column as its first ROW).
TEST(AttaChildren, ThePreviewRootConversionAgreesWithTheChildComposition) {
    float M[9];
    for (int i = 0; i < 9; ++i) M[i] = static_cast<float>(kParentM[i]);

    // The same frame built two ways: the literal row-major conversion the oracle
    // uses, and the engine-matrix conversion the preview path uses.
    const glm::mat4 literal = ParentFromRowMajor(kParentM, kParentO);
    const glm::mat4 converted = igi::IgiEngineMatrixToGlm4(M);
    glm::mat4 with_pos = converted;
    with_pos[3] = glm::vec4(static_cast<float>(kParentO[0]), static_cast<float>(kParentO[1]),
                            static_cast<float>(kParentO[2]), 1.0f);
    for (int c = 0; c < 3; ++c) {
        for (int r = 0; r < 3; ++r) {
            EXPECT_NEAR(Element(with_pos, r, c), Element(literal, r, c), 1e-6)
                << "the conversion must reproduce the engine's product, not its transpose";
        }
    }

    // The engine's image of the local X axis, straight from the stored array
    // (w = 0: a direction, so the translation is not added).
    const glm::vec4 x_axis = with_pos * glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
    EXPECT_NEAR(x_axis.x, kParentM[0], 1e-6);
    EXPECT_NEAR(x_axis.y, kParentM[3], 1e-6);
    EXPECT_NEAR(x_axis.z, kParentM[6], 1e-6);

    // And the child frame composed on it is the oracle's child frame.
    ExpectMatchesIgiRe(with_pos, kParentM, kParentO, kMainRotor.pos, kMainRotor.m);
}
