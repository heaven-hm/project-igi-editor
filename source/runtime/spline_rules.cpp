// spline_rules.cpp - the SplineObj / SplineObjWaypoint rules, ported branch by
// branch from IGI-RE. Every function carries the IGI-RE file:line it mirrors;
// every branch that differs from the obvious reading says so.
//
// See spline_rules.h for the issue mapping (#79 authored model, #69 curve) and
// the list of oracle functions. The oracle's translation units are compiled with
// `#pragma GCC optimize("excess-precision=standard")` so their x87 intermediates
// keep 80 bits; this port keeps the same value ranges (float fields, long double
// intermediates) but a host x86-64 build evaluates in SSE2, so results are exact
// to the engine's precision and not bit-identical. Marked in the doc, not hidden.

#include "runtime/spline_rules.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace igi {
namespace {

// ---------------------------------------------------------------------------
// Authored token readers
// ---------------------------------------------------------------------------

// Token layout: [0] = task id, [1] = type, [2] = name, [3] = declared
// parameter 0. So declared parameter k lives at token index k + 3 - the same
// layout mission_fog.cpp and smoke.cpp use.
float TokenFloat(const std::vector<std::string>& tokens, std::size_t index, float fallback = 0.0f) {
    if (index >= tokens.size()) return fallback;
    const std::string& token = tokens[index];
    if (token.empty()) return fallback;
    char* end = nullptr;
    const float value = std::strtof(token.c_str(), &end);
    if (end == token.c_str()) return fallback;
    return value;
}

double TokenDouble(const std::vector<std::string>& tokens, std::size_t index, double fallback = 0.0) {
    if (index >= tokens.size()) return fallback;
    const std::string& token = tokens[index];
    if (token.empty()) return fallback;
    char* end = nullptr;
    const double value = std::strtod(token.c_str(), &end);
    if (end == token.c_str()) return fallback;
    return value;
}

int TokenInt(const std::vector<std::string>& tokens, std::size_t index, int fallback = 0) {
    if (index >= tokens.size()) return fallback;
    const std::string& token = tokens[index];
    if (token.empty()) return fallback;
    char* end = nullptr;
    const long value = std::strtol(token.c_str(), &end, 10);
    if (end == token.c_str()) return fallback;
    return static_cast<int>(value);
}

// A bool8 authored value: the retail scripts write TRUE/FALSE, and the QVM
// identifier table holds exactly those two (the smoke corpus test relies on the
// same encoding). Anything else falls back to a number.
bool TokenBool(const std::vector<std::string>& tokens, std::size_t index, bool fallback = false) {
    if (index >= tokens.size()) return fallback;
    const std::string& token = tokens[index];
    if (token == "TRUE") return true;
    if (token == "FALSE") return false;
    return TokenInt(tokens, index, fallback ? 1 : 0) != 0;
}

std::string TokenString(const std::vector<std::string>& tokens, std::size_t index) {
    if (index >= tokens.size()) return std::string();
    return tokens[index];
}

// ---------------------------------------------------------------------------
// x87 and libm helpers
// ---------------------------------------------------------------------------

// HALF_PI / PI / TWO_PI as the engine's long-double constants
// (src/math/vecmath.h:4-6).
constexpr long double kHalfPiL = 0x1.921fb54442d1bp+0L;
constexpr long double kTwoPiL = 0x1.921fb54442d1bp+2L;

// `x87_sqrt` is a bare `fsqrt` (src/math/x87.h:5-9); `sqrtl` is the same
// operation through the C library.
long double X87Sqrt(long double x) {
    return std::sqrt(x);
}

// `x87_sin` / `x87_cos` are bare `fsin` / `fcos` (src/math/x87.h:19-27).
long double X87Sin(long double x) {
    return std::sin(x);
}

long double X87Cos(long double x) {
    return std::cos(x);
}

// `Atan2`, src/math/matrix.c:27-41. The engine's version is `fpatan` with a
// special case: for x == 0 it returns sign(y) * HALF_PI, and for x == 0 && y == 0
// it shows an error and then SPINS FOREVER (`for (;;) ;`, :31-33). A library
// cannot reproduce a hang, so this returns +HALF_PI for the (0, 0) case and the
// divergence is recorded in the doc rather than hidden.
long double EngineAtan2(long double y, long double x) {
    if (x == 0.0L) {
        if (y == 0.0L) return kHalfPiL;  // engine: Error_Show + infinite loop
        const long double sign = y < 0.0L ? -1.0L : 1.0L;
        return sign * kHalfPiL;
    }
    return std::atan2(y, x);
}

// `IsZero`, src/tagitem/spline.c:57-59: `!islessgreater(x, 0)` - true for +0, -0
// and NaN, false otherwise.
bool IsZero(float x) {
    return !std::islessgreater(x, 0.0f);
}

// Math_ASinF, HOOK(0x4B27D0), src/file/vec.c:48-58. The argument is a FLOAT,
// the result a double, and the value is clamped to [-1, 1] first - with the
// NaN-swallowing `!(d >= -1.0)` form, so a NaN ratio returns asin(-1.0).
double MathASinF(float x) {
    double d = x;
    if (!(d >= -1.0)) return std::asin(-1.0);
    if (d > 1.0) d = 1.0;
    return std::asin(d);
}

// `Vec3f_NormalizeOrdered`, src/tagitem/spline.c:586-595: when any component is
// non-zero, all THREE components are scaled by 1/sqrt of the sum of squares taken
// in the order (a, b, c). The order matters to the last bit.
void Vec3fNormalizeOrdered(float* v, int a, int b, int c) {
    if (!IsZero(v[0]) || !IsZero(v[1]) || !IsZero(v[2])) {
        const long double inv =
            1.0L / X87Sqrt(static_cast<long double>(v[a]) * v[a] +
                           static_cast<long double>(v[b]) * v[b] +
                           static_cast<long double>(v[c]) * v[c]);
        v[0] = static_cast<float>(v[0] * inv);
        v[1] = static_cast<float>(v[1] * inv);
        v[2] = static_cast<float>(v[2] * inv);
    }
}

// ---------------------------------------------------------------------------
// The engine's matrix primitives
// ---------------------------------------------------------------------------

// `Mul`, src/math/matrix.c:11-25: row-major a * b, summing k = 0, 2, 1 in that
// order. The element order is the engine's and is reproduced literally.
void MatrixMul(float* out, const float* a, const float* b) {
    float t[9];
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            const float* r = &a[i * 3];
            t[i * 3 + j] = static_cast<float>(static_cast<long double>(r[0]) * b[j] +
                                              static_cast<long double>(r[2]) * b[6 + j] +
                                              static_cast<long double>(r[1]) * b[3 + j]);
        }
    }
    std::memcpy(out, t, sizeof(t));
}

// Matrix_SetRotX, HOOK(0x4B3B60), src/math/matrix.c:112-128.
void MatrixSetRotX(float* m, float a) {
    const long double s = X87Sin(a);
    const float c = static_cast<float>(X87Cos(a));
    m[0] = 1.0f;
    m[1] = 0.0f;
    m[2] = 0.0f;
    m[3] = 0.0f;
    m[6] = 0.0f;
    m[4] = c;
    m[5] = static_cast<float>(-s);
    m[8] = c;
    m[7] = static_cast<float>(s);
}

// Matrix_SetRotY, HOOK(0x4B3BA0), src/math/matrix.c:130-146.
void MatrixSetRotY(float* m, float a) {
    const long double s = X87Sin(a);
    const long double c = X87Cos(a);
    m[1] = 0.0f;
    m[3] = 0.0f;
    m[4] = 1.0f;
    m[5] = 0.0f;
    m[7] = 0.0f;
    m[0] = static_cast<float>(c);
    m[2] = static_cast<float>(s);
    m[6] = static_cast<float>(-s);
    m[8] = static_cast<float>(c);
}

// A transposition used by setup_rigid: `transpose10`, src/sound/segsetup.c:76-86.
// The 3x3 is transposed word by word and the tenth word is COPIED, not moved.
void Transpose10(float* dst, const float* src) {
    float t[10];
    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 3; c++) {
            t[r * 3 + c] = src[c * 3 + r];
        }
    }
    t[9] = src[9];
    std::memcpy(dst, t, sizeof(t));
}

// Vec3d_Distance, HOOK(0x4B32B0): the chord length as a long double.
double Vec3dDistance(const double a[3], const double b[3]) {
    const long double dx = static_cast<long double>(a[0]) - b[0];
    const long double dy = static_cast<long double>(a[1]) - b[1];
    const long double dz = static_cast<long double>(a[2]) - b[2];
    return static_cast<double>(X87Sqrt(dz * dz + dy * dy + dx * dx));
}

}  // namespace

// ---------------------------------------------------------------------------
// The authored parameter tables
// ---------------------------------------------------------------------------

const SplineParamDesc* SplineObjParamTable() {
    // Index order is the ParamTable_Add order (src/editobj/spline.c:205-214);
    // the authored names and types are the retail
    // Task_DeclareParameters("SplineObj", ...) line, which ships in
    // `MISSIONS/location0/level1/objects.qsc:2-3` and is reproduced by every
    // OBJECTS.QVM; the range column is gSplineObjSegmentRange
    // (src/editobj/spline.c:202-203), registered only on the
    // "Number of Matrices / Segment" row (`:209`).
    static const SplineParamDesc kTable[kSplineObjParamCount] = {
        // authored name                    engine field  type       n  range?  min  max
        {"Linear Segments",                "rigid",       "bool8",   1, false,  0.0f, 0.0f},
        {"Display waypoints",              "collide",     "bool8",   1, false,  0.0f, 0.0f},
        {"Snap Length",                    "snap",        "bool8",   1, false,  0.0f, 0.0f},
        {"Automatic Orientation",          "autoOrient",  "bool8",   1, false,  0.0f, 0.0f},
        {"Number of Matrices / Segment",   "maxSegments", "Int32",   1, true,   2.0f, 20.0f},
        {"Collision LOD",                  "unk20",       "Int32",   1, false,  0.0f, 0.0f},
        {"Position",                       "pos",         "Real32x3", 3, false, 0.0f, 0.0f},
        {"Gamma Orientation",              "angle",       "Angle",   1, false,  0.0f, 0.0f},
        {"Dirlight Angular Effect",        "color1",      "RGB",     3, false,  0.0f, 0.0f},
        {"Dirlight Ambient Effect",        "color2",      "RGB",     3, false,  0.0f, 0.0f},
    };
    return kTable;
}

const SplineParamDesc* SplineObjWaypointParamTable() {
    // ParamTable_Add order (src/editobj/spline.c:216-223); authored names and
    // types from `objects.qsc:4`. "Orientation" is declared Real32x9 but consumes
    // THREE script arguments (ParamReal32x9_Parse, HOOK(0x4EFBA0),
    // src/physobj/paramops.c:423-435 builds a matrix from three Euler angles), and
    // "Position" is ObjectPos, an alias of Real64x3 (src/physobj/paramops.c:181),
    // which also consumes three.
    static const SplineParamDesc kTable[kSplineObjWaypointParamCount] = {
        {"Orientation",             "rot",        "Real32x9", 3, false, 0.0f, 0.0f},
        {"Position",                "point",      "ObjectPos", 3, false, 0.0f, 0.0f},
        {"Waypoint Model",          "modelName",  "String16", 1, false, 0.0f, 0.0f},
        {"Segment Model",           "segModel",   "String16", 1, false, 0.0f, 0.0f},
        {"NumAreas",                "segments",   "Int32",    1, true,  2.0f, 20.0f},
        {"Align",                   "straight",   "bool8",    1, false, 0.0f, 0.0f},
        {"Flip",                    "reversed",   "bool8",    1, false, 0.0f, 0.0f},
        {"Automatic Orientation",   "autoOrient", "bool8",    1, false, 0.0f, 0.0f},
    };
    return kTable;
}

// ---------------------------------------------------------------------------
// Parsing the authored form
// ---------------------------------------------------------------------------

bool ParseSplineObjParams(const std::vector<std::string>& arg_tokens, SplineObjParams* out) {
    if (out == nullptr) return false;
    const SplineParamDesc* table = SplineObjParamTable();
    // The engine's ParamCursor_Need rejects a call with fewer tokens than the
    // table needs (src/physobj/paramops.c, ParamCursor_Need), so a truncated task
    // is reported rather than half-filled.
    if (arg_tokens.size() < kSplineFirstParam + static_cast<std::size_t>(kSplineObjTokenCount)) {
        return false;
    }

    SplineObjParams parsed;  // the SplineObj_Create defaults, src/editobj/spline.c:227-242
    std::size_t cursor = kSplineFirstParam;
    for (int i = 0; i < kSplineObjParamCount; i++) {
        switch (i) {
            case kSplineObjRigid:
                parsed.rigid = TokenBool(arg_tokens, cursor);
                break;
            case kSplineObjCollide:
                parsed.collide = TokenBool(arg_tokens, cursor);
                break;
            case kSplineObjSnap:
                parsed.snap = TokenBool(arg_tokens, cursor);
                break;
            case kSplineObjAutoOrient:
                parsed.auto_orient = TokenBool(arg_tokens, cursor);
                break;
            case kSplineObjMaxSegments:
                parsed.max_segments = TokenInt(arg_tokens, cursor, kSplineMaxSegments);
                break;
            case kSplineObjCollisionLod:
                parsed.collision_lod = TokenInt(arg_tokens, cursor, 0);
                break;
            case kSplineObjPosition:
                // Param_Real32x3: three tokens into a Vec3f (paramops.c:174).
                parsed.pos[0] = TokenDouble(arg_tokens, cursor);
                parsed.pos[1] = TokenDouble(arg_tokens, cursor + 1);
                parsed.pos[2] = TokenDouble(arg_tokens, cursor + 2);
                break;
            case kSplineObjAngle:
                parsed.angle = TokenFloat(arg_tokens, cursor, kSplinePi);
                break;
            case kSplineObjColour1:
            case kSplineObjColour2: {
                float* colour = (i == kSplineObjColour1) ? parsed.colour1 : parsed.colour2;
                colour[0] = TokenFloat(arg_tokens, cursor, colour[0]);
                colour[1] = TokenFloat(arg_tokens, cursor + 1, colour[1]);
                colour[2] = TokenFloat(arg_tokens, cursor + 2, colour[2]);
                break;
            }
            default:
                break;
        }
        cursor += static_cast<std::size_t>(table[i].token_count);
    }

    *out = parsed;
    return true;
}

bool ParseSplineObjWaypointParams(const std::vector<std::string>& arg_tokens,
                                  SplineObjWaypointParams* out) {
    if (out == nullptr) return false;
    const SplineParamDesc* table = SplineObjWaypointParamTable();
    if (arg_tokens.size() <
        kSplineFirstParam + static_cast<std::size_t>(kSplineObjWaypointTokenCount)) {
        return false;
    }

    SplineObjWaypointParams parsed;
    std::size_t cursor = kSplineFirstParam;
    for (int i = 0; i < kSplineObjWaypointParamCount; i++) {
        switch (i) {
            case kSplineWaypointOrientation:
                // The authored triple IS the Euler triple; the engine keeps the
                // 3x3 built from it (ParamReal32x9_Parse).
                parsed.orientation[0] = TokenFloat(arg_tokens, cursor);
                parsed.orientation[1] = TokenFloat(arg_tokens, cursor + 1);
                parsed.orientation[2] = TokenFloat(arg_tokens, cursor + 2);
                break;
            case kSplineWaypointPosition:
                parsed.point[0] = TokenDouble(arg_tokens, cursor);
                parsed.point[1] = TokenDouble(arg_tokens, cursor + 1);
                parsed.point[2] = TokenDouble(arg_tokens, cursor + 2);
                break;
            case kSplineWaypointModel:
                parsed.model_name = TokenString(arg_tokens, cursor);
                break;
            case kSplineWaypointSegModel:
                parsed.seg_model = TokenString(arg_tokens, cursor);
                break;
            case kSplineWaypointSegments:
                parsed.segments = TokenInt(arg_tokens, cursor, kSplineMaxSegments);
                break;
            case kSplineWaypointStraight:
                parsed.straight = TokenBool(arg_tokens, cursor);
                break;
            case kSplineWaypointReversed:
                parsed.reversed = TokenBool(arg_tokens, cursor);
                break;
            case kSplineWaypointAutoOrient:
                parsed.auto_orient = TokenBool(arg_tokens, cursor);
                break;
            default:
                break;
        }
        cursor += static_cast<std::size_t>(table[i].token_count);
    }

    *out = parsed;
    return true;
}

// ---------------------------------------------------------------------------
// Derived authored rules
// ---------------------------------------------------------------------------

int SplineEffectiveSegmentCount(int waypoint_segments, int max_segments, bool rigid) {
    // SplineObjWaypoint_Changed, src/tagitem/spline.c:771-772.
    int n = waypoint_segments < max_segments ? waypoint_segments : max_segments;
    if (n > 20) n = 20;
    // SplineObjSegment_Setup, src/sound/segsetup.c:267-270.
    if (rigid) n = 1;
    return n;
}

bool SplineSpanIsRigid(bool container_rigid, bool waypoint_straight) {
    // src/tagitem/spline.c:769 and :80-81 / :818-820.
    return container_rigid || waypoint_straight;
}

void SplineTangent(const float rot[10], double distance, double out[3]) {
    // src/sound/segsetup.c:98-100: elements 0, 3 and 6 - the first COLUMN.
    out[0] = static_cast<double>(rot[0]) * distance;
    out[1] = static_cast<double>(rot[3]) * distance;
    out[2] = static_cast<double>(rot[6]) * distance;
}

void EngineSetEuler(float ax, float ay, float az, float out[10]) {
    // Matrix_SetEuler, HOOK(0x4B38E0), src/math/matrix.c:99-110:
    // Rz(az) * Ry(ay) * Rx(ax), with the engine's Mul summation order.
    // The three 10-float frames are the engine's Matrix temporaries; they are
    // declared with ten elements because SetRot* writes the spare `count` word.
    float m[10];
    float r[10];
    EngineSetRotZ(az, m);
    MatrixSetRotY(r, ay);
    MatrixMul(m, m, r);
    MatrixSetRotX(r, ax);
    MatrixMul(m, m, r);
    std::memcpy(out, m, sizeof(float) * 9);
    out[9] = 0.0f;  // m->count = 0
}

void EngineMatrixGetEuler(const float m[10], float* ax, float* ay, float* az) {
    // Matrix_GetEuler, HOOK(0x4B3C50), src/math/matrix.c:181-208. The three
    // angles come back normalised into [0, 2pi) with the LONG DOUBLE TWO_PI
    // (src/math/vecmath.h:6), not the float one setup_segments unwraps with.
    const long double cy = X87Sqrt(static_cast<long double>(m[0]) * m[0] +
                                   static_cast<long double>(m[3]) * m[3]);
    long double x;
    long double y;
    double rz;

    if (cy > 0.0001L) {
        const double rx = static_cast<double>(EngineAtan2(m[7], m[8]));
        y = EngineAtan2(-static_cast<long double>(m[6]), cy);
        rz = static_cast<double>(EngineAtan2(m[3], m[0]));
        x = rx;
    } else {
        const double rx = static_cast<double>(EngineAtan2(-static_cast<long double>(m[5]), m[4]));
        y = EngineAtan2(-static_cast<long double>(m[6]), cy);
        rz = 0.0;
        x = rx;
    }
    if (x < 0.0L) x += kTwoPiL;
    if (y < 0.0L) y += kTwoPiL;
    if (rz < 0.0) rz = static_cast<double>(static_cast<long double>(rz) + kTwoPiL);
    if (ax) *ax = static_cast<float>(x);
    if (ay) *ay = static_cast<float>(y);
    if (az) *az = static_cast<float>(rz);
}

void EngineSetRotZ(float a, float out[10]) {
    // Matrix_SetRotZ, HOOK(0x4B3BE0), src/math/matrix.c:148-164.
    const long double s = X87Sin(a);
    const float c = static_cast<float>(X87Cos(a));
    out[2] = 0.0f;
    out[5] = 0.0f;
    out[6] = 0.0f;
    out[7] = 0.0f;
    out[8] = 1.0f;
    out[9] = 0.0f;
    out[0] = c;
    out[1] = static_cast<float>(-s);
    out[4] = c;
    out[3] = static_cast<float>(s);
}

void Vec3ToAngles(const float v[3], float* pitch, float* yaw) {
    // Vec3_ToAngles, HOOK(0x4B2D00), src/file/vec.c:232-246. The `v[0] == 0`
    // guard is the engine's `IsZero(v[0])` and yields yaw 0, not +/-pi/2.
    if (IsZero(v[0])) {
        *yaw = 0.0f;
    } else {
        // `fpatan(v[1], v[0])` is a long double atan2 with no special cases.
        *yaw = static_cast<float>(
            std::atan2(static_cast<long double>(v[1]), static_cast<long double>(v[0])));
    }
    const long double len = X87Sqrt(static_cast<long double>(v[0]) * v[0] +
                                    static_cast<long double>(v[1]) * v[1] +
                                    static_cast<long double>(v[2]) * v[2]);
    if (IsZero(static_cast<float>(len))) {
        *pitch = 0.0f;
    } else {
        // `fchs(Math_ASinF(v[2] / len))`. Math_ASinF takes a FLOAT and clamps it
        // to [-1, 1] before calling asin (src/file/vec.c:48-58), and the sign is
        // inverted - the opposite of the usual pitch convention.
        const float ratio = static_cast<float>(static_cast<long double>(v[2]) / len);
        *pitch = static_cast<float>(-MathASinF(ratio));
    }
}

void SplineWaypointFrame(const SplineObjWaypointParams& waypoint, float out_rot[10]) {
    // ParamReal32x9_Parse, HOOK(0x4EFBA0), src/physobj/paramops.c:423-435:
    // Matrix_SetEuler(m, x, y, z) from the three authored angles.
    EngineSetEuler(waypoint.orientation[0], waypoint.orientation[1], waypoint.orientation[2],
                   out_rot);
}

void SplineNewWaypointPoint(const SplineObjWaypointParams& neighbour, SplineNewWaypointStep step,
                            double model_extent, bool step_forward, double out_point[3]) {
    // SplineObjWaypoint_Create, src/tagitem/spline.c:463-546. The new waypoint
    // starts on the neighbour's point and frame.
    float rot[10];
    SplineWaypointFrame(neighbour, rot);
    out_point[0] = neighbour.point[0];
    out_point[1] = neighbour.point[1];
    out_point[2] = neighbour.point[2];

    // SplineObjWaypoint_Create declares the first column's X as `long double` in
    // the FORWARD branch (:464) but as `float` in the BACKWARD one (:502), so the
    // sum of squares handed to x87_sqrt is a long-double expression forward and a
    // FLOAT one backward, and the normalised X is narrowed to float backward but
    // not forward. Both are reproduced; unifying them would change the axis in the
    // last bits.
    long double x = rot[0];
    float y = rot[3];
    float z = rot[6];
    if (!IsZero(rot[0]) || !IsZero(y) || !IsZero(z)) {
        const long double sq = step_forward
                                   ? (x * x + y * y + z * z)
                                   : static_cast<long double>(
                                         static_cast<float>(rot[0]) * rot[0] + y * y + z * z);
        const long double inv = 1.0L / X87Sqrt(sq);
        x = x * inv;
        y = static_cast<float>(y * inv);
        z = static_cast<float>(z * inv);
        if (!step_forward) x = static_cast<float>(x);  // the backward branch's x is a float
    }

    long double step_x;
    float step_y;
    float step_z;
    if (step == kSplineStepModelExtent) {
        // `seg->end - seg->start`, the segment model's own X extent. The forward
        // branch (:483-489) and the backward branch (:523-529) are identical
        // here, including the float products for y and z.
        const long double len = model_extent;
        const float d = static_cast<float>(len);
        step_x = x * len;
        step_y = static_cast<float>(d * y);
        step_z = static_cast<float>(d * z);
    } else if (step_forward) {
        // :491-493, the forward default: a long double product in all three.
        step_x = x * kSplineDefaultSegLength;
        step_y = kSplineDefaultSegLength * y;
        step_z = kSplineDefaultSegLength * z;
    } else if (step == kSplineStepUnit) {
        // :530-532: only `dx = x` is assigned, so y and z keep their NORMALISED
        // values - the backward step is a unit vector, not kDefaultSegLength.
        step_x = x;
        step_y = y;
        step_z = z;
    } else {
        // :520-522, the backward default: `dx = kDefaultSegLength * x` is a FLOAT
        // product widened to long double, unlike the forward case above.
        step_x = static_cast<long double>(kSplineDefaultSegLength * x);
        step_y = kSplineDefaultSegLength * y;
        step_z = kSplineDefaultSegLength * z;
    }

    if (step_forward) {
        out_point[0] += static_cast<double>(step_x);
        out_point[1] += static_cast<double>(step_y);
        out_point[2] += static_cast<double>(step_z);
    } else {
        out_point[0] -= static_cast<double>(step_x);
        out_point[1] -= static_cast<double>(step_y);
        out_point[2] -= static_cast<double>(step_z);
    }
}

void SplineSnapPoint(const double previous[3], const double point[3], double model_extent,
                     double out_point[3]) {
    // SplineObjWaypoint_Changed, src/tagitem/spline.c:774-786. Note that
    // `dx * dx + dy * dy` is a FLOAT expression and only then widened.
    float dx = static_cast<float>(point[0] - previous[0]);
    float dy = static_cast<float>(point[1] - previous[1]);
    const long double dz = static_cast<long double>(point[2]) - previous[2];
    const long double len = model_extent;
    const long double k = len / X87Sqrt((dx * dx + dy * dy) + dz * dz);
    dx = static_cast<float>(dx * k);
    dy = static_cast<float>(dy * k);
    out_point[0] = static_cast<double>(dx) + previous[0];
    out_point[1] = static_cast<double>(dy) + previous[1];
    out_point[2] = static_cast<double>(dz * k) + previous[2];
}

void SplineAutoOrientFrame(const double* previous, const double* point, const double* next,
                           float out_rot[10]) {
    // Waypoint_AutoOrient, src/tagitem/spline.c:598-676.
    float r[10] = {0.0f};
    float dir[3] = {0.0f, 0.0f, 0.0f};
    float u[3];
    float c[3];
    float d[3];

    if (previous) {
        dir[0] = static_cast<float>(point[0] - previous[0]);
        dir[1] = static_cast<float>(point[1] - previous[1]);
        dir[2] = static_cast<float>(point[2] - previous[2]);
    }
    if (next) {
        const float dy = static_cast<float>(next[1] - point[1]);
        const float dz = static_cast<float>(next[2] - point[2]);
        dir[0] = static_cast<float>(next[0] - point[0]) + dir[0];
        dir[1] = dy + dir[1];
        dir[2] = dz + dir[2];
    }
    if (IsZero(dir[0]) && IsZero(dir[1]) && IsZero(dir[2])) {
        r[1] = 0.0f;
        r[2] = 0.0f;
        r[0] = 1.0f;
    } else {
        std::memcpy(r, dir, sizeof(dir));
    }
    if (IsZero(r[0]) && IsZero(r[1])) {
        r[3] = 0.0f;
        r[4] = 1.0f;
        r[5] = 0.0f;
    } else {
        const long double b = r[1];
        r[5] = 0.0f;
        r[3] = static_cast<float>(b);
        const long double a = -static_cast<long double>(r[0]);
        r[4] = static_cast<float>(a);
        if (!IsZero(static_cast<float>(b)) || !IsZero(static_cast<float>(a))) {
            const long double inv = 1.0L / X87Sqrt(b * b + a * a);
            r[3] = static_cast<float>(b * inv);
            r[4] = static_cast<float>(a * inv);
        }
    }
    {
        const float t0 = r[4] * r[2] - r[5] * r[1];
        const float t1 = -(r[3] * r[2] - r[5] * r[0]);
        const float t2 = r[3] * r[1] - r[4] * r[0];
        r[6] = t0;
        r[7] = t1;
        r[8] = t2;
    }
    std::memcpy(u, r, sizeof(u));
    Vec3fNormalizeOrdered(u, 0, 1, 2);
    c[0] = r[2] * r[7] - r[8] * r[1];
    c[1] = -(r[2] * r[6] - r[8] * r[0]);
    c[2] = r[1] * r[6] - r[0] * r[7];
    d[0] = c[2] * u[1] - c[1] * u[2];
    d[1] = -(c[2] * u[0] - c[0] * u[2]);
    d[2] = c[1] * u[0] - c[0] * u[1];
    Vec3fNormalizeOrdered(c, 2, 1, 0);
    Vec3fNormalizeOrdered(d, 0, 1, 2);

    // Columns: 0 = the summed direction, 1 = c, 2 = d.
    out_rot[0] = u[0];
    out_rot[1] = c[0];
    out_rot[2] = d[0];
    out_rot[3] = u[1];
    out_rot[4] = c[1];
    out_rot[5] = d[1];
    out_rot[6] = u[2];
    out_rot[7] = c[2];
    out_rot[8] = d[2];
    out_rot[9] = 0.0f;
}

bool SplineStraightenRun(std::vector<SplineObjWaypointParams>& waypoints, std::size_t changed) {
    // Waypoint_Straighten, src/tagitem/spline.c:686-756.
    if (changed >= waypoints.size()) return false;

    std::size_t first;
    std::size_t last;
    if (waypoints[changed].straight) {
        first = last = changed;
    } else if (changed + 1 < waypoints.size()) {
        first = last = changed + 1;
    } else {
        return false;  // the engine would dereference a null `next` here
    }

    int count = 0;
    if (waypoints[first].straight) {
        // Walk back to the first non-straight waypoint, or to the front. The walk
        // counts every waypoint it passes INCLUDING the non-straight one it stops
        // on (:699-709), which is the anchor the even split starts from - so
        // `count` is exactly the number of steps from `first` to `last`.
        std::size_t p = first;
        while (p > 0) {
            first = p - 1;
            p = first;
            count++;
            if (!waypoints[first].straight) break;
        }
    }
    // Forward, counting only the straight members (:710-713).
    for (std::size_t p = last + 1; p < waypoints.size() && waypoints[p].straight; p++) {
        last = p;
        count++;
    }
    // A run with a single member: the engine returns without touching the frame
    // (:714-715), so a lone "Align" waypoint at the very start of a rail is left
    // completely alone.
    if (first == last) return false;

    const double dx = waypoints[last].point[0] - waypoints[first].point[0];
    float step[3];
    step[1] = static_cast<float>(waypoints[last].point[1] - waypoints[first].point[1]);
    step[2] = static_cast<float>(waypoints[last].point[2] - waypoints[first].point[2]);
    const long double inv = 1.0f / static_cast<long double>(count);
    const float invf = static_cast<float>(inv);
    step[0] = static_cast<float>(inv * dx);
    step[1] = invf * step[1];
    step[2] = invf * step[2];

    float pitch = 0.0f;
    float yaw = 0.0f;
    Vec3ToAngles(step, &pitch, &yaw);
    float m[10];
    // Matrix_SetEuler(&m, 0.0f, pitch, yaw) - the roll is forced to zero for a
    // straightened run (:727).
    EngineSetEuler(0.0f, pitch, yaw, m);
    // The engine keeps the frame; the editor's authored form is the Euler triple,
    // so it is converted back with the same rule Matrix_GetEuler uses. That
    // normalises into [0, 2pi), which is frame-equivalent because SetEuler builds
    // the matrix from sin/cos of the angles.
    float ori[3];
    EngineMatrixGetEuler(m, &ori[0], &ori[1], &ori[2]);

    double pos[3] = {waypoints[first].point[0], waypoints[first].point[1],
                     waypoints[first].point[2]};
    std::memcpy(waypoints[first].orientation, ori, sizeof(ori));
    for (std::size_t cur = first + 1; cur != last; cur++) {
        std::memcpy(waypoints[cur].orientation, ori, sizeof(ori));
        pos[0] += static_cast<double>(step[0]);
        pos[1] += static_cast<double>(step[1]);
        pos[2] += static_cast<double>(step[2]);
        std::memcpy(waypoints[cur].point, pos, sizeof(pos));
    }
    // `last` is the far ANCHOR of the even split, so the engine gives it the frame
    // but never a stepped position (:749-753): its authored position is what the
    // split is measured against, and `count` is exactly the number of steps from
    // `first` to `last`, so the run IS evenly divided. (:753-755 also re-runs the
    // segment setup for last and for the waypoint after it, which needs the world.)
    std::memcpy(waypoints[last].orientation, ori, sizeof(ori));
    return true;
}

void SplineContainerTranslate(std::vector<SplineObjWaypointParams>& waypoints,
                              const double delta[3], float angle) {
    // SplineObj_Changed, src/editobj/spline.c:296-315. The delta is rotated by
    // RotZ(angle) and added to every waypoint's absolute position.
    float m[10];
    EngineSetRotZ(angle, m);
    const float dx = static_cast<float>(delta[0]);
    const float dy = static_cast<float>(delta[1]);
    const float dz = static_cast<float>(delta[2]);
    const float x = m[2] * dz + m[1] * dy + m[0] * dx;
    const float y = m[5] * dz + m[4] * dy + m[3] * dx;
    const float z = m[8] * dz + m[7] * dy + m[6] * dx;
    for (SplineObjWaypointParams& w : waypoints) {
        w.point[0] += static_cast<double>(x);
        w.point[1] += static_cast<double>(y);
        w.point[2] += static_cast<double>(z);
    }
}

void SplineContainerRotate(std::vector<SplineObjWaypointParams>& waypoints, float delta_angle) {
    // SplineObj_Changed, src/editobj/spline.c:256-295. The pivot is the FIRST
    // waypoint's point (:263).
    if (waypoints.empty()) return;
    float m[10];
    EngineSetRotZ(delta_angle, m);
    const double cx = waypoints[0].point[0];
    const double cy = waypoints[0].point[1];
    const double cz = waypoints[0].point[2];
    for (SplineObjWaypointParams& w : waypoints) {
        float rot[10];
        SplineWaypointFrame(w, rot);
        const long double dx = w.point[0] - cx;
        const long double dy = w.point[1] - cy;
        const long double dz = w.point[2] - cz;
        // The engine declares dx/dy/dz as long double and `x`/`y` as float
        // (src/editobj/spline.c:264-276), so the products and the sum are 80-bit
        // and only the RESULT is narrowed; `z` stays long double (:276).
        const float x = static_cast<float>(static_cast<long double>(m[2]) * dz +
                                           static_cast<long double>(m[1]) * dy +
                                           static_cast<long double>(m[0]) * dx);
        const float y = static_cast<float>(static_cast<long double>(m[5]) * dz +
                                           static_cast<long double>(m[4]) * dy +
                                           static_cast<long double>(m[3]) * dx);
        const long double z = static_cast<long double>(m[8]) * dz +
                              static_cast<long double>(m[7]) * dy +
                              static_cast<long double>(m[6]) * dx;
        w.point[0] = static_cast<double>(x) + cx;
        w.point[1] = static_cast<double>(y) + cy;
        w.point[2] = static_cast<double>(z) + cz;

        // The engine's own element order, src/editobj/spline.c:278-286. It is
        // `rot_waypoint * RotZ(delta)` (row-major), not the pre-multiply a reader
        // might expect from a container rotation.
        float n[9];
        n[0] = m[0] * rot[0] + m[6] * rot[2] + m[3] * rot[1];
        n[1] = m[1] * rot[0] + m[7] * rot[2] + m[4] * rot[1];
        n[2] = m[2] * rot[0] + m[8] * rot[2] + m[5] * rot[1];
        n[3] = m[0] * rot[3] + m[6] * rot[5] + m[3] * rot[4];
        n[4] = m[1] * rot[3] + m[7] * rot[5] + m[4] * rot[4];
        n[5] = m[2] * rot[3] + m[8] * rot[5] + m[5] * rot[4];
        n[6] = m[6] * rot[8] + m[3] * rot[7] + m[0] * rot[6];
        n[7] = m[7] * rot[8] + m[4] * rot[7] + m[1] * rot[6];
        n[8] = m[8] * rot[8] + m[5] * rot[7] + m[2] * rot[6];
        // The engine stores a Matrix, not angles; the editor's authored form is
        // the Euler triple, so the frame is converted back with the same rule
        // Matrix_GetEuler uses.
        float euler[10] = {n[0], n[1], n[2], n[3], n[4], n[5], n[6], n[7], n[8], 0.0f};
        EngineMatrixGetEuler(euler, &w.orientation[0], &w.orientation[1], &w.orientation[2]);
    }
}

// ---------------------------------------------------------------------------
// The curve
// ---------------------------------------------------------------------------

HermiteBasis HermiteInit(double p0, double p1, double m0, double m1) {
    // Hermite_Init, HOOK(0x518130), src/terrdraw/flare.c:44-52.
    HermiteBasis h{};
    h.c[0] = m0;
    h.c[1] = p0;
    h.c[2] = p0 + p0 - (p1 + p1) + m0 + m1;
    h.c[3] = p1 * 3.0 - p0 * 3.0 - (m0 + m0) - m1;
    return h;
}

long double HermiteEval(const HermiteBasis& h, double u) {
    // Hermite_Eval, HOOK(0x518190), src/terrdraw/flare.c:54-61.
    const long double uu = static_cast<long double>(u) * u;
    return u * h.c[0] + (uu * u * h.c[2] + uu * h.c[3]) + h.c[1];
}

long double HermiteSlope(const HermiteBasis& h, double u) {
    // Hermite_Slope, HOOK(0x5181C0), src/terrdraw/flare.c:63-69.
    const long double t = static_cast<long double>(u) * h.c[3];
    return static_cast<long double>(u) * h.c[2] * u * 3.0 + (t + t) + h.c[0];
}

int SplineTruncateToInt(long double value) {
    // `x87_fistp`, src/dxinit/dxinit.h:22-28: `fistpll` into a long long under
    // the current x87 rounding mode, then `(int)r` - the low 32 bits. FPU_SetTruncate
    // (HOOK 0x498460, src/dxinit/dxinit.c:75-81) makes that mode round-toward-zero
    // at startup (src/win/appmain.c:212), so the conversion truncates.
    //
    // For a NaN, an infinity or anything outside the 64-bit range, `fistpll`
    // stores the integer-indefinite value 0x8000000000000000; its low 32 bits are
    // zero, so the engine returns 0. That is what makes the `end == start`
    // degenerate case well defined rather than a crash: every vertex's t is
    // +/-infinity or NaN, k becomes 0, and f becomes 0 or 1.
    if (!std::isfinite(value) || value >= 9223372036854775808.0L ||
        value < -9223372036854775808.0L) {
        return 0;
    }
    const long long r = static_cast<long long>(value);
    return static_cast<int>(r);
}

SplineSpanJoints BuildSplineSpanJoints(const SplineSpan& span) {
    SplineSpanJoints out;
    // SplineObjSegment_Setup, src/sound/segsetup.c:267-270.
    const int segments = SplineEffectiveSegmentCount(span.segments, kSplineMaxSegments, span.rigid);

    out.mid[0] = (span.from[0] + span.to[0]) * 0.5;  // :103-105
    out.mid[1] = (span.to[1] + span.from[1]) * 0.5;
    out.mid[2] = (span.to[2] + span.from[2]) * 0.5;
    out.joints.assign(static_cast<std::size_t>(segments) + 1, SplineJoint{});
    // The engine writes `o->segLength[o->segments + 1] = 0` after setup_segments
    // (src/sound/segsetup.c:252-253), so the array runs to segments + 1.
    out.seg_length.assign(static_cast<std::size_t>(segments) + 2, 0.0f);
    out.length = 0.0f;  // :106

    // :97-100: the chord length and the three Hermite bases. The tangents are
    // each waypoint frame's first column scaled by the chord.
    const double dist = Vec3dDistance(span.from, span.to);
    HermiteBasis h[3];
    for (int k = 0; k < 3; k++) {
        const int row = (k == 0) ? 0 : (k == 1 ? 3 : 6);
        h[k] = HermiteInit(span.from[k], span.to[k], static_cast<double>(span.rot_from[row]) * dist,
                           static_cast<double>(span.rot_to[row]) * dist);
    }

    float ax0 = 0.0f;
    float ay0 = 0.0f;
    float az0 = 0.0f;
    float ax1 = 0.0f;
    float ay1 = 0.0f;
    float az1 = 0.0f;
    EngineMatrixGetEuler(span.rot_from, &ax0, &ay0, &az0);  // :101
    EngineMatrixGetEuler(span.rot_to, &ax1, &ay1, &az1);    // :102

    // `inv` is a FLOAT: the oracle declares it `float inv;` (src/sound/segsetup.c:94)
    // and assigns it the LONG DOUBLE quotient `inv = kOne / (long double)o->segments`
    // (:107), which narrows to float. The loop then widens that float again for
    // `long double t = i * (long double)inv;` (:109). The narrowing is load-bearing:
    // for segments 10 and 20 it moves `tf = t` in the last bits, and `tf` feeds the
    // interpolated roll `ax = (a1f - a0f) * tf + a0f` (:188). Keeping the quotient
    // in long double here - as this port did - is not what the engine computes.
    // For segments == 0 the quotient is an infinity and every joint collapses; the
    // engine does not guard it and neither does this port.
    const float inv = static_cast<float>(1.0L / static_cast<long double>(segments));

    for (int i = 0; i <= segments; i++) {
        const long double t = i * static_cast<long double>(inv);
        const float tf = static_cast<float>(t);
        float* pos = out.joints[static_cast<std::size_t>(i)].pos;
        float* rot = out.joints[static_cast<std::size_t>(i)].rot;

        if (span.rigid) {
            // :114-142. A rigid span is a straight line at a single RotZ(yaw)
            // frame; the whole span length is recorded once, at i == 0.
            const long double px = t * (static_cast<long double>(span.to[0]) - span.from[0]) +
                                   span.from[0];
            const double py = (span.to[1] - span.from[1]) * tf + span.from[1];
            const double pz = (span.to[2] - span.from[2]) * tf + span.from[2];
            pos[0] = static_cast<float>(px - out.mid[0]);
            pos[1] = static_cast<float>(py - out.mid[1]);
            pos[2] = static_cast<float>(pz - out.mid[2]);
            std::memset(rot, 0, 40);  // :124
            const long double dx = static_cast<long double>(span.to[0]) - span.from[0];
            float d[3];
            d[0] = static_cast<float>(dx);
            d[1] = static_cast<float>(span.to[1] - span.from[1]);
            d[2] = static_cast<float>(span.to[2] - span.from[2]);
            float pitch = 0.0f;
            float yaw = 0.0f;
            if (!(dx == 0.0L && d[1] == 0.0f && d[2] == 0.0f)) {
                Vec3ToAngles(d, &pitch, &yaw);  // :129-134
            }
            (void)pitch;
            EngineSetRotZ(yaw, rot);  // :135
            if (i == 0) {
                // :136-142: the chord length TIMES inv. For a rigid span the
                // clamp above has already forced segments to 1, so this is the
                // chord length itself.
                const long double len =
                    X87Sqrt(static_cast<long double>(d[0]) * d[0] +
                            static_cast<long double>(d[1]) * d[1] +
                            static_cast<long double>(d[2]) * d[2]) * inv;
                out.seg_length[1] = static_cast<float>(len);
                out.length = static_cast<float>(len + out.length);
            }
            continue;
        }

        // :143-199, the curved span.
        long double a0 = ax0;
        long double a1 = ax1;
        float a0f = ax0;
        float a1f = ax1;
        // The unwrap uses the FLOAT pi (kPi, src/sound/segsetup.c:63), not the
        // long-double PI Matrix_GetEuler normalised with.
        if (static_cast<long double>(ax1) - a0 > kSplinePi) {
            do {
                a0 += kSplineTwoPi;
            } while (static_cast<long double>(ax1) - a0 > kSplinePi);
            a0f = static_cast<float>(a0);
        }
        if (!(static_cast<long double>(ax1) - a0 >= -kSplinePi)) {
            do {
                a1 += kSplineTwoPi;
            } while (!(a1 - a0 >= -kSplinePi));
            a1f = static_cast<float>(a1);
        }
        const double td = static_cast<double>(t);
        pos[0] = static_cast<float>(HermiteEval(h[0], td) - out.mid[0]);
        pos[1] = static_cast<float>(HermiteEval(h[1], td) - out.mid[1]);
        pos[2] = static_cast<float>(HermiteEval(h[2], td) - out.mid[2]);

        if (i == 0) {
            std::memcpy(rot, span.rot_from, 40);  // :164-165
            continue;
        }

        // :167-174 and :175-199 both measure the sub-segment from the PREVIOUS
        // joint, which in the engine's segPos[21][3] array is pos[-3..-1].
        const float* prev = out.joints[static_cast<std::size_t>(i) - 1].pos;
        const long double dx = static_cast<long double>(pos[0]) - prev[0];
        const double dy = static_cast<long double>(pos[1]) - prev[1];
        const double dz = static_cast<long double>(pos[2]) - prev[2];
        const long double len =
            X87Sqrt(dx * dx + dy * static_cast<long double>(dy) + dz * static_cast<long double>(dz));

        if (i == segments) {
            std::memcpy(rot, span.rot_to, 40);  // :171
        } else {
            float s[3];
            float pitch = 0.0f;
            float yaw = 0.0f;
            // The slope is ROUNDED TO FLOAT before Vec3_ToAngles (:179-181).
            s[0] = static_cast<float>(HermiteSlope(h[0], td));
            s[1] = static_cast<float>(HermiteSlope(h[1], td));
            s[2] = static_cast<float>(HermiteSlope(h[2], td));
            if (!(s[0] == 0.0f && s[1] == 0.0f && s[2] == 0.0f)) {
                Vec3ToAngles(s, &pitch, &yaw);
            }
            // Roll: the interpolated Euler X, with the ROLL constant across the
            // span; pitch and yaw come from the slope. NOT a zero-roll frame.
            const float ax = (a1f - a0f) * tf + a0f;
            EngineSetEuler(ax, pitch, yaw, rot);
        }
        out.seg_length[static_cast<std::size_t>(i)] = static_cast<float>(len);
        out.length = static_cast<float>(len + out.length);
    }

    // src/sound/segsetup.c:252-253.
    out.seg_length[static_cast<std::size_t>(segments) + 1] = 0.0f;
    return out;
}

float BuildSplineRigidFrame(const double from[3], const double to[3], float min_x, float max_x,
                            bool reversed, float out_rot[10]) {
    // setup_rigid, src/sound/segsetup.c:203-251.
    //
    // `if (Vec3d_Equal(from, to)) return radius;` (:209-211; Vec3d_Equal is `==`
    // on the three doubles, src/math/vec3.c:112-115): the frame is left UNTOUCHED
    // and the radius is 1. Reproduced - out_rot is not written.
    if (from[0] == to[0] && from[1] == to[1] && from[2] == to[2]) return 1.0f;

    const long double dx = static_cast<long double>(to[0]) - from[0];
    float d[3];
    d[0] = static_cast<float>(dx);
    d[1] = static_cast<float>(to[1] - from[1]);
    d[2] = static_cast<float>(to[2] - from[2]);
    if (dx == 0.0L && d[1] == 0.0f && d[2] == 0.0f) {
        d[0] = 1.0f;  // :218-220
        d[1] = 0.0f;
        d[2] = 0.0f;
    }

    // `k = kOne / ((long double)o->maxX - o->minX)` (:219): a LONG DOUBLE
    // reciprocal with no zero guard - maxX == minX gives an infinity.
    const long double k = 1.0L / (static_cast<long double>(max_x) - min_x);
    float pitch = 0.0f;
    float yaw = 0.0f;
    // The engine does NOT call Vec3_ToAngles here: it starts from
    // `transpose10(o->rot, SEG_ROT(o, 0))` (:220), the joint-0 frame that
    // setup_segments wrote for this same chord. That frame is RotZ(yaw) of this
    // same direction (:135), so recomputing it is equivalent - and for the
    // all-zero direction the engine's `pitch = yaw = 0` shortcut (:129-134) gives
    // the same yaw that Vec3_ToAngles' `IsZero(v[0])` guard yields.
    Vec3ToAngles(d, &pitch, &yaw);
    (void)pitch;

    float rot[10];
    EngineSetRotZ(yaw, rot);      // the joint-0 frame from setup_segments
    Transpose10(out_rot, rot);    // :220
    // The chord direction becomes the first ROW of the transposed frame...
    out_rot[0] = d[0];            // :221-223
    out_rot[1] = d[1];
    out_rot[2] = d[2];
    const float r0 = static_cast<float>(k * out_rot[0]);  // :224-229
    out_rot[0] = r0;
    const float r1 = static_cast<float>(k * out_rot[1]);
    out_rot[1] = r1;
    const float r2 = static_cast<float>(k * out_rot[2]);
    out_rot[2] = r2;
    if (reversed) {               // :230-235
        out_rot[0] = -r0;
        out_rot[1] = -r1;
        out_rot[2] = -r2;
    }
    Transpose10(out_rot, out_rot);  // :236 - ...and the second transpose makes it
                                    // the first COLUMN, i.e. the tangent.

    // :237-247: the bound is the longest ROW of the transposed frame, which is
    // the longest column of the frame itself.
    const long double n0 = X87Sqrt(static_cast<long double>(out_rot[0]) * out_rot[0] +
                                   static_cast<long double>(out_rot[1]) * out_rot[1] +
                                   static_cast<long double>(out_rot[2]) * out_rot[2]);
    const long double n1 = X87Sqrt(static_cast<long double>(out_rot[3]) * out_rot[3] +
                                   static_cast<long double>(out_rot[4]) * out_rot[4] +
                                   static_cast<long double>(out_rot[5]) * out_rot[5]);
    const long double n2 = X87Sqrt(static_cast<long double>(out_rot[6]) * out_rot[6] +
                                   static_cast<long double>(out_rot[7]) * out_rot[7] +
                                   static_cast<long double>(out_rot[8]) * out_rot[8]);
    long double radius = n0;
    if (!(n0 >= n1)) radius = n1;
    if (!(radius >= n2)) radius = n2;

    // :248-249 also fill `o->invRot` (transpose10 + Matrix_Invert). Nothing in
    // the decompilation reads invRot - the only occurrences are its declaration
    // (src/sound/segsetup.c:46) and those two writes - so it is not produced.
    return static_cast<float>(radius);
}

int SplineScanModelExtent(const SplineExtentSource* lods, int lod_count, int stride, float* out_min_x,
                          float* out_max_x) {
    // SplineObjSegment_Setup, src/sound/segsetup.c:277-293.
    float min_x = 0.0f;
    float max_x = 0.0f;
    int visited = 0;
    for (int k = 0; k < lod_count; k++) {
        if (lods[k].verts == nullptr) continue;
        const float* v = lods[k].verts;
        for (int j = 0; j < lods[k].vert_count; j++, v += stride) {
            if (!(*v >= min_x)) min_x = *v;
            if (*v > max_x) max_x = *v;
            visited++;
        }
    }
    if (out_min_x) *out_min_x = min_x;
    if (out_max_x) *out_max_x = max_x;
    return visited;
}

// ---------------------------------------------------------------------------
// SplineMesh_GetVerts
// ---------------------------------------------------------------------------

int SplineMeshGetVerts(const SplineSpanJoints& joints, const SplineMeshBend& bend,
                       const SplineMeshPart& part, float* out, int out_capacity) {
    // SplineMesh_GetVerts, HOOK(0x49A050), src/dxinit/spline.c:38-105.
    if (part.verts == nullptr || out == nullptr || part.vert_count <= 0) return 0;
    if (part.stride < 3) return 0;
    // The engine writes into the fixed 60000-float gSplineVerts buffer with no
    // check (src/dxinit/spline.c:118); this port refuses to overrun the caller's
    // buffer and reports 0 vertices instead of corrupting memory.
    if (static_cast<long long>(part.vert_count) * 3 > out_capacity) return 0;

    const float* v = part.verts;
    float* dst = out;

    if (bend.rigid) {
        // :48-59. `float mid = (o->start + o->end) * kHalfF` - the midpoint of
        // the MODEL's X range, which is what the setup_rigid frame was built to
        // map onto the chord.
        const float mid = (bend.extent.start + bend.extent.end) * 0.5f;
        const float* rot = bend.rigid_rot;
        if (rot == nullptr) return 0;
        for (int i = 0; i < part.vert_count; i++, v += part.stride, dst += 3) {
            // `long double px = v[0][0] - mid` is a FLOAT subtraction widened to
            // long double, not a long double subtraction.
            const long double px = v[0] - mid;
            const float py = v[1];
            const float pz = v[2];
            // The element order is the engine's literal order (:56-58), which is
            // the row-major product rot * (px, py, pz).
            dst[0] = static_cast<float>(px * rot[0] + pz * rot[2] + py * rot[1]);
            dst[1] = static_cast<float>(px * rot[3] + pz * rot[5] + py * rot[4]);
            dst[2] = static_cast<float>(px * rot[6] + pz * rot[8] + py * rot[7]);
        }
        return part.vert_count;
    }

    // :60-100. The model's X range maps onto joint 0 .. joint[segments].
    const int segments = static_cast<int>(joints.joints.size()) - 1;
    // The engine indexes segRot[k] and segRot[k + 1] (:80-83); with fewer than
    // two joints that reads before the array. Refused rather than reproduced.
    if (segments < 1) return 0;
    // `float inv = kOne / (o->end - o->start)` (:40): a FLOAT division with no
    // zero guard, computed before the rigid test in the engine.
    const float inv = 1.0f / (bend.extent.end - bend.extent.start);
    // `long double base = -(long double)(o->reversed ? o->end : o->start)` (:61).
    const long double base =
        -static_cast<long double>(bend.reversed ? bend.extent.end : bend.extent.start);
    // `long double scale = o->segments * (long double)inv` (:62).
    long double scale = static_cast<long double>(segments) * static_cast<long double>(inv);
    if (bend.reversed) scale = -scale;

    for (int i = 0; i < part.vert_count; i++, v += part.stride, dst += 3) {
        const long double t = (base + v[0]) * scale;
        int k = SplineTruncateToInt(t);
        // `if (k == o->segments) k--` (:73-74) absorbs the last joint's t == segments.
        if (k == segments) k--;
        // The engine indexes with no bounds check. Clamped here so a vertex
        // outside the model's own X range cannot read foreign memory; recorded
        // as a divergence in the header and the doc.
        if (k < 0) k = 0;
        if (k > segments - 1) k = segments - 1;
        long double f = t - k;
        // The clamp is `if (kZero > f) f = kZero; else if (!(kOne >= f)) f = kOne;`
        // (:76-79): a NaN f becomes 1.0, not 0.0.
        if (0.0f > f) {
            f = 0.0f;
        } else if (!(1.0f >= f)) {
            f = 1.0f;
        }
        const float* ra = joints.joints[static_cast<std::size_t>(k)].rot;
        const float* rb = joints.joints[static_cast<std::size_t>(k) + 1].rot;
        const float* pa = joints.joints[static_cast<std::size_t>(k)].pos;
        const float* pb = joints.joints[static_cast<std::size_t>(k) + 1].pos;

        // The oracle declares a0 and b0 as long double but a1/a2/b1/b2 as float
        // (:66-67), so each `r[2]*z + r[1]*y` sub-expression is a FLOAT
        // computation and only the accumulation with the joint position is long
        // double. Declared with exactly those types so the arithmetic matches.
        long double a0;
        float a1;
        float a2;
        long double b0;
        float b1;
        float b2;
        a0 = ra[2] * v[2] + ra[1] * v[1];
        a1 = ra[5] * v[2] + ra[4] * v[1];
        a2 = ra[8] * v[2] + ra[7] * v[1];
        a0 += pa[0];
        a1 = a1 + pa[1];
        a2 = a2 + pa[2];
        b0 = rb[2] * v[2] + rb[1] * v[1];
        b1 = rb[5] * v[2] + rb[4] * v[1];
        b2 = rb[8] * v[2] + rb[7] * v[1];
        b0 += pb[0];
        b1 = b1 + pb[1];
        b2 = b2 + pb[2];
        dst[0] = static_cast<float>((b0 - a0) * f + a0);
        dst[1] = static_cast<float>((b1 - a1) * f + a1);
        dst[2] = static_cast<float>((b2 - a2) * f + a2);
    }
    return part.vert_count;
}

float SplineRigidRadius(float best_lod_radius, float longest_frame_column) {
    // SplineObjSegment_Setup, src/sound/segsetup.c:294-306:
    //   radius = setup_rigid(...);      // the longest frame column
    //   best   = max over LODs of *(float *)(LOD_INFO(*models) + 0x78);
    //   radius = best * radius * kRadiusScale;
    // `best` is declared `long double best = kZero;` (:295) while `radius` is a
    // float, so the product is evaluated in LONG DOUBLE, left to right, and
    // narrowed once on the assignment to the float field `o->radius`. Computing
    // it in float rounds twice instead of once.
    const long double product = static_cast<long double>(best_lod_radius) *
                                static_cast<long double>(longest_frame_column) *
                                static_cast<long double>(kSplineRadiusScale);
    return static_cast<float>(product);
}

float SplineBentRadius(const SplineSpanJoints& joints, const SplineMeshBend& bend,
                       const SplineMeshPart* parts, int part_count) {
    // SplineObjSegment_Setup, src/sound/segsetup.c:307-322.
    float radius = 0.0f;
    std::vector<float> scratch;
    for (int p = 0; p < part_count; p++) {
        const SplineMeshPart& part = parts[p];
        if (part.vert_count <= 0) continue;
        const std::size_t need = static_cast<std::size_t>(part.vert_count) * 3u;
        scratch.resize(need);
        const int count = SplineMeshGetVerts(joints, bend, part, scratch.data(), static_cast<int>(need));
        const float* v = scratch.data();
        // `for (j = count; j > 0; j--, v += 3)` - walked down, though the maximum
        // does not care; the sum's order (x, z, y) is the engine's.
        for (int j = count; j > 0; j--, v += 3) {
            const long double d = X87Sqrt(static_cast<long double>(v[0]) * v[0] +
                                          static_cast<long double>(v[2]) * v[2] +
                                          static_cast<long double>(v[1]) * v[1]);
            if (!(radius > static_cast<float>(d))) radius = static_cast<float>(d);
        }
        // The floor is INSIDE the part loop, so part 0 can lift the radius to 1
        // before part 1 is measured.
        if (!(radius > 1.0f)) radius = 1.0f;
    }
    return radius;
}

// ---------------------------------------------------------------------------
// SplineCache_Remove / SplineCache_Reset
// ---------------------------------------------------------------------------

void SplineCache::Remove(const void* obj) {
    // SplineCache_Remove, HOOK(0x49A010), src/dxinit/spline.c:22-34. The loop
    // bound is 1 even though the array is declared with 2 entries.
    for (int i = 0; i < kLiveSlots; i++) {
        if (obj == slots_[i]) {
            slots_[i] = nullptr;
            data_[i] = 0;
            return;
        }
    }
}

void SplineCache::Reset() {
    // SplineCache_Reset, HOOK(0x49A340), src/dxinit/spline.c:107-111.
    slots_[0] = nullptr;
}

const void* SplineCache::Slot(int index) const {
    if (index < 0 || index >= kSlots) return nullptr;
    return slots_[index];
}

int SplineCache::Data(int index) const {
    if (index < 0 || index >= kSlots) return 0;
    return data_[index];
}

// ---------------------------------------------------------------------------
// Float_Log2
// ---------------------------------------------------------------------------
//
// The single definition lives in `source/runtime/mission_fog.cpp`, which ported
// it first for the fog rules (`mission_fog.h`). This module only declares it
// (`spline_rules.h`, `igi::FloatLog2`): the engine keeps `Float_Log2` in
// `src/dxinit/spline.c:14-19` (HOOK 0x499FF0) and more than one rule group needs
// it, but defining it here as well is a duplicate symbol when `igi-editor` and
// `igi_tests` link both translation units — which they do, since both are
// `source/runtime/*` and both are listed in `target_sources(igi_tests ...)`.
//
// The two bodies were identical: the raw IEEE-754 bit pattern compared as a
// SIGNED int, so every negative float (sign bit set), every sign-set NaN and
// everything in [0, 1) returns 0, and x >= 1 returns (bits - 0x3F800000) >> 20 =
// 8 * exponent + the top three mantissa bits.

}  // namespace igi
