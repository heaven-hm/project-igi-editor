// elevator_rules.cpp - Elevator rail placement and parameter parsing implementation.
#include "runtime/elevator_rules.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>

#include "runtime/anim_rules.h"

namespace igi {

namespace {

constexpr float kHalf = 0.5f;
constexpr float kSmallAngle = 0.01f;

inline long double X87Sqrt(long double x) { return std::sqrt(x); }
inline long double X87Sin(long double x) { return std::sin(x); }

inline bool IsZeroL(long double value) { return !(value < 0.0L || value > 0.0L); }

// Row-major matrix indexing adapter.
inline double MatrixElem(const glm::dmat3& matrix, int row, int col) {
    return matrix[col][row];
}

void RotToQuat(double quat[4], const glm::dmat3& matrix) {
    static const int next_axis[3] = {1, 2, 0};
    const long double trace = (MatrixElem(matrix, 2, 2) + MatrixElem(matrix, 1, 1)) + MatrixElem(matrix, 0, 0);

    if (trace > 0.0f) {
        long double root = X87Sqrt(trace + 1.0f);

        quat[3] = static_cast<double>(root * kHalf);
        root = kHalf / root;
        quat[0] = -((MatrixElem(matrix, 1, 2) - MatrixElem(matrix, 2, 1)) * root);
        quat[1] = -((MatrixElem(matrix, 2, 0) - MatrixElem(matrix, 0, 2)) * root);
        quat[2] = -((MatrixElem(matrix, 0, 1) - MatrixElem(matrix, 1, 0)) * root);
    } else {
        int primary_axis = 0;
        long double root;

        if (MatrixElem(matrix, 1, 1) > MatrixElem(matrix, 0, 0)) primary_axis = 1;
        if (MatrixElem(matrix, 2, 2) > MatrixElem(matrix, primary_axis, primary_axis)) primary_axis = 2;
        const int secondary_axis = next_axis[primary_axis];
        const int tertiary_axis = next_axis[secondary_axis];
        root = X87Sqrt(((MatrixElem(matrix, primary_axis, primary_axis) - MatrixElem(matrix, secondary_axis, secondary_axis)) -
                        MatrixElem(matrix, tertiary_axis, tertiary_axis)) + 1.0f);
        quat[primary_axis] = static_cast<double>(root * kHalf);
        if (!IsZeroL(root)) root = kHalf / root;
        quat[secondary_axis] = (MatrixElem(matrix, primary_axis, secondary_axis) + MatrixElem(matrix, secondary_axis, primary_axis)) * root;
        quat[tertiary_axis] = (MatrixElem(matrix, primary_axis, tertiary_axis) + MatrixElem(matrix, tertiary_axis, primary_axis)) * root;
        quat[3] = -((MatrixElem(matrix, tertiary_axis, secondary_axis) - MatrixElem(matrix, secondary_axis, tertiary_axis)) * root);
    }
}

glm::dmat3 QuatToMatrix(const double quat[4]) {
    long double x_sq2 = quat[0] * quat[0];
    x_sq2 = x_sq2 + x_sq2;
    long double xy2 = quat[1] * quat[0];
    xy2 = xy2 + xy2;
    long double xz2 = 2 * (quat[2] * quat[0]);
    long double xw2 = 2 * (quat[3] * quat[0]);
    long double y_sq2 = 2 * (quat[1] * quat[1]);
    long double yz2 = 2 * (quat[2] * quat[1]);
    long double yw2 = 2 * (quat[3] * quat[1]);
    long double z_sq2 = 2 * (quat[2] * quat[2]);
    long double zw2 = quat[3] * quat[2];
    zw2 = zw2 + zw2;

    glm::dmat3 out_matrix(1.0);
    out_matrix[0][0] = static_cast<double>((1.0f - y_sq2) - z_sq2);
    out_matrix[1][0] = static_cast<double>(xy2 - zw2);
    out_matrix[2][0] = static_cast<double>(yw2 + xz2);
    out_matrix[0][1] = static_cast<double>(zw2 + xy2);
    out_matrix[1][1] = static_cast<double>((1.0f - x_sq2) - z_sq2);
    out_matrix[2][1] = static_cast<double>(yz2 - xw2);
    out_matrix[0][2] = static_cast<double>(xz2 - yw2);
    out_matrix[1][2] = static_cast<double>(yz2 + xw2);
    out_matrix[2][2] = static_cast<double>((1.0f - x_sq2) - y_sq2);
    return out_matrix;
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

    if (rail.path.joints.size() < 2 || !(len > 0.0)) return out;

    // Wrap distance along the rail.
    const double d = spline_geometry::WrapRailDistance(dist, len);
    if (!(d > 0.0)) return out;

    // Walk to the joint pair containing distance d.
    std::size_t i = 0;
    while (i + 2 < rail.path.arc.size() && rail.path.arc[i + 1] < d) ++i;
    const double segLen = rail.path.arc[i + 1] - rail.path.arc[i];
    const double t = (segLen > 0.0) ? (d - rail.path.arc[i]) / segLen : 0.0;

    out.position = rail.path.joints[i].pos +
                   (rail.path.joints[i + 1].pos - rail.path.joints[i].pos) * t;
    out.found = true;

    // Apply orientation unless explicitly forced by parameter.
    if (!params.force_orientation) {
        const glm::dmat3 frame =
            BlendJointRotation(rail.path.joints[i].rot, rail.path.joints[i + 1].rot, t);
        out.matrix = place_matrix * frame;
    }
    return out;
}

}  // namespace igi
