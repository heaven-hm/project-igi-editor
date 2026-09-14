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

// open-igi WorldScene.BendSegment: deform every vertex along local X through the
// span's Hermite curve using the zero-roll frame. Output interleaved vertices use
// the same 10-float layout as model.cpp (pos, normal, uv, uv2).
struct BentSegmentMesh {
    std::vector<float> interleaved;
    std::vector<int> submeshVertexCounts;
};

std::optional<BentSegmentMesh> BendSegmentMesh(
    const ParsedGeometry& geometry,
    const SplineSegment& segment);

} // namespace spline_geometry
