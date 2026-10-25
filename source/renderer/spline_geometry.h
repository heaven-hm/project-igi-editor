#pragma once

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
BentSegmentMesh BendSegmentMesh(
    const ParsedGeometry& geometry,
    const SplineSpan& span);

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
