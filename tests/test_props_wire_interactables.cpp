/******************************************************************************
 * @file    test_props_wire_interactables.cpp
 * @brief   Host-native tests for the Wire / zipline / StationaryGun /
 *          AIStationaryGunHolder parity maths of GitHub issue #42 and the
 *          "Wire Width" entry in docs/KNOWN_ISSUES.md.
 *
 * Every expectation below is derived from the IGI-RE oracle, cited by file:line.
 * The helpers under test are pure (source/level/task_schema.cpp), so this file
 * needs no GL, no windows.h and no level archive.
 *
 * Oracle: /Users/haseeb-mir/Documents/Code/IGI-RE
 *   src/props/wire.c             Wire_Open 0x43EFF0, Wire_ParamsChanged 0x43F3D0,
 *                                Wire_IsSloped 0x43F8A0, kWireScale/kWireSlope
 *   src/game/misc.c              Matrix_SetYAxis 0x414A60, kNearlyUp
 *   src/math/matrix.c            Matrix_GetEuler 0x4B3C50
 *   src/humanstate/humanstate.c  HumanPlayer_StartAction case 9, ZiplineUpdate
 *   src/weapon/humanplayer.c     HumanPlayer_UpdateZipline 0x411F20
 *   src/statgun/statgun.c        StationaryGun_Open 0x473ED0, Clamp 0x4748B0
 *   src/comprun/gunholder.c      GunHolder_Init 0x46E370
 *****************************************************************************/
#include <gtest/gtest.h>

#include "level/task_schema.h"

#include <cmath>
#include <string>
#include <vector>

using namespace TaskSchemaNS;
using namespace TaskSchemaNS::Props;

namespace {

constexpr double kPi      = 3.14159265358979323846;
constexpr double kDegToRad = kPi / 180.0;

// A wire direction with the given elevation above the XY plane, at length 4096
// (exactly 1 metre at 4096 world units/m — IGI-RE src/props/wire.c:320).
Vec3 AnchorsForElevation(double degrees, double sign_z = 1.0) {
    const double r = 4096.0;
    return Vec3(r * std::cos(degrees * kDegToRad), 0.0,
                sign_z * r * std::sin(degrees * kDegToRad));
}

std::vector<std::string> WireArgs(const char* a, const char* b, const char* c,
                                  const char* d, const char* e, const char* f) {
    return {"12", "\"Wire\"", "\"wire1\"", a, b, c, d, e, f, "\"320_01_1\""};
}

}  // namespace

// ── Constants ────────────────────────────────────────────────────────────────

// IGI-RE src/props/wire.c:320-321:
//   DATA(5334B0) double kWireScale INIT(0.000244140625);
//   ALIAS(kUnitsToMeters, kWireScale, 0);
TEST(PropsParityConstants, WireScaleIsOneUnitPerMetre) {
    EXPECT_DOUBLE_EQ(kWireScale, 1.0 / 4096.0);
    EXPECT_DOUBLE_EQ(kWireScale, 0.000244140625);
}

// IGI-RE src/props/wire.c:323: DATA(533834) float kWireSlope INIT(0.034906585f)
// — 2 degrees, the Wire_IsSloped threshold (wire.c:304-317).
TEST(PropsParityConstants, WireSlopeIsTwoDegrees) {
    EXPECT_NEAR(kWireSlope, 2.0 * kDegToRad, 1e-7);
}

// IGI-RE src/humanstate/humanstate.c:1394 — the zipline integrator's constant
// and friction: `speed = -ziplineDir[2] * 0x1.613d32p+6f + speed * 0.99f`.
TEST(PropsParityConstants, ZiplineIntegratorConstants) {
    EXPECT_DOUBLE_EQ(kZiplineAccel, 88.30976104736328);
    EXPECT_DOUBLE_EQ(kZiplineAccel, 0x1.613d32p+6);
    EXPECT_DOUBLE_EQ(kZiplineFriction, 0.99);
}

// IGI-RE src/weapon/humanplayer.c:136-138: kWireEnd 3072.0 (0.75 m),
// kZiplineDown 3686.4 (0.9 m).
TEST(PropsParityConstants, ZiplineRiderOffsets) {
    EXPECT_DOUBLE_EQ(kWireEnd, 3072.0);
    EXPECT_DOUBLE_EQ(kZiplineDown, 3686.4);
    EXPECT_DOUBLE_EQ(kWireEnd * kWireScale, 0.75);
    EXPECT_NEAR(kZiplineDown * kWireScale, 0.9, 1e-12);
}

// IGI-RE src/statgun/statgun.c:113-118 (StationaryGun_CreateHandler defaults) and
// src/statgun/statgun.c:140-141 (`gStatGunAmmoRange[0] = 0; [1] = 100000;`).
TEST(PropsParityConstants, StationaryGunDefaults) {
    EXPECT_STREQ(kStatGunDefaultWeapon, "WEAPON_ID_M2HB");
    EXPECT_DOUBLE_EQ(kStatGunDefaultMaxPitch, 25.0);
    EXPECT_DOUBLE_EQ(kStatGunDefaultMinPitch, -25.0);
    EXPECT_DOUBLE_EQ(kStatGunDefaultYawRange, 90.0);
    EXPECT_EQ(kStatGunDefaultAmmo, -1);
    EXPECT_EQ(kStatGunAmmoMin, 0);
    EXPECT_EQ(kStatGunAmmoMax, 100000);
    // statgun.c:521: DATA(53387C) float kTurnStep INIT(0.08726647f) = 5 deg/tick.
    EXPECT_NEAR(kStatGunTurnStep, 5.0 * kDegToRad, 1e-6);
}

// IGI-RE src/comprun/gunholder.c:121-126 defaults and gunholder.c:155-156
// (`gGunHolderRange[0] = 0.0f; [1] = 100.0f;`).
TEST(PropsParityConstants, GunHolderDefaults) {
    EXPECT_DOUBLE_EQ(kGunHolderDefaultAngleX, 360.0);
    EXPECT_DOUBLE_EQ(kGunHolderDefaultAngleY, 360.0);
    EXPECT_DOUBLE_EQ(kGunHolderDefaultRange, 10.0);
    EXPECT_EQ(kGunHolderDefaultTeam, 1);
    EXPECT_DOUBLE_EQ(kGunHolderRangeMin, 0.0);
    EXPECT_DOUBLE_EQ(kGunHolderRangeMax, 100.0);
}

// ── Wire span geometry (Wire_ParamsChanged 0x43F3D0) ─────────────────────────

// IGI-RE src/props/wire.c:179 — `w->obj.scale[1] = len * kWireScale;` — so a wire
// spanning exactly one metre gets scale[1] == 1.0, which is what the editor's
// 40.96 * scale convention maps onto the game's RigidDynCubeObj.scale[].
TEST(WireSpan, YScaleIsLengthOverMetres) {
    const WireSpan s = ComputeWireSpan(Vec3(0, 0, 0), Vec3(4096, 0, 0));
    ASSERT_TRUE(s.valid);
    EXPECT_DOUBLE_EQ(s.length, 4096.0);
    EXPECT_DOUBLE_EQ(s.yScale, 1.0);
    EXPECT_DOUBLE_EQ(s.mid.x, 2048.0);
    EXPECT_DOUBLE_EQ(s.mid.y, 0.0);
    EXPECT_DOUBLE_EQ(s.mid.z, 0.0);
    // wire.c:177 — the +Y axis is the normalised end - start vector.
    EXPECT_DOUBLE_EQ(s.dir.x, 1.0);
    EXPECT_DOUBLE_EQ(s.dir.y, 0.0);
    EXPECT_DOUBLE_EQ(s.dir.z, 0.0);
}

// IGI-RE src/props/wire.c:181-186 — the object is placed at the anchor midpoint.
TEST(WireSpan, MidpointAndLengthForDiagonal) {
    const WireSpan s = ComputeWireSpan(Vec3(100, 200, 300), Vec3(1100, 1200, 1300));
    ASSERT_TRUE(s.valid);
    EXPECT_DOUBLE_EQ(s.mid.x, 600.0);
    EXPECT_DOUBLE_EQ(s.mid.y, 700.0);
    EXPECT_DOUBLE_EQ(s.mid.z, 800.0);
    EXPECT_NEAR(s.length, std::sqrt(3.0) * 1000.0, 1e-9);
    EXPECT_NEAR(s.yScale, s.length / 4096.0, 1e-15);
}

// A wire whose anchors coincide is degenerate: the game would divide by zero in
// the normalisation at wire.c:177, so the helper reports invalid and the editor
// falls back to plain object placement.
TEST(WireSpan, DegenerateAnchorsAreInvalid) {
    const WireSpan s = ComputeWireSpan(Vec3(5, 5, 5), Vec3(5, 5, 5));
    EXPECT_FALSE(s.valid);
    EXPECT_DOUBLE_EQ(s.length, 0.0);
    EXPECT_DOUBLE_EQ(s.yScale, 0.0);
}

// IGI-RE src/game/misc.c:129-190 Matrix_SetYAxis (HOOK 0x414A60): the basis whose
// second column is the wire direction. Hand-derived for dir = +X:
//   w = (0,-1,0), u = (0,0,1)  =>  m = [0 1 0; -1 0 0; 0 0 1]
// i.e. local X -> -Y, local Y -> +X, local Z -> +Z.
TEST(WireSpan, YAxisBasisPutsTheWireAlongLocalY) {
    const Basis b = YAxisBasis(Vec3(1, 0, 0));
    EXPECT_DOUBLE_EQ(b.y.x, 1.0);
    EXPECT_DOUBLE_EQ(b.y.y, 0.0);
    EXPECT_DOUBLE_EQ(b.y.z, 0.0);
    EXPECT_DOUBLE_EQ(b.x.x, 0.0);
    EXPECT_DOUBLE_EQ(b.x.y, -1.0);
    EXPECT_DOUBLE_EQ(b.x.z, 0.0);
    EXPECT_DOUBLE_EQ(b.z.x, 0.0);
    EXPECT_DOUBLE_EQ(b.z.y, 0.0);
    EXPECT_DOUBLE_EQ(b.z.z, 1.0);

    // Every wire direction yields an orthonormal, right-handed basis whose Y
    // column is the direction itself (misc.c:186-188 writes dir into m[1],m[4],m[7]).
    for (double deg : {5.0, 30.0, 60.0, 80.0, 89.0}) {
        const Vec3 d = AnchorsForElevation(deg);
        const Vec3 n(d.x / 4096.0, d.y / 4096.0, d.z / 4096.0);
        const Basis bb = YAxisBasis(n);
        EXPECT_NEAR(bb.y.x, n.x, 1e-12) << deg;
        EXPECT_NEAR(bb.y.y, n.y, 1e-12) << deg;
        EXPECT_NEAR(bb.y.z, n.z, 1e-12) << deg;
        EXPECT_NEAR(bb.x.x * bb.x.x + bb.x.y * bb.x.y + bb.x.z * bb.x.z, 1.0, 1e-12) << deg;
        EXPECT_NEAR(bb.z.x * bb.z.x + bb.z.y * bb.z.y + bb.z.z * bb.z.z, 1.0, 1e-12) << deg;
        EXPECT_NEAR(bb.x.x * bb.y.x + bb.x.y * bb.y.y + bb.x.z * bb.y.z, 0.0, 1e-12) << deg;
        // Right-handed: x cross y == z.
        const Vec3 c(bb.x.y * bb.y.z - bb.x.z * bb.y.y,
                     bb.x.z * bb.y.x - bb.x.x * bb.y.z,
                     bb.x.x * bb.y.y - bb.x.y * bb.y.x);
        EXPECT_NEAR(c.x, bb.z.x, 1e-12) << deg;
        EXPECT_NEAR(c.y, bb.z.y, 1e-12) << deg;
        EXPECT_NEAR(c.z, bb.z.z, 1e-12) << deg;
    }
}

// ── Zipline classification (Wire_IsSloped 0x43F8A0) ──────────────────────────

// IGI-RE src/props/wire.c:304-317 compares the matrix X-Euler against 2 degrees;
// src/humanstate/humanstate.c:958-963 turns that into ziplineEnter vs wireEnter.
// A level wire is a hand-over-wire prop.
TEST(WireSlope, LevelWireIsNotAZipline) {
    EXPECT_FALSE(ComputeWireSpan(Vec3(0, 0, 0), Vec3(4096, 0, 0)).isZipline);
    EXPECT_FALSE(ComputeWireSpan(Vec3(0, 0, 0), Vec3(4096, 4096, 0)).isZipline);
    EXPECT_FALSE(ComputeWireSpan(Vec3(0, 0, 0), AnchorsForElevation(1.9)).isZipline);
}

// Both an uphill and a downhill wire are ziplines: Wire_IsSloped takes the
// absolute value of the X-Euler, and Matrix_GetEuler (src/math/matrix.c:181-208)
// normalises a negative elevation into [0, 2pi), so a downhill wire reports
// ~2pi - elevation. Either way |slope| > 2 degrees.
TEST(WireSlope, SlopedWireIsAZiplineInBothDirections) {
    EXPECT_TRUE(ComputeWireSpan(Vec3(0, 0, 0), AnchorsForElevation(2.1)).isZipline);
    EXPECT_TRUE(ComputeWireSpan(Vec3(0, 0, 0), AnchorsForElevation(45.0)).isZipline);
    EXPECT_TRUE(ComputeWireSpan(Vec3(0, 0, 0), AnchorsForElevation(45.0, -1.0)).isZipline);

    // Uphill: the X-Euler is the elevation itself.
    const WireSpan up = ComputeWireSpan(Vec3(0, 0, 0), AnchorsForElevation(45.0));
    EXPECT_NEAR(up.slope, 45.0 * kDegToRad, 1e-9);
    // Downhill: same magnitude, reported in [0, 2pi) by Matrix_GetEuler.
    const WireSpan down = ComputeWireSpan(Vec3(0, 0, 0), AnchorsForElevation(45.0, -1.0));
    EXPECT_NEAR(down.slope, 2.0 * kPi - 45.0 * kDegToRad, 1e-9);
}

// Matrix_SetYAxis (src/game/misc.c:129-190) has two branches, selected by
// |dir.z| > kNearlyUp (0.95, misc.c:99). Above that threshold the X-Euler
// saturates at 90 degrees, but the wire is still a zipline — this reproduces the
// game's behaviour rather than "fixing" it.
TEST(WireSlope, NearVerticalWireSaturatesTheXEuler) {
    const WireSpan s = ComputeWireSpan(Vec3(0, 0, 0), Vec3(0, 0, 4096));
    ASSERT_TRUE(s.valid);
    EXPECT_TRUE(s.isZipline);
    EXPECT_NEAR(s.slope, kPi / 2.0, 1e-9);

    const WireSpan steep = ComputeWireSpan(Vec3(0, 0, 0), AnchorsForElevation(80.0));
    EXPECT_TRUE(steep.isZipline);
    EXPECT_NEAR(steep.slope, kPi / 2.0, 1e-9);
}

// ── Zipline ride direction and speed ─────────────────────────────────────────

// IGI-RE src/weapon/humanplayer.c:557-568 HumanPlayer_UpdateZipline: the ride
// direction is the model's point 0 -> point 1 vector, negated when d.z > 0 so the
// ride always runs downhill, then normalised.
TEST(ZiplineRide, AlwaysRunsDownhill) {
    const Vec3 up = ZiplineRideDirection(Vec3(0, 0, 0), AnchorsForElevation(45.0));
    EXPECT_LT(up.z, 0.0);
    EXPECT_NEAR(up.z, -std::sqrt(0.5), 1e-12);

    const Vec3 down = ZiplineRideDirection(Vec3(0, 0, 0), AnchorsForElevation(45.0, -1.0));
    EXPECT_NEAR(down.z, -std::sqrt(0.5), 1e-12);

    // A level wire has d.z == 0: the game does not flip it, so the authored
    // direction is kept verbatim.
    const Vec3 level = ZiplineRideDirection(Vec3(0, 0, 0), Vec3(4096, 0, 0));
    EXPECT_DOUBLE_EQ(level.x, 1.0);
    EXPECT_DOUBLE_EQ(level.z, 0.0);
}

TEST(ZiplineRide, DegenerateAnchorsGiveZeroDirection) {
    const Vec3 d = ZiplineRideDirection(Vec3(7, 8, 9), Vec3(7, 8, 9));
    EXPECT_DOUBLE_EQ(d.x, 0.0);
    EXPECT_DOUBLE_EQ(d.y, 0.0);
    EXPECT_DOUBLE_EQ(d.z, 0.0);
}

// IGI-RE src/humanstate/humanstate.c:1394-1398.
TEST(ZiplineRide, SpeedIntegratesToTheTerminalSpeed) {
    const Vec3 dir = ZiplineRideDirection(Vec3(0, 0, 0), AnchorsForElevation(45.0, -1.0));
    const double expectedTerminal = -dir.z * kZiplineAccel / (1.0 - kZiplineFriction);
    EXPECT_NEAR(ZiplineTerminalSpeed(dir), expectedTerminal, 1e-9);
    EXPECT_NEAR(expectedTerminal, 6244.4430881554, 1e-6);

    double v = 0.0;
    // v_n = T * (1 - friction^n); 0.99^5000 ~ 1.5e-22, so this lands on T.
    for (int i = 0; i < 5000; ++i) v = ZiplineSpeedStep(v, dir);
    EXPECT_NEAR(v, expectedTerminal, 1e-9);
    // 2000 ticks still leaves a T * 0.99^2000 ~ 1.2e-5 residual: the ride keeps
    // accelerating towards the terminal speed, exactly as in the game.
    double v2000 = 0.0;
    for (int i = 0; i < 2000; ++i) v2000 = ZiplineSpeedStep(v2000, dir);
    EXPECT_NEAR(v2000, expectedTerminal * (1.0 - std::pow(kZiplineFriction, 2000.0)), 1e-6);

    // velocity = speed * direction (humanstate.c:1396-1398).
    const Vec3 vel = ZiplineVelocity(v, dir);
    EXPECT_NEAR(vel.z, v * dir.z, 1e-9);
    EXPECT_NEAR(vel.x, v * dir.x, 1e-9);
}

// ── QSC argument parsing ─────────────────────────────────────────────────────

// IGI-RE src/props/wire.c:89-113 Wire_Open (0x43EFF0) param order:
// start ObjectPos (args 3-5), end ObjectPos (args 6-8), model String16 (arg 9).
TEST(ParseWireAnchors, ReadsBothAnchors) {
    Vec3 a, b;
    ASSERT_TRUE(ParseWireAnchors(WireArgs("10", "20", "30", "40", "50", "60"), a, b));
    EXPECT_DOUBLE_EQ(a.x, 10.0);
    EXPECT_DOUBLE_EQ(a.y, 20.0);
    EXPECT_DOUBLE_EQ(a.z, 30.0);
    EXPECT_DOUBLE_EQ(b.x, 40.0);
    EXPECT_DOUBLE_EQ(b.y, 50.0);
    EXPECT_DOUBLE_EQ(b.z, 60.0);
}

TEST(ParseWireAnchors, RejectsMissingOrNonNumericEnd) {
    Vec3 a, b;
    // No End anchor at all (the pre-fix editor schema): must not silently pretend
    // the wire ends where it starts.
    EXPECT_FALSE(ParseWireAnchors({"1", "\"Wire\"", "\"w\"", "10", "20", "30"}, a, b));
    EXPECT_FALSE(ParseWireAnchors(WireArgs("10", "20", "30", "x", "y", "z"), a, b));
    EXPECT_FALSE(ParseWireAnchors(WireArgs("10", "20", "30", "40", "50", ""), a, b));
}

// IGI-RE src/statgun/statgun.c:142-150 StationaryGun_Open (0x473ED0) param order.
TEST(ParseStationaryGunArgs, ReadsTheFireArcAndAmmo) {
    const std::vector<std::string> args = {
        "7", "\"StationaryGun\"", "\"mg\"", "100", "200", "300",
        "0", "0", "0", "\"320_01_1\"", "\"WEAPON_ID_M2HB\"",
        "30", "-20", "120", "500"};
    const StationaryGunParams p = ParseStationaryGunArgs(args);
    ASSERT_TRUE(p.valid);
    EXPECT_EQ(p.weapon, "WEAPON_ID_M2HB");
    EXPECT_DOUBLE_EQ(p.maxPitch, 30.0);
    EXPECT_DOUBLE_EQ(p.minPitch, -20.0);
    EXPECT_DOUBLE_EQ(p.yawRange, 120.0);
    EXPECT_EQ(p.ammo, 500);
}

// The game fills in its own defaults for any parameter the level omits
// (statgun.c:113-118), so a bare position block must not become all-zeroes.
TEST(ParseStationaryGunArgs, FallsBackToGameDefaults) {
    const StationaryGunParams p =
        ParseStationaryGunArgs({"7", "\"StationaryGun\"", "\"mg\"", "1", "2", "3"});
    ASSERT_TRUE(p.valid);
    EXPECT_EQ(p.weapon, kStatGunDefaultWeapon);
    EXPECT_DOUBLE_EQ(p.maxPitch, 25.0);
    EXPECT_DOUBLE_EQ(p.minPitch, -25.0);
    EXPECT_DOUBLE_EQ(p.yawRange, 90.0);
    EXPECT_EQ(p.ammo, -1);

    // No position block at all: not a StationaryGun record.
    EXPECT_FALSE(ParseStationaryGunArgs({"7", "\"StationaryGun\"", "\"mg\""}).valid);
}

// IGI-RE src/comprun/gunholder.c:157-165 GunHolder_Init (0x46E370) param order.
TEST(ParseGunHolderArgs, ReadsAnglesRangeAndExpressions) {
    const std::vector<std::string> args = {
        "9", "\"AIStationaryGunHolder\"", "\"n\"", "1", "2", "3",
        "0", "0", "0", "\"m\"", "45", "90", "25", "\"a==1\"", "\"t==2\""};
    const GunHolderParams p = ParseGunHolderArgs(args);
    ASSERT_TRUE(p.valid);
    EXPECT_DOUBLE_EQ(p.angleX, 45.0);
    EXPECT_DOUBLE_EQ(p.angleY, 90.0);
    EXPECT_DOUBLE_EQ(p.range, 25.0);
    EXPECT_EQ(p.activeExpr, "a==1");
    EXPECT_EQ(p.teamExpr, "t==2");
}

TEST(ParseGunHolderArgs, FallsBackToGameDefaults) {
    const GunHolderParams p =
        ParseGunHolderArgs({"9", "\"AIStationaryGunHolder\"", "\"n\"", "1", "2", "3"});
    ASSERT_TRUE(p.valid);
    EXPECT_DOUBLE_EQ(p.angleX, 360.0);
    EXPECT_DOUBLE_EQ(p.angleY, 360.0);
    EXPECT_DOUBLE_EQ(p.range, 10.0);
    EXPECT_TRUE(p.activeExpr.empty());
    EXPECT_TRUE(p.teamExpr.empty());
}

TEST(ParseTokens, HandlesQuotesAndGarbage) {
    double v = 123.0;
    EXPECT_TRUE(ParseDoubleToken("-4.5", v));
    EXPECT_DOUBLE_EQ(v, -4.5);
    EXPECT_FALSE(ParseDoubleToken("\"WEAPON_ID_M2HB\"", v));
    EXPECT_FALSE(ParseDoubleToken("", v));
    EXPECT_DOUBLE_EQ(v, -4.5);   // untouched on failure

    EXPECT_EQ(UnquoteToken("\"abc\""), "abc");
    EXPECT_EQ(UnquoteToken("abc"), "abc");
}

// ── Builtin schemas (what the inspector exposes) ─────────────────────────────

// GitHub issue #42: the Wire's second anchor was missing from the schema, so the
// inspector showed one position and the wire could never be configured.
TEST(BuiltinSchema, WireExposesBothAnchors) {
    const TaskSchema* sc = GetBuiltinSchema("Wire");
    ASSERT_NE(sc, nullptr);
    ASSERT_EQ(sc->size(), 3u);
    EXPECT_EQ((*sc)[0].name, "Position");
    EXPECT_EQ((*sc)[0].argOffset, 3);
    EXPECT_EQ((*sc)[0].argCount, 3);
    EXPECT_EQ((*sc)[1].name, "End");
    EXPECT_EQ((*sc)[1].argOffset, 6);
    EXPECT_EQ((*sc)[1].argCount, 3);
    EXPECT_EQ((*sc)[2].name, "Model");
    EXPECT_EQ((*sc)[2].argOffset, 9);
}

// The End anchor uses Real64x3 (the type ObjectPos aliases, IGI-RE
// src/physobj/paramops.c:185) so the editor's ObjectPos position-drag — which
// always writes obj.pos — cannot be triggered on it.
TEST(BuiltinSchema, WireEndIsAThreeComponentField) {
    const TaskSchema* sc = GetBuiltinSchema("Wire");
    ASSERT_NE(sc, nullptr);
    EXPECT_EQ((*sc)[1].typeName, "Real64x3");
    EXPECT_EQ(TypeArgCount("Real64x3"), 3);
    EXPECT_EQ(TypeArgCount("ObjectPos"), 3);
    EXPECT_EQ(TypeArgCount("Real32x9"), 3);
    EXPECT_EQ(TypeArgCount("VarString"), 1);
    EXPECT_EQ(TypeArgCount("String32"), 1);
}

// IGI-RE src/statgun/statgun.c:142-150.
TEST(BuiltinSchema, StationaryGunExposesWeaponArcAndAmmo) {
    const TaskSchema* sc = GetBuiltinSchema("StationaryGun");
    ASSERT_NE(sc, nullptr);
    ASSERT_EQ(sc->size(), 8u);
    const char* names[] = {"Position", "Orientation", "Model", "Weapon",
                           "Max Pitch", "Min Pitch", "Yaw Range", "Ammo"};
    const char* types[] = {"ObjectPos", "Real32x9", "String16", "String32",
                           "Degrees", "Degrees", "Degrees", "Int32"};
    const int offs[] = {3, 6, 9, 10, 11, 12, 13, 14};
    for (size_t i = 0; i < 8; ++i) {
        EXPECT_EQ((*sc)[i].name, names[i]) << "field " << i;
        EXPECT_EQ((*sc)[i].typeName, types[i]) << "field " << i;
        EXPECT_EQ((*sc)[i].argOffset, offs[i]) << "field " << i;
    }
}

// IGI-RE src/comprun/gunholder.c:157-165.
TEST(BuiltinSchema, GunHolderExposesAnglesRangeAndExpressions) {
    const TaskSchema* sc = GetBuiltinSchema("AIStationaryGunHolder");
    ASSERT_NE(sc, nullptr);
    ASSERT_EQ(sc->size(), 8u);
    const char* names[] = {"Position", "Orientation", "Model", "Angle X",
                           "Angle Y", "Range", "Active Expr", "Team Expr"};
    const char* types[] = {"ObjectPos", "Real32x9", "String16", "Degrees",
                           "Degrees", "Real32", "VarString", "VarString"};
    const int offs[] = {3, 6, 9, 10, 11, 12, 13, 14};
    for (size_t i = 0; i < 8; ++i) {
        EXPECT_EQ((*sc)[i].name, names[i]) << "field " << i;
        EXPECT_EQ((*sc)[i].typeName, types[i]) << "field " << i;
        EXPECT_EQ((*sc)[i].argOffset, offs[i]) << "field " << i;
    }
}

// IGI-RE src/props/fence.c:100-121 Fence_Open (0x43F900): the electric-fence
// condition expression at arg 8 was never exposed.
TEST(BuiltinSchema, FenceExposesItsConditionExpression) {
    const TaskSchema* sc = GetBuiltinSchema("Fence");
    ASSERT_NE(sc, nullptr);
    ASSERT_EQ(sc->size(), 4u);
    EXPECT_EQ((*sc)[0].name, "Position");
    EXPECT_EQ((*sc)[0].argOffset, 3);
    EXPECT_EQ((*sc)[1].name, "Heading");
    EXPECT_EQ((*sc)[1].argOffset, 6);
    EXPECT_EQ((*sc)[2].name, "Model");
    EXPECT_EQ((*sc)[2].argOffset, 7);
    EXPECT_EQ((*sc)[3].name, "Condition");
    EXPECT_EQ((*sc)[3].typeName, "VarString");
    EXPECT_EQ((*sc)[3].argOffset, 8);
}

// The three interactables must not be swallowed by the generic
// "pos/ori/model only" bucket any more.
TEST(BuiltinSchema, InteractablesAreNotGeneric) {
    for (const char* t : {"StationaryGun", "AIStationaryGunHolder"}) {
        const TaskSchema* sc = GetBuiltinSchema(t);
        ASSERT_NE(sc, nullptr) << t;
        EXPECT_GT(sc->size(), 3u) << t;
    }
}

// ── Runtime schema (Task_DeclareParameters) — the one the level actually loads ─

// The retail level declares Wire itself, and a registered schema takes precedence
// over the builtin (GetSchema, source/level/task_schema.cpp):
//
//   Task_DeclareParameters("Wire", "Start position", "ObjectPos",
//                          "Stop position", "ObjectPos", "Model", "String16");
//   MISSIONS/location0/level1/objects.qsc:69
//
// Before the fix, that runtime schema replaced the builtin's Real64x3 "End" row
// with a second ObjectPos. ObjectPos is the widget bound to the task's OWN
// transform (App::ApplyPropPositionDrag writes obj.pos, and
// UpdateCoordinatesInLine then writes obj.pos to args 3-5), so on level 1 — the
// level that has the authored wires — the Stop position pad moved the START
// anchor instead of the stop anchor. ParseDeclaration now demotes every ObjectPos
// after the first to Real64x3, the type ObjectPos aliases
// (src/physobj/paramops.c:185); the three tokens and their offset are unchanged.
TEST(RuntimeSchema, WireDeclarationKeepsTheStopAnchorOutOfObjectPos) {
    const TaskSchema sc = ParseDeclaration(
        {"Wire", "Start position", "ObjectPos",
         "Stop position", "ObjectPos", "Model", "String16"});
    ASSERT_EQ(sc.size(), 3u);
    EXPECT_EQ(sc[0].name, "Start position");
    EXPECT_EQ(sc[0].typeName, "ObjectPos");   // the task's own position @3-5
    EXPECT_EQ(sc[0].argOffset, 3);
    EXPECT_EQ(sc[0].argCount, 3);
    EXPECT_EQ(sc[1].name, "Stop position");
    EXPECT_EQ(sc[1].typeName, "Real64x3");     // NOT ObjectPos: no obj.pos binding
    EXPECT_EQ(sc[1].argOffset, 6);
    EXPECT_EQ(sc[1].argCount, 3);
    EXPECT_EQ(sc[2].name, "Model");
    EXPECT_EQ(sc[2].typeName, "String16");
    EXPECT_EQ(sc[2].argOffset, 9);

    // And that is what GetSchema returns once a level has loaded, since the
    // registered schema shadows the builtin one.
    RegisterSchema("Wire", sc);
    const TaskSchema* live = GetSchema("Wire");
    ASSERT_NE(live, nullptr);
    ASSERT_EQ(live->size(), 3u);
    EXPECT_EQ((*live)[1].typeName, "Real64x3");
    ClearRegisteredSchemas();
}

// A declaration whose ONLY ObjectPos is not the first field (ConditionalSound,
// objects.qsc:78: Condition, SoundDef, Position) must keep that ObjectPos: the
// demotion is about a SECOND position parameter, not about its field index.
TEST(RuntimeSchema, FirstObjectPosStaysRegardlessOfItsIndex) {
    const TaskSchema sc = ParseDeclaration(
        {"ConditionalSound", "Condition", "VarString", "SoundDef", "String32",
         "Position", "ObjectPos", "Simple", "bool8"});
    ASSERT_EQ(sc.size(), 4u);
    EXPECT_EQ(sc[2].typeName, "ObjectPos");
    EXPECT_EQ(sc[2].argOffset, 5);   // 3 + 1 + 1
}

// ── Shipped data (level 1 is the retail level that authors objects.qsc Wires) ─

// MISSIONS/location0/level1/objects.qsc:888-890, verbatim:
//   Task_New(3500, "Wire", "", 24631402.0, -56199400.0, 174434752.0,
//                                 24760932.0, -56297984.0, 174434752.0, "320_01_1")
//   Task_New(3501, "Wire", "", 24649340.0, -55968532.0, 174500528.0,
//                                 24441788.0, -56251736.0, 174470384.0, "320_01_1")
//   Task_New(3502, "Wire", "", 24472610.0, -55944580.0, 174442496.0,
//                                 24446002.0, -56045108.0, 174433600.0, "320_01_1")
//
// Every value below is Wire_ParamsChanged (0x43F3D0, src/props/wire.c:160-191)
// applied to those tokens: mid = (start+end)/2, length = |end-start|,
// yScale = length/4096 (kWireScale, wire.c:320-321). Wire 3500 is level (dz = 0),
// so Wire_IsSloped (wire.c:304-317) is false and it is a hand-over wire; 3501 and
// 3502 descend, so their Matrix_SetYAxis X-Euler is > 2 degrees and they are the
// ziplines HumanPlayer_StartAction case 9 rides (humanstate.c:958-963).
TEST(ShippedWires, LevelOneWiresResolveToTheEngineSpan) {
    struct Case {
        double s[3], e[3];
        double length, yScale;
        bool zipline;
    };
    const Case cases[] = {
        {{24631402.0, -56199400.0, 174434752.0},
         {24760932.0, -56297984.0, 174434752.0}, 162778.45667040831, 39.740834148048904, false},
        {{24649340.0, -55968532.0, 174500528.0},
         {24441788.0, -56251736.0, 174470384.0}, 352407.43331547367, 86.036971024285563, true},
        {{24472610.0, -55944580.0, 174442496.0},
         {24446002.0, -56045108.0, 174433600.0}, 104369.55142185866, 25.480847515102212, true},
    };
    for (const Case& c : cases) {
        const WireSpan w = ComputeWireSpan(Vec3(c.s[0], c.s[1], c.s[2]),
                                           Vec3(c.e[0], c.e[1], c.e[2]));
        ASSERT_TRUE(w.valid);
        EXPECT_NEAR(w.length, c.length, 1e-6);
        EXPECT_NEAR(w.yScale, c.yScale, 1e-9);
        EXPECT_NEAR(w.mid.x, (c.s[0] + c.e[0]) / 2.0, 1e-9);
        EXPECT_NEAR(w.mid.y, (c.s[1] + c.e[1]) / 2.0, 1e-9);
        EXPECT_NEAR(w.mid.z, (c.s[2] + c.e[2]) / 2.0, 1e-9);
        EXPECT_EQ(w.isZipline, c.zipline);
    }
}
