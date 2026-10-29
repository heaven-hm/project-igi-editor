#include "spline_geometry.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>

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

// ---------------------------------------------------------------------------
// The engine's RENDER path (Direct3DRender_FillSplineVB, HOOK 0x49CC80)
// ---------------------------------------------------------------------------

// The engine's source vertex is `RigidMesh.vertices`: 8 floats - position xyz,
// normal xyz, uv (src/render/render.c:56-61), the layout the extent scan
// (src/sound/segsetup.c:277-293) and Direct3DRender_FillSplineVB (:637-668) both
// read. The editor's parsed vertex carries the same three attributes plus uv2,
// so it is read in place with its own stride; the asserts keep that true.
constexpr int kRenderVertexFloats = static_cast<int>(sizeof(RenderVertex) / sizeof(float));
static_assert(sizeof(RenderVertex) == 15 * sizeof(float),
              "RenderVertex must stay 15 contiguous floats: the engine's source "
              "vertex is read straight out of it");
static_assert(offsetof(RenderVertex, rawPos) == 3 * sizeof(float),
              "RenderVertex::rawPos must stay the second attribute");
static_assert(offsetof(RenderVertex, normal) == 6 * sizeof(float),
              "RenderVertex::normal must stay the third attribute");
static_assert(offsetof(RenderVertex, uv) == 9 * sizeof(float),
              "RenderVertex::uv must stay the fourth attribute");

// setup_rigid, src/sound/segsetup.c:203-251. The frame maps the model's local X
// range onto the chord:
//
//   d     = to - from                       (1,0,0) when the chord is zero
//   k     = 1 / (maxX - minX)
//   yaw   = Vec3_ToAngles(d).yaw            (0 when d.x == 0)
//   row 0 = d * k                           (negated when the span is reversed)
//   row 1 = (-sin yaw, cos yaw, 0)
//   row 2 = (0, 0, 1)
//
// Rows 1 and 2 come from the RotZ(yaw) frame setup_segments wrote into joint 0
// (src/sound/segsetup.c:129-135): setup_rigid transposes that joint into the
// segment frame, overwrites its row 0 with the scaled chord, and transposes
// back. The result is row-major, which is what Direct3DRender_FillSplineVB's
// straight branch multiplies by - `dst[0] = (z * m[2] + y * m[1]) + x * m[0]`
// (src/render/render.c:641-643).
//
// The engine reads this frame out of `ModelObj.matrix` (:635), and nothing in
// IGI-RE writes a spline ModelObj's spline state, so the copy itself has no
// oracle. What the oracle does prove is which transform a rigid span gets:
// SplineObj_BendVerts' rigid branch (src/shadow/spline.c:29-46) applies `s->rot`
// - the frame setup_rigid built - to the same vertex array, and the rigid joints
// setup_segments writes carry only RotZ(yaw), which cannot map the model's X
// range onto the chord.
void BuildRigidSpanFrame(const glm::dvec3& from, const glm::dvec3& to, float min_x, float max_x,
                         bool reversed, float out_matrix[10]) {
    float d[3] = {static_cast<float>(to.x - from.x), static_cast<float>(to.y - from.y),
                  static_cast<float>(to.z - from.z)};
    if (d[0] == 0.0f && d[1] == 0.0f && d[2] == 0.0f) {
        d[0] = 1.0f;
        d[1] = 0.0f;
        d[2] = 0.0f;
    }
    // `k = kOne / ((long double)o->maxX - o->minX)` (:219): a long-double
    // quotient, narrowed once per component on assignment.
    const long double k = 1.0L / (static_cast<long double>(max_x) - min_x);
    float pitch = 0.0f;
    float yaw = 0.0f;
    igi::Vec3ToAngles(d, &pitch, &yaw);
    (void)pitch;
    float c0 = static_cast<float>(k * static_cast<long double>(d[0]));
    float c1 = static_cast<float>(k * static_cast<long double>(d[1]));
    float c2 = static_cast<float>(k * static_cast<long double>(d[2]));
    if (reversed) {
        c0 = -c0;
        c1 = -c1;
        c2 = -c2;
    }
    const double s = std::sin(static_cast<double>(yaw));
    const double c = std::cos(static_cast<double>(yaw));
    out_matrix[0] = c0;                       out_matrix[1] = static_cast<float>(-s);
    out_matrix[2] = 0.0f;
    out_matrix[3] = c1;                       out_matrix[4] = static_cast<float>(c);
    out_matrix[5] = 0.0f;
    out_matrix[6] = c2;                       out_matrix[7] = 0.0f;
    out_matrix[8] = 1.0f;                     out_matrix[9] = 0.0f;
}

// One render block's triangles as the model's own index list. The engine adds
// the group's vertex base at draw time (`start + g->base`, src/render/render.c:530),
// which is why the indices are not rewritten here.
void AppendBlockIndices(const ParsedGeometry& geometry, size_t triangleStart, size_t triangleCount,
                        EngineSpanMesh& out) {
    const uint32_t vertexCount = static_cast<uint32_t>(geometry.vertices.size());
    std::vector<uint32_t> indices;
    indices.reserve(triangleCount * 3);
    for (size_t t = triangleStart; t < triangleStart + triangleCount; ++t) {
        if (t >= geometry.triangles.size()) break;
        const std::array<uint32_t, 3>& tri = geometry.triangles[t];
        if (tri[0] >= vertexCount || tri[1] >= vertexCount || tri[2] >= vertexCount) continue;
        indices.push_back(tri[0]);
        indices.push_back(tri[1]);
        indices.push_back(tri[2]);
    }
    if (!indices.empty()) out.indices.push_back(std::move(indices));
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

EngineSpanMesh BuildEngineSpanMesh(
    const ParsedGeometry& geometry,
    const EngineSpanInput& span) {
    EngineSpanMesh result;
    const int vertexCount = static_cast<int>(geometry.vertices.size());
    if (vertexCount <= 0 || geometry.triangles.empty()) return result;

    // The engine's source mesh, `RigidMesh.vertices` (8 floats per vertex:
    // position xyz, normal xyz, uv - src/render/render.c:56-61). The parsed
    // vertex stores the unscaled XTRV position in `rawPos`, which is what the
    // engine's extent scan and bend read as v[0..2].
    std::vector<float> packed(static_cast<size_t>(vertexCount) * 8);
    igi::spline::PackSplineMeshSource(
        &geometry.vertices[0].rawPos.x, kRenderVertexFloats,
        &geometry.vertices[0].normal.x, kRenderVertexFloats,
        &geometry.vertices[0].uv.x, kRenderVertexFloats,
        vertexCount, packed.data());

    // The model's local X range: SplineObjSegment_Setup, src/sound/segsetup.c:277-293.
    // Both bounds START AT ZERO (`o->minX = 0; o->maxX = 0;`), so the range
    // always contains the model origin - a model that sits entirely at x > 0
    // still gets minX = 0 and its far end lands mid-span.
    float min_x = 0.0f;
    float max_x = 0.0f;
    igi::SplineExtentSource lods[1];
    lods[0].verts = packed.data();
    lods[0].vert_count = vertexCount;
    igi::SplineScanModelExtent(lods, 1, 8, &min_x, &max_x);
    // setup_rigid divides by `maxX - minX` (src/sound/segsetup.c:219) and
    // spline_params by the same range (src/render/render.c:325); a zero range has
    // no meaningful parameterisation. Refused rather than reproduced.
    if (!(max_x > min_x)) return result;

    igi::spline::SplineSpanInput input;
    input.from.point[0] = span.from.x;
    input.from.point[1] = span.from.y;
    input.from.point[2] = span.from.z;
    input.to.point[0] = span.to.x;
    input.to.point[1] = span.to.y;
    input.to.point[2] = span.to.z;
    // ParamReal32x9_Parse, HOOK 0x4EFBA0, src/physobj/paramops.c:423-435: the
    // three authored angles go through Matrix_SetEuler, in float.
    igi::SplineObjWaypointParams from_params;
    from_params.orientation[0] = static_cast<float>(span.rotFrom.x);
    from_params.orientation[1] = static_cast<float>(span.rotFrom.y);
    from_params.orientation[2] = static_cast<float>(span.rotFrom.z);
    igi::SplineWaypointFrame(from_params, input.from.rot);
    igi::SplineObjWaypointParams to_params;
    to_params.orientation[0] = static_cast<float>(span.rotTo.x);
    to_params.orientation[1] = static_cast<float>(span.rotTo.y);
    to_params.orientation[2] = static_cast<float>(span.rotTo.z);
    igi::SplineWaypointFrame(to_params, input.to.rot);

    // A waypoint that declares no "NumAreas" keeps the container's count
    // (SplineObjWaypoint_Create, src/tagitem/spline.c:544), which the editor
    // stores as 0.
    const int waypointSegments =
        span.waypointSegments > 0 ? span.waypointSegments : span.maxSegments;
    input.segments =
        igi::SplineEffectiveSegmentCount(waypointSegments, span.maxSegments, span.rigid);
    input.rigid = span.rigid;
    input.reversed = span.reversed;
    input.spline_min = min_x;
    input.spline_max = max_x;
    // `ModelObj.matrix`, read by the straight branch only (src/render/render.c:635-646).
    if (span.rigid) {
        BuildRigidSpanFrame(span.from, span.to, min_x, max_x, span.reversed, input.matrix);
    }

    const igi::spline::SplineSpanGeometry built = igi::spline::BuildSplineSpanGeometry(input);
    const igi::spline::SplineMeshSource source{packed.data(), vertexCount, 8};
    std::vector<igi::spline::SplineRenderVertex> transformed(static_cast<size_t>(vertexCount));
    if (igi::spline::FillSplineSpanVertices(built, source, transformed.data(), vertexCount) !=
        vertexCount) {
        return result;
    }

    // The renderer's own vertex: the engine's 8 floats, at the editor's render
    // scale, plus uv2. Output vertex i IS source vertex i (the engine transforms
    // in place and never reindexes), so uv2 comes from the same source vertex.
    result.interleaved.resize(static_cast<size_t>(vertexCount) * 10);
    const double invScale = 1.0 / 40.96;
    for (int i = 0; i < vertexCount; i++) {
        const igi::spline::SplineRenderVertex& v = transformed[static_cast<size_t>(i)];
        float* out = result.interleaved.data() + static_cast<size_t>(i) * 10;
        out[0] = static_cast<float>(static_cast<double>(v.pos[0]) * invScale);
        out[1] = static_cast<float>(static_cast<double>(v.pos[1]) * invScale);
        out[2] = static_cast<float>(static_cast<double>(v.pos[2]) * invScale);
        out[3] = v.normal[0];
        out[4] = v.normal[1];
        out[5] = v.normal[2];
        // MEF UVs use image-space V and the GL texture upload uses the opposite
        // origin, so V is flipped at the model sampling boundary - the same rule
        // AppendBentVertex and model.cpp apply.
        out[6] = v.uv[0];
        out[7] = 1.0f - v.uv[1];
        // uv2 is the editor's own attribute: the engine's vertex is 8 floats
        // (src/render/render.c:56-61) and never carries a second UV.
        out[8] = geometry.vertices[static_cast<size_t>(i)].uv2.x;
        out[9] = geometry.vertices[static_cast<size_t>(i)].uv2.y;
    }
    result.vertexCount = vertexCount;

    if (!geometry.renderBlocks.empty()) {
        for (const auto& block : geometry.renderBlocks) {
            AppendBlockIndices(geometry, block.triangleStart, block.triangleCount, result);
        }
    } else {
        AppendBlockIndices(geometry, 0, geometry.triangles.size(), result);
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

std::vector<glm::dvec3> RailPolyline(const RailPath& path) {
    std::vector<glm::dvec3> out;
    out.reserve(path.joints.size());
    for (const SplineJoint& joint : path.joints) out.push_back(joint.pos);
    return out;
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
