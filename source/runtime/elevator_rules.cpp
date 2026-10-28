// elevator_rules.cpp - see elevator_rules.h for the oracle and the scope.
#include "runtime/elevator_rules.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>

#include "runtime/anim_rules.h"

namespace igi {

namespace {

// `kHalf` (src/math/mathvars.h:21) is 0.5f.
constexpr float kHalf = 0.5f;

// `kSmallAngle` DATA(0x533714) INIT(0.01f) (src/tagitem/spline.c:850).
constexpr float kSmallAngle = 0.01f;

// The engine's x87 intrinsics, on the host libm. The oracle is compiled with
// excess precision kept, so every intermediate here is `long double`.
inline long double X87Sqrt(long double x) { return std::sqrt(x); }
inline long double X87Sin(long double x) { return std::sin(x); }

// `IsZeroL` (src/tagitem/spline.c:62-65): the original's fcom/test treats a
// value as zero unless it is strictly less-or-greater than 0, i.e. NaN and 0
// both count as zero.
inline bool IsZeroL(long double x) { return !(x < 0.0L || x > 0.0L); }

// The engine's row-major `m[9]` -> glm. `g[col][row] == m[row * 3 + col]`, the
// same mapping `renderer/spline_geometry.cpp`'s ToGlm/FromGlm use.
inline double E(const glm::dmat3& g, int row, int col) { return g[col][row]; }

// `Rot_ToQuat` (static, src/tagitem/spline.c:85-117). `q` is (x, y, z, w) plus
// the engine's depth counter at q[4], which this port does not carry (it is
// never read by the spline path: `Segment_BlendRot` only copies it through).
void RotToQuat(double q[4], const glm::dmat3& e) {
    static const int next[3] = {1, 2, 0};
    const long double t = (E(e, 2, 2) + E(e, 1, 1)) + E(e, 0, 0);

    if (t > 0.0f) {
        long double s = X87Sqrt(t + 1.0f);

        q[3] = static_cast<double>(s * kHalf);
        s = kHalf / s;
        // e[5] - e[7] == g[2][1] - g[1][2] == E(e, 1, 2) - E(e, 2, 1)
        q[0] = -((E(e, 1, 2) - E(e, 2, 1)) * s);
        // e[6] - e[2] == g[0][2] - g[2][0] == E(e, 2, 0) - E(e, 0, 2)
        q[1] = -((E(e, 2, 0) - E(e, 0, 2)) * s);
        // e[1] - e[3] == g[1][0] - g[0][1] == E(e, 0, 1) - E(e, 1, 0)
        q[2] = -((E(e, 0, 1) - E(e, 1, 0)) * s);
    } else {
        int i = 0;
        long double s;

        if (E(e, 1, 1) > E(e, 0, 0)) i = 1;
        if (E(e, 2, 2) > E(e, i, i)) i = 2;
        const int j = next[i];
        const int k = next[j];
        s = X87Sqrt(((E(e, i, i) - E(e, j, j)) - E(e, k, k)) + 1.0f);
        q[i] = static_cast<double>(s * kHalf);
        if (!IsZeroL(s)) s = kHalf / s;
        q[j] = (E(e, i, j) + E(e, j, i)) * s;
        q[k] = (E(e, i, k) + E(e, k, i)) * s;
        q[3] = -((E(e, k, j) - E(e, j, k)) * s);
    }
}

// The tail of `Segment_BlendRot` (src/tagitem/spline.c:159-181): a unit
// quaternion to the engine's row-major matrix.
glm::dmat3 QuatToMatrix(const double r[4]) {
    long double a = r[0] * r[0];
    a = a + a;
    long double b = r[1] * r[0];
    b = b + b;
    long double c = 2 * (r[2] * r[0]);
    long double d = 2 * (r[3] * r[0]);
    long double e = 2 * (r[1] * r[1]);
    long double f = 2 * (r[2] * r[1]);
    long double g = 2 * (r[3] * r[1]);
    long double h = 2 * (r[2] * r[2]);
    long double iz = r[3] * r[2];
    iz = iz + iz;

    // m[0] = (1 - e) - h    m[1] = b - iz      m[2] = g + c
    // m[3] = iz + b         m[4] = (1 - a) - h  m[5] = f - d
    // m[6] = c - g          m[7] = f + d        m[8] = (1 - a) - e
    glm::dmat3 out(1.0);
    out[0][0] = static_cast<double>((1.0f - e) - h);
    out[1][0] = static_cast<double>(b - iz);
    out[2][0] = static_cast<double>(g + c);
    out[0][1] = static_cast<double>(iz + b);
    out[1][1] = static_cast<double>((1.0f - a) - h);
    out[2][1] = static_cast<double>(f - d);
    out[0][2] = static_cast<double>(c - g);
    out[1][2] = static_cast<double>(f + d);
    out[2][2] = static_cast<double>((1.0f - a) - e);
    return out;
}

std::string ToLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

// The engine's Bool8 script token. `ParamBool8_Parse` reads the script's
// boolean literal; the decompiled text spells it TRUE/FALSE and the editor
// keeps the raw token, so accept the numeric spellings too.
bool TokenBool(const std::vector<std::string>& tokens, std::size_t index, bool fallback) {
    if (index >= tokens.size()) return fallback;
    const std::string t = ToLower(tokens[index]);
    if (t == "true" || t == "1") return true;
    if (t == "false" || t == "0") return false;
    return fallback;
}

}  // namespace

ElevatorParams ParseElevatorParams(const std::vector<std::string>& arg_tokens) {
    ElevatorParams out;
    if (arg_tokens.size() < kElevatorMinTokens) return out;

    const std::string& graph = arg_tokens[kElevatorGraphArg];
    char* end = nullptr;
    const long value = std::strtol(graph.c_str(), &end, 10);
    if (end == graph.c_str() || (end && *end != '\0')) return out;

    out.graph = static_cast<int>(value);
    out.linear = TokenBool(arg_tokens, kElevatorLinearArg, false);
    out.force_orientation = TokenBool(arg_tokens, kElevatorForceOrientationArg, false);

    const std::string& height = arg_tokens[kElevatorStartHeightArg];
    end = nullptr;
    out.start_height = std::strtod(height.c_str(), &end);
    if (end == height.c_str()) out.start_height = 0.0;

    out.parsed = true;
    return out;
}

glm::dmat3 BlendJointRotation(const glm::dmat3& a, const glm::dmat3& b, double t) noexcept {
    double q0[4];
    double q1[4];
    double r[4];

    RotToQuat(q0, a);
    RotToQuat(q1, b);

    EngineQuat ea;
    ea.x = static_cast<float>(q0[0]);
    ea.y = static_cast<float>(q0[1]);
    ea.z = static_cast<float>(q0[2]);
    ea.w = static_cast<float>(q0[3]);
    EngineQuat eb;
    eb.x = static_cast<float>(q1[0]);
    eb.y = static_cast<float>(q1[1]);
    eb.z = static_cast<float>(q1[2]);
    eb.w = static_cast<float>(q1[3]);

    int flip = 0;
    float angle = 0.0f;
    float invSin = 1.0f;
    QuatSlerpPrepare(flip, angle, invSin, ea, eb);
    if (flip) {
        q1[0] = -q1[0];
        q1[1] = -q1[1];
        q1[2] = -q1[2];
        q1[3] = -q1[3];
    }

    if (!(angle < 0.0f || angle > 0.0f)) {  // IsZero(angle)
        r[0] = q0[0];
        r[1] = q0[1];
        r[2] = q0[2];
        r[3] = q0[3];
    } else {
        long double s0;
        long double s1;

        if (!(angle >= kSmallAngle)) {
            s0 = 1.0f - t;
            s1 = t;
        } else {
            const long double u = angle * t;
            s0 = X87Sin(angle - u) * invSin;
            s1 = X87Sin(u) * invSin;
        }
        r[0] = static_cast<double>(s1 * q1[0] + s0 * q0[0]);
        r[1] = static_cast<double>(s1 * q1[1] + s0 * q0[1]);
        r[2] = static_cast<double>(s1 * q1[2] + s0 * q0[2]);
        r[3] = static_cast<double>(s1 * q1[3] + s0 * q0[3]);
    }

    return QuatToMatrix(r);
}

ElevatorPlacement PlaceElevatorOnRail(const ElevatorRail& rail,
                                      const ElevatorParams& params,
                                      const glm::dmat3& place_matrix) noexcept {
    ElevatorPlacement out;
    out.matrix = place_matrix;

    if (params.graph == -1) return out;

    const double len = rail.path.length;
    double dist = params.start_height;
    double height = params.start_height;

    // `if (dist >= len) { dist = len - kTenth; MOV32(e->height, len); }`
    // `if (!(dist >= kZero)) { dist = 0; e->height = 0; }`
    if (dist >= len) {
        dist = len - static_cast<double>(kElevatorEndEpsilon);
        height = len;
    }
    if (!(dist >= 0.0)) {
        dist = 0.0;
        height = 0.0;
    }
    out.height = height;

    if (params.linear) {
        // `t = e->height / len` -- the CLAMPED height, over the rail length.
        // A zero-length rail has no parameterisation; the engine would divide by
        // zero, so it is refused rather than reproduced.
        if (!(len > 0.0)) return out;
        const double t = height / len;
        out.position = rail.first_waypoint + (rail.last_waypoint - rail.first_waypoint) * t;
        out.found = true;
        return out;
    }

    // The `else` branch: `SplineObj_GetPoints(graph, &m, &e->place, &found,
    // dist, 0, 0, offset)` with `offset = {0, 0, 0}`.
    if (rail.path.joints.size() < 2 || !(len > 0.0)) return out;

    // The engine wraps `dist` into [0, length) first (src/tagitem/spline.c:243-255)
    // and refuses a non-positive result (`if (!(d > 0.0f)) { *found = 0; return; }`).
    // `dist` is already inside [0, length] here because of the clamps above; the
    // only value that reaches the refusal is exactly 0.
    const double d = spline_geometry::WrapRailDistance(dist, len);
    if (!(d > 0.0)) return out;

    // Walk to the joint pair containing `d`. `path.arc[i]` is the cumulative
    // chord length to joint i, which is what `SEG_LEN` accumulates
    // (src/sound/segsetup.c:141,174,195 -> `o->length = len + o->length`).
    std::size_t i = 0;
    while (i + 2 < rail.path.arc.size() && rail.path.arc[i + 1] < d) ++i;
    const double segLen = rail.path.arc[i + 1] - rail.path.arc[i];
    const double t = (segLen > 0.0) ? (d - rail.path.arc[i]) / segLen : 0.0;

    out.position = rail.path.joints[i].pos +
                   (rail.path.joints[i + 1].pos - rail.path.joints[i].pos) * t;
    out.found = true;

    // `if (!e->unk18D) mat_mul(&e->obj.matrix, &e->placeMatrix, &m, 4);`
    // mat_mul is `out = a * b` (src/props/props.h:69-87); `b_rows = 4` only
    // reorders the additions inside row 2, which does not change the value.
    if (!params.force_orientation) {
        const glm::dmat3 frame =
            BlendJointRotation(rail.path.joints[i].rot, rail.path.joints[i + 1].rot, t);
        out.matrix = place_matrix * frame;
    }
    return out;
}

}  // namespace igi
