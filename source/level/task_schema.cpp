#include "level/task_schema.h"

#include <cmath>

namespace TaskSchemaNS {

int TypeArgCount(const std::string& t) {
    // "ObjectPos" is an alias of "Real64x3" in the game (IGI-RE
    // src/physobj/paramops.c:183 — kParamAliases {"ObjectPos", &gParamObjectPos,
    // &gParamReal64x3}); both consume three script args (Param_Three, paramops.c:143).
    if (t == "ObjectPos" || t == "Real64x3" || t == "Real32x9" || t == "RGB" ||
        t == "Real32x3"  || t == "Colour")   return 3;
    return 1;   // String16, VarString, Real32, Int32, Bool, …
}

const std::map<std::string, TaskSchema>& GetBuiltinSchemas() {
    static const std::map<std::string, TaskSchema> schemas = [] {
        std::map<std::string, TaskSchema> s;

        auto add = [](TaskSchema& sc, const char* name, const char* type, int off) {
            FieldDef f; f.name = name; f.typeName = type;
            f.argOffset = off; f.argCount = TypeArgCount(type);
            sc.push_back(f);
        };

        // Building / EditRigidObj / Terminal: pos@3, ori@6, model@9
        for (const char* t : {"Building","EditRigidObj","Terminal"}) {
            add(s[t], "Position",    "ObjectPos", 3);
            add(s[t], "Orientation", "Real32x9",  6);
            add(s[t], "Model",       "String16",  9);
        }
        // HumanSoldier types: pos@3, gamma(1 float)@6, model@7, then AI params 8..10.
        // Confirmed against real objects.qsc data: arg9 is a small index that always
        // matches an existing common/ANIMS/<NNN>.IFF file (Bone Hierarchy); arg10 is an
        // arbitrary animation_id within that file (Stand Animation), or -1 for none.
        for (const char* t : {"HumanSoldier","HumanSoldierFemale"}) {
            add(s[t], "Position",        "ObjectPos", 3);
            add(s[t], "Gamma",           "Real32",    6);
            add(s[t], "Model",           "String16",  7);
            add(s[t], "Team",            "Int32",     8);
            add(s[t], "Bone Heirachy",   "Int32",     9);
            add(s[t], "Stand Animation", "Int32",     10);
        }
        // HumanPlayer's retail declaration stops after Team. OpenIGI binds it to
        // animation set 000 and uses stand clip 2 when no stand is authored.
        { auto& sc = s["HumanPlayer"];
          add(sc, "Position", "ObjectPos", 3);
          add(sc, "Gamma", "Real32", 6);
          add(sc, "Model", "String16", 7);
          add(sc, "Team", "Int32", 8); }
        // Door: pos@3, (3 unknown physics args @6), ori@9, model@12
        { auto& sc = s["Door"];
          add(sc, "Position",    "ObjectPos", 3);
          add(sc, "Orientation", "Real32x9",  9);
          add(sc, "Model",       "String16",  12); }
        // Car / Heli: pos@3, (2 unknowns @6), gamma@8, (4 unknowns @9), model@13
        for (const char* t : {"Car","Heli"}) {
            add(s[t], "Position", "ObjectPos", 3);
            add(s[t], "Heading",  "Real32",    8);
            add(s[t], "Model",    "String16",  13);
        }
        // ExplodeObject: pos@3, ori@6, model@9, destroyed_model@10, ...
        { auto& sc = s["ExplodeObject"];
          add(sc, "Position",        "ObjectPos", 3);
          add(sc, "Orientation",     "Real32x9",  6);
          add(sc, "Model",           "String16",  9);
          add(sc, "Destroyed model", "String16",  10); }
        // Switch: pos@3, ori@6, count@9, reverse@10, models@11..15, loop@16
        { auto& sc = s["Switch"];
          add(sc, "Position",    "ObjectPos", 3);
          add(sc, "Orientation", "Real32x9",  6);
          add(sc, "Model",       "String16",  11); }
        // Wire: start pos@3, end pos@6, model@9.
        // IGI-RE src/props/wire.c:89-113 Wire_Open (0x43EFF0) adds exactly three
        // params in this order — Param_ObjectPos start, Param_ObjectPos end,
        // Param_String16 model — so the End anchor occupies args 6,7,8. Without
        // it the inspector showed only one anchor and the wire (and therefore any
        // zipline, which is a sloped Wire) could not be authored or previewed.
        //
        // The End anchor is declared "Real64x3" (the type ObjectPos aliases,
        // paramops.c:183) rather than "ObjectPos": the editor's position drag
        // (App::ApplyPropPositionDrag) treats EVERY ObjectPos field as the
        // object's own position and writes obj.pos, so a second ObjectPos field
        // would move the start anchor instead of the end one. Real64x3 renders as
        // three labelled numeric boxes and commits to argTokens[6..8].
        { auto& sc = s["Wire"];
          add(sc, "Position", "ObjectPos", 3);
          add(sc, "End",      "Real64x3",  6);
          add(sc, "Model",    "String16",  9); }
        // AlarmControl / SCameraControl: pos@3, ori@6, model@9 (usually "waypoint")
        for (const char* t : {"AlarmControl","SCameraControl"}) {
            add(s[t], "Position",    "ObjectPos", 3);
            add(s[t], "Orientation", "Real32x9",  6);
            add(s[t], "Model",       "String16",  9);
        }
        // SCamera: layout from IGI-RE src/scamera/scamera.c:116-131 (HOOK 0x442730,
        // ParamTable_New(15)) and from the retail level script's own declaration:
        //   Task_DeclareParameters("SCamera", "Holder Position","ObjectPos",
        //     "Holder Gamma","Angle", "Holder Model","String16",
        //     "Camera Alpha","Angle", "Camera Gamma","Angle",
        //     "Camera Model","String16", "Camera Destroyed Model","String16",
        //     "Rotate Gamma Right (d)","Int16", "Rotate Gamma Left (d)","Int16",
        //     "Rotate Gamma Speed (d/s)","Int16", "Gamma Delay (s)","Real32",
        //     "Viewcone Alpha (d)","Int16", "Viewcone Gamma (d)","Int16",
        //     "Viewcone length (m)","Real32", "On Expression","VarString")
        // There is no 3-float orientation here: arg 9 is an extra yaw, not roll.
        { auto& sc = s["SCamera"];
          add(sc, "Position",           "ObjectPos", 3);
          add(sc, "Holder Gamma",       "Real32",    6);
          add(sc, "Holder Model",       "String16",  7);
          add(sc, "Camera Alpha",       "Real32",    8);
          add(sc, "Camera Gamma",       "Real32",    9);
          add(sc, "Camera Model",       "String16", 10);
          add(sc, "Destroyed Model",    "String16", 11);
          add(sc, "Rotate Gamma Right", "Int32",    12);
          add(sc, "Rotate Gamma Left",  "Int32",    13);
          add(sc, "Rotate Gamma Speed", "Int32",    14);
          add(sc, "Gamma Delay",        "Real32",   15);
          add(sc, "Viewcone Alpha",     "Int32",    16);
          add(sc, "Viewcone Gamma",     "Int32",    17);
          add(sc, "Viewcone Length",    "Real32",   18);
          add(sc, "On Expression",      "VarString", 19); }
        // SplineObjWaypoint: ori first@3, pos@6, waypoint marker@9,
        // segment model for the span ending here@10.
        { auto& sc = s["SplineObjWaypoint"];
          add(sc, "Orientation", "Real32x9",  3);
          add(sc, "Position",    "ObjectPos", 6);
          add(sc, "Model",       "String16",  9);
          add(sc, "Segment Model", "String16", 10); }
        // AmbientArea
        { auto& sc = s["AmbientArea"];
          add(sc, "Position",    "ObjectPos", 3);
          add(sc, "Orientation", "Real32x9",  6); }
        // Train: 1D rail position@3 (Real32), RailroadQTaskID@5, Model@6
        // Position is not a world XYZ but a 1D distance along the spline.
        // We use the special type "TrainPos1D" so the parser knows to set posIsRail=true.
        { auto& sc = s["Train"];
          add(sc, "Position", "TrainPos1D", 3);
          add(sc, "Model",    "String256",  6); }
        // Fence: pos@3, heading@6, model@7, condition@8.
        // IGI-RE src/props/fence.c:100-121 Fence_Open (0x43F900) declares four
        // params: Param_ObjectPos pos, Param_Angle angle, Param_String16 model,
        // Param_VarString cond. The condition was never exposed, so a level's
        // electric-fence expression was invisible in the inspector.
        { auto& sc = s["Fence"];
          add(sc, "Position",  "ObjectPos", 3);
          add(sc, "Heading",   "Real32",    6);
          add(sc, "Model",     "String16",  7);
          add(sc, "Condition", "VarString", 8); }
        // Cabinet: same layout as HumanSoldier
        { auto& sc = s["Cabinet"];
          add(sc, "Position", "ObjectPos", 3);
          add(sc, "Heading",  "Real32",    6);
          add(sc, "Model",    "String16",  7); }
        // Generic positioned objects: pos@3, ori@6, model scanned from arg 9+
        for (const char* t : {"AlarmLight","Elevator","Generator",
                               "GenericPickup","GenericTBA","Plane","Radio",
                               "RotatingObject","Siren", "GunPickup"}) {
            add(s[t], "Position",    "ObjectPos", 3);
            add(s[t], "Orientation", "Real32x9",  6);
            add(s[t], "Model",       "String16",  9);
        }
        // StationaryGun (the player-mannable machinegun): IGI-RE
        // src/statgun/statgun.c:142-150 StationaryGun_Open (0x473ED0) adds
        // exactly eight params in this order —
        //   basePos  ObjectPos  (3 args @3)
        //   baseMatrix Real32x9 (3 args @6; ParamReal32x9_Parse 0x4EFBA0 consumes
        //                        three Euler angles, paramops.c:423-435)
        //   model    String16   (@9)
        //   weapon   String32   (@10)  default "WEAPON_ID_M2HB" (statgun.c:116)
        //   maxPitch Degrees    (@11)  default 25  (statgun.c:117)
        //   minPitch Degrees    (@12)  default -25 (statgun.c:118)
        //   yawRange Degrees    (@13)  default 90  (statgun.c:115)
        //   ammo     Int32      (@14)  default -1, range 0..100000 (statgun.c:140-141)
        // The old generic schema stopped at Model, so the weapon, the fire arc
        // and the ammo were neither parsed nor editable (issue #42).
        { auto& sc = s["StationaryGun"];
          add(sc, "Position",    "ObjectPos", 3);
          add(sc, "Orientation", "Real32x9",  6);
          add(sc, "Model",       "String16",  9);
          add(sc, "Weapon",      "String32",  10);
          add(sc, "Max Pitch",   "Degrees",   11);
          add(sc, "Min Pitch",   "Degrees",   12);
          add(sc, "Yaw Range",   "Degrees",   13);
          add(sc, "Ammo",        "Int32",     14); }
        // AIStationaryGunHolder (the AI fixed gun that also spots for enemies):
        // IGI-RE src/comprun/gunholder.c:157-165 GunHolder_Init (0x46E370) adds
        // eight params in this order —
        //   pos        ObjectPos (@3)   matrix Real32x9 (@6)  model String16 (@9)
        //   angleX     Degrees   (@10)  default 360 (gunholder.c:121)
        //   angleY     Degrees   (@11)  default 360 (gunholder.c:122)
        //   range      Real32    (@12)  default 10, range 0..100 (gunholder.c:123,155-156)
        //   activeExpr VarString (@13)  default ""  (gunholder.c:124-125)
        //   teamExpr   VarString (@14)  team default 1 (gunholder.c:126)
        { auto& sc = s["AIStationaryGunHolder"];
          add(sc, "Position",    "ObjectPos", 3);
          add(sc, "Orientation", "Real32x9",  6);
          add(sc, "Model",       "String16",  9);
          add(sc, "Angle X",     "Degrees",   10);
          add(sc, "Angle Y",     "Degrees",   11);
          add(sc, "Range",       "Real32",    12);
          add(sc, "Active Expr", "VarString", 13);
          add(sc, "Team Expr",   "VarString", 14); }
        // AmmoPickup: pos@3, ori@6, model@9, ammo@10
        { auto& sc = s["AmmoPickup"];
          add(sc, "Position",    "ObjectPos", 3);
          add(sc, "Orientation", "Real32x9",  6);
          add(sc, "Model",       "String16",  9);
          add(sc, "Ammo",        "Int16",     10); }

        return s;
    }();
    return schemas;
}

const TaskSchema* GetBuiltinSchema(const std::string& taskType) {
    const auto& m = GetBuiltinSchemas();
    auto it = m.find(taskType);
    return it == m.end() ? nullptr : &it->second;
}

// --- Runtime schemas from Task_DeclareParameters ----------------------------

static std::string Unquote(const std::string& s) {
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
        return s.substr(1, s.size() - 2);
    return s;
}

TaskSchema ParseDeclaration(const std::vector<std::string>& declArgs) {
    TaskSchema schema;
    if (declArgs.size() < 3) return schema; // need TypeName + at least one (name,type)
    int off = 3; // id, type, note occupy 0..2; first declared field starts at 3
    for (size_t i = 1; i + 1 < declArgs.size(); i += 2) {
        FieldDef f;
        f.name      = Unquote(declArgs[i]);
        f.typeName  = Unquote(declArgs[i + 1]);
        f.argCount  = TypeArgCount(f.typeName);
        f.argOffset = off;
        off += f.argCount;
        schema.push_back(f);
    }
    return schema;
}

static std::map<std::string, TaskSchema>& RegisteredSchemas() {
    // Runtime declarations are scoped to the parsing thread. The headless MCP
    // service can inspect different project levels concurrently without one
    // request replacing another request's declared offsets.
    static thread_local std::map<std::string, TaskSchema> s;
    return s;
}

void RegisterSchema(const std::string& taskType, TaskSchema schema) {
    if (taskType.empty() || schema.empty()) return;
    RegisteredSchemas()[taskType] = std::move(schema);
}

void ClearRegisteredSchemas() {
    RegisteredSchemas().clear();
}

const TaskSchema* GetSchema(const std::string& taskType) {
    const auto& reg = RegisteredSchemas();
    auto it = reg.find(taskType);
    if (it != reg.end()) return &it->second;
    return GetBuiltinSchema(taskType);
}

// ─── Prop / interactable parity maths (see task_schema.h) ────────────────────
namespace Props {

double Vec3::Length() const { return std::sqrt(x * x + y * y + z * z); }

namespace {

constexpr double kTwoPi = 6.283185307179586476925286766559;

// IGI-RE src/game/misc.c:129-190 Matrix_SetYAxis (HOOK 0x414A60): the rotation
// whose second column is `dir`. Row-major 3x3, like the game's `Matrix`.
void SetYAxis(double m[9], const Vec3& dir) {
    const double x = dir.x, y = dir.y, z = dir.z;
    double u[3], v[3], w[3];

    auto normalize = [](double p[3]) {
        const double len = std::sqrt(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]);
        if (len != 0.0) { p[0] /= len; p[1] /= len; p[2] /= len; }
    };

    if (std::fabs(z) > kNearlyUp) {          // misc.c:136
        u[0] = 0.0; u[1] = -z; u[2] = y;
        normalize(u);
        v[0] = y * u[2] - z * u[1];          // v = dir x u
        v[1] = z * u[0] - x * u[2];
        v[2] = x * u[1] - y * u[0];
        w[0] = v[0]; w[1] = v[1]; w[2] = v[2];
        normalize(w);
    } else {
        v[0] = -y; v[1] = -x; v[2] = 0.0;    // misc.c:157-159
        w[0] = v[0]; w[1] = v[1]; w[2] = v[2];
        normalize(w);
        v[0] = z * w[1] - y * w[2];          // v = w x dir
        v[1] = x * w[2] - z * w[0];
        v[2] = y * w[0] - x * w[1];
        u[0] = v[0]; u[1] = v[1]; u[2] = v[2];
        normalize(u);
    }

    m[0] = w[0]; m[1] = x; m[2] = u[0];
    m[3] = w[1]; m[4] = y; m[5] = u[1];
    m[6] = w[2]; m[7] = z; m[8] = u[2];
}

// IGI-RE src/math/matrix.c:181-208 Matrix_GetEuler (HOOK 0x4B3C50): only the
// X-Euler is needed, and the game normalises it into [0, 2pi).
double EulerX(const double m[9]) {
    const double cy = std::sqrt(m[0] * m[0] + m[3] * m[3]);
    double rx = (cy > 0.0001) ? std::atan2(m[7], m[8]) : std::atan2(-m[5], m[4]);
    if (rx < 0.0) rx += kTwoPi;
    return rx;
}

}  // namespace

WireSpan ComputeWireSpan(const Vec3& start, const Vec3& end) {
    WireSpan s;
    s.start = start;
    s.end   = end;
    // Wire_ParamsChanged (0x43F3D0, IGI-RE src/props/wire.c:181-186): the object
    // is positioned at the midpoint of the two anchors.
    s.mid = (start + end) * 0.5;

    const Vec3 d = end - start;
    s.length = d.Length();
    s.valid  = s.length > 0.0;
    if (!s.valid) return s;

    // wire.c:177 — Matrix_SetYAxis(&m, &f) with f = normalised (end - start),
    // wire.c:179 — `w->obj.scale[1] = len * kWireScale`.
    s.dir    = Vec3(d.x / s.length, d.y / s.length, d.z / s.length);
    s.yScale = s.length * kWireScale;

    double m[9];
    SetYAxis(m, s.dir);
    s.slope = EulerX(m);

    // wire.c:304-317 Wire_IsSloped: `a = ax; if (ax < kZero) a = -a;
    // return a > kWireSlope;` — ax is already in [0, 2pi) coming out of
    // Matrix_GetEuler, so this is `slope > 2 degrees`. humanstate.c:958-963 uses
    // it to decide zipline (use kind 9 rides it) vs hand-over-hand wire.
    s.isZipline = std::fabs(s.slope) > kWireSlope;
    return s;
}

Basis YAxisBasis(const Vec3& yAxis) {
    double m[9];
    SetYAxis(m, yAxis);
    // The game's `Matrix` is row-major; m[1], m[4], m[7] is its second column.
    // Return the three columns as world-space axis directions.
    Basis b;
    b.x = Vec3(m[0], m[3], m[6]);
    b.y = Vec3(m[1], m[4], m[7]);
    b.z = Vec3(m[2], m[5], m[8]);
    return b;
}

Vec3 ZiplineRideDirection(const Vec3& start, const Vec3& end) {
    // IGI-RE src/weapon/humanplayer.c:557-568 HumanPlayer_UpdateZipline: the
    // model's point 0 -> point 1 vector, negated when d.z > 0 so the ride always
    // runs downhill, then normalised.
    Vec3 d = end - start;
    if (d.z > 0.0) d = Vec3(-d.x, -d.y, -d.z);
    const double len = d.Length();
    if (len <= 0.0) return Vec3();
    return Vec3(d.x / len, d.y / len, d.z / len);
}

double ZiplineSpeedStep(double speed, const Vec3& rideDir) {
    // IGI-RE src/humanstate/humanstate.c:1394.
    return -rideDir.z * kZiplineAccel + speed * kZiplineFriction;
}

Vec3 ZiplineVelocity(double speed, const Vec3& rideDir) {
    // IGI-RE src/humanstate/humanstate.c:1396-1398.
    return rideDir * speed;
}

double ZiplineTerminalSpeed(const Vec3& rideDir) {
    // Fixed point of ZiplineSpeedStep: a / (1 - friction).
    return -rideDir.z * kZiplineAccel / (1.0 - kZiplineFriction);
}

bool ParseDoubleToken(const std::string& token, double& out) {
    if (token.empty()) return false;
    try {
        size_t used = 0;
        const double v = std::stod(token, &used);
        if (used == 0) return false;
        out = v;
        return true;
    } catch (...) {
        return false;
    }
}

std::string UnquoteToken(const std::string& token) {
    if (token.size() >= 2 && token.front() == '"' && token.back() == '"')
        return token.substr(1, token.size() - 2);
    return token;
}

bool ParseWireAnchors(const std::vector<std::string>& args, Vec3& start, Vec3& end) {
    // Wire_Open (0x43EFF0, IGI-RE src/props/wire.c:89-113): start ObjectPos at
    // args 3-5, end ObjectPos at args 6-8.
    auto tok = [&](int i) -> const std::string& {
        static const std::string empty;
        return (i >= 0 && i < (int)args.size()) ? args[i] : empty;
    };
    double s[3], e[3];
    for (int i = 0; i < 3; ++i) {
        if (!ParseDoubleToken(tok(3 + i), s[i])) return false;
        if (!ParseDoubleToken(tok(6 + i), e[i])) return false;
    }
    start = Vec3(s[0], s[1], s[2]);
    end   = Vec3(e[0], e[1], e[2]);
    return true;
}

StationaryGunParams ParseStationaryGunArgs(const std::vector<std::string>& args) {
    StationaryGunParams p;
    auto tok = [&](int i) -> const std::string& {
        static const std::string empty;
        return (i >= 0 && i < (int)args.size()) ? args[i] : empty;
    };

    // The whole param block starts at arg 3 (basePos); without it this is not a
    // StationaryGun record at all.
    double probe = 0.0;
    p.valid = ParseDoubleToken(tok(3), probe);
    if (!p.valid) return p;

    if (!tok(10).empty()) p.weapon = UnquoteToken(tok(10));
    ParseDoubleToken(tok(11), p.maxPitch);
    ParseDoubleToken(tok(12), p.minPitch);
    ParseDoubleToken(tok(13), p.yawRange);
    double ammo = 0.0;
    if (ParseDoubleToken(tok(14), ammo)) p.ammo = (int)std::llround(ammo);
    return p;
}

GunHolderParams ParseGunHolderArgs(const std::vector<std::string>& args) {
    GunHolderParams p;
    auto tok = [&](int i) -> const std::string& {
        static const std::string empty;
        return (i >= 0 && i < (int)args.size()) ? args[i] : empty;
    };

    double probe = 0.0;
    p.valid = ParseDoubleToken(tok(3), probe);
    if (!p.valid) return p;

    ParseDoubleToken(tok(10), p.angleX);
    ParseDoubleToken(tok(11), p.angleY);
    ParseDoubleToken(tok(12), p.range);
    p.activeExpr = UnquoteToken(tok(13));
    p.teamExpr   = UnquoteToken(tok(14));
    return p;
}

// ── Ladder placement (issue #68) ────────────────────────────────────────────

namespace {

double LadderDot(const Vec3& a, const Vec3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

// Vec3_Angle (HOOK 0x4B2B30, IGI-RE src/file/vec.c:189-208): the angle between
// two vectors, 0 when either is zero, clamped at the acos domain edges.
double LadderAngleBetween(const Vec3& a, const Vec3& b) {
    const double la = a.Length();
    const double lb = b.Length();
    if (la <= 0.0 || lb <= 0.0) return 0.0;
    const double c = LadderDot(a, b) / (la * lb);
    if (c >= 1.0) return 0.0;
    if (!(c > -1.0)) return 3.14159265358979323846;
    return std::acos(c);
}

} // namespace

LadderPlacement ComputeLadderPlacement(
    const Vec3& origin,
    const Vec3& activation_top,
    const Vec3& foot,
    const Vec3& head,
    const Basis& orientation) {
    LadderPlacement p;
    p.origin = origin;
    p.activationTop = activation_top;
    p.foot = foot;
    p.head = head;
    p.axisX = orientation.x;
    p.axisY = orientation.y;
    p.axisZ = orientation.z;

    // The mount offsets are applied along the ladder matrix's own columns, so a
    // degenerate basis (no authored orientation) cannot produce a placement.
    if (p.axisY.Length() < 0.5 || p.axisZ.Length() < 0.5) return p;

    // HumanPlayer_UpdateLadder, IGI-RE src/weapon/humanplayer.c:312-314.
    p.bottomMount = foot +
        p.axisY * kLadderBottomStandoffUnits +
        p.axisZ * kLadderBottomLiftUnits;
    // IGI-RE src/weapon/humanplayer.c:304-306.
    p.topMount = head +
        p.axisZ * kLadderTopBackUnits -
        p.axisY * kLadderTopStandoffUnits;

    // IGI-RE src/props/ladder.c:86-91: the rung count is
    // (int)round_ll(|point2.z - point3.z| * kRungScale) on the *model-local* Z,
    // which is the foot->head difference projected on the ladder's own Z column.
    // `round_ll` is an x87 `fistpll` (src/props/props.h:50-57), i.e. round-to-
    // nearest-even, so `std::nearbyint` reproduces it where `std::llround` does not
    // (they differ on exact ties). Kept identical to `LadderClimbLine` in
    // source/player_ladder.cpp, which implements the same rule for the runtime.
    const double height = std::abs(LadderDot(foot - head, p.axisZ));
    p.rungCount = (int)std::nearbyint(height / kLadderRungSpacingUnits);
    if (p.rungCount < 1) p.rungCount = 1;
    p.topStep = p.rungCount - kLadderTopStepMargin;
    if (p.topStep < 0) p.topStep = 0;

    // Matrix_GetEuler (HOOK 0x4B3C50, IGI-RE src/math/matrix.c:181-191) reads
    // az = atan2(m[3], m[0]); HumanPlayer_UpdateLadder adds pi and hands that to
    // Human_SetYaw (humanplayer.c:320-322).
    p.playerYawRadians = std::atan2(p.axisX.y, p.axisX.x) + 3.14159265358979323846;
    p.valid = true;
    return p;
}

LadderActivation LadderActivate(
    const LadderPlacement& ladder,
    const Vec3& player_eye,
    const Vec3& view_axis_y) {
    LadderActivation result;
    if (!ladder.valid) return result;

    // IGI-RE src/props/ladder.c:130-138. lx/ly are the player's coordinates in
    // the ladder's frame: the row-major matrix's columns are the local axes.
    const Vec3 from_origin = player_eye - ladder.origin;
    const double lx = LadderDot(from_origin, ladder.axisX);
    const double ly = LadderDot(from_origin, ladder.axisY);
    const double y = std::abs(ly);
    if (y < kLadderActivateRadiusUnits &&
        lx > -kLadderHalfWidthUnits &&
        lx < kLadderHalfWidthUnits) {
        // ladder.c:139-154 — compare the view's own Y column against the ladder's
        // Y column, flipped to the player's side when the player is behind it.
        Vec3 n = ladder.axisY;
        if (ly > 0.0) n = n * -1.0;
        const double angle = LadderAngleBetween(view_axis_y, n);
        // ladder.c:155-157 — the reference point is magic point 1's world Z.
        if (angle < kLadderFacingAngleRadians &&
            ladder.activationTop.z > player_eye.z &&
            player_eye.z > ladder.origin.z) {
            result.bottom = true;
        }
    }

    // ladder.c:164-173 — offered from the top side of magic point 1, within
    // UseQuery_Consider's own limits (src/game/game.c:896-928: |top - eye| <
    // q->range and the point ahead of the view). Runs even when the bottom test
    // failed, and wins, which is why ladder.c:161 clears the flag first.
    const Vec3 to_top = ladder.activationTop - player_eye;
    if (LadderDot(player_eye - ladder.activationTop, ladder.axisY) < 0.0 &&
        to_top.Length() < kLadderActivateRadiusUnits &&
        LadderDot(to_top, view_axis_y) > 0.0) {
        result.top = true;
    }

    return result;
}

} // namespace Props

} // namespace TaskSchemaNS
