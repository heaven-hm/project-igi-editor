#pragma once

// The engine's own span render path (`igi::spline`, source/level/spline_geometry.h):
// `BuildEngineSpanMesh` below is the adapter onto it.
#include "level/spline_geometry.h"
#include "mef_native.h"

#include <array>
#include <glm/glm.hpp>

#include <optional>
#include <string_view>
#include <vector>

namespace spline_geometry {

struct SplineTile {
    glm::dvec3 begin;
    glm::dvec3 end;
    glm::dmat4 model;
};

struct SplineSegment {
    glm::dvec3 p0;
    glm::dvec3 p1;
    glm::dvec3 tangent0;
    glm::dvec3 tangent1;
    bool linear = false;
};

// A SplineObjWaypoint names the model for the span that ends at that waypoint.
std::string_view ResolveSegmentModel(
    std::string_view startWaypointModel,
    std::string_view endWaypointModel,
    std::string_view fallbackModel) noexcept;

// Converts a waypoint's Z-Y-X Euler orientation (Rz(gamma) · Ry(beta) · Rx(alpha), the engine
// convention reverse-engineered as 0x4B38E0 and matching open-igi's FromEngineEulerAngles)
// into its local-X spline tangent — the first column of that matrix — scaled to the span chord.
glm::dvec3 MakeWaypointTangent(
    const glm::dvec3& euler,
    double chordLength) noexcept;

glm::dvec3 SampleSegment(const SplineSegment& segment, double t);

std::optional<SplineTile> MakeXAlignedTile(
    glm::dvec3 begin,
    glm::dvec3 end,
    double localMinX,
    double localLength,
    double crossScale);

// Build a tile for a model whose longitudinal local axis is 0 (X), 1 (Y), or
// 2 (Z), retaining the orientation of the model's two cross-section axes.
std::optional<SplineTile> MakeAxisAlignedTile(
    glm::dvec3 begin,
    glm::dvec3 end,
    int longitudinalAxis,
    double localMin,
    double localLength,
    double crossScale);

std::optional<SplineTile> MakeAxisAlignedTileWithForward(
    glm::dvec3 begin,
    const glm::dvec3& forward,
    int longitudinalAxis,
    double localMin,
    double localLength,
    double crossScale,
    double span);

glm::dvec3 SampleSegmentTangent(const SplineSegment& segment, double t);

// Output interleaved vertices use the same 10-float layout as model.cpp
// (pos, normal, uv, uv2).
struct BentSegmentMesh {
    std::vector<float> interleaved;
    std::vector<int> submeshVertexCounts;
};

// ---------------------------------------------------------------------------
// SplineObj spans and rails (IGI-RE src/editobj/spline.c, src/tagitem/spline.c,
// src/sound/segsetup.c, src/shadow/spline.c)
// ---------------------------------------------------------------------------

// The engine's orientation matrix: Matrix_SetEuler (HOOK 0x4B38E0,
// src/math/matrix.c:99-110) composes Rz(az) * Ry(ay) * Rx(ax) row-major, so the
// first column is the object's local X axis. `euler` is (ax, ay, az).
glm::dmat3 EngineEulerMatrix(const glm::dvec3& euler) noexcept;

// The inverse: Matrix_GetEuler (HOOK 0x4B3C50, src/math/matrix.c:181-208) returns
// (ax, ay, az) normalised into [0, 2pi). Used to store an engine frame in the
// editor's Euler-typed object fields.
glm::dvec3 EngineMatrixToEuler(const glm::dmat3& frame) noexcept;

// One joint of a SplineObj span: IGI-RE SplineObjSegment.segPos (0x104) and
// .segRot (0x200), src/sound/soundtypes.h:57-58.
struct SplineJoint {
    glm::dvec3 pos{0.0};   // relative to the span's mid point
    glm::dmat3 rot{1.0};   // the joint's orientation (row-major 3x3)
};

// One SplineObj span: the segment running from waypoint `from` to waypoint `to`
// (IGI-RE SplineObjSegment_Setup, HOOK 0x4E4530, src/sound/segsetup.c:256-366).
struct SplineSpan {
    glm::dvec3 from{0.0};
    glm::dvec3 to{0.0};
    glm::dmat3 rotFrom{1.0};   // Matrix_SetEuler of the `from` waypoint's angles
    glm::dmat3 rotTo{1.0};     // Matrix_SetEuler of the `to` waypoint's angles
    int segments = 20;         // SplineObjWaypoint "NumAreas", clamped to [2, 20]
    bool rigid = false;        // SplineObj "Linear Segments" || waypoint "Align"
    bool reversed = false;     // SplineObjWaypoint "Flip"
};

struct SplineSpanJoints {
    glm::dvec3 mid{0.0};               // (from + to) / 2, the joint origin
    std::vector<SplineJoint> joints;   // segments + 1 entries
    std::vector<double> segLength;     // [i] = |joints[i] - joints[i-1]|, i >= 1
    double length = 0.0;               // sum of segLength
};

SplineSpanJoints BuildSplineSpan(const SplineSpan& span);

// Bends the model's vertices along the span the way the engine does:
// SplineObj_BendVerts (HOOK 0x4E1440, src/shadow/spline.c:24-90) picks the joint
// pair from the vertex's local X and blends the two joints' frames. The returned
// positions are relative to the span's mid point.
//
// This is the COLLISION path's twin and it EXPANDS every indexed triangle into
// three of its own vertices. The renderer must not draw a span with it: see
// `BuildEngineSpanMesh` below for the engine's RENDER path.
BentSegmentMesh BendSegmentMesh(
    const ParsedGeometry& geometry,
    const SplineSpan& span);

// ---------------------------------------------------------------------------
// The engine's placed-span draw path: Direct3DRender_FillSplineVB (HOOK
// 0x49CC80, src/render/render.c:613-670) and its INDEXED draw (draw_group,
// src/render/render.c:522-532)
// ---------------------------------------------------------------------------
//
// The engine transforms the segment model's vertex buffer IN PLACE - one output
// vertex per source vertex, in source order, no reindexing (`FillSplineVertices`
// walks `for (i = 0; i < src.vert_count; i++, v += src.stride)` and writes
// out[i], source/level/spline_geometry.cpp:165-242) - and then draws it INDEXED
// with the model's OWN index buffer:
//
//   Direct3DRender_DrawSplineMesh (HOOK 0x49C850, src/render/render.c:537-604)
//     -> Direct3DRender_FillSplineVB (HOOK 0x49CC80, :613-670)
//     -> draw_group (:522-532):
//        IDirect3DDevice7_DrawIndexedPrimitiveVB(DEV, d->primType, vb,
//            start + g->base, g->vertexCount, (WORD *)g->indices, g->indexCount, 0)
//
// So the vertex list stays the model's own list and the index buffer stays the
// model's own index buffer. `EngineSpanMesh` below is that result converted to
// the renderer's own 10-float interleaved vertex layout.

// One authored span, as the renderer's draw path sees it.
struct EngineSpanInput {
    glm::dvec3 from{0.0};
    glm::dvec3 to{0.0};
    // The waypoints' authored Euler triples (ax, ay, az). The engine builds each
    // waypoint frame from them with Matrix_SetEuler (ParamReal32x9_Parse,
    // HOOK 0x4EFBA0, src/physobj/paramops.c:423-435) - igi::SplineWaypointFrame.
    glm::dvec3 rotFrom{0.0};
    glm::dvec3 rotTo{0.0};
    // The ending waypoint's authored "NumAreas", and the container's "Number of
    // Matrices/Segment" (SplineObj arg 7 -> maxSegments, SplineObj_Open
    // HOOK 0x502250, src/editobj/spline.c:209). The engine combines them in
    // SplineObjWaypoint_Changed (src/tagitem/spline.c:771-772): min(waypoint,
    // container), then min 20, then 1 when the span is rigid
    // (SplineObjSegment_Setup, src/sound/segsetup.c:268-270). A waypoint that
    // declares no NumAreas keeps the container's count - SplineObjWaypoint_Create
    // sets `w->segments = obj->maxSegments` (src/tagitem/spline.c:544) - which
    // the editor expresses as `waypointSegments == 0`.
    int waypointSegments = 20;
    int maxSegments = 20;
    // `w->rigid = obj->rigid || w->straight` (src/tagitem/spline.c:769), passed
    // on as `w->rigid || w->straight` (:80-81): the container's "Linear
    // Segments" or the waypoint's "Align".
    bool rigid = false;
    // The ending waypoint's authored "Flip" (SplineObjWaypoint.reversed,
    // src/editobj/editobjtypes.h:6-25).
    bool reversed = false;
};

// The transformed span, ready for the GPU.
struct EngineSpanMesh {
    // The renderer's interleaved GPU vertex - 10 floats: position xyz, normal
    // xyz, uv, uv2 - one entry per SOURCE vertex, in source order. Positions are
    // relative to the span's mid point and stored at the editor's 1/40.96 render
    // scale, exactly as `AppendBentVertex` stores them.
    std::vector<float> interleaved;
    // One index list per render block: the model's own triangle indices, which
    // the engine keeps and draws (`g->indices`, src/render/render.c:530-531).
    std::vector<std::vector<uint32_t>> indices;
    int vertexCount = 0;
};

// Runs the engine's transform (`igi::spline::FillSplineSpanVertices`) over the
// parsed segment mesh and converts the result into the renderer's layout.
// Returns an empty mesh (vertexCount 0) when the model has no usable X range or
// the span is degenerate.
EngineSpanMesh BuildEngineSpanMesh(
    const ParsedGeometry& geometry,
    const EngineSpanInput& span);

// The whole SplineObj path: every span's joints chained in waypoint order, with
// the cumulative arc length the engine keeps in SplineObj.length.
struct RailPath {
    std::vector<SplineJoint> joints;   // world space, joints[0] = first waypoint
    std::vector<double> arc;           // arc[i] = distance from joints[0]
    double length = 0.0;
};

RailPath BuildRailPath(const std::vector<SplineSpan>& spans);

// SplineObj_GetPoints (HOOK 0x4FEC10, src/tagitem/spline.c:219-454): the scalar
// is signed and end-relative — negative values measure backwards from the last
// waypoint, and the result is wrapped into [0, length).
double WrapRailDistance(double distance, double length) noexcept;

// Point at an arc distance from joints[0], walked along the joint polyline
// exactly as the engine's segment/spacing walk does.
bool SampleRailPoint(const RailPath& path, double arcDistance, glm::dvec3& out) noexcept;

// Matrix_SetDirection (HOOK 0x4B4010, src/math/matrix.c:244-277) followed by the
// train's fixed axis remap `orient` (src/missile/train.c:172-190).
glm::dmat3 TrackDirectionMatrix(const glm::dvec3& trackDirection) noexcept;

// Train_Place (HOOK 0x42A2C0, src/missile/train.c:209-274). `railDistance` is the
// authored signed scalar (Position), `trainLength` the model's local X extent
// (Train_ParamsChanged, HOOK 0x42A150, src/missile/train.c:142-167), `frac` its
// |minX| / length, `flip` the authored "Flip Direction" and `displacementX`/`Y`
// the Displacement fields, clamped to gTrainOffsetRange (src/missile/train.c:380)
// and scaled by kFixed (4096 units/m, src/math/mathvars.h:7).
struct TrainPlacement {
    glm::dvec3 position{0.0};
    glm::dmat3 matrix{1.0};
    bool found = false;
};

TrainPlacement PlaceTrainOnRail(
    const RailPath& path,
    double railDistance,
    double trainLength,
    double frac,
    double displacementX,
    double displacementY,
    bool flip) noexcept;

} // namespace spline_geometry
