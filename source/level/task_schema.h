#pragma once
#include <string>
#include <vector>
#include <map>

// Shared QSC task-parameter schema definitions.
//
// Extracted from verify_level.cpp so that both the level verifier and the
// in-editor typed property UI resolve a task type's parameter layout from a
// single source of truth.
namespace TaskSchemaNS {

struct FieldDef {
    std::string name;
    std::string typeName;
    int argOffset = 0;  // absolute index in the Task_New arg list (0 = taskId)
    int argCount  = 1;  // number of Task_New args this field consumes
};
using TaskSchema = std::vector<FieldDef>;

// How many Task_New args one field of a given type consumes (QSC wire format).
// Real32x9 is a 3x3 rotation matrix at runtime but is serialised as 3 Euler
// angles in QSC; all other multi-float types follow their obvious count.
int TypeArgCount(const std::string& typeName);

// Hardcoded schemas for all known IGI task types.  Task_DeclareParameters is a
// compile-time directive stripped from QVM, so QVM-decompiled files never
// contain it; these mirror what the game engine defines.
const std::map<std::string, TaskSchema>& GetBuiltinSchemas();

// Schema for one task type, or nullptr if no builtin schema exists.
const TaskSchema* GetBuiltinSchema(const std::string& taskType);

// --- Runtime schemas parsed from the level's Task_DeclareParameters ---------
// declArgs is the Task_DeclareParameters argument list: [0]=TypeName, then
// (paramName, paramType) pairs. Field argOffsets start at 3 (id, type, note
// occupy 0-2) and advance by TypeArgCount(type). Surrounding quotes are stripped.
TaskSchema ParseDeclaration(const std::vector<std::string>& declArgs);

// Register/clear schemas parsed from the current level. Registered schemas take
// precedence over builtins so the property editor exposes EVERY declared field.
void RegisterSchema(const std::string& taskType, TaskSchema schema);
void ClearRegisteredSchemas();

// Resolve a task type to its schema: registered (level) first, then builtin.
// Returns nullptr if neither exists.
const TaskSchema* GetSchema(const std::string& taskType);

// The QSC argument index of the FIRST declared model slot, or -1 when the
// declaration names none.
//
// A "model slot" is a single-token string field (String16 / String32 /
// String256) whose authored name contains "model" case-insensitively, taken in
// declaration order. The engine's ParamTable does the same thing structurally:
// every task type that draws a model adds one Param_String16 field bound to its
// `FIELD(model)` / `FIELD(holder_model)` member, and the create handler feeds
// that member to RigidDynCubeObj_SetModel / BoneDynCubeObj_SetModel (e.g.
// src/missile/train.c:149, src/props/wire.c:180, src/scamera/scamera.c:240).
//
// This is what lets LevelObjects::Load resolve the model of a task type it has
// no hand-written branch for, exactly as the level file declares it — instead
// of guessing from the token's shape (the old "8-character NNN_NN_N" scan,
// which silently dropped every longer id such as AlarmLight's "1729_01_1").
//
// It returns the slot's index, not a value: callers decide whether an empty or
// sentinel value ("waypoint") should be kept. Types with several model slots
// return the first one (ExplodeObject "Model" before "Destroyed model"; Switch
// "On model" before the other four; SCamera "Holder Model" before the lens).
int FindDeclaredModelArg(const TaskSchema& schema);

// ─────────────────────────────────────────────────────────────────────────────
// Prop / interactable parity maths.
//
// Pure, host-portable helpers so the editor's inspector and viewport preview
// agree with the decompiled game. Every constant cites its IGI-RE file:line;
// see docs/igi-re-parity/PROPS_WIRE_INTERACTABLES.md for the full trace.
// ─────────────────────────────────────────────────────────────────────────────
namespace Props {

// Minimal 3-double vector so this header stays dependency-free: the `igi_mcp`
// target compiles this translation unit without glm on its include path.
// Callers convert from glm::dvec3 with an explicit `Props::Vec3(v.x, v.y, v.z)`.
struct Vec3 {
    double x = 0.0, y = 0.0, z = 0.0;
    Vec3() = default;
    Vec3(double ax, double ay, double az) : x(ax), y(ay), z(az) {}
    Vec3 operator-(const Vec3& o) const { return Vec3(x - o.x, y - o.y, z - o.z); }
    Vec3 operator+(const Vec3& o) const { return Vec3(x + o.x, y + o.y, z + o.z); }
    Vec3 operator*(double s) const { return Vec3(x * s, y * s, z * s); }
    double Length() const;
};

// IGI-RE src/props/wire.c:320-321 — `DATA(5334B0) double kWireScale
// INIT(0.000244140625)` with `ALIAS(kUnitsToMeters, kWireScale, 0)`:
// 1 world unit = 1/4096 m (4096 units per metre).
constexpr double kWireScale = 0.000244140625;  // 1/4096

// IGI-RE src/props/wire.c:322-323 — `DATA(533834) float kWireSlope
// INIT(0.034906585f)` = 2°. Wire_IsSloped (0x43F8A0, wire.c:304-317) compares
// the matrix X-Euler against it.
constexpr double kWireSlope = 0.034906585;

// IGI-RE src/game/misc.c:99 — `static const float kNearlyUp = 0.95f`, used by
// Matrix_SetYAxis (0x414A60, misc.c:129-190) to pick its basis branch.
constexpr double kNearlyUp = 0.95;

// IGI-RE src/weapon/humanplayer.c:136-138 — the rider hangs kWireEnd short of
// each anchor and kZiplineDown below the wire (0.75 m and 0.9 m).
constexpr double kWireEnd = 3072.0;
constexpr double kZiplineDown = 3686.4;

// IGI-RE src/humanstate/humanstate.c:1389-1399 — HumanPlayer_ZiplineUpdate
// (0x40DEF0) integrates
//     speed = -dir.z * 0x1.613d32p+6 + speed * 0.99
// 0x1.613d32p+6 == 88.30976104736328 world units/tick.
constexpr double kZiplineAccel = 88.30976104736328;
constexpr double kZiplineFriction = 0.99;

// IGI-RE src/statgun/statgun.c:113-118 — StationaryGun_CreateHandler defaults.
constexpr const char* kStatGunDefaultWeapon = "WEAPON_ID_M2HB";
constexpr int    kStatGunDefaultAmmo     = -1;
constexpr double kStatGunDefaultMaxPitch = 25.0;
constexpr double kStatGunDefaultMinPitch = -25.0;
constexpr double kStatGunDefaultYawRange = 90.0;
// IGI-RE src/statgun/statgun.c:140-141 — `gStatGunAmmoRange[] = {0, 100000}`.
constexpr int kStatGunAmmoMin = 0;
constexpr int kStatGunAmmoMax = 100000;
// IGI-RE src/statgun/statgun.c:521 — `DATA(53387C) float kTurnStep
// INIT(0.08726647f)` = 5° per tick, the unmanned-gun slew rate (statgun.c:178).
constexpr double kStatGunTurnStep = 0.08726647;

// IGI-RE src/comprun/gunholder.c:121-124 — AIStationaryGunHolder (GunHolder)
// defaults; gunholder.c:141-142 sets gGunHolderRange[] = {0, 100}.
constexpr double kGunHolderDefaultAngleX = 360.0;
constexpr double kGunHolderDefaultAngleY = 360.0;
constexpr double kGunHolderDefaultRange  = 10.0;
constexpr int    kGunHolderDefaultTeam   = 1;
constexpr double kGunHolderRangeMin = 0.0;
constexpr double kGunHolderRangeMax = 100.0;

// Wire geometry resolved from the two authored anchors, using the game's own
// Wire_ParamsChanged (0x43F3D0, IGI-RE src/props/wire.c:160-191) maths.
struct WireSpan {
    Vec3 start{};
    Vec3 end{};
    Vec3 mid{};    // game object position: (start + end) / 2
    Vec3 dir{};    // normalised end - start (local +Y); zero if degenerate
    double length = 0.0;    // |end - start| in world units
    double yScale = 0.0;    // game's obj.scale[1] = length * kWireScale
    double slope  = 0.0;    // matrix X-Euler of the Y-axis frame, radians, [0, 2pi)
    bool   valid  = false;  // false when the two anchors coincide
    bool   isZipline = false;  // slope > kWireSlope -> use kind 9 rides it as a zipline
};

// Resolve a wire's span exactly like the game: midpoint position, +Y axis along
// end - start, Y stretch = length * kWireScale, and the Wire_IsSloped test that
// decides zipline (humanstate.c:958-963) vs hand-over-hand wire.
WireSpan ComputeWireSpan(const Vec3& start, const Vec3& end);

// The rotation the game builds in Wire_ParamsChanged (Matrix_SetYAxis, HOOK
// 0x414A60, IGI-RE src/game/misc.c:129-190): the world directions of the model's
// local axes, with `y` equal to the normalised wire direction. Renderers can
// turn this straight into a column-major basis (glm::mat3(x, y, z)).
struct Basis {
    Vec3 x{}, y{}, z{};
};
Basis YAxisBasis(const Vec3& yAxis);

// Zipline travel direction: IGI-RE src/weapon/humanplayer.c:557-568 takes the
// model point 0 -> point 1 vector and negates it when d.z > 0, so the ride
// always runs downhill. Returns a zero vector when the anchors coincide.
Vec3 ZiplineRideDirection(const Vec3& start, const Vec3& end);

// One tick of HumanPlayer_ZiplineUpdate (humanstate.c:1394-1398).
double ZiplineSpeedStep(double speed, const Vec3& rideDir);
Vec3 ZiplineVelocity(double speed, const Vec3& rideDir);

// Fixed point of ZiplineSpeedStep: kZiplineAccel * -rideDir.z / (1 - friction).
double ZiplineTerminalSpeed(const Vec3& rideDir);

// ── QSC token helpers ───────────────────────────────────────────────────────
// Parse one QSC numeric token. Returns false (leaving `out` untouched) when the
// token is absent or not a number, so a missing/odd authored arg falls back to
// the game's own default instead of becoming 0.
bool ParseDoubleToken(const std::string& token, double& out);
// Strip one layer of surrounding double quotes (argTokens keep them).
std::string UnquoteToken(const std::string& token);

// ── Wire anchors (issue #42 / zipline) ──────────────────────────────────────
// IGI-RE src/props/wire.c:89-113 Wire_Open (0x43EFF0) declares start ObjectPos
// (args 3-5), end ObjectPos (args 6-8), model String16 (arg 9). Returns false —
// leaving start/end untouched — when the End anchor is missing or not numeric.
bool ParseWireAnchors(const std::vector<std::string>& args, Vec3& start, Vec3& end);

// ── StationaryGun (issue #42) ───────────────────────────────────────────────
// IGI-RE src/statgun/statgun.c:142-150 StationaryGun_Open (0x473ED0) param table
// order; offsets below are absolute Task_New arg indices. Degrees fields are
// degrees in the QSC and are converted with kDegToRad by the game
// (StationaryGun_Clamp 0x4748B0, statgun.c:275-299).
struct StationaryGunParams {
    bool valid = false;                          // false: no position block authored
    std::string weapon = kStatGunDefaultWeapon;  // arg@10 String32
    double maxPitch = kStatGunDefaultMaxPitch;   // arg@11 Degrees
    double minPitch = kStatGunDefaultMinPitch;   // arg@12 Degrees
    double yawRange = kStatGunDefaultYawRange;   // arg@13 Degrees, symmetric
    int    ammo     = kStatGunDefaultAmmo;       // arg@14 Int32, 0..100000
};
StationaryGunParams ParseStationaryGunArgs(const std::vector<std::string>& args);

// ── AIStationaryGunHolder ───────────────────────────────────────────────────
// IGI-RE src/comprun/gunholder.c:157-165 GunHolder_Init (0x46E370) param table
// order; gunholder.c:121-126 defaults.
struct GunHolderParams {
    bool valid = false;
    double angleX = kGunHolderDefaultAngleX;  // arg@10 Degrees
    double angleY = kGunHolderDefaultAngleY;  // arg@11 Degrees
    double range  = kGunHolderDefaultRange;   // arg@12 Real32, 0..100
    std::string activeExpr;                   // arg@13 VarString
    std::string teamExpr;                     // arg@14 VarString
};
GunHolderParams ParseGunHolderArgs(const std::vector<std::string>& args);

// ── Ladder placement (issue #68) ────────────────────────────────────────────
// IGI-RE src/props/ladder.c:122-175 Ladder_Use (HOOK 0x4404C0) is the whole
// activation test, src/props/ladder.c:65-93 Ladder_Create (HOOK 0x440280)
// derives the rung count, and src/weapon/humanplayer.c:273-340
// HumanPlayer_UpdateLadder (HOOK 0x4113E0) computes the two mount positions.
// A ladder task owns four XTVM "magic" points and the handlers index them
// 0-based, so point 1 is the activation reference above the ladder, point 2 the
// foot and point 3 the head (ladder.c:86-87, :155, :164; humanplayer.c:300,308).
//
// Every constant below is DATA/static in the retail binary; each citation gives
// the IGI-RE file:line that declares it.

// src/props/ladder.c:53-54 + :200-203 — kReachMin/kReachMax = ∓2867.2 world
// units, applied to the player's local X in ladder.c:138 as the strict band
// kReachMin < localX < kReachMax. 2867.2 units = 70 cm at 4096 units/m.
constexpr double kLadderHalfWidthUnits = 2867.2;

// src/humanbase/human.c:168 `static const float kUseRange = 6.0f` is written
// into HumanUseQuery at human.c:1287-1288 (`q.unk78 = 0; q.range = kUseRange;`):
// an int 0 at +0x78 followed by that float at +0x7C. Ladder_Use reads q->range
// through the UseQuery/ViewQuery view (src/props/propstypes.h:14,
// src/game/game.c:190), where `range` is the *double* at +0x78, so the eight
// bytes are 0x40C0000000000000 and the value Ladder_Use compares in ladder.c:138
// is exactly 8192.0 world units = 2 m. That agrees with the 8192.0 in issue #68.
constexpr double kLadderActivateRadiusUnits = 8192.0;

// src/props/ladder.c:55 declares kLadderAngle; src/plane/gear.c:156-157 makes it
// ALIAS(kLadderAngle, kQuarterPi) = 0.7853982f rad = 45°. Vec3_Angle (HOOK
// 0x4B2B30, src/file/vec.c:189-208) returns the *angle* between two vectors, so
// the facing gate is angle(view column 1, ladder column 1) < 45°. The cosine
// form (0.70710678) that issue #68 quotes is the equivalent unit-vector test.
// The retail value is a float: (float)0.7853982 == 0.78539818525314331, a hair
// above pi/4, so an exactly-45° view still passes ladder.c:157.
constexpr double kLadderFacingAngleRadians = 0.7853982;
constexpr double kLadderFacingCosine = 0.70710678118654752;

// src/weapon/humanplayer.c:130-133 — the four mount offsets. The bottom mount
// adds kLadderBack along the ladder matrix's +Y column and kLadderUp along its
// +Z column (humanplayer.c:312-314); the top mount adds kLadderTopBack along +Z
// and subtracts kLadderTopUp along +Y (humanplayer.c:304-306).
constexpr double kLadderBottomStandoffUnits = 1515.52;   // +Y, 37 cm
constexpr double kLadderBottomLiftUnits     = 3915.776;  // +Z, 95.6 cm
constexpr double kLadderTopBackUnits        = 3481.6;    // +Z, 85 cm
constexpr double kLadderTopStandoffUnits    = 1638.4;    // -Y, 40 cm

// src/props/ladder.c:52 + DATA(533860) kRungScale INIT(0.0008138020833333334)
// = 1 / 1228.8; ladder.c:86-91 rounds |point2.z - point3.z| * kRungScale.
constexpr double kLadderRungSpacingUnits = 1228.8;   // 30 cm
// src/weapon/humanplayer.c:323 and src/humanstate/humanstate.c:1182 — the climb
// stops 6 rungs short of the top (`obj->steps - 6`).
constexpr int kLadderTopStepMargin = 6;
// src/props/ladder.c:59-63 Fence_GetClimbTime + DATA(533528) kClimbTime = 6.0f.
constexpr double kLadderClimbTimeSeconds = 6.0;

struct LadderPlacement {
    bool valid = false;
    Vec3 origin{};         // ladder task position (`l->obj.pos`)
    Vec3 activationTop{};  // magic point 1 in world space (ladder.c:155-156)
    Vec3 foot{};           // magic point 2, world
    Vec3 head{};           // magic point 3, world
    Vec3 axisX{}, axisY{}, axisZ{};  // ladder matrix columns 0/1/2 in world
    Vec3 bottomMount{};    // humanplayer.c:312-314
    Vec3 topMount{};       // humanplayer.c:304-306
    int rungCount = 1;     // `obj->steps`, ladder.c:91
    int topStep = 0;       // rungCount - kLadderTopStepMargin, floored at 0
    double playerYawRadians = 0.0;  // Matrix_GetEuler(m).z + pi (humanplayer.c:320-322)
};

// Build the placement from the four authored points and the ladder's orientation
// columns. The points must already be world space — what Model_GetPoint returns
// (model point rotated/scaled by the object matrix) plus the object position,
// per its callers at src/missile/missile.c:316-322 and ladder.c:155-156.
// `foot`/`head` are the climb line and `rungCount` is its rung count, using the
// *local* Z difference exactly as ladder.c:88 does. `valid` stays false when the
// orientation basis is degenerate.
LadderPlacement ComputeLadderPlacement(const Vec3& origin, const Vec3& activationTop,
                                       const Vec3& foot, const Vec3& head,
                                       const Basis& orientation);

// The two offers Ladder_Use can make. `bottom` mirrors ladder.c:138-162 and
// `top` mirrors ladder.c:164-173; the top block runs unconditionally, which is
// why the bottom branch clears the top flag first (ladder.c:161).
struct LadderActivation {
    bool bottom = false;
    bool top = false;
};

// `playerEye` is the human's eye position (`q->pos`, human.c:1283) and
// `viewAxisY` is column 1 of the view matrix (`q->matrix.m[1],m[4],m[7]`,
// ladder.c:143-145). Both are world space.
LadderActivation LadderActivate(const LadderPlacement& ladder, const Vec3& playerEye,
                                const Vec3& viewAxisY);

} // namespace Props

} // namespace TaskSchemaNS
