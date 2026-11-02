// spline_rules.cpp - SplineObj and SplineObjWaypoint curve and geometric rules.
//
// MIT License.

#include "runtime/spline_rules.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace igi {
namespace {

// ---------------------------------------------------------------------------
// Parameter token readers
// ---------------------------------------------------------------------------

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
// Math and trigonometry helpers
// ---------------------------------------------------------------------------

constexpr long double kHalfPiL = 0x1.921fb54442d1bp+0L;
constexpr long double kTwoPiL = 0x1.921fb54442d1bp+2L;

inline long double MathSqrt(long double x) {
    return std::sqrt(x);
}

inline long double MathSin(long double x) {
    return std::sin(x);
}

inline long double MathCos(long double x) {
    return std::cos(x);
}

// 2D arc-tangent with zero-guarding fallback.
long double EngineAtan2(long double y, long double x) {
    if (x == 0.0L) {
        if (y == 0.0L) return kHalfPiL;
        const long double sign = y < 0.0L ? -1.0L : 1.0L;
        return sign * kHalfPiL;
    }
    return std::atan2(y, x);
}

// Checks if a floating point value is identically zero or unordered.
inline bool IsZero(float x) {
    return !std::islessgreater(x, 0.0f);
}

// Clamped arcsine to prevent domain errors on floating point imprecision.
double MathASinF(float x) {
    double d = x;
    if (!(d >= -1.0)) return std::asin(-1.0);
    if (d > 1.0) d = 1.0;
    return std::asin(d);
}

// Normalizes a 3D vector in-place given an explicit summation order of indices
// (index_a, index_b, index_c) to preserve identical floating-point rounding precision.
void Vec3fNormalizeOrdered(float* vector3, int index_a, int index_b, int index_c) {
    if (!IsZero(vector3[0]) || !IsZero(vector3[1]) || !IsZero(vector3[2])) {
        const long double sum_squares =
            static_cast<long double>(vector3[index_a]) * vector3[index_a] +
            static_cast<long double>(vector3[index_b]) * vector3[index_b] +
            static_cast<long double>(vector3[index_c]) * vector3[index_c];
        const long double inv_norm = 1.0L / MathSqrt(sum_squares);
        vector3[0] = static_cast<float>(vector3[0] * inv_norm);
        vector3[1] = static_cast<float>(vector3[1] * inv_norm);
        vector3[2] = static_cast<float>(vector3[2] * inv_norm);
    }
}

// ---------------------------------------------------------------------------
// Matrix linear algebra primitives
// ---------------------------------------------------------------------------

// 3x3 row-major matrix multiplication with high-precision accumulation.
// Multiplies out_matrix = matrix_a * matrix_b, evaluating products with indices (0, 2, 1).
void MatrixMul(float* out_matrix, const float* matrix_a, const float* matrix_b) {
    float result[9];
    for (int row = 0; row < 3; row++) {
        const float* row_ptr = &matrix_a[row * 3];
        for (int col = 0; col < 3; col++) {
            const long double term0 = static_cast<long double>(row_ptr[0]) * matrix_b[col];
            const long double term2 = static_cast<long double>(row_ptr[2]) * matrix_b[6 + col];
            const long double term1 = static_cast<long double>(row_ptr[1]) * matrix_b[3 + col];
            result[row * 3 + col] = static_cast<float>(term0 + term2 + term1);
        }
    }
    std::memcpy(out_matrix, result, sizeof(result));
}

// Constructs a 3x3 rotation matrix around the X axis for angle (radians).
void MatrixSetRotX(float* out_matrix, float angle) {
    const long double sin_val = MathSin(angle);
    const float cos_val = static_cast<float>(MathCos(angle));
    out_matrix[0] = 1.0f;
    out_matrix[1] = 0.0f;
    out_matrix[2] = 0.0f;
    out_matrix[3] = 0.0f;
    out_matrix[6] = 0.0f;
    out_matrix[4] = cos_val;
    out_matrix[5] = static_cast<float>(-sin_val);
    out_matrix[8] = cos_val;
    out_matrix[7] = static_cast<float>(sin_val);
}

// Constructs a 3x3 rotation matrix around the Y axis for angle (radians).
void MatrixSetRotY(float* out_matrix, float angle) {
    const long double sin_val = MathSin(angle);
    const long double cos_val = MathCos(angle);
    out_matrix[1] = 0.0f;
    out_matrix[3] = 0.0f;
    out_matrix[4] = 1.0f;
    out_matrix[5] = 0.0f;
    out_matrix[7] = 0.0f;
    out_matrix[0] = static_cast<float>(cos_val);
    out_matrix[2] = static_cast<float>(sin_val);
    out_matrix[6] = static_cast<float>(-sin_val);
    out_matrix[8] = static_cast<float>(cos_val);
}

// Transposes a 3x3 matrix in a 10-float buffer and copies the tenth element.
void Transpose10(float* dst_matrix, const float* src_matrix) {
    float temp[10];
    for (int row = 0; row < 3; row++) {
        for (int col = 0; col < 3; col++) {
            temp[row * 3 + col] = src_matrix[col * 3 + row];
        }
    }
    temp[9] = src_matrix[9];
    std::memcpy(dst_matrix, temp, sizeof(temp));
}

// Euclidean distance between two 3D points.
double Vec3dDistance(const double a[3], const double b[3]) {
    const long double dx = static_cast<long double>(a[0]) - b[0];
    const long double dy = static_cast<long double>(a[1]) - b[1];
    const long double dz = static_cast<long double>(a[2]) - b[2];
    return static_cast<double>(MathSqrt(dz * dz + dy * dy + dx * dx));
}

}  // namespace

// ---------------------------------------------------------------------------
// Parameter metadata tables
// ---------------------------------------------------------------------------

const SplineParamDesc* SplineObjParamTable() {
    static const SplineParamDesc kTable[kSplineObjParamCount] = {
        {"Linear Segments", "rigid", "bool8", 1, false, 0.0f, 0.0f},
        {"Display waypoints", "collide", "bool8", 1, false, 0.0f, 0.0f},
        {"Snap Length", "snap", "bool8", 1, false, 0.0f, 0.0f},
        {"Automatic Orientation", "autoOrient", "bool8", 1, false, 0.0f, 0.0f},
        {"Number of Matrices / Segment", "maxSegments", "Int32", 1, true, 2.0f, 20.0f},
        {"Collision LOD", "unk20", "Int32", 1, false, 0.0f, 0.0f},
        {"Position", "pos", "Real32x3", 3, false, 0.0f, 0.0f},
        {"Gamma Orientation", "angle", "Angle", 1, false, 0.0f, 0.0f},
        {"Dirlight Angular Effect", "color1", "RGB", 3, false, 0.0f, 0.0f},
        {"Dirlight Ambient Effect", "color2", "RGB", 3, false, 0.0f, 0.0f},
    };
    return kTable;
}

const SplineParamDesc* SplineObjWaypointParamTable() {
    static const SplineParamDesc kTable[kSplineObjWaypointParamCount] = {
        {"Orientation", "rot", "Real32x9", 3, false, 0.0f, 0.0f},
        {"Position", "point", "ObjectPos", 3, false, 0.0f, 0.0f},
        {"Waypoint Model", "model", "String16", 1, false, 0.0f, 0.0f},
        {"Segment Model", "segModel", "String16", 1, false, 0.0f, 0.0f},
        {"NumAreas", "segments", "Int32", 1, true, 2.0f, 20.0f},
        {"Align", "straight", "bool8", 1, false, 0.0f, 0.0f},
        {"Flip", "reversed", "bool8", 1, false, 0.0f, 0.0f},
        {"Automatic Orientation", "autoOrient", "bool8", 1, false, 0.0f, 0.0f},
    };
    return kTable;
}

// ---------------------------------------------------------------------------
// Parameter parsing
// ---------------------------------------------------------------------------

bool ParseSplineObjParams(const std::vector<std::string>& arg_tokens, SplineObjParams* out) {
    if (out == nullptr) return false;
    if (arg_tokens.size() < kSplineFirstParam + static_cast<std::size_t>(kSplineObjTokenCount)) {
        return false;
    }

    SplineObjParams parsed;
    const SplineParamDesc* table = SplineObjParamTable();
    std::size_t cursor = kSplineFirstParam;

    for (int i = 0; i < kSplineObjParamCount; i++) {
        const std::size_t base = cursor;
        switch (i) {
            case kSplineObjRigid:
                parsed.rigid = TokenBool(arg_tokens, base);
                break;
            case kSplineObjCollide:
                parsed.collide = TokenBool(arg_tokens, base);
                break;
            case kSplineObjSnap:
                parsed.snap = TokenBool(arg_tokens, base);
                break;
            case kSplineObjAutoOrient:
                parsed.auto_orient = TokenBool(arg_tokens, base);
                break;
            case kSplineObjMaxSegments:
                parsed.max_segments = TokenInt(arg_tokens, base, kSplineMaxSegments);
                break;
            case kSplineObjCollisionLod:
                parsed.collision_lod = TokenInt(arg_tokens, base);
                break;
            case kSplineObjPosition:
                parsed.pos[0] = TokenDouble(arg_tokens, base + 0);
                parsed.pos[1] = TokenDouble(arg_tokens, base + 1);
                parsed.pos[2] = TokenDouble(arg_tokens, base + 2);
                break;
            case kSplineObjAngle:
                parsed.angle = TokenFloat(arg_tokens, base, kSplinePi);
                break;
            case kSplineObjColour1:
                parsed.colour1[0] = TokenFloat(arg_tokens, base + 0, 1.0f);
                parsed.colour1[1] = TokenFloat(arg_tokens, base + 1, 1.0f);
                parsed.colour1[2] = TokenFloat(arg_tokens, base + 2, 1.0f);
                break;
            case kSplineObjColour2:
                parsed.colour2[0] = TokenFloat(arg_tokens, base + 0, 0.0f);
                parsed.colour2[1] = TokenFloat(arg_tokens, base + 1, 0.0f);
                parsed.colour2[2] = TokenFloat(arg_tokens, base + 2, 0.0f);
                break;
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
    if (arg_tokens.size() <
        kSplineFirstParam + static_cast<std::size_t>(kSplineObjWaypointTokenCount)) {
        return false;
    }

    SplineObjWaypointParams parsed;
    const SplineParamDesc* table = SplineObjWaypointParamTable();
    std::size_t cursor = kSplineFirstParam;

    for (int i = 0; i < kSplineObjWaypointParamCount; i++) {
        const std::size_t base = cursor;
        switch (i) {
            case kSplineWaypointOrientation:
                parsed.orientation[0] = TokenFloat(arg_tokens, base + 0);
                parsed.orientation[1] = TokenFloat(arg_tokens, base + 1);
                parsed.orientation[2] = TokenFloat(arg_tokens, base + 2);
                break;
            case kSplineWaypointPosition:
                parsed.point[0] = TokenDouble(arg_tokens, base + 0);
                parsed.point[1] = TokenDouble(arg_tokens, base + 1);
                parsed.point[2] = TokenDouble(arg_tokens, base + 2);
                break;
            case kSplineWaypointModel:
                parsed.model_name = TokenString(arg_tokens, base);
                break;
            case kSplineWaypointSegModel:
                parsed.seg_model = TokenString(arg_tokens, base);
                break;
            case kSplineWaypointSegments:
                parsed.segments = TokenInt(arg_tokens, base, kSplineMaxSegments);
                break;
            case kSplineWaypointStraight:
                parsed.straight = TokenBool(arg_tokens, base);
                break;
            case kSplineWaypointReversed:
                parsed.reversed = TokenBool(arg_tokens, base);
                break;
            case kSplineWaypointAutoOrient:
                parsed.auto_orient = TokenBool(arg_tokens, base);
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
// Derived evaluation rules
// ---------------------------------------------------------------------------

int SplineEffectiveSegmentCount(int waypoint_segments, int max_segments, bool rigid) {
    int n = waypoint_segments < max_segments ? waypoint_segments : max_segments;
    if (n > 20) n = 20;
    if (rigid) n = 1;
    return n;
}

bool SplineSpanIsRigid(bool container_rigid, bool waypoint_straight) {
    return container_rigid || waypoint_straight;
}

void SplineTangent(const float rot[10], double distance, double out[3]) {
    out[0] = static_cast<double>(rot[0]) * distance;
    out[1] = static_cast<double>(rot[3]) * distance;
    out[2] = static_cast<double>(rot[6]) * distance;
}

void EngineSetEuler(float ax, float ay, float az, float out[10]) {
    float m[10];
    float r[10];
    EngineSetRotZ(az, m);
    MatrixSetRotY(r, ay);
    MatrixMul(m, m, r);
    MatrixSetRotX(r, ax);
    MatrixMul(m, m, r);
    std::memcpy(out, m, sizeof(float) * 9);
    out[9] = 0.0f;
}

void EngineMatrixGetEuler(const float m[10], float* ax, float* ay, float* az) {
    const long double cy = MathSqrt(static_cast<long double>(m[0]) * m[0] +
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
    const long double s = MathSin(a);
    const float c = static_cast<float>(MathCos(a));
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
    if (IsZero(v[0])) {
        *yaw = 0.0f;
    } else {
        *yaw = static_cast<float>(
            std::atan2(static_cast<long double>(v[1]), static_cast<long double>(v[0])));
    }
    const long double len = MathSqrt(static_cast<long double>(v[0]) * v[0] +
                                     static_cast<long double>(v[1]) * v[1] +
                                     static_cast<long double>(v[2]) * v[2]);
    if (IsZero(static_cast<float>(len))) {
        *pitch = 0.0f;
    } else {
        const float ratio = static_cast<float>(static_cast<long double>(v[2]) / len);
        *pitch = static_cast<float>(-MathASinF(ratio));
    }
}

void SplineWaypointFrame(const SplineObjWaypointParams& waypoint, float out_rot[10]) {
    EngineSetEuler(waypoint.orientation[0], waypoint.orientation[1], waypoint.orientation[2],
                   out_rot);
}

void SplineNewWaypointPoint(const SplineObjWaypointParams& neighbour, SplineNewWaypointStep step,
                            double model_extent, bool step_forward, double out_point[3]) {
    float rot[10];
    SplineWaypointFrame(neighbour, rot);
    out_point[0] = neighbour.point[0];
    out_point[1] = neighbour.point[1];
    out_point[2] = neighbour.point[2];

    long double x = rot[0];
    float y = rot[3];
    float z = rot[6];
    if (!IsZero(rot[0]) || !IsZero(y) || !IsZero(z)) {
        const long double sq = step_forward
                                   ? (x * x + y * y + z * z)
                                   : static_cast<long double>(
                                         static_cast<float>(rot[0]) * rot[0] + y * y + z * z);
        const long double inv = 1.0L / MathSqrt(sq);
        x = x * inv;
        y = static_cast<float>(y * inv);
        z = static_cast<float>(z * inv);
    }

    if (step_forward) {
        const long double len =
            (step == kSplineStepModelExtent) ? model_extent : kSplineDefaultSegLength;
        out_point[0] += static_cast<double>(x * len);
        out_point[1] += static_cast<double>(y * static_cast<float>(len));
        out_point[2] += static_cast<double>(z * static_cast<float>(len));
    } else {
        float dx = static_cast<float>(x);
        float dy = y;
        float dz = z;
        if (step == kSplineStepDefaultLength) {
            dx = static_cast<float>(kSplineDefaultSegLength * dx);
            dy = static_cast<float>(kSplineDefaultSegLength * dy);
            dz = static_cast<float>(kSplineDefaultSegLength * dz);
        } else if (step == kSplineStepModelExtent) {
            dx = static_cast<float>(model_extent * static_cast<double>(dx));
            dy = static_cast<float>(model_extent * static_cast<double>(dy));
            dz = static_cast<float>(model_extent * static_cast<double>(dz));
        }
        out_point[0] -= static_cast<double>(dx);
        out_point[1] -= static_cast<double>(dy);
        out_point[2] -= static_cast<double>(dz);
    }
}

void SplineSnapPoint(const double previous[3], const double point[3], double model_extent,
                     double out_point[3]) {
    float dx = static_cast<float>(point[0] - previous[0]);
    float dy = static_cast<float>(point[1] - previous[1]);
    const long double dz = static_cast<long double>(point[2]) - previous[2];
    const long double len = model_extent;
    const long double k = len / MathSqrt((dx * dx + dy * dy) + dz * dz);
    dx = static_cast<float>(dx * k);
    dy = static_cast<float>(dy * k);
    out_point[0] = static_cast<double>(dx) + previous[0];
    out_point[1] = static_cast<double>(dy) + previous[1];
    out_point[2] = static_cast<double>(dz * k) + previous[2];
}

void SplineAutoOrientFrame(const double* previous, const double* point, const double* next,
                           float out_rot[10]) {
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
            const long double inv = 1.0L / MathSqrt(b * b + a * a);
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
    if (changed >= waypoints.size()) return false;

    std::size_t first;
    std::size_t last;
    if (waypoints[changed].straight) {
        first = last = changed;
    } else if (changed + 1 < waypoints.size()) {
        first = last = changed + 1;
    } else {
        return false;
    }

    int count = 0;
    if (waypoints[first].straight) {
        std::size_t p = first;
        while (p > 0) {
            first = p - 1;
            p = first;
            count++;
            if (!waypoints[first].straight) break;
        }
    }
    for (std::size_t p = last + 1; p < waypoints.size() && waypoints[p].straight; p++) {
        last = p;
        count++;
    }
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
    EngineSetEuler(0.0f, pitch, yaw, m);

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
    std::memcpy(waypoints[last].orientation, ori, sizeof(ori));
    return true;
}

void SplineContainerTranslate(std::vector<SplineObjWaypointParams>& waypoints,
                              const double delta[3], float angle) {
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
        float euler[10] = {n[0], n[1], n[2], n[3], n[4], n[5], n[6], n[7], n[8], 0.0f};
        EngineMatrixGetEuler(euler, &w.orientation[0], &w.orientation[1], &w.orientation[2]);
    }
}

// ---------------------------------------------------------------------------
// Hermite curve evaluation
// ---------------------------------------------------------------------------

HermiteBasis HermiteInit(double start_point, double end_point, double start_tangent, double end_tangent) {
    HermiteBasis hermite{};
    hermite.c[0] = start_tangent;
    hermite.c[1] = start_point;
    hermite.c[2] = start_point + start_point - (end_point + end_point) + start_tangent + end_tangent;
    hermite.c[3] = end_point * 3.0 - start_point * 3.0 - (start_tangent + start_tangent) - end_tangent;
    return hermite;
}

long double HermiteEval(const HermiteBasis& hermite, double param_u) {
    const long double param_u_sq = static_cast<long double>(param_u) * param_u;
    return param_u * hermite.c[0] + (param_u_sq * param_u * hermite.c[2] + param_u_sq * hermite.c[3]) + hermite.c[1];
}

long double HermiteSlope(const HermiteBasis& hermite, double param_u) {
    const long double slope_term = static_cast<long double>(param_u) * hermite.c[3];
    return static_cast<long double>(param_u) * hermite.c[2] * param_u * 3.0 + (slope_term + slope_term) + hermite.c[0];
}

// 64-bit integer limits for floating-point truncation guard.
// In C/C++ notation:
// LONG_MIN = -9223372036854775808L
// LONG_MAX =  9223372036854775807L
constexpr long double kSplineTruncateLimitMin = -9223372036854775808.0L;
constexpr long double kSplineTruncateLimitMax =  9223372036854775807.0L;

int SplineTruncateToInt(long double value) {
    if (!std::isfinite(value) || value > kSplineTruncateLimitMax ||
        value < kSplineTruncateLimitMin) {
        return 0;
    }
    const long long integer_val = static_cast<long long>(value);
    return static_cast<int>(integer_val);
}

SplineSpanJoints BuildSplineSpanJoints(const SplineSpan& span) {
    SplineSpanJoints out;
    const int segments = SplineEffectiveSegmentCount(span.segments, kSplineMaxSegments, span.rigid);

    out.mid[0] = (span.from[0] + span.to[0]) * 0.5;
    out.mid[1] = (span.to[1] + span.from[1]) * 0.5;
    out.mid[2] = (span.to[2] + span.from[2]) * 0.5;
    out.joints.assign(static_cast<std::size_t>(segments) + 1, SplineJoint{});
    out.seg_length.assign(static_cast<std::size_t>(segments) + 2, 0.0f);
    out.length = 0.0f;

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
    EngineMatrixGetEuler(span.rot_from, &ax0, &ay0, &az0);
    EngineMatrixGetEuler(span.rot_to, &ax1, &ay1, &az1);

    const float inv = static_cast<float>(1.0L / static_cast<long double>(segments));

    for (int i = 0; i <= segments; i++) {
        const long double t = i * static_cast<long double>(inv);
        const float tf = static_cast<float>(t);
        float* pos = out.joints[static_cast<std::size_t>(i)].pos;
        float* rot = out.joints[static_cast<std::size_t>(i)].rot;

        if (span.rigid) {
            const long double px = t * (static_cast<long double>(span.to[0]) - span.from[0]) +
                                   span.from[0];
            const double py = (span.to[1] - span.from[1]) * tf + span.from[1];
            const double pz = (span.to[2] - span.from[2]) * tf + span.from[2];
            pos[0] = static_cast<float>(px - out.mid[0]);
            pos[1] = static_cast<float>(py - out.mid[1]);
            pos[2] = static_cast<float>(pz - out.mid[2]);
            std::memset(rot, 0, 40);
            const long double dx = static_cast<long double>(span.to[0]) - span.from[0];
            float d[3];
            d[0] = static_cast<float>(dx);
            d[1] = static_cast<float>(span.to[1] - span.from[1]);
            d[2] = static_cast<float>(span.to[2] - span.from[2]);
            float pitch = 0.0f;
            float yaw = 0.0f;
            if (!(dx == 0.0L && d[1] == 0.0f && d[2] == 0.0f)) {
                Vec3ToAngles(d, &pitch, &yaw);
            }
            (void)pitch;
            EngineSetRotZ(yaw, rot);
            if (i == 0) {
                const long double len =
                    MathSqrt(static_cast<long double>(d[0]) * d[0] +
                             static_cast<long double>(d[1]) * d[1] +
                             static_cast<long double>(d[2]) * d[2]) * inv;
                out.seg_length[1] = static_cast<float>(len);
                out.length = static_cast<float>(len + out.length);
            }
            continue;
        }

        long double a0 = ax0;
        long double a1 = ax1;
        float a0f = ax0;
        float a1f = ax1;
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
            std::memcpy(rot, span.rot_from, 40);
            continue;
        }

        const float* prev = out.joints[static_cast<std::size_t>(i) - 1].pos;
        const long double dx = static_cast<long double>(pos[0]) - prev[0];
        const double dy = static_cast<long double>(pos[1]) - prev[1];
        const double dz = static_cast<long double>(pos[2]) - prev[2];
        const long double len =
            MathSqrt(dx * dx + dy * static_cast<long double>(dy) + dz * static_cast<long double>(dz));

        if (i == segments) {
            std::memcpy(rot, span.rot_to, 40);
        } else {
            float s[3];
            float pitch = 0.0f;
            float yaw = 0.0f;
            s[0] = static_cast<float>(HermiteSlope(h[0], td));
            s[1] = static_cast<float>(HermiteSlope(h[1], td));
            s[2] = static_cast<float>(HermiteSlope(h[2], td));
            if (!(s[0] == 0.0f && s[1] == 0.0f && s[2] == 0.0f)) {
                Vec3ToAngles(s, &pitch, &yaw);
            }
            const float ax = (a1f - a0f) * tf + a0f;
            EngineSetEuler(ax, pitch, yaw, rot);
        }
        out.seg_length[static_cast<std::size_t>(i)] = static_cast<float>(len);
        out.length = static_cast<float>(len + out.length);
    }

    out.seg_length[static_cast<std::size_t>(segments) + 1] = 0.0f;
    return out;
}

float BuildSplineRigidFrame(const double from[3], const double to[3], float min_x, float max_x,
                            bool reversed, float out_rot[10]) {
    if (from[0] == to[0] && from[1] == to[1] && from[2] == to[2]) return 1.0f;

    const long double dx = static_cast<long double>(to[0]) - from[0];
    float d[3];
    d[0] = static_cast<float>(dx);
    d[1] = static_cast<float>(to[1] - from[1]);
    d[2] = static_cast<float>(to[2] - from[2]);
    if (dx == 0.0L && d[1] == 0.0f && d[2] == 0.0f) {
        d[0] = 1.0f;
        d[1] = 0.0f;
        d[2] = 0.0f;
    }

    const long double k = 1.0L / (static_cast<long double>(max_x) - min_x);
    float pitch = 0.0f;
    float yaw = 0.0f;
    Vec3ToAngles(d, &pitch, &yaw);
    (void)pitch;

    float rot[10];
    EngineSetRotZ(yaw, rot);
    Transpose10(out_rot, rot);
    out_rot[0] = d[0];
    out_rot[1] = d[1];
    out_rot[2] = d[2];
    const float r0 = static_cast<float>(k * out_rot[0]);
    out_rot[0] = r0;
    const float r1 = static_cast<float>(k * out_rot[1]);
    out_rot[1] = r1;
    const float r2 = static_cast<float>(k * out_rot[2]);
    out_rot[2] = r2;
    if (reversed) {
        out_rot[0] = -r0;
        out_rot[1] = -r1;
        out_rot[2] = -r2;
    }
    Transpose10(out_rot, out_rot);

    const long double n0 = MathSqrt(static_cast<long double>(out_rot[0]) * out_rot[0] +
                                    static_cast<long double>(out_rot[1]) * out_rot[1] +
                                    static_cast<long double>(out_rot[2]) * out_rot[2]);
    const long double n1 = MathSqrt(static_cast<long double>(out_rot[3]) * out_rot[3] +
                                    static_cast<long double>(out_rot[4]) * out_rot[4] +
                                    static_cast<long double>(out_rot[5]) * out_rot[5]);
    const long double n2 = MathSqrt(static_cast<long double>(out_rot[6]) * out_rot[6] +
                                    static_cast<long double>(out_rot[7]) * out_rot[7] +
                                    static_cast<long double>(out_rot[8]) * out_rot[8]);
    long double radius = n0;
    if (!(n0 >= n1)) radius = n1;
    if (!(radius >= n2)) radius = n2;

    return static_cast<float>(radius);
}

int SplineScanModelExtent(const SplineExtentSource* lods, int lod_count, int stride, float* out_min_x,
                          float* out_max_x) {
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
// Mesh deformation
// ---------------------------------------------------------------------------

int SplineMeshGetVerts(const SplineSpanJoints& joints, const SplineMeshBend& bend,
                       const SplineMeshPart& part, float* out, int out_capacity) {
    if (part.verts == nullptr || out == nullptr || part.vert_count <= 0) return 0;
    if (part.stride < 3) return 0;
    if (static_cast<long long>(part.vert_count) * 3 > out_capacity) return 0;

    const float* v = part.verts;
    float* dst = out;

    if (bend.rigid) {
        const float mid = (bend.extent.start + bend.extent.end) * 0.5f;
        const float* rot = bend.rigid_rot;
        if (rot == nullptr) return 0;
        for (int i = 0; i < part.vert_count; i++, v += part.stride, dst += 3) {
            const long double px = v[0] - mid;
            const float py = v[1];
            const float pz = v[2];
            dst[0] = static_cast<float>(px * rot[0] + pz * rot[2] + py * rot[1]);
            dst[1] = static_cast<float>(px * rot[3] + pz * rot[5] + py * rot[4]);
            dst[2] = static_cast<float>(px * rot[6] + pz * rot[8] + py * rot[7]);
        }
        return part.vert_count;
    }

    const int segments = static_cast<int>(joints.joints.size()) - 1;
    if (segments < 1) return 0;

    const float inv = 1.0f / (bend.extent.end - bend.extent.start);
    const long double base =
        -static_cast<long double>(bend.reversed ? bend.extent.end : bend.extent.start);
    long double scale = static_cast<long double>(segments) * static_cast<long double>(inv);
    if (bend.reversed) scale = -scale;

    for (int i = 0; i < part.vert_count; i++, v += part.stride, dst += 3) {
        const long double t = (base + v[0]) * scale;
        int k = SplineTruncateToInt(t);
        if (k == segments) k--;
        if (k < 0) k = 0;
        if (k > segments - 1) k = segments - 1;
        long double f = t - k;
        if (0.0f > f) {
            f = 0.0f;
        } else if (!(1.0f >= f)) {
            f = 1.0f;
        }
        const float* ra = joints.joints[static_cast<std::size_t>(k)].rot;
        const float* rb = joints.joints[static_cast<std::size_t>(k) + 1].rot;
        const float* pa = joints.joints[static_cast<std::size_t>(k)].pos;
        const float* pb = joints.joints[static_cast<std::size_t>(k) + 1].pos;

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
    const long double product = static_cast<long double>(best_lod_radius) *
                                static_cast<long double>(longest_frame_column) *
                                static_cast<long double>(kSplineRadiusScale);
    return static_cast<float>(product);
}

float SplineBentRadius(const SplineSpanJoints& joints, const SplineMeshBend& bend,
                       const SplineMeshPart* parts, int part_count) {
    float radius = 0.0f;
    std::vector<float> scratch;
    for (int p = 0; p < part_count; p++) {
        const SplineMeshPart& part = parts[p];
        if (part.vert_count <= 0) continue;
        const std::size_t need = static_cast<std::size_t>(part.vert_count) * 3u;
        scratch.resize(need);
        const int count = SplineMeshGetVerts(joints, bend, part, scratch.data(), static_cast<int>(need));
        const float* v = scratch.data();
        for (int j = count; j > 0; j--, v += 3) {
            const long double d = MathSqrt(static_cast<long double>(v[0]) * v[0] +
                                           static_cast<long double>(v[2]) * v[2] +
                                           static_cast<long double>(v[1]) * v[1]);
            if (!(radius > static_cast<float>(d))) radius = static_cast<float>(d);
        }
        if (!(radius > 1.0f)) radius = 1.0f;
    }
    return radius;
}

// ---------------------------------------------------------------------------
// Spline cache
// ---------------------------------------------------------------------------

void SplineCache::Remove(const void* obj) {
    for (int i = 0; i < kLiveSlots; i++) {
        if (obj == slots_[i]) {
            slots_[i] = nullptr;
            data_[i] = 0;
            return;
        }
    }
}

void SplineCache::Reset() {
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

}  // namespace igi
