#pragma once
/******************************************************************************
 * @file    statgun_mount.h
 * @brief   A placed `StationaryGun`'s MOUNTED WEAPON -- the engine's own
 *          gun-matrix x mount-local-matrix product, host-testable.
 *
 * WHAT THIS IS. A placed `StationaryGun` (the task type the retail scripts
 * author; `TaskType_New("StationaryGun", ...)`, StationaryGun_Open HOOK
 * 0x473ED0, src/statgun/statgun.c:126) is not only the holder model the editor
 * already draws. StationaryGun_Changed (HOOK 0x474980, statgun.c:310-340)
 * evaluates the authored `weapon` string (param slot 3, String32, default
 * "WEAPON_ID_M2HB" at statgun.c:114,146), resolves a weapon type with
 * WeaponType_Find (HOOK 0x413BD0), creates a Gun task with Gun_New
 * (HOOK 0x477C50) and MOUNTS it with StationaryGun_Aim (HOOK 0x474420):
 *
 *     if (!w) return;                                       // no weapon -> nothing mounted
 *     Matrix_SetRotZ(&o->obj.matrix, o->outYaw);            // :241
 *     for (i = 0; i < 9; i++)                               // :242-243
 *         t.m[i] = mat_elem(n, i / 3, m, i % 3, kLast[i]);  // t = baseMatrix * RotZ(outYaw)
 *     t.count = o->baseMatrix.count + o->obj.matrix.count + 1;
 *     o->obj.matrix = t;                                    // :245
 *     model = o->obj.model;
 *     if (model && ...vertex_count < 1)
 *         idx = 0;                                          // :246-248
 *     Model_GetPoint(&p, o, idx);                           // :249
 *     p.x = p.x + o->obj.pos.x;                             // :250-252
 *     p.y = p.y + o->obj.pos.y;
 *     p.z = p.z + o->obj.pos.z;
 *     Matrix_SetEuler(&e, o->outPitch, 0.0f, 0x1.921fb6p+1f);   // :253
 *     for (i = 0; i < 9; i++)                               // :254-255
 *         t.m[i] = mat_elem(m, i / 3, e.m, i % 3, 1);       // t = o->obj.matrix * e
 *     t.count = o->obj.matrix.count + e.count + 1;
 *     e = t;                                                // :257
 *     Weapon_Move(w, &p, &e);                               // :258  ORIG(0x4F2310)
 *     o->muzzlePos = p;                                     // :259-260
 *     o->muzzleMatrix = e;
 *
 * `e` is NOT authored data and NOT a per-weapon offset: it is synthesised right
 * there from the gun's *runtime aim* (`outPitch`) and a fixed PI about Z. There
 * is no other local mount transform anywhere in the engine, so this module adds
 * none. `Weapon_Move` (HOOK 0x4F2310, src/vehbase/weapon.c:35-40) forwards the
 * pair to the Gun task's move handler, and `Gun_MoveHandler` (HOOK 0x478A90,
 * src/guncasing/gun.c:510-526) assigns them verbatim --
 * `g->o.matrix = *msg->matrix;` -- so the weapon's drawn world transform IS
 * `e`, and `p` is its position. That is the product this header composes.
 *
 * THE REST STATE. `outPitch`/`outYaw` are reset to zero by
 * StationaryGun_Changed (statgun.c:319-320) and are only advanced while a human
 * mans the gun (StationaryGun_RunHandler, statgun.c:166-171,
 * `o->outPitch = o->pitch + o->recoilPitch`). A placed gun with nobody on it is
 * therefore at outPitch = outYaw = 0 and the mount reduces to
 * `baseMatrix * RotZ(PI)`. Note the gun's *body* is never pitched by the engine;
 * StationaryGun_Aim only ever writes RotZ(outYaw) into `o->obj.matrix`
 * (statgun.c:241), and the pitch lives solely in the weapon mount.
 *
 * THE MUZZLE POSITION. `idx` is 1 (statgun.c:246-248 only drops it to 0 when
 * the model's vertex count is below 1). `Model_GetPoint` (HOOK 0x414E20,
 * src/game/misc.c:192-200) returns model point `i` scaled by the object's
 * `scale` and rotated by its matrix -- Mesh3D_GetWorldVertex (HOOK 0x4D0A50,
 * src/lod/mesh3d.c:420-441) -- and returns zero when `i` is out of range.
 * `ModelLod_GetVertexCount` is `MeshHeader.vertex_count` (0x4D0D20,
 * src/lod/mesh3d.c:505-509; the int16 at +0x7C, src/model/modeltypes.h:8-28),
 * which counts the MEF_MVTX point array ("XTVM" on disk) that the editor reads
 * as `Mesh::magicVertices` (source/renderer/model.cpp:34-36). The shipped M2HB
 * holder model `123_02_1` carries exactly ONE point, so index 1 is out of range
 * and the engine's muzzle position is the gun position itself; the corpus test
 * (tests/test_statgun_mount.cpp) reads that count out of the shipped `.mef`.
 *
 * LAYOUT/CONVENTION. `Matrix::m[9]` is row-major and applied as
 * `world = M * local` -- the convention Mesh3D_GetWorldVertex uses
 * (`r0 = z*m[2] + y*m[1] + x*m[0]`, mesh3d.c:432-438) and the one
 * source/renderer/igi_transform.h already documents for the editor's glm
 * matrices (`Matrix_SetEuler` = Rz * Ry * Rx, X applied to a point first).
 * The arithmetic here is double; the engine stores float, so a caller that
 * needs the engine's exact bits must round to float. Angles are radians.
 *
 * Oracle: /Users/haseeb-mir/Documents/Code/IGI-RE (read-only). Every constant
 * and every operation below cites its function + address + file:line.
 *****************************************************************************/

#include <cstddef>
#include <cmath>

namespace igi {
namespace statgun {

struct Vec3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

// Row-major, the engine's `Matrix::m[9]` (src/math/mathvars.h / matrix usage).
struct Mat3 {
    double m[9] = {1.0, 0.0, 0.0,
                   0.0, 1.0, 0.0,
                   0.0, 0.0, 1.0};
};

// The Z angle StationaryGun_Aim passes to Matrix_SetEuler (statgun.c:253:
// `0x1.921fb6p+1f`). It is a FLOAT PI (3.1415927410125732421875), not the
// double 3.141592653589793, and the difference is kept so the mount's bits are
// the engine's input, not a rounded copy.
constexpr double kMountZ = 3.1415927410125732421875;

constexpr double kPi = 3.14159265358979323846;

// Matrix_SetRotX HOOK 0x4B3B60, src/math/matrix.c:112-130.
inline Mat3 SetRotX(double a) {
    const double c = std::cos(a);
    const double s = std::sin(a);
    Mat3 r;
    r.m[0] = 1.0; r.m[1] = 0.0; r.m[2] = 0.0;
    r.m[3] = 0.0; r.m[4] = c;   r.m[5] = -s;
    r.m[6] = 0.0; r.m[7] = s;   r.m[8] = c;
    return r;
}

// Matrix_SetRotY HOOK 0x4B3BA0, src/math/matrix.c:132-146.
inline Mat3 SetRotY(double a) {
    const double c = std::cos(a);
    const double s = std::sin(a);
    Mat3 r;
    r.m[0] = c;   r.m[1] = 0.0; r.m[2] = s;
    r.m[3] = 0.0; r.m[4] = 1.0; r.m[5] = 0.0;
    r.m[6] = -s;  r.m[7] = 0.0; r.m[8] = c;
    return r;
}

// Matrix_SetRotZ HOOK 0x4B3BE0, src/math/matrix.c:148-164.
inline Mat3 SetRotZ(double a) {
    const double c = std::cos(a);
    const double s = std::sin(a);
    Mat3 r;
    r.m[0] = c;   r.m[1] = -s;  r.m[2] = 0.0;
    r.m[3] = s;   r.m[4] = c;   r.m[5] = 0.0;
    r.m[6] = 0.0; r.m[7] = 0.0; r.m[8] = 1.0;
    return r;
}

// `Mul` in src/math/matrix.c:10-25: out = a * b, and the original always sums
// the k terms in the order 0, 2, 1 (`(long double)r[0] * b[j] +
// (long double)r[2] * b[6 + j] + (long double)r[1] * b[3 + j]`). The order is
// reproduced; with doubles it changes at most the last bit.
inline Mat3 Mul(const Mat3& a, const Mat3& b) {
    Mat3 t;
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            t.m[i * 3 + j] = a.m[i * 3 + 0] * b.m[0 * 3 + j] +
                             a.m[i * 3 + 2] * b.m[2 * 3 + j] +
                             a.m[i * 3 + 1] * b.m[1 * 3 + j];
        }
    }
    return t;
}

// `mat_elem` (src/statgun/statgun.h:66-70): element (i, j) of a * b, with the
// caller choosing which k term is added LAST. StationaryGun_Aim passes the
// per-element kLast table at statgun.c:227 for the first product and 1 for the
// mount product, so the two products differ only in floating-point summation
// order -- never in the formula. Reproduced verbatim.
inline double Elem(const Mat3& a, int i, const Mat3& b, int j, int last) {
    const int k0 = (last == 0) ? 1 : 0;
    const int k1 = (last == 2) ? 1 : 2;
    return a.m[i * 3 + k0] * b.m[k0 * 3 + j] +
           a.m[i * 3 + k1] * b.m[k1 * 3 + j] +
           a.m[i * 3 + last] * b.m[last * 3 + j];
}

// Matrix_SetEuler HOOK 0x4B38E0, src/math/matrix.c:99-110:
//     Matrix_SetRotZ(m, az); Matrix_SetRotY(&r, ay); Mul(m, m, &r);
//     Matrix_SetRotX(&r, ax); Mul(m, m, &r);
// i.e. Rz(az) * Ry(ay) * Rx(ax). This is also exactly what
// ParamReal32x9_Parse (HOOK 0x4EFBA0, src/physobj/paramops.c:423-435) builds
// from a task's authored `Real32x9` orientation triple, which is the
// `baseMatrix` StationaryGun_Aim starts from.
inline Mat3 SetEuler(double ax, double ay, double az) {
    return Mul(Mul(SetRotZ(az), SetRotY(ay)), SetRotX(ax));
}

// Mesh3D_GetWorldVertex HOOK 0x4D0A50, src/lod/mesh3d.c:429-440: the point is
// scaled by the object's scale (already folded into `v` by the caller) and
// rotated by the matrix, with the rotation's term order 2, 1, 0 on the Z term.
inline Vec3 Rotate(const Mat3& mm, const Vec3& v) {
    return Vec3{
        v.z * mm.m[2] + v.y * mm.m[1] + v.x * mm.m[0],
        v.z * mm.m[5] + v.y * mm.m[4] + v.x * mm.m[3],
        v.z * mm.m[8] + v.y * mm.m[7] + v.x * mm.m[6]};
}

// The model point StationaryGun_Aim reads: index 1, unless the model has fewer
// than 1 point, in which case the engine's own guard (statgun.c:246-248) makes
// it 0 -- and Model_GetPoint returns zero for any out-of-range index anyway
// (src/game/misc.c:195-198), so a model with fewer than two points yields
// (0,0,0) whichever branch it takes. `points` are mesh-local (the editor's
// `Mesh::magicVertices`, MEF units / 40.96).
inline Vec3 ModelPoint1(const Vec3* points, std::size_t count) {
    if (points != nullptr && count > 1) return points[1];
    return Vec3{};
}

// The mounted weapon's world transform.
struct WeaponMount {
    Mat3 rotation;    // `e` after statgun.c:254-257 -- the Gun task's matrix
    Vec3 position;    // `p` after statgun.c:249-252 -- the Gun task's position
    Vec3 modelPoint;  // the gun-model point used, before rotation/scale
};

// Compose the mount exactly as StationaryGun_Aim does. `gunPos` is the gun's
// world position (`o->obj.pos`, the authored ObjectPos), `gunBase` is the
// authored orientation (`o->baseMatrix`, Matrix_SetEuler of the QSC triple),
// `outPitch`/`outYaw` are the gun's runtime aim in radians, and `modelPoint` is
// in mesh-local units with `pointScale` = 40.96 (the editor's mesh-local ->
// engine-unit factor) times the object's own `scale` (the engine multiplies the
// point by `RigidDynCubeObj::scale`, mesh3d.c:434-436).
inline WeaponMount ComposeWeaponMount(const Vec3& gunPos, const Mat3& gunBase,
                                      double outPitch, double outYaw,
                                      const Vec3& modelPoint, double pointScale) {
    static const int kLast[9] = {1, 0, 0, 0, 0, 0, 0, 1, 1};  // statgun.c:227

    // o->obj.matrix = baseMatrix * RotZ(outYaw)     (statgun.c:241-245)
    const Mat3 yaw = SetRotZ(outYaw);
    Mat3 aim;
    for (int i = 0; i < 9; ++i) aim.m[i] = Elem(gunBase, i / 3, yaw, i % 3, kLast[i]);

    // Model_GetPoint + o->obj.pos                   (statgun.c:249-252)
    const Vec3 scaled{modelPoint.x * pointScale, modelPoint.y * pointScale,
                      modelPoint.z * pointScale};
    const Vec3 rotated = Rotate(aim, scaled);

    // Matrix_SetEuler(&e, outPitch, 0, PI)          (statgun.c:253)
    const Mat3 mountLocal = SetEuler(outPitch, 0.0, kMountZ);

    // t = o->obj.matrix * e                          (statgun.c:254-257)
    WeaponMount out;
    out.rotation = Mul(aim, mountLocal);
    out.position = Vec3{gunPos.x + rotated.x, gunPos.y + rotated.y, gunPos.z + rotated.z};
    out.modelPoint = modelPoint;
    return out;
}

}  // namespace statgun
}  // namespace igi
