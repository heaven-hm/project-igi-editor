/******************************************************************************
 * @file    test_statgun_mount.cpp
 * @brief   A placed `StationaryGun`'s MOUNTED WEAPON.
 *
 * WHAT IS PROVED HERE, in increasing order of strength:
 *
 *   1. the composition in source/runtime/statgun_mount.h reproduces the engine's
 *      product *differentially*: a second, independent transcription of the same
 *      oracle lines (expanded element formulae, a different floating-point sum
 *      order) is compared element by element;
 *   2. the closed-form consequence of that product -- a gun at rest mounts its
 *      weapon at `baseMatrix * RotZ(PI)`, so the mount adds exactly PI about Z;
 *      and the gun BODY is never pitched, only the weapon;
 *   3. the shipped data: the real LEVEL4 StationaryGun record, the real M2HB
 *      mount model's point count, and the retail WEAPONS/<NAME>/WEAPON.QVM model
 *      argument, read read-only from the install (IGI_GAME_PATH; self-skips).
 *
 * ORACLE. /Users/haseeb-mir/Documents/Code/IGI-RE (read-only), cited per line:
 *   src/statgun/statgun.c    StationaryGun_Changed 0x474980 (:310-340),
 *                            StationaryGun_Aim 0x474420 (:225-272),
 *                            param table/Open 0x473ED0 (:142-150)
 *   src/vehbase/weapon.c     Weapon_Move 0x4F2310 (:35-40)
 *   src/guncasing/gun.c      Gun_MoveHandler 0x478A90 (:510-526),
 *                            Gun_ParamChangedHandler 0x4789C0 (:479-507)
 *   src/math/matrix.c        Mul (:10-25), Matrix_SetEuler 0x4B38E0 (:99-110),
 *                            Matrix_SetRotX/Y/Z 0x4B3B60/0x4B3BA0/0x4B3BE0
 *   src/statgun/statgun.h    mat_elem (:66-70)
 *   src/lod/mesh3d.c         Mesh3D_GetWorldVertex 0x4D0A50 (:420-441),
 *                            ModelLod_GetVertexCount 0x4D0D20 (:505-509)
 *   src/game/misc.c          Model_GetPoint 0x414E20 (:192-200)
 *   src/physobj/paramops.c   ParamReal32x9_Parse 0x4EFBA0 (:423-435)
 *   src/model/modeltypes.h   MeshHeader (vertex_count at +0x7C)
 *
 * No GL, no windows.h, no level archive: the first two groups are pure maths and
 * always run; the corpus group reads files and self-skips without IGI_GAME_PATH.
 *****************************************************************************/

#include <gtest/gtest.h>

#include "level/task_schema.h"
#include "lod/mef_mesh_header.h"
#include "runtime/statgun_mount.h"
#include "runtime/weapon_config.h"
#include "runtime/weapon_model_lookup.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace {

using igi::statgun::Mat3;
using igi::statgun::Vec3;

constexpr double kPi = 3.14159265358979323846;

// `0x1.921fb6p+1f` -- the FLOAT PI StationaryGun_Aim passes to Matrix_SetEuler
// (src/statgun/statgun.c:253). Not the double PI: 3.141592653589793.
constexpr double kFloatPi = 3.1415927410125732421875;

// The authored orientation of the shipped LEVEL4 StationaryGun
// (MISSIONS/location0/LEVEL4/objects.qsc:482, Task_New(775, "StationaryGun",
// ... 0, 0, 4.4103131294250488 ...)); the corpus test below re-reads it.
constexpr double kLevel4Az = 4.4103131294250488;

// The authored position of the same record, in world units.
constexpr double kLevel4X = 112915288.0;
constexpr double kLevel4Y = -56117940.0;
constexpr double kLevel4Z = 175294512.0;

// ── The independent reference transcription ─────────────────────────────────
//
// Deliberately NOT built on igi::statgun::*: it expands the same oracle lines as
// write each element, and sums k in the order 0, 1, 2 (the engine's Mul sums
// 0, 2, 1 -- src/math/matrix.c:10-25 and mat_elem at src/statgun/statgun.h:66),
// so agreement is not one function comparing itself.

struct RefMat {
    double m[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
};

RefMat RefMul(const RefMat& a, const RefMat& b) {
    RefMat t;
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            t.m[i * 3 + j] = a.m[i * 3 + 0] * b.m[0 * 3 + j] +
                             a.m[i * 3 + 1] * b.m[1 * 3 + j] +
                             a.m[i * 3 + 2] * b.m[2 * 3 + j];
        }
    }
    return t;
}

RefMat RefRotX(double a) {
    const double c = std::cos(a), s = std::sin(a);
    return RefMat{{1, 0, 0, 0, c, -s, 0, s, c}};
}
RefMat RefRotY(double a) {
    const double c = std::cos(a), s = std::sin(a);
    return RefMat{{c, 0, s, 0, 1, 0, -s, 0, c}};
}
RefMat RefRotZ(double a) {
    const double c = std::cos(a), s = std::sin(a);
    return RefMat{{c, -s, 0, s, c, 0, 0, 0, 1}};
}

// Matrix_SetEuler: Rz(az) * Ry(ay) * Rx(ax) (src/math/matrix.c:99-110).
RefMat RefEuler(double ax, double ay, double az) {
    return RefMul(RefMul(RefRotZ(az), RefRotY(ay)), RefRotX(ax));
}

// Mesh3D_GetWorldVertex: world = M * local, expanded (src/lod/mesh3d.c:432-438).
Vec3 RefRotate(const RefMat& mm, const Vec3& v) {
    return Vec3{v.x * mm.m[0] + v.y * mm.m[1] + v.z * mm.m[2],
                v.x * mm.m[3] + v.y * mm.m[4] + v.z * mm.m[5],
                v.x * mm.m[6] + v.y * mm.m[7] + v.z * mm.m[8]};
}

struct RefMount {
    RefMat rotation;
    Vec3 position;
};

RefMount RefCompose(const Vec3& gunPos, const RefMat& base, double pitch, double yaw,
                    const Vec3& point, double pointScale) {
    RefMount out;
    const RefMat aim = RefMul(base, RefRotZ(yaw));            // statgun.c:241-245
    const Vec3 scaled{point.x * pointScale, point.y * pointScale, point.z * pointScale};
    const Vec3 moved = RefRotate(aim, scaled);                // mesh3d.c:432-438
    out.position = Vec3{gunPos.x + moved.x, gunPos.y + moved.y, gunPos.z + moved.z};
    const RefMat mount = RefEuler(pitch, 0.0, kFloatPi);      // statgun.c:253
    out.rotation = RefMul(aim, mount);                        // statgun.c:254-257
    return out;
}

void ExpectMatNear(const Mat3& actual, const RefMat& expected, double tol) {
    for (int i = 0; i < 9; ++i) {
        EXPECT_NEAR(actual.m[i], expected.m[i], tol) << "element " << i;
    }
}

// ── Corpus helpers ──────────────────────────────────────────────────────────

std::string GameRoot() {
    const char* env = std::getenv("IGI_GAME_PATH");
    if (env != nullptr && *env != '\0') return std::string(env);
    return std::string();
}

bool FileExists(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return f.good();
}

std::vector<uint8_t> ReadFileBytes(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)),
                                std::istreambuf_iterator<char>());
}

std::string Trim(const std::string& s) {
    const size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return std::string();
    const size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

// The ILFF MESH chunk's `vertex_count` (MeshHeader +0x7C, src/model/modeltypes.h:24)
// and the MEF_MVTX ("XTVM") point count, via the engine's own chunk walk that
// source/lod/mef_mesh_header.h already implements and the corpus tests use.
struct MefPointInfo {
    bool found = false;
    int vertex_count = 0;
    size_t xtvm_count = 0;
};

MefPointInfo ReadMefPointInfo(const std::vector<uint8_t>& bytes) {
    using namespace igi::lod::mef_detail;
    MefPointInfo out;
    if (bytes.size() < kIlffHeaderSize || std::memcmp(bytes.data(), "ILFF", 4) != 0) return out;
    Walker w{bytes.data(), bytes.size()};
    for (size_t c = w.firstChild(0); c != Walker::kNo; c = w.next(c)) {
        const uint32_t id = ReadU32(bytes.data() + c);
        const size_t d = w.data(c, 0);
        if (id == 0x4D455348u /* "HSEM" MEF_MESH */) {
            if (d == Walker::kNo || !w.has(d, 0x7E)) continue;
            int16_t vc = 0;
            std::memcpy(&vc, bytes.data() + d + 0x7C, sizeof(vc));
            out.vertex_count = vc;
            out.found = true;
        } else if (id == 0x4D565458u /* "XTVM" MEF_MVTX */) {
            // 16-byte records: xyz + one word (src/renderer/mef_native.cpp:790-805).
            out.xtvm_count = ReadU32(bytes.data() + c + 0x04) / 16u;
        }
    }
    return out;
}

// The shipped LEVEL4 `Task_New(...)` arguments for the StationaryGun, split on
// the top-level commas of its QSC line.
struct Level4Record {
    bool found = false;
    std::vector<std::string> args;
};

Level4Record ReadLevel4StationaryGun(const std::string& root) {
    Level4Record out;
    const std::string path = root + "/MISSIONS/location0/LEVEL4/objects.qsc";
    std::ifstream f(path);
    if (!f.good()) return out;
    std::string line;
    while (std::getline(f, line)) {
        if (line.find("Task_New(") == std::string::npos) continue;
        if (line.find("\"StationaryGun\"") == std::string::npos) continue;
        // Split the argument list; no authored argument here contains a comma or
        // a nested call, which the assertions below re-check by shape.
        const size_t open = line.find('(');
        const size_t close = line.rfind(')');
        if (open == std::string::npos || close == std::string::npos || close < open) return out;
        std::string body = line.substr(open + 1, close - open - 1);
        std::string cur;
        for (char ch : body) {
            if (ch == ',') {
                out.args.push_back(Trim(cur));
                cur.clear();
            } else {
                cur.push_back(ch);
            }
        }
        out.args.push_back(Trim(cur));
        out.found = true;
        return out;
    }
    return out;
}

std::string Unquote(const std::string& s) {
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"') return s.substr(1, s.size() - 2);
    return s;
}

double ToDouble(const std::string& s) { return std::atof(s.c_str()); }

}  // namespace

// ===========================================================================
// 1. The mount's constants and primitives
// ===========================================================================

// StationaryGun_Aim passes `0x1.921fb6p+1f` (a float) as the mount's Z angle
// (src/statgun/statgun.c:253); it is NOT the double PI.
TEST(StatGunMount, MountAngleIsTheEnginesFloatPi) {
    EXPECT_DOUBLE_EQ(igi::statgun::kMountZ, kFloatPi);
    EXPECT_DOUBLE_EQ(igi::statgun::kMountZ, (double)3.14159274f);
    EXPECT_NE(igi::statgun::kMountZ, kPi);
    EXPECT_NEAR(igi::statgun::kMountZ, kPi, 1e-7);
}

// Matrix_SetRotZ/RotX/RotY (0x4B3BE0/0x4B3B60/0x4B3BA0, matrix.c:112-164) build
// the ordinary right-handed matrices for `world = M * local`, the convention
// Mesh3D_GetWorldVertex uses (mesh3d.c:432-438).
TEST(StatGunMount, RotationPrimitivesMatchTheEngineRows) {
    const Mat3 id = igi::statgun::SetEuler(0.0, 0.0, 0.0);
    for (int i = 0; i < 9; ++i) EXPECT_DOUBLE_EQ(id.m[i], (i % 4 == 0) ? 1.0 : 0.0);

    // Rz(+90 deg) maps local +X to world +Y: column 0 is (m[0], m[3], m[6]).
    const Mat3 rz = igi::statgun::SetRotZ(kPi / 2);
    EXPECT_NEAR(rz.m[0], 0.0, 1e-12);
    EXPECT_NEAR(rz.m[3], 1.0, 1e-12);
    EXPECT_NEAR(rz.m[6], 0.0, 1e-12);

    // Rx(+90 deg) maps local +Y to world +Z: column 1 is (m[1], m[4], m[7]).
    const Mat3 rx = igi::statgun::SetRotX(kPi / 2);
    EXPECT_NEAR(rx.m[1], 0.0, 1e-12);
    EXPECT_NEAR(rx.m[4], 0.0, 1e-12);
    EXPECT_NEAR(rx.m[7], 1.0, 1e-12);

    // Matrix_SetEuler = Rz * Ry * Rx (matrix.c:99-110): with only az set it is
    // Rz(az); with only ax, Rx(ax).
    const Mat3 rz_only = igi::statgun::SetEuler(0.0, 0.0, 0.7);
    ExpectMatNear(rz_only, RefRotZ(0.7), 1e-15);
    const Mat3 rx_only = igi::statgun::SetEuler(0.7, 0.0, 0.0);
    ExpectMatNear(rx_only, RefRotX(0.7), 1e-15);
}

// ===========================================================================
// 2. The composition, differentially
// ===========================================================================

// The general case: an aimed gun with a real (non-zero) model point. The two
// independent transcriptions must agree to double precision.
TEST(StatGunMount, DifferentialAgainstTheOracleLines) {
    const Vec3 gunPos{kLevel4X, kLevel4Y, kLevel4Z};
    const Mat3 base = igi::statgun::SetEuler(0.0, 0.0, kLevel4Az);
    const RefMat refBase = RefEuler(0.0, 0.0, kLevel4Az);

    struct Case {
        double pitch;
        double yaw;
        Vec3 point;
    };
    const Case cases[] = {
        {0.0, 0.0, Vec3{0.0, 0.0, 0.0}},              // shipped model point count == 1 -> zero
        {0.0, 0.0, Vec3{0.37, -1.5, 11.25}},          // a real mount offset, synthetic
        {0.35, -0.2, Vec3{0.37, -1.5, 11.25}},        // user aim, pitched and yawed
        {-0.4363323129985824, 1.2, Vec3{-2.0, 0.5, 3.0}},  // minPitch (-25 deg) + yaw
    };
    for (const Case& c : cases) {
        const double pointScale = 40.96;
        const igi::statgun::WeaponMount got = igi::statgun::ComposeWeaponMount(
            gunPos, base, c.pitch, c.yaw, c.point, pointScale);
        const RefMount want = RefCompose(gunPos, refBase, c.pitch, c.yaw, c.point, pointScale);

        ExpectMatNear(got.rotation, want.rotation, 1e-12);
        // The authored coordinates are ~1.7e8 world units, so the position is
        // compared at the precision of that magnitude, not at 1e-12.
        EXPECT_NEAR(got.position.x, want.position.x, 1e-6);
        EXPECT_NEAR(got.position.y, want.position.y, 1e-6);
        EXPECT_NEAR(got.position.z, want.position.z, 1e-6);
    }
}

// At rest (outPitch = outYaw = 0, the state StationaryGun_Changed leaves,
// src/statgun/statgun.c:319-320) the mount is `baseMatrix * Rz(PI)`. Because the
// mount's Z angle is the FLOAT PI, the closed form is Rz(az + float-PI).
TEST(StatGunMount, RestMountAddsExactlyPiAboutZ) {
    const Mat3 base = igi::statgun::SetEuler(0.0, 0.0, kLevel4Az);
    const igi::statgun::WeaponMount mount = igi::statgun::ComposeWeaponMount(
        Vec3{kLevel4X, kLevel4Y, kLevel4Z}, base, 0.0, 0.0, Vec3{}, 40.96);
    ExpectMatNear(mount.rotation, RefRotZ(kLevel4Az + kFloatPi), 1e-12);
    EXPECT_NEAR(mount.position.x, kLevel4X, 1e-9);
    EXPECT_NEAR(mount.position.y, kLevel4Y, 1e-9);
    EXPECT_NEAR(mount.position.z, kLevel4Z, 1e-9);
}

// The gun BODY is never pitched: StationaryGun_Aim writes only RotZ(outYaw) into
// o->obj.matrix (src/statgun/statgun.c:241) and the pitch appears solely in the
// weapon mount `e` (:253-257). Distorting this would tilt the tripod in the
// editor while the engine leaves it upright.
TEST(StatGunMount, OnlyTheWeaponIsPitchedTheBodyKeepsItsYawOnly) {
    const Vec3 gunPos{0.0, 0.0, 0.0};
    const Mat3 base = igi::statgun::SetEuler(0.0, 0.0, 0.5);
    const double pitch = 0.4;
    const double yaw = 0.25;

    const igi::statgun::WeaponMount mount = igi::statgun::ComposeWeaponMount(
        gunPos, base, pitch, yaw, Vec3{}, 40.96);

    // body = base * Rz(yaw)  (statgun.c:241-245)
    const Mat3 body = igi::statgun::Mul(base, igi::statgun::SetRotZ(yaw));
    ExpectMatNear(body, RefMul(RefEuler(0.0, 0.0, 0.5), RefRotZ(yaw)), 1e-15);
    // The mount is body * Rz(PI) * Rx(pitch), i.e. the body itself is untouched.
    const Mat3 expected = igi::statgun::Mul(body, igi::statgun::SetEuler(pitch, 0.0, kFloatPi));
    for (int i = 0; i < 9; ++i) EXPECT_NEAR(mount.rotation.m[i], expected.m[i], 1e-15);
    // With pitch != 0 the weapon is NOT the body any more.
    double diff = 0.0;
    for (int i = 0; i < 9; ++i) diff += std::fabs(mount.rotation.m[i] - body.m[i]);
    EXPECT_GT(diff, 0.1);
}

// Model_GetPoint's index rule (src/statgun/statgun.c:246-249 plus
// src/game/misc.c:195-198): index 1, and an out-of-range index yields zero.
TEST(StatGunMount, ModelPointRuleIsIndexOneOrZero) {
    const Vec3 pts[3] = {Vec3{9, 9, 9}, Vec3{1, 2, 3}, Vec3{4, 5, 6}};
    EXPECT_DOUBLE_EQ(igi::statgun::ModelPoint1(nullptr, 0).x, 0.0);
    EXPECT_DOUBLE_EQ(igi::statgun::ModelPoint1(pts, 0).x, 0.0);
    // One point: the engine's idx stays 1, Model_GetPoint rejects 1 < 1, so zero.
    EXPECT_DOUBLE_EQ(igi::statgun::ModelPoint1(pts, 1).x, 0.0);
    EXPECT_DOUBLE_EQ(igi::statgun::ModelPoint1(pts, 2).x, 1.0);
    EXPECT_DOUBLE_EQ(igi::statgun::ModelPoint1(pts, 2).z, 3.0);
    EXPECT_DOUBLE_EQ(igi::statgun::ModelPoint1(pts, 3).y, 2.0);
}

// The property panel keys a task's schema on the task TYPE the level authors
// (renderer_draw.cpp collects child schemas with GetSchema(child.type)), and a
// null schema renders no rows at all. The engine's mounted-gun task type is
// "StationaryGun" -- TaskType_New("StationaryGun", ...),
// StationaryGun_Open HOOK 0x473ED0, src/statgun/statgun.c:126 -- and its schema
// carries all eight ParamTable rows (StationaryGun_Open, statgun.c:142-150),
// including "Weapon" at arg 10. "StatGun" and "SentryGun" are not task types:
// IGI-RE has no such symbol (the only mounted-gun source is statgun.c) and no
// shipped level authors a Task_New of either name; SentryGun is a WEAPON
// definition (WEAPONS/SENTRYGUN/WEAPON.QVM, WEAPON_ID_SENTRY) consumed through
// this same StationaryGun path, not a task type of its own.
TEST(StatGunMount, TheMountedGunTaskTypeIsStationaryGunAndItsSchemaHasTheWeaponRow) {
    using namespace TaskSchemaNS;
    const TaskSchema* sc = GetBuiltinSchema("StationaryGun");
    ASSERT_NE(sc, nullptr);
    bool has_weapon = false;
    bool has_model = false;
    for (const FieldDef& f : *sc) {
        if (f.name == "Weapon") {
            has_weapon = true;
            EXPECT_EQ(f.argOffset, 10);          // StationaryGun_Open statgun.c:146
            EXPECT_EQ(f.typeName, "String32");
        }
        if (f.name == "Model") {
            has_model = true;
            EXPECT_EQ(f.argOffset, 9);           // statgun.c:145
        }
    }
    EXPECT_TRUE(has_weapon);
    EXPECT_TRUE(has_model);
    // Not task types -> not schema keys. A placement of either name does not
    // exist in the engine or the shipped data, so no panel lookup is owed.
    EXPECT_EQ(GetBuiltinSchema("StatGun"), nullptr);
    EXPECT_EQ(GetBuiltinSchema("SentryGun"), nullptr);
}

// ===========================================================================
// 3. The shipped data (self-skips without IGI_GAME_PATH)
// ===========================================================================

// The fixture values above are the shipped ones, re-read from the level script.
// MISSIONS/location0/LEVEL4/objects.qsc:482 is the only text-authored
// StationaryGun in the retail tree; the record is
//   Task_New(775, "StationaryGun", "", 112915288.0, -56117940.0, 175294512.0,
//            0, 0, 4.4103131294250488, "123_02_1", "WEAPON_ID_M2HB",
//            25.0, -25.0, 180.0, -1)
TEST(StatGunMountCorpus, TheShippedLevel4RecordIsTheFixture) {
    const std::string root = GameRoot();
    if (root.empty()) GTEST_SKIP() << "set IGI_GAME_PATH to a retail install to run this test";
    const Level4Record rec = ReadLevel4StationaryGun(root);
    if (!rec.found) GTEST_SKIP() << root << "/MISSIONS/location0/LEVEL4/objects.qsc has no StationaryGun";

    // args: 0 id, 1 type, 2 note, 3/4/5 ObjectPos, 6/7/8 Real32x9, 9 model,
    // 10 weapon, 11 maxPitch, 12 minPitch, 13 yawRange, 14 ammo
    // (StationaryGun_Open 0x473ED0, src/statgun/statgun.c:142-150).
    ASSERT_GE(rec.args.size(), 15u);
    EXPECT_EQ(Unquote(rec.args[1]), "StationaryGun");
    EXPECT_DOUBLE_EQ(ToDouble(rec.args[3]), kLevel4X);
    EXPECT_DOUBLE_EQ(ToDouble(rec.args[4]), kLevel4Y);
    EXPECT_DOUBLE_EQ(ToDouble(rec.args[5]), kLevel4Z);
    EXPECT_DOUBLE_EQ(ToDouble(rec.args[6]), 0.0);
    EXPECT_DOUBLE_EQ(ToDouble(rec.args[7]), 0.0);
    EXPECT_DOUBLE_EQ(ToDouble(rec.args[8]), kLevel4Az);
    EXPECT_EQ(Unquote(rec.args[9]), "123_02_1");       // the holder model
    EXPECT_EQ(Unquote(rec.args[10]), "WEAPON_ID_M2HB"); // the mounted weapon
}

// The mount position is the gun model's point 1 (statgun.c:246-249). The shipped
// M2HB holder model 123_02_1 has ONE point, so the engine's index-1 read is out
// of range and Model_GetPoint returns zero (src/game/misc.c:195-198): the weapon
// sits at the gun's own position. This is read out of the shipped .mef, so the
// editor's decision to use (0,0,0) is the engine's, not a guess.
TEST(StatGunMountCorpus, TheShippedHolderModelHasOnePointSoTheMuzzleIsTheGunPosition) {
    const std::string root = GameRoot();
    if (root.empty()) GTEST_SKIP() << "set IGI_GAME_PATH to a retail install to run this test";
    const std::string mef =
        root + "/MISSIONS/location0/LEVEL4/MODELS/models/123_02_1.mef";
    if (!FileExists(mef)) GTEST_SKIP() << mef << " not found";

    const std::vector<uint8_t> bytes = ReadFileBytes(mef);
    const igi::lod::MefMeshHeader header = igi::lod::ReadMefMeshHeader(bytes);
    ASSERT_TRUE(header.valid) << mef;

    const MefPointInfo points = ReadMefPointInfo(bytes);
    ASSERT_TRUE(points.found);
    EXPECT_EQ(points.vertex_count, 1);   // MeshHeader +0x7C
    EXPECT_EQ(points.xtvm_count, 1u);    // MEF_MVTX records, 16 bytes each
    // The engine's count is the MVTX count, so the two agree on the shipped file.
    EXPECT_EQ((size_t)points.vertex_count, points.xtvm_count);

    // One point -> index 1 is out of range -> zero, and the composed weapon
    // position is exactly the authored gun position. (The lone point's value is
    // irrelevant: the engine never reads index 0 here.)
    const Vec3 onePoint[1] = {Vec3{0.0, 0.0, 11.22179925441742}};  // XTVM point 0 / 40.96
    const Vec3 noPoint = igi::statgun::ModelPoint1(onePoint, points.xtvm_count);
    EXPECT_DOUBLE_EQ(noPoint.x, 0.0);
    EXPECT_DOUBLE_EQ(noPoint.y, 0.0);
    EXPECT_DOUBLE_EQ(noPoint.z, 0.0);
    const igi::statgun::WeaponMount mount = igi::statgun::ComposeWeaponMount(
        Vec3{kLevel4X, kLevel4Y, kLevel4Z},
        igi::statgun::SetEuler(0.0, 0.0, kLevel4Az), 0.0, 0.0, noPoint, 40.96);
    EXPECT_DOUBLE_EQ(mount.position.x, kLevel4X);
    EXPECT_DOUBLE_EQ(mount.position.y, kLevel4Y);
    EXPECT_DOUBLE_EQ(mount.position.z, kLevel4Z);
}

// The authored `weapon` string resolves to the model the engine would load:
// WeaponType_Find (0x413BD0) -> `wt->key` (DefineWeaponType argument 29) ->
// VirModel_Find (src/guncasing/gun.c:506). Both shipped mounted-gun weapons are
// pinned: M2HB, the default, and SENTRY, the sentry-gun definition the StatGun
// path also consumes (WEAPONS/SENTRYGUN/WEAPON.QVM defines WEAPON_ID_SENTRY).
TEST(StatGunMountCorpus, TheAuthoredWeaponResolvesToTheWeaponTypesModel) {
    const std::string root = GameRoot();
    if (root.empty()) GTEST_SKIP() << "set IGI_GAME_PATH to a retail install to run this test";

    struct Expectation {
        const char* path;
        const char* enum_name;
        const char* model;
    };
    const Expectation expectations[] = {
        {"M2HB", "WEAPON_ID_M2HB", "123_01_1"},
        {"SENTRYGUN", "WEAPON_ID_SENTRY", "456_13_1"},
    };
    for (const Expectation& e : expectations) {
        const std::string qvm = root + "/WEAPONS/" + e.path + "/WEAPON.QVM";
        if (!FileExists(qvm)) GTEST_SKIP() << qvm << " not found";

        // The weapon type itself, read with the editor's own DefineWeaponType
        // argument reader (argument 1 is the id constant, argument 29 the model;
        // src/weapon/weapontype.c:287-360).
        const igi::weapons::CallArgs call =
            igi::weapons::ReadCallArgsFromQvmFile(qvm, "DefineWeaponType");
        ASSERT_TRUE(call.found) << qvm << ": " << call.error;
        const igi::weapons::WeaponTypeConfig cfg =
            igi::weapons::ParseWeaponTypeArgs(call.args);
        EXPECT_EQ(cfg.Name(), e.enum_name) << qvm;
        EXPECT_EQ(cfg.Model(), e.model) << qvm;

        // The editor's resolution used by ResolveMountedWeapon must be that same
        // model, not the pickup mapping's word for it.
        EXPECT_EQ(igi::weapons::FindWeaponModelForEnum(root, e.enum_name), e.model);
        EXPECT_EQ(igi::weapons::FindWeaponModelForEnum(root, "WEAPON_ID_NOT_A_WEAPON"), "");
    }
}

// The M2HB mount is the weapon the shipped LEVEL4 placement draws; the same
// composition with the resolved weapon model is therefore the record's drawn
// weapon transform. This is the end-to-end link: authored args -> resolved
// weapon model -> composed transform, all from shipped data.
TEST(StatGunMountCorpus, TheShippedPlacementComposesTheShippedWeapon) {
    const std::string root = GameRoot();
    if (root.empty()) GTEST_SKIP() << "set IGI_GAME_PATH to a retail install to run this test";
    const Level4Record rec = ReadLevel4StationaryGun(root);
    if (!rec.found) GTEST_SKIP() << "no StationaryGun record in LEVEL4";
    ASSERT_GE(rec.args.size(), 15u);

    const std::string weapon = Unquote(rec.args[10]);
    const std::string model = igi::weapons::FindWeaponModelForEnum(root, weapon);
    ASSERT_EQ(model, "123_01_1");  // WEAPONS/M2HB/WEAPON.QVM argument 29

    const MefPointInfo points = ReadMefPointInfo(
        ReadFileBytes(root + "/MISSIONS/location0/LEVEL4/MODELS/models/" +
                      Unquote(rec.args[9]) + ".mef"));
    const Vec3 onePoint[1] = {Vec3{0.0, 0.0, 11.22179925441742}};  // XTVM point 0 / 40.96
    const Vec3 point = igi::statgun::ModelPoint1(onePoint, points.xtvm_count);

    const igi::statgun::WeaponMount mount = igi::statgun::ComposeWeaponMount(
        Vec3{ToDouble(rec.args[3]), ToDouble(rec.args[4]), ToDouble(rec.args[5])},
        igi::statgun::SetEuler(ToDouble(rec.args[6]), ToDouble(rec.args[7]),
                               ToDouble(rec.args[8])),
        0.0, 0.0, point, 40.96);
    // The drawn weapon's rotation is the authored base turned by PI, and its
    // position is the authored gun position.
    ExpectMatNear(mount.rotation, RefRotZ(kLevel4Az + kFloatPi), 1e-12);
    EXPECT_DOUBLE_EQ(mount.position.x, ToDouble(rec.args[3]));
    EXPECT_DOUBLE_EQ(mount.position.y, ToDouble(rec.args[4]));
    EXPECT_DOUBLE_EQ(mount.position.z, ToDouble(rec.args[5]));
}
