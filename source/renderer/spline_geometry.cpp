#include "spline_geometry.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace spline_geometry {
namespace {

bool IsFinite(const glm::dvec3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

// open-igi/sub_4E4530 zero-roll frame: tangent along X, level side in XY, up completes
// the basis. Matches RailroadPath.Evaluate and WorldScene.BendSegment.
bool BuildZeroRollBasis(const glm::dvec3& forward,
                        glm::dvec3& outForward,
                        glm::dvec3& outSide,
                        glm::dvec3& outUp) {
    const double forwardLen = glm::length(forward);
    if (forwardLen <= 1e-12) return false;
    outForward = forward / forwardLen;
    outSide = glm::dvec3(-outForward.y, outForward.x, 0.0);
    const double sideLen = glm::length(outSide);
    if (sideLen < 1e-9) {
        outSide = glm::dvec3(0.0, 1.0, 0.0);
    } else {
        outSide /= sideLen;
    }
    outUp = glm::cross(outForward, outSide);
    return true;
}

} // namespace

std::string_view ResolveSegmentModel(
    std::string_view startWaypointModel,
    std::string_view endWaypointModel,
    std::string_view fallbackModel) noexcept {
    (void)startWaypointModel;
    return endWaypointModel.empty() ? fallbackModel : endWaypointModel;
}

glm::dvec3 MakeWaypointTangent(
    const glm::dvec3& euler,
    double chordLength) noexcept {
    if (!IsFinite(euler) || !std::isfinite(chordLength) || chordLength <= 0.0) {
        return glm::dvec3(0.0);
    }

    const double sinY = std::sin(euler.y);
    const double cosY = std::cos(euler.y);
    const double sinZ = std::sin(euler.z);
    const double cosZ = std::cos(euler.z);
    // Column 1 (local X axis) of the engine orientation matrix Rz(gamma) · Ry(beta) · Rx(alpha),
    // matching open-igi's FromEngineEulerAngles / Tangent. Yaw-only authoring (alpha=beta=0)
    // collapses to (cos gamma, sin gamma, 0), so this also satisfies every shipped yaw-only
    // waypoint, but it stays correct for multi-axis tuples where Z-X-Y would diverge.
    const glm::dvec3 localX(
        cosZ * cosY,
        sinZ * cosY,
        -sinY);
    return localX * chordLength;
}

glm::dvec3 SampleSegment(const SplineSegment& segment, double t) {
    if (segment.linear) return segment.p0 + t * (segment.p1 - segment.p0);

    const double t2 = t * t;
    const double t3 = t2 * t;
    return (2.0 * t3 - 3.0 * t2 + 1.0) * segment.p0
         + (t3 - 2.0 * t2 + t) * segment.tangent0
         + (-2.0 * t3 + 3.0 * t2) * segment.p1
         + (t3 - t2) * segment.tangent1;
}

glm::dvec3 SampleSegmentTangent(const SplineSegment& segment, double t) {
    if (segment.linear) return segment.p1 - segment.p0;

    const glm::dvec3 chord = segment.p1 - segment.p0;
    const double t2 = t * t;
    const double d10 = 3.0 * t2 - 4.0 * t + 1.0;
    const double d01 = 6.0 * t - 6.0 * t2;
    const double d11 = 3.0 * t2 - 2.0 * t;
    return d10 * segment.tangent0 + d01 * chord + d11 * segment.tangent1;
}

namespace {

// kPi (DATA 533400, src/tagitem/spline.c:849) — kept local so this translation
// unit does not depend on the precompiled header for it.
constexpr double kPi = 3.14159265358979323846;

// Row-major 3x3, matching the engine's Matrix.m[] indexing exactly.
struct Mat3 {
    double m[9];
};

Mat3 FromGlm(const glm::dmat3& g) {
    Mat3 r{};
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            r.m[row * 3 + col] = g[col][row];
        }
    }
    return r;
}

glm::dmat3 ToGlm(const Mat3& a) {
    glm::dmat3 g(1.0);
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            g[col][row] = a.m[row * 3 + col];
        }
    }
    return g;
}

// m * (x, y, z), row-major like the engine's mat_mul/matrix-vector products.
glm::dvec3 Mul(const Mat3& a, const glm::dvec3& v) {
    return glm::dvec3(
        a.m[0] * v.x + a.m[1] * v.y + a.m[2] * v.z,
        a.m[3] * v.x + a.m[4] * v.y + a.m[5] * v.z,
        a.m[6] * v.x + a.m[7] * v.y + a.m[8] * v.z);
}

// Matrix_SetRotZ, HOOK 0x4B3BE0, src/math/matrix.c:148-164.
Mat3 RotZ(double a) {
    const double s = std::sin(a);
    const double c = std::cos(a);
    Mat3 r{};
    r.m[0] = c;      r.m[1] = -s;     r.m[2] = 0.0;
    r.m[3] = s;      r.m[4] = c;      r.m[5] = 0.0;
    r.m[6] = 0.0;    r.m[7] = 0.0;    r.m[8] = 1.0;
    return r;
}

// Matrix_SetRotY, HOOK 0x4B3BA0, src/math/matrix.c:130-146.
Mat3 RotY(double a) {
    const double s = std::sin(a);
    const double c = std::cos(a);
    Mat3 r{};
    r.m[0] = c;      r.m[1] = 0.0;    r.m[2] = s;
    r.m[3] = 0.0;    r.m[4] = 1.0;    r.m[5] = 0.0;
    r.m[6] = -s;     r.m[7] = 0.0;    r.m[8] = c;
    return r;
}

// Matrix_SetRotX, HOOK 0x4B3B60, src/math/matrix.c:112-128.
Mat3 RotX(double a) {
    const double s = std::sin(a);
    const double c = std::cos(a);
    Mat3 r{};
    r.m[0] = 1.0;    r.m[1] = 0.0;    r.m[2] = 0.0;
    r.m[3] = 0.0;    r.m[4] = c;      r.m[5] = -s;
    r.m[6] = 0.0;    r.m[7] = s;      r.m[8] = c;
    return r;
}

Mat3 MulM(const Mat3& a, const Mat3& b) {
    Mat3 t{};
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            t.m[i * 3 + j] = a.m[i * 3 + 0] * b.m[0 * 3 + j]
                           + a.m[i * 3 + 1] * b.m[1 * 3 + j]
                           + a.m[i * 3 + 2] * b.m[2 * 3 + j];
        }
    }
    return t;
}

// Matrix_SetEuler, HOOK 0x4B38E0, src/math/matrix.c:99-110: Rz(az) * Ry(ay) * Rx(ax).
Mat3 SetEuler(double ax, double ay, double az) {
    Mat3 r = MulM(RotZ(az), RotY(ay));
    return MulM(r, RotX(ax));
}

// Hermite_Init, HOOK 0x518130, src/terrdraw/flare.c:44-52:
// h(u) = m0*u + (2*p0 - 2*p1 + m0 + m1)*u^2 + (3*p1 - 3*p0 - 2*m0 - m1)*u^3 + p0.
struct Hermite {
    double c[4];

    Hermite(double p0, double p1, double m0, double m1) {
        c[0] = m0;
        c[1] = p0;
        c[2] = p0 + p0 - (p1 + p1) + m0 + m1;
        c[3] = p1 * 3.0 - p0 * 3.0 - (m0 + m0) - m1;
    }

    // Hermite_Eval, HOOK 0x518190, src/terrdraw/flare.c:54-61.
    double Eval(double u) const {
        const double uu = u * u;
        return u * c[0] + (uu * u * c[2] + uu * c[3]) + c[1];
    }

    // Hermite_Slope, HOOK 0x5181C0, src/terrdraw/flare.c:63-69.
    double Slope(double u) const {
        const double t0 = u * c[3];
        return u * c[2] * u * 3.0 + (t0 + t0) + c[0];
    }
};

// Vec3_ToAngles, HOOK 0x4B2D00, src/file/vec.c:233-246: yaw = atan2(y, x),
// pitch = -asin(z / |v|).
void ToAngles(const glm::dvec3& v, double& pitch, double& yaw) {
    yaw = (v.x == 0.0) ? 0.0 : std::atan2(v.y, v.x);
    const double len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    pitch = (len == 0.0) ? 0.0 : -std::asin(v.z / len);
}

// Matrix_GetEuler, HOOK 0x4B3C50, src/math/matrix.c:181-208. The engine's Atan2
// only special-cases x == 0 (it spins on a fully zero pair), so the caller must
// not hand it one; a zero column falls back to 0 here instead of hanging.
void GetEuler(const Mat3& e, double& ax, double& ay, double& az) {
    const double cy = std::sqrt(e.m[0] * e.m[0] + e.m[3] * e.m[3]);
    double rx;
    double ry;
    double rz;
    if (cy > 0.0001) {
        rx = std::atan2(e.m[7], e.m[8]);
        ry = std::atan2(-e.m[6], cy);
        rz = std::atan2(e.m[3], e.m[0]);
    } else {
        rx = std::atan2(-e.m[5], e.m[4]);
        ry = std::atan2(-e.m[6], cy);
        rz = 0.0;
    }
    const double twoPi = 2.0 * kPi;
    if (rx < 0.0) rx += twoPi;
    if (ry < 0.0) ry += twoPi;
    if (rz < 0.0) rz += twoPi;
    ax = rx;
    ay = ry;
    az = rz;
}

// The FPU rounds toward zero: FPU_SetTruncate (HOOK 0x498460,
// src/dxinit/dxinit.c:75-81) sets the x87 control word's RC bits to 11 at
// startup (src/win/appmain.c:212), so every `fistp` in the engine truncates.
int TruncateToInt(double f) {
    return static_cast<int>(f);
}

void AppendBentVertex(std::vector<float>& out,
                      const glm::dvec3& worldOffset,
                      const glm::dvec3& normal,
                      const glm::vec2& uv,
                      const glm::vec2& uv2) {
    const double invScale = 1.0 / 40.96;
    out.push_back(static_cast<float>(worldOffset.x * invScale));
    out.push_back(static_cast<float>(worldOffset.y * invScale));
    out.push_back(static_cast<float>(worldOffset.z * invScale));
    out.push_back(static_cast<float>(normal.x));
    out.push_back(static_cast<float>(normal.y));
    out.push_back(static_cast<float>(normal.z));
    out.push_back(uv.x);
    out.push_back(1.0f - uv.y);
    out.push_back(uv2.x);
    out.push_back(uv2.y);
}

} // namespace

BentSegmentMesh BendSegmentMesh(
    const ParsedGeometry& geometry,
    const SplineSpan& span) {
    BentSegmentMesh result;
    if (geometry.vertices.empty() || geometry.triangles.empty()) {
        return result;
    }

    float minX = std::numeric_limits<float>::max();
    float maxX = std::numeric_limits<float>::lowest();
    for (const RenderVertex& vertex : geometry.vertices) {
        minX = std::min(minX, vertex.rawPos.x);
        maxX = std::max(maxX, vertex.rawPos.x);
    }
    const double spanX = static_cast<double>(maxX) - static_cast<double>(minX);
    if (!(spanX > 0.0)) return result;

    // SplineObj_BendVerts, HOOK 0x4E1440, src/shadow/spline.c:24-90. The rigid
    // branch (src/shadow/spline.c:29-46) ignores the joints and rotates the model
    // about its own local mid by the scaled track frame; the curved branch
    // (:48-89) uses the vertex's local X to pick a joint pair and blends the two
    // joints' frames.
    const Mat3 rigidFrame = [&] {
        glm::dvec3 d = span.to - span.from;
        if (d.x == 0.0 && d.y == 0.0 && d.z == 0.0) d = glm::dvec3(1.0, 0.0, 0.0);
        double pitch = 0.0;
        double yaw = 0.0;
        if (span.from != span.to) ToAngles(d, pitch, yaw);
        const Mat3 rot0 = RotZ(yaw);
        // setup_rigid, src/sound/segsetup.c:203-250: column 0 is the chord scaled
        // onto the model's local X range, columns 1 and 2 come from RotZ(yaw).
        const double k = 1.0 / spanX;
        double c0 = d.x * k;
        double c1 = d.y * k;
        double c2 = d.z * k;
        if (span.reversed) {
            c0 = -c0;
            c1 = -c1;
            c2 = -c2;
        }
        Mat3 out{};
        out.m[0] = c0;          out.m[3] = c1;          out.m[6] = c2;
        out.m[1] = rot0.m[1];   out.m[4] = rot0.m[4];   out.m[7] = rot0.m[7];
        out.m[2] = rot0.m[2];   out.m[5] = rot0.m[5];   out.m[8] = rot0.m[8];
        return out;
    }();

    SplineSpanJoints built;
    if (!span.rigid) built = BuildSplineSpan(span);
    const int segments = span.rigid ? 1 : static_cast<int>(built.joints.size()) - 1;
    const double rigidMid = (static_cast<double>(minX) + static_cast<double>(maxX)) * 0.5;
    const double inv = 1.0 / spanX;
    const double base = -(span.reversed ? static_cast<double>(maxX) : static_cast<double>(minX));
    double scale = segments * inv;
    if (span.reversed) scale = -scale;

    std::vector<glm::dvec3> bentPositions(geometry.vertices.size());
    std::vector<glm::dvec3> bentNormals(geometry.vertices.size());
    for (size_t i = 0; i < geometry.vertices.size(); ++i) {
        const RenderVertex& vertex = geometry.vertices[i];
        const double vx = static_cast<double>(vertex.rawPos.x);
        const double vy = static_cast<double>(vertex.rawPos.y);
        const double vz = static_cast<double>(vertex.rawPos.z);

        if (span.rigid) {
            bentPositions[i] = Mul(rigidFrame, glm::dvec3(vx - rigidMid, vy, vz));
            bentNormals[i] = glm::normalize(Mul(rigidFrame,
                glm::dvec3(vertex.normal.x, vertex.normal.y, vertex.normal.z)));
            continue;
        }

        const double f = (base + vx) * scale;
        int k = TruncateToInt(f);
        if (k == segments) k--;
        if (k < 0) k = 0;
        if (k >= segments) k = segments - 1;
        const double frac = std::clamp(f - k, 0.0, 1.0);

        const Mat3 ra = FromGlm(built.joints[static_cast<size_t>(k)].rot);
        const Mat3 rb = FromGlm(built.joints[static_cast<size_t>(k) + 1].rot);
        const glm::dvec3 la(0.0, vy, vz);
        const glm::dvec3 a = built.joints[static_cast<size_t>(k)].pos + Mul(ra, la);
        const glm::dvec3 b = built.joints[static_cast<size_t>(k) + 1].pos + Mul(rb, la);
        bentPositions[i] = a + (b - a) * frac;

        const glm::dvec3 ln(vertex.normal.x, vertex.normal.y, vertex.normal.z);
        const glm::dvec3 na = Mul(ra, ln);
        const glm::dvec3 nb = Mul(rb, ln);
        bentNormals[i] = glm::normalize(na + (nb - na) * frac);
    }

    const auto buildRange = [&](size_t triangleStart, size_t triangleCount) {
        std::vector<float> verts;
        verts.reserve(triangleCount * 30);
        for (size_t triIndex = triangleStart;
             triIndex < triangleStart + triangleCount; ++triIndex) {
            if (triIndex >= geometry.triangles.size()) break;
            const auto& tri = geometry.triangles[triIndex];
            if (tri[0] >= geometry.vertices.size() ||
                tri[1] >= geometry.vertices.size() ||
                tri[2] >= geometry.vertices.size()) {
                continue;
            }
            for (uint32_t index : tri) {
                const RenderVertex& src = geometry.vertices[index];
                AppendBentVertex(verts, bentPositions[index], bentNormals[index], src.uv, src.uv2);
            }
        }
        if (verts.empty()) return;
        result.submeshVertexCounts.push_back(static_cast<int>(verts.size() / 10));
        result.interleaved.insert(result.interleaved.end(), verts.begin(), verts.end());
    };

    if (!geometry.renderBlocks.empty()) {
        for (const auto& block : geometry.renderBlocks) {
            buildRange(block.triangleStart, block.triangleCount);
        }
    } else {
        buildRange(0, geometry.triangles.size());
    }

    return result;
}

std::optional<SplineTile> MakeAxisAlignedTileWithForward(
    glm::dvec3 begin,
    const glm::dvec3& forward,
    int longitudinalAxis,
    double localMin,
    double localLength,
    double crossScale,
    double span) {
    if (!IsFinite(begin) ||
        longitudinalAxis < 0 || longitudinalAxis > 2 ||
        !std::isfinite(localMin) || !std::isfinite(localLength) ||
        !std::isfinite(crossScale) || localLength <= 1e-9 || crossScale <= 0.0 ||
        !std::isfinite(span) || span <= 1e-9) {
        return std::nullopt;
    }

    glm::dvec3 fwd;
    glm::dvec3 right;
    glm::dvec3 up;
    if (!BuildZeroRollBasis(forward, fwd, right, up)) return std::nullopt;
    const double sx = span / localLength;

    glm::dmat4 model(1.0);
    const glm::dvec4 longitudinal(fwd * sx, 0.0);
    const glm::dvec4 crossRight(right * crossScale, 0.0);
    const glm::dvec4 crossUp(up * crossScale, 0.0);
    if (longitudinalAxis == 0) {
        model[0] = longitudinal;
        model[1] = crossRight;
        model[2] = crossUp;
    } else if (longitudinalAxis == 1) {
        model[0] = crossRight;
        model[1] = longitudinal;
        model[2] = crossUp;
    } else {
        model[0] = crossRight;
        model[1] = crossUp;
        model[2] = longitudinal;
    }
    model[3] = glm::dvec4(begin - fwd * (sx * localMin), 1.0);
    const glm::dvec3 end = begin + fwd * span;
    return SplineTile{begin, end, model};
}

std::optional<SplineTile> MakeAxisAlignedTile(
    glm::dvec3 begin,
    glm::dvec3 end,
    int longitudinalAxis,
    double localMin,
    double localLength,
    double crossScale) {
    if (!IsFinite(begin) || !IsFinite(end) ||
        longitudinalAxis < 0 || longitudinalAxis > 2 ||
        !std::isfinite(localMin) || !std::isfinite(localLength) ||
        !std::isfinite(crossScale) || localLength <= 1e-9 || crossScale <= 0.0) {
        return std::nullopt;
    }

    const glm::dvec3 delta = end - begin;
    const double span = glm::length(delta);
    if (!std::isfinite(span) || span <= 1e-9) return std::nullopt;

    glm::dvec3 forward;
    glm::dvec3 right;
    glm::dvec3 up;
    if (!BuildZeroRollBasis(delta, forward, right, up)) return std::nullopt;
    const double sx = span / localLength;

    glm::dmat4 model(1.0);
    const glm::dvec4 longitudinal(forward * sx, 0.0);
    const glm::dvec4 crossRight(right * crossScale, 0.0);
    const glm::dvec4 crossUp(up * crossScale, 0.0);
    if (longitudinalAxis == 0) {
        model[0] = longitudinal;
        model[1] = crossRight;
        model[2] = crossUp;
    } else if (longitudinalAxis == 1) {
        model[0] = crossRight;
        model[1] = longitudinal;
        model[2] = crossUp;
    } else {
        model[0] = crossRight;
        model[1] = crossUp;
        model[2] = longitudinal;
    }
    model[3] = glm::dvec4(begin - forward * (sx * localMin), 1.0);
    return SplineTile{begin, end, model};
}

std::optional<SplineTile> MakeXAlignedTile(
    glm::dvec3 begin,
    glm::dvec3 end,
    double localMinX,
    double localLength,
    double crossScale) {
    return MakeAxisAlignedTile(begin, end, 0, localMinX, localLength, crossScale);
}

glm::dmat3 EngineEulerMatrix(const glm::dvec3& euler) noexcept {
    return ToGlm(SetEuler(euler.x, euler.y, euler.z));
}

// Matrix_GetEuler, HOOK 0x4B3C50, src/math/matrix.c:181-208. The engine returns
// the three angles already normalised into [0, 2pi).
glm::dvec3 EngineMatrixToEuler(const glm::dmat3& frame) noexcept {
    double ax = 0.0;
    double ay = 0.0;
    double az = 0.0;
    GetEuler(FromGlm(frame), ax, ay, az);
    return glm::dvec3(ax, ay, az);
}

SplineSpanJoints BuildSplineSpan(const SplineSpan& span) {
    SplineSpanJoints out;
    // SplineObjSegment_Setup, src/sound/segsetup.c:267-270: a rigid span is
    // always a single sub-segment; otherwise the count is the waypoint's, capped
    // at 20.  SplineObjWaypoint_Changed (HOOK 0x500EF0, src/tagitem/spline.c:771-772):
    //   n = w->segments < obj->maxSegments ? w->segments : obj->maxSegments;
    //   w->segments = n > 20 ? 20 : n;
    // That is a CAP only -- the editor used to clamp to a minimum of 2, an invented
    // lower bound the engine does not have.
    const int segments = span.rigid ? 1 : std::min(span.segments, 20);
    out.mid = (span.from + span.to) * 0.5;
    out.joints.assign(static_cast<size_t>(segments) + 1, SplineJoint{});
    out.segLength.assign(static_cast<size_t>(segments) + 2, 0.0);

    const Mat3 rf = FromGlm(span.rotFrom);
    const Mat3 rt = FromGlm(span.rotTo);
    const double dist = glm::length(span.to - span.from);

    // setup_segments, src/sound/segsetup.c:88-199: the tangents are the first
    // column of each waypoint's frame (row-major elements 0, 3, 6 — the local X
    // axis) scaled by the chord length.
    const Hermite hx(span.from.x, span.to.x, rf.m[0] * dist, rt.m[0] * dist);
    const Hermite hy(span.from.y, span.to.y, rf.m[3] * dist, rt.m[3] * dist);
    const Hermite hz(span.from.z, span.to.z, rf.m[6] * dist, rt.m[6] * dist);

    const double inv = 1.0 / segments;
    if (span.rigid) {
        glm::dvec3 d = span.to - span.from;
        if (d.x == 0.0 && d.y == 0.0 && d.z == 0.0) d = glm::dvec3(1.0, 0.0, 0.0);
        double pitch = 0.0;
        double yaw = 0.0;
        if (span.from != span.to) ToAngles(d, pitch, yaw);
        const glm::dmat3 rz = ToGlm(RotZ(yaw));
        for (int i = 0; i <= segments; ++i) {
            const double t = i * inv;
            out.joints[static_cast<size_t>(i)].pos =
                span.from + (span.to - span.from) * t - out.mid;
            out.joints[static_cast<size_t>(i)].rot = rz;
        }
    } else {
        double ax0 = 0.0;
        double ay0 = 0.0;
        double az0 = 0.0;
        double ax1 = 0.0;
        double ay1 = 0.0;
        double az1 = 0.0;
        GetEuler(rf, ax0, ay0, az0);
        GetEuler(rt, ax1, ay1, az1);
        (void)ay0;
        (void)az0;
        (void)ay1;
        (void)az1;

        // src/sound/segsetup.c:150-172 unwraps the roll across the span; the two
        // bounds do not depend on i, so they are resolved once here.
        double a0 = ax0;
        double a1 = ax1;
        const double pi = kPi;
        const double twoPi = 2.0 * pi;
        if (ax1 - a0 > pi) {
            do {
                a0 += twoPi;
            } while (ax1 - a0 > pi);
        }
        if (!(ax1 - a0 >= -pi)) {
            do {
                a1 += twoPi;
            } while (!(a1 - a0 >= -pi));
        }

        for (int i = 0; i <= segments; ++i) {
            const double t = i * inv;
            SplineJoint& joint = out.joints[static_cast<size_t>(i)];
            joint.pos = glm::dvec3(hx.Eval(t), hy.Eval(t), hz.Eval(t)) - out.mid;

            if (i == 0) {
                // src/sound/segsetup.c:174-175 copies the waypoint frame verbatim.
                joint.rot = span.rotFrom;
            } else if (i == segments) {
                joint.rot = span.rotTo;
            } else {
                const glm::dvec3 slope(hx.Slope(t), hy.Slope(t), hz.Slope(t));
                double pitch = 0.0;
                double yaw = 0.0;
                if (slope != glm::dvec3(0.0)) ToAngles(slope, pitch, yaw);
                const double ax = (a1 - a0) * t + a0;
                joint.rot = ToGlm(SetEuler(ax, pitch, yaw));
            }
        }
    }

    // src/sound/segsetup.c:186-191: segLength[i] is the distance from joint i-1.
    out.length = 0.0;
    for (int i = 1; i <= segments; ++i) {
        const double len = glm::length(
            out.joints[static_cast<size_t>(i)].pos - out.joints[static_cast<size_t>(i) - 1].pos);
        out.segLength[static_cast<size_t>(i)] = len;
        out.length += len;
    }
    return out;
}

RailPath BuildRailPath(const std::vector<SplineSpan>& spans) {
    RailPath path;
    for (const SplineSpan& span : spans) {
        const SplineSpanJoints built = BuildSplineSpan(span);
        for (size_t i = 0; i < built.joints.size(); ++i) {
            // Consecutive spans share the waypoint they meet at: the engine has
            // one segment per waypoint after the first (SplineObj_GetPoints,
            // src/tagitem/spline.c:219-454), so the shared joint is not repeated.
            if (!path.joints.empty() && i == 0) continue;
            SplineJoint joint = built.joints[i];
            joint.pos += built.mid;
            path.joints.push_back(joint);
        }
    }

    path.arc.assign(path.joints.size(), 0.0);
    for (size_t i = 1; i < path.joints.size(); ++i) {
        path.arc[i] = path.arc[i - 1] +
            glm::length(path.joints[i].pos - path.joints[i - 1].pos);
    }
    path.length = path.arc.empty() ? 0.0 : path.arc.back();
    return path;
}

double WrapRailDistance(double distance, double length) noexcept {
    if (!std::isfinite(distance) || !(length > 0.0)) return 0.0;
    double d = distance;
    if (!(d >= 0.0)) {
        do {
            d += length;
        } while (!(d >= 0.0));
    }
    if (d >= length) {
        do {
            d -= length;
        } while (d >= length);
    }
    return d;
}

namespace {

// Walks the joint polyline to an already-wrapped arc distance. The engine
// reaches the same point by accumulating the per-joint SEG_LEN and then blending
// the two joints (src/tagitem/spline.c:288-315); because the walk always starts
// from a point that lies on the polyline, the circle/angle construction in the
// spacing loop reduces to this arc-length step.
glm::dvec3 SampleArcNoWrap(const RailPath& path, double d) {
    if (path.joints.size() < 2 || path.length <= 0.0) {
        return path.joints.empty() ? glm::dvec3(0.0) : path.joints.front().pos;
    }
    if (!(d > 0.0)) return path.joints.front().pos;
    if (d >= path.length) return path.joints.back().pos;

    size_t i = 0;
    while (i + 2 < path.arc.size() && path.arc[i + 1] < d) ++i;
    const double segLen = path.arc[i + 1] - path.arc[i];
    const double t = (segLen > 0.0) ? (d - path.arc[i]) / segLen : 0.0;
    return path.joints[i].pos + (path.joints[i + 1].pos - path.joints[i].pos) * t;
}

} // namespace

bool SampleRailPoint(const RailPath& path, double arcDistance, glm::dvec3& out) noexcept {
    if (path.joints.size() < 2 || !(path.length > 0.0)) return false;
    if (!std::isfinite(arcDistance)) return false;
    out = SampleArcNoWrap(path, WrapRailDistance(arcDistance, path.length));
    return true;
}

glm::dmat3 TrackDirectionMatrix(const glm::dvec3& trackDirection) noexcept {
    // Matrix_SetDirection, HOOK 0x4B4010, src/math/matrix.c:244-277: the frame has
    // no roll — `up` is the direction turned 90 degrees in the XY plane.
    const glm::dvec3 d = trackDirection;
    glm::dvec3 up;
    if (d.x == 0.0 && d.y == 0.0) {
        up = glm::dvec3(0.0, 1.0, 0.0);
    } else {
        up = glm::dvec3(d.y, -d.x, 0.0);
        const double len = glm::length(up);
        if (len > 0.0) up /= len;
    }
    up.z = 0.0;

    glm::dvec3 side = glm::cross(up, d);
    const double sideLen = glm::length(side);
    if (sideLen > 0.0) side /= sideLen;

    glm::dvec3 s = glm::cross(d, side);
    const double sLen = glm::length(s);
    if (sLen > 0.0) s /= sLen;

    glm::dvec3 t = glm::cross(side, s);
    const double tLen = glm::length(t);
    if (tLen > 0.0) t /= tLen;

    Mat3 m{};
    m.m[0] = side.x; m.m[1] = side.y; m.m[2] = side.z;   // column 0
    m.m[3] = s.x;    m.m[4] = s.y;    m.m[5] = s.z;      // column 1
    m.m[6] = t.x;    m.m[7] = t.y;    m.m[8] = t.z;      // column 2

    // orient(), src/missile/train.c:172-190: row 0 = -row 2, then transpose.
    const double r0 = m.m[0];
    const double r1 = m.m[1];
    const double r2 = m.m[2];
    m.m[0] = -m.m[6];
    m.m[1] = -m.m[7];
    m.m[2] = -m.m[8];
    Mat3 n{};
    n.m[0] = m.m[0]; n.m[1] = m.m[3]; n.m[2] = r0;
    n.m[3] = m.m[1]; n.m[4] = m.m[4]; n.m[5] = r1;
    n.m[6] = m.m[2]; n.m[7] = m.m[5]; n.m[8] = r2;
    return ToGlm(n);
}

TrainPlacement PlaceTrainOnRail(
    const RailPath& path,
    double railDistance,
    double trainLength,
    double frac,
    double displacementX,
    double displacementY,
    bool flip) noexcept {
    TrainPlacement result;
    if (path.joints.size() < 2 || !(path.length > 0.0)) return result;
    if (!std::isfinite(railDistance) || !std::isfinite(trainLength) ||
        !std::isfinite(frac)) {
        return result;
    }

    // SplineObj_GetPoints, src/tagitem/spline.c:238-263: the scalar is wrapped
    // into [0, length) and a wrapped value of exactly 0 takes the engine's
    // at_start path, which repeats the first waypoint.
    const double arc0 = WrapRailDistance(railDistance, path.length);
    if (arc0 == 0.0) {
        result.position = path.joints.front().pos;
        result.matrix = glm::dmat3(1.0);
        result.found = true;
        return result;
    }

    // src/missile/train.c:221: offsets[0] = reverse ? length : -length, so an
    // unflipped train reads its second point behind the first.
    const double spacing = flip ? trainLength : -trainLength;
    const double arc1 = std::clamp(arc0 + spacing, 0.0, path.length);
    const glm::dvec3 p0 = SampleArcNoWrap(path, arc0);
    const glm::dvec3 p1 = SampleArcNoWrap(path, arc1);

    const glm::dvec3 dir = p1 - p0;
    Mat3 m = FromGlm(glm::dmat3(1.0));
    glm::dvec3 pos = p0;
    if (dir != glm::dvec3(0.0)) {
        m = FromGlm(TrackDirectionMatrix(dir));
        pos = p0 + dir * frac;
    }

    // src/missile/train.c:240-248: the displacement is scaled by kFixed and
    // applied through the frame's columns 1 and 2 (row-major elements 1/2, 4/5,
    // 7/8), clamped to gTrainOffsetRange (src/missile/train.c:380-381).
    const double kFixed = 4096.0;
    const double sx = std::clamp(displacementX, -5.0, 5.0) * kFixed;
    const double sy = std::clamp(displacementY, -5.0, 5.0) * kFixed;
    pos.x += sx * m.m[1] + sy * m.m[2];
    pos.y += sy * m.m[5] + sx * m.m[4];
    pos.z += sx * m.m[7] + sy * m.m[8];

    result.position = pos;
    result.matrix = ToGlm(m);
    result.found = true;
    return result;
}

} // namespace spline_geometry
