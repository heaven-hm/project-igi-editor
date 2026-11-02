#pragma once

#include "common.h"

#include <algorithm>

namespace igi {

// Capture cameras intentionally work closer than the editor's normal near
// plane. World geometry is scaled before projection, so retain a tenfold gap
// between the near plane and the closest camera target.
inline float ResolveCaptureNearPlane(float target_distance_world,
                                     float configured_near_plane) {
  constexpr float kMinimumNearPlane = 0.01f;
  constexpr float kTargetDistanceFraction = 0.10f;
  const float projected_distance =
      std::max(0.0f, target_distance_world) * RENDERER_MODEL_SCALE_DOWN;
  const float capture_near = std::max(
      kMinimumNearPlane, projected_distance * kTargetDistanceFraction);
  return std::max(0.0001f, std::min(configured_near_plane, capture_near));
}

inline float ResolveBuildingCaptureOrbitRadius(float bound_radius) {
  constexpr float kMinimumOrbitRadius = 12000.0f;
  constexpr float kOutsideBoundsFactor = 1.15f;
  return std::max(kMinimumOrbitRadius,
                  std::max(0.0f, bound_radius) * kOutsideBoundsFactor);
}

// Building attachments remain visible while the camera is within the model's
// transformed bounding sphere, regardless of their authored portal distance.
inline bool ShouldDrawBuildingAttachments(float camera_distance_to_center,
                                         float model_bound_radius,
                                         float portal_distance_world,
                                         bool lod_enabled) noexcept {
  return !lod_enabled ||
         camera_distance_to_center <= portal_distance_world ||
         camera_distance_to_center <= model_bound_radius;
}

}  // namespace igi
