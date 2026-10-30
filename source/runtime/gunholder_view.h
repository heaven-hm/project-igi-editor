#pragma once
/******************************************************************************
 * @file    gunholder_view.h
 * @brief   `AIStationaryGunHolder` -- the engine's detection volume and the gun
 *          it carries, as host-testable rules.
 *
 * ORACLE: /Users/haseeb-mir/Documents/Code/IGI-RE (read-only), the decompiled
 * retail IGI.exe. Every constant and every operation below cites its function,
 * its HOOK address and its file:line.
 *
 * WHAT THE TASK IS. `AIStationaryGunHolder` is the fixed gun that SPOTS for the
 * AI (Level 14's "sentry guns"): TaskType_New at GunHolder_Init HOOK 0x46E370,
 * src/comprun/gunholder.c:146. Its ParamTable (gunholder.c:157-165) has eight
 * authored slots, and three of them are not a model and a transform at all --
 * they are the fields of an AIDetector view cone:
 *
 *     view.maxPitch = angleX * kDegToRad   // arg 10, "Viewcone Alpha"
 *     view.maxYaw   = angleY * kDegToRad   // arg 11, "Viewcone Gamma"
 *     view.range    = range  * 4096.0f     // arg 12, "Viewcone Length", metres
 *
 * (GunHolder_Changed HOOK 0x46E7F0, gunholder.c:228-230; kRangeScale is
 * gunholder.c:102.)
 *
 * THE VOLUME IS NOT A CIRCULAR CONE. It is the test AIView_Contains
 * (HOOK 0x489960, src/levelflow/aidetect.c:282-305) applies to a candidate
 * position:
 *
 *     l = M^T (p - view.pos)                        // aidetect.c:276-279
 *     len = |l|
 *     pitch = |Vec3d_PitchAngle(l)|
 *     yaw   = |Vec3d_YawAngle(l)|
 *     inside  <=>  !(len > range) && !(pitch >= maxPitch) && !(yaw >= maxYaw)
 *
 * and the two angles are NOT spherical coordinates:
 *
 *     Vec3d_PitchAngle (HOOK 0x4148F0, src/game/misc.c:41-57)
 *         zeroes x, then returns signed acos(y/|(y,z)|) -- the angle measured
 *         from local +Y in the YZ plane, negative below.
 *     Vec3d_YawAngle   (HOOK 0x414950, src/game/misc.c:59-73)
 *         zeroes z, then returns signed acos(y/|(x,y)|) -- the angle measured
 *         from local +Y in the XY plane, negative towards +x.
 *
 * So each limit is a WEDGE ABOUT LOCAL +Y measured in one coordinate plane,
 * and the volume is the intersection of two wedges with a ball:
 *
 *   * the "forward" axis is the object's local +Y, not +Z and not +X;
 *   * a limit >= 180 deg makes its test vacuous (`|yaw| <= pi` always), and a
 *     limit of 360 deg -- what every shipped holder authors -- therefore leaves
 *     the RANGE ALONE: a sphere of `range` metres, not a cone;
 *   * the two angles are independent one-plane measurements, so even
 *     `maxPitch == maxYaw` gives a pyramid-like region, not a circular cone.
 *
 * The `pitch`/`yaw` helpers the angles call (Vec3d_IsZero 0x4B3320,
 * Vec3d_Normalize 0x4B3100) are only ORIG'd in IGI-RE, not decompiled; they are
 * transcribed here as their names state (an all-zero test and a divide by the
 * length), which is the only reading their two callers support.
 *
 * THE GUN. The holder is not just its own model: GunHolder_Changed builds
 *     t->obj.attached = MagicObjContainer_New(NULL, t);
 *     RigidDynCubeObj_CreateMagicObjs(t->obj.attached, t, t);   // :223-224
 * and RigidDynCubeObj_CreateMagicObjs (0x4C4950, src/magicobj/rigidcoll.c:
 * 381-434) creates one task per ATTA record of the holder's model, looking the
 * record's NAME up in the DefineMagicObj table. Level 14's holder model
 * `456_11_1` carries exactly one ATTA record, `456_12_1`
 * (MISSIONS/location0/LEVEL14/MODELS/LEVEL14.RES, measured), and
 * MAGICOBJ.QVM:372 defines it:
 *
 *     DefineMagicObj("456_12_1", "456_12_1", TASKTYPE_AISTATIONARYGUN,
 *                    "456_13_1", 361, -10, -90, 10000, 60, 60,
 *                    "tank_turret", 45, 0.2);
 *
 * TASKTYPE_AISTATIONARYGUN is the turning gun MagicObj (AIStationaryGun_Open
 * HOOK 0x46F2B0, src/binocular/stationarygun.c:191-203). AIStationaryGun_Create
 * (0x46EAE0, :123-155) makes a RigidDynCubeObj of `def->model` (456_12_1) and
 * mounts a weapon on it:
 *
 *     Gun_New(g, 0, *WeaponType_FindByKey(g->def->weapon), g->def->ammo, 0);
 *     WeaponSlots_SelectNext(g, g->weapons, 0);                      // :145-147
 *
 * `g->def->weapon` is DefineMagicObj's first data argument (:395-411), i.e.
 * "456_13_1" for this definition -- a WEAPON TYPE KEY, not a model id: the
 * WEAPONS/SENTRYGUN/WEAPON.QVM definition sets `WeaponType.key` (argument 29,
 * DefineWeaponType HOOK 0x413F90, src/weapon/weapontype.c:335-337) to
 * "456_13_1", and WeaponType.key is the model the Gun task loads
 * (Gun_ParamChangedHandler, src/guncasing/gun.c:493-506). So the sentry gun's
 * assembly is three models -- holder 456_11_1, turret 456_12_1, mounted weapon
 * 456_13_1 -- and the editor drew only the first two.
 *
 * The rest pose of the mounted weapon is AIStationaryGun_PlaceWeapon
 * (HOOK 0x470050, src/binocular/stationarygun.c:464-487):
 *
 *     Model_GetPoint(&pos, g, 1);            // gun model point 1 = the muzzle
 *     pos += g->obj.pos;
 *     Matrix_SetEuler(&r, g->pitch, 0, g->yaw);
 *     mat_mul(&m, &g->owner->matrix, &r, 0, 1, 0);   // owner = the HOLDER
 *     Weapon_Move(w, &pos, &m);
 *
 * At rest pitch = yaw = 0 (AIStationaryGun_Create :143-144), so the weapon sits
 * at the gun's position and takes the HOLDER's matrix; and the gun's position is
 * the ATTA world position CreateMagicObjs computed,
 * `pos = owner.pos + owner.matrix * att.pos` (rigidcoll.c:399-412). `456_12_1`
 * carries no XTVM point array at all (measured: MVTX chunk size 0), so the
 * engine's point-1 lookup returns zero -- the same out-of-range rule
 * Model_GetPoint's callers rely on (src/statgun/statgun.c:246-249 drops idx to
 * 0 only when the vertex count is below 1; Model_GetPoint returns zero when the
 * index is past the end, per the editor's measured M2HB case in
 * tests/test_statgun_mount.cpp).
 *
 * NOT MODELLED HERE (runtime, not editor-preview): the holder's per-tick
 * expression evaluation and AIDetector_Update (gunholder.c:170-197), the AI
 * aiming/firing sweep (stationarygun.c:209-354), and the in-game COMPUTER MAP
 * sprite (GunHolder_Draw HOOK 0x46E9F0, gunholder.c:248-257, sprite
 * ComputerRes+0x468) -- the editor has no computer-map surface.
 *****************************************************************************/

#include <cmath>
#include <cstddef>
#include <vector>

namespace igi {
namespace gunholder {

struct Vec3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

// Row-major, the engine's `Matrix::m[9]`; world = M * local.
struct Mat3 {
    double m[9] = {1.0, 0.0, 0.0,
                   0.0, 1.0, 0.0,
                   0.0, 0.0, 1.0};
};

struct Segment {
    Vec3 a;
    Vec3 b;
};

// gunholder.c:102
constexpr double kRangeScale = 4096.0;
// gunholder.c:101
constexpr double kDegToRad = 0.017453292519943295;
// gunholder.c:124-126 -- GunHolder_Create's defaults, and the ones a missing
// authored argument falls back to (ParamTable_Add only overwrites what the
// script supplies).
constexpr double kDefaultAngleX = 360.0;
constexpr double kDefaultAngleY = 360.0;
constexpr double kDefaultRangeMetres = 10.0;
// gunholder.c:155-156 -- gGunHolderRange[] = {0, 100}
constexpr double kRangeMinMetres = 0.0;
constexpr double kRangeMaxMetres = 100.0;

// A detection view, the part of DetectView (src/levelflow/levelflowtypes.h:
// 11-22) that the holder authors. `maxPitch`/`maxYaw` are radians, `range` is
// in world units.
struct View {
    Vec3 pos;
    Mat3 matrix;
    double maxPitch = kDefaultAngleX * kDegToRad;
    double maxYaw = kDefaultAngleY * kDegToRad;
    double range = kDefaultRangeMetres * kRangeScale;
};

// The authored params -> the view the engine builds (GunHolder_Changed,
// gunholder.c:228-230). Kept as one function so no caller can apply a different
// conversion.
inline View MakeView(const Vec3& pos, const Mat3& matrix,
                     double angleXdeg, double angleYdeg, double rangeMetres) {
    View v;
    v.pos = pos;
    v.matrix = matrix;
    v.maxPitch = angleXdeg * kDegToRad;
    v.maxYaw = angleYdeg * kDegToRad;
    v.range = rangeMetres * kRangeScale;
    return v;
}

// Vec3d_PitchAngle HOOK 0x4148F0, src/game/misc.c:41-57. Angle of v in the YZ
// plane from +Y, negative below (`!(v.z >= 0)` negates, which also catches NaN).
inline double PitchAngle(Vec3 v) {
    v.x = 0.0;
    const double len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    if (!(v.x != 0.0 || v.y != 0.0 || v.z != 0.0)) return 0.0f;  // Vec3d_IsZero
    const double y = v.y / len;                                  // Vec3d_Normalize
    double a = std::acos(y);
    if (!(v.z >= 0.0)) a = -a;
    return a;
}

// Vec3d_YawAngle HOOK 0x414950, src/game/misc.c:59-73. Angle of v in the XY
// plane from +Y, negative towards +x (`v.x > 0` negates).
inline double YawAngle(Vec3 v) {
    v.z = 0.0;
    const double len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    if (!(v.x != 0.0 || v.y != 0.0 || v.z != 0.0)) return 0.0f;  // Vec3d_IsZero
    const double y = v.y / len;                                  // Vec3d_Normalize
    double a = std::acos(y);
    if (v.x > 0.0) a = -a;
    return a;
}

// The local vector of a world point: l = M^T (p - view.pos), the aidetect.c:
// 276-279 arithmetic written out (`l.x = m[6]*dz + m[3]*dy + m[0]*dx`).
inline Vec3 ToLocal(const Mat3& matrix, const Vec3& origin, const Vec3& point) {
    const double dx = point.x - origin.x;
    const double dy = point.y - origin.y;
    const double dz = point.z - origin.z;
    Vec3 l;
    l.x = matrix.m[6] * dz + matrix.m[3] * dy + matrix.m[0] * dx;
    l.y = matrix.m[7] * dz + matrix.m[4] * dy + matrix.m[1] * dx;
    l.z = matrix.m[8] * dz + matrix.m[5] * dy + matrix.m[2] * dx;
    return l;
}

// The angular half of AIView_Contains on a LOCAL vector, verbatim from
// aidetect.c:294-301 (`!(pitch >= maxPitch)` rather than `<`, so a NaN limit
// would accept -- as the engine does).
inline bool LocalDirectionInView(const Vec3& local, double maxPitch, double maxYaw) {
    const double pitch = std::fabs(PitchAngle(local));
    const double yaw = std::fabs(YawAngle(local));
    return !(pitch >= maxPitch) && !(yaw >= maxYaw);
}

// AIView_Contains HOOK 0x489960, src/levelflow/aidetect.c:282-305.
inline bool ViewContains(const View& view, const Vec3& point) {
    const Vec3 l = ToLocal(view.matrix, view.pos, point);
    const double len = std::sqrt(l.x * l.x + l.y * l.y + l.z * l.z);
    if (len > view.range) return false;
    return LocalDirectionInView(l, view.maxPitch, view.maxYaw);
}

// True when the angular limits accept every direction (each angle is in
// (-pi, pi], so a limit >= pi can never fail). Every shipped holder authors
// 360/360, which makes the detection volume a BALL of `range`.
inline bool ViewLimitsAreVacuous(double maxPitch, double maxYaw) {
    return maxPitch >= 3.14159265358979323846 && maxYaw >= 3.14159265358979323846;
}

// The rim of the detection volume, as line segments on the sphere of radius
// `range`: every cell of an azimuth/elevation grid whose corners disagree about
// membership contributes its in/out edge. The membership question is the
// engine's own `LocalDirectionInView`, so the drawn surface is the engine's
// region and not an approximation of a cone. Returns an empty list when the
// limits are vacuous (draw a ball instead) or when `range` is not positive.
inline std::vector<Segment> ViewVolumeRim(const View& view, int azimuth_steps,
                                          int elevation_steps) {
    std::vector<Segment> out;
    if (!(view.range > 0.0)) return out;
    if (ViewLimitsAreVacuous(view.maxPitch, view.maxYaw)) return out;
    if (azimuth_steps < 3) azimuth_steps = 3;
    if (elevation_steps < 2) elevation_steps = 2;

    // Direction -> surface point in world space: u is built in the view's LOCAL
    // frame (so the angles are measured exactly as the engine measures them) and
    // rotated out by M (world = M * local, the same product ToLocal inverts).
    auto world = [&](double az, double el) {
        const Vec3 u{std::cos(el) * std::cos(az),
                     std::cos(el) * std::sin(az),
                     std::sin(el)};
        Vec3 w;
        w.x = view.matrix.m[0] * u.x + view.matrix.m[1] * u.y + view.matrix.m[2] * u.z;
        w.y = view.matrix.m[3] * u.x + view.matrix.m[4] * u.y + view.matrix.m[5] * u.z;
        w.z = view.matrix.m[6] * u.x + view.matrix.m[7] * u.y + view.matrix.m[8] * u.z;
        return Vec3{view.pos.x + w.x * view.range,
                    view.pos.y + w.y * view.range,
                    view.pos.z + w.z * view.range};
    };
    auto inside = [&](double az, double el) {
        const Vec3 u{std::cos(el) * std::cos(az),
                     std::cos(el) * std::sin(az),
                     std::sin(el)};
        return LocalDirectionInView(u, view.maxPitch, view.maxYaw);
    };

    for (int i = 0; i < azimuth_steps; ++i) {
        const double az0 = 2.0 * 3.14159265358979323846 * i / azimuth_steps;
        const double az1 = 2.0 * 3.14159265358979323846 * (i + 1) / azimuth_steps;
        for (int j = 0; j < elevation_steps; ++j) {
            const double el0 = -1.57079632679489661923 +
                               3.14159265358979323846 * j / elevation_steps;
            const double el1 = -1.57079632679489661923 +
                               3.14159265358979323846 * (j + 1) / elevation_steps;
            const bool a = inside(az0, el0), b = inside(az1, el0);
            const bool c = inside(az0, el1), d = inside(az1, el1);
            if (a != b) out.push_back({world(az0, el0), world(az1, el0)});
            if (a != c) out.push_back({world(az0, el0), world(az0, el1)});
            if (d != b) out.push_back({world(az1, el1), world(az1, el0)});
            if (d != c) out.push_back({world(az1, el1), world(az0, el1)});
        }
    }
    return out;
}

// CreateMagicObjs' world position of an attachment point (rigidcoll.c:399-412):
// `arg.pos = owner.pos + owner.matrix * att.pos`, with `att.pos` in raw world
// units.
inline Vec3 AttachmentWorldPos(const Mat3& owner_matrix, const Vec3& owner_pos,
                               const Vec3& att_pos) {
    Vec3 p;
    p.x = owner_matrix.m[1] * att_pos.y + owner_matrix.m[2] * att_pos.z +
          owner_matrix.m[0] * att_pos.x + owner_pos.x;
    p.y = owner_matrix.m[4] * att_pos.y + owner_matrix.m[5] * att_pos.z +
          owner_matrix.m[3] * att_pos.x + owner_pos.y;
    p.z = owner_matrix.m[8] * att_pos.z + owner_matrix.m[6] * att_pos.x +
          owner_matrix.m[7] * att_pos.y + owner_pos.z;
    return p;
}

}  // namespace gunholder
}  // namespace igi
