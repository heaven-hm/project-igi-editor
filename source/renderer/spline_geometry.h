#pragma once

#include <glm/glm.hpp>

#include <optional>
#include <string_view>

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

// Converts a waypoint's Z-X-Y Euler orientation into its local-X spline
// tangent, scaled to the span chord length.
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

} // namespace spline_geometry
