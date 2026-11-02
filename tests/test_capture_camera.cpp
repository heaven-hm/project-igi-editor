#include <gtest/gtest.h>

#include "capture_camera.h"

TEST(CaptureCameraTest, TightensNearPlaneForClosePropCapture) {
  const float near_plane = igi::ResolveCaptureNearPlane(264.0f, 2.8672f);

  EXPECT_NEAR(near_plane, 0.0264f, 0.0001f);
  EXPECT_LT(near_plane, 264.0f * RENDERER_MODEL_SCALE_DOWN);
}

TEST(CaptureCameraTest, NeverExpandsConfiguredNearPlane) {
  EXPECT_FLOAT_EQ(igi::ResolveCaptureNearPlane(10000.0f, 0.05f), 0.05f);
}

TEST(CaptureCameraTest, BuildingOrbitStaysOutsideMeshBounds) {
  constexpr float kLevel14BuildingBoundRadius = 66750.742188f;

  EXPECT_GT(igi::ResolveBuildingCaptureOrbitRadius(kLevel14BuildingBoundRadius),
            kLevel14BuildingBoundRadius);
}
