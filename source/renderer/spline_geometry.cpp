#include "spline_geometry.h"

#include <cmath>

namespace spline_geometry {
namespace {

bool IsFinite(const glm::dvec3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
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

    const double t2 = t * t;
    const double t3 = t2 * t;
    const double h10 = t3 - 2.0 * t2 + t;
    const double h01 = -2.0 * t3 + 3.0 * t2;
    const double h11 = t3 - t2;
    const double d10 = 3.0 * t2 - 4.0 * t + 1.0;
    const double d01 = 6.0 * t - 6.0 * t2;
    const double d11 = 3.0 * t2 - 2.0 * t;
    return h10 * segment.tangent0 + h01 * (segment.p1 - segment.p0) + h11 * segment.tangent1;
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

    const double forwardLen = glm::length(forward);
    if (forwardLen <= 1e-12) return std::nullopt;
    const glm::dvec3 fwd = forward / forwardLen;

    const glm::dvec3 reference = std::abs(fwd.z) < 0.99
        ? glm::dvec3(0.0, 0.0, 1.0)
        : glm::dvec3(0.0, 1.0, 0.0);
    const glm::dvec3 right = glm::normalize(glm::cross(reference, fwd));
    const glm::dvec3 up = glm::cross(fwd, right);
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

    const glm::dvec3 forward = delta / span;
    const glm::dvec3 reference = std::abs(forward.z) < 0.99
        ? glm::dvec3(0.0, 0.0, 1.0)
        : glm::dvec3(0.0, 1.0, 0.0);
    const glm::dvec3 right = glm::normalize(glm::cross(reference, forward));
    const glm::dvec3 up = glm::cross(forward, right);
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

} // namespace spline_geometry
