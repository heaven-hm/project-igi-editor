#include <gtest/gtest.h>

#include "../source/runtime/editor_camera_start.h"
#include "../source/runtime/graph_camera_target.h"
#include "../source/capture_camera.h"
#include "../source/renderer/renderer_objects_internal.h"

namespace {

TEST(EditorCameraStartTest, RejectsPersistedCameraOnFreshAndSwitchedLevelLoads) {
    const glm::vec3 stale_camera(127000280.0f, -37829456.0f, 175303728.0f);

    EXPECT_FALSE(igi::ShouldUseSavedEditorCamera(-1, 12, stale_camera));
    EXPECT_FALSE(igi::ShouldUseSavedEditorCamera(9, 12, stale_camera));
}

TEST(EditorCameraStartTest, ReusesPersistedCameraOnSameLevelReload) {
    const glm::vec3 saved_camera(112255288.0f, -42206448.0f, 179544736.0f);

    EXPECT_TRUE(igi::ShouldUseSavedEditorCamera(12, 12, saved_camera));
    EXPECT_FALSE(igi::ShouldUseSavedEditorCamera(12, 12, glm::vec3(0.0f)));
}

TEST(EditorCameraStartTest, ResetLevelForcesAuthoredStartCamera) {
    // When a level reset is triggered, last_loaded_level is reset to -1
    // and saved camera is zeroed, ensuring ShouldUseSavedEditorCamera evaluates false
    const glm::vec3 zeroed_camera(0.0f);
    EXPECT_FALSE(igi::ShouldUseSavedEditorCamera(-1, 9, zeroed_camera));

    const glm::vec3 stale_camera(127000280.0f, -37829456.0f, 175303728.0f);
    EXPECT_FALSE(igi::ShouldUseSavedEditorCamera(-1, 9, stale_camera));
}

TEST(EditorCameraStartTest, FocusesOutsideModelBounds) {
    const auto pose = igi::FocusCameraOnModel(glm::vec3(10.0f, 20.0f, 30.0f), 1000.0f);

    EXPECT_NE(pose.position.y, 20.0f);
    EXPECT_LT(pose.position.y, 20.0f);
    EXPECT_LT(pose.pitch, 0.0f);
}

TEST(EditorCameraStartTest, KeepsBuildingAttachmentsVisibleInsideModelBounds) {
    // Level 12's authored start lies within FortressWinchHouse's transformed
    // bounds but beyond its short portal distance. The normal viewport must
    // retain the structural attachments in that valid interior view.
    EXPECT_TRUE(igi::ShouldDrawBuildingAttachments(
        31000.0f, 199877.0f, 25600.0f, true));
}

TEST(EditorCameraStartTest, FortressWinchHouseCutoffDistanceKeepsAttachmentsVisibleAtStart) {
    // In Level 12, FortressWinchHouse (463_01_1) has LOD cutoff = 600m (2,457,600 units).
    // The player / start camera is at distance ~98,000 units (~24m) from the building.
    // The attachment culling must NOT cull the winch machinery, cables, or glass rooms.
    const float distToCamera = 98000.0f;
    const float boundRadius = 34693.0f;
    const float cutoffDistance = 600.0f * 4096.0f; // 600m in world units

    EXPECT_TRUE(igi::ShouldDrawBuildingAttachments(
        distToCamera, boundRadius, cutoffDistance, true));

    // Beyond cutoff distance (e.g. 700m), attachments should be culled
    const float farDistance = 700.0f * 4096.0f;
    EXPECT_FALSE(igi::ShouldDrawBuildingAttachments(
        farDistance, boundRadius, cutoffDistance, true));
}

TEST(EditorCameraStartTest, F11CameraSnapToObjectFramesOutsideModelBounds) {
    const glm::dvec3 objPos(112284896.0, -42113812.0, 179532464.0);
    const float boundRadius = 34693.0f;
    const glm::dvec3 viewerForward(0.0, 1.0, 0.0);

    // Standard F11 framing: distance is at least 1.6 * boundRadius
    const GraphCameraPose poseNormal = MakeF11ObjectCameraPose(
        objPos, boundRadius, viewerForward, /*shiftHeld=*/false);

    const double distNormal = glm::distance(poseNormal.position, objPos);
    EXPECT_GE(distNormal, static_cast<double>(boundRadius * 1.6f));
    EXPECT_NE(poseNormal.position, objPos);

    // Shift+F11 framing: distance is at least 3.0 * boundRadius
    const GraphCameraPose poseShift = MakeF11ObjectCameraPose(
        objPos, boundRadius, viewerForward, /*shiftHeld=*/true);

    const double distShift = glm::distance(poseShift.position, objPos);
    EXPECT_GE(distShift, static_cast<double>(boundRadius * 3.0f));
    EXPECT_GT(distShift, distNormal);
}

TEST(EditorCameraStartTest, IdentifiesZyxEulerModelsCorrectly) {
    // 506_ slide up doors use ZYX Euler rotation order
    EXPECT_TRUE(IsZyxEulerModel("506_01_1"));
    EXPECT_TRUE(IsZyxEulerModel("506_02_1"));
    // 615_01_1 missile on rack/carriage in Level 9/13 uses ZYX Euler rotation order
    EXPECT_TRUE(IsZyxEulerModel("615_01_1"));

    // Standard objects use default ZXY Euler rotation order
    EXPECT_FALSE(IsZyxEulerModel("615_10_1")); // carriage
    EXPECT_FALSE(IsZyxEulerModel("205_01_1")); // desk
    EXPECT_FALSE(IsZyxEulerModel(""));
}

#include "../source/runtime/editor_cursor_mode.h"

TEST(EditorCameraStartTest, ResolveCursorModeForTerrainTools) {
    using igi::CursorMode;
    using igi::ResolveCursorMode;

    // Terrain edit brushes: 0=Raise/Lift, 1=Lower, 2=Soften, 3=Flatten, other=Drop
    EXPECT_EQ(ResolveCursorMode(false, false, true, 0, -1, -1), CursorMode::TerrainLift);
    EXPECT_EQ(ResolveCursorMode(false, false, true, 1, -1, -1), CursorMode::TerrainLower);
    EXPECT_EQ(ResolveCursorMode(false, false, true, 2, -1, -1), CursorMode::TerrainSoften);
    EXPECT_EQ(ResolveCursorMode(false, false, true, 3, -1, -1), CursorMode::TerrainFlatten);
    EXPECT_EQ(ResolveCursorMode(false, false, true, 4, -1, -1), CursorMode::TerrainDrop);

    // Normal mode: Always Default (pointer arrow) matching main branch behavior
    EXPECT_EQ(ResolveCursorMode(false, false, false, 0, -1, -1), CursorMode::Default);
    EXPECT_EQ(ResolveCursorMode(false, false, false, 0, -1, 5), CursorMode::Default);
    EXPECT_EQ(ResolveCursorMode(false, false, false, 0, 2, -1), CursorMode::Default);
    EXPECT_EQ(ResolveCursorMode(false, false, false, 0, 2, 5), CursorMode::Default);

    // Camera mode overrides all: Camera on LMB down, Move on movement
    EXPECT_EQ(ResolveCursorMode(true, true, true, 0, -1, -1), CursorMode::Camera);
    EXPECT_EQ(ResolveCursorMode(true, false, true, 0, -1, -1), CursorMode::Move);
    EXPECT_EQ(ResolveCursorMode(true, true, false, 0, -1, -1), CursorMode::Camera);
    EXPECT_EQ(ResolveCursorMode(true, false, false, 0, -1, -1), CursorMode::Move);
}

// Regression Guard: The cursor must NEVER return Hover (highlighttool.spr,
// a 33x33 blue-and-white square) or Selected (activetool.spr, a 33x33 white square)
// under ANY combination of scene hover or selection states.
TEST(EditorCameraStartTest, RegressionGuard_NeverReturnsSquareToolIcons) {
    using igi::CursorMode;
    using igi::ResolveCursorMode;

    const bool bools[] = {false, true};
    const int brushes[] = {-2, -1, 0, 1, 2, 3, 4, 10};
    const int objectIndices[] = {-1, 0, 1, 42, 999};

    for (bool cam : bools) {
        for (bool lmb : bools) {
            for (bool terrainEdit : bools) {
                for (int brush : brushes) {
                    for (int selIdx : objectIndices) {
                        for (int hovIdx : objectIndices) {
                            CursorMode mode = ResolveCursorMode(
                                cam, lmb, terrainEdit, brush, selIdx, hovIdx);

                            // Absolute invariant: square toolbar icons must NEVER be used as cursors
                            EXPECT_NE(mode, CursorMode::Hover)
                                << "Regression: Hover square tool icon (blue/white square) was returned!";
                            EXPECT_NE(mode, CursorMode::Selected)
                                << "Regression: Selected square tool icon was returned!";
                            EXPECT_NE(mode, CursorMode::Inactive)
                                << "Regression: Inactive tool icon was returned!";

                            if (cam) {
                                EXPECT_EQ(mode, lmb ? CursorMode::Camera : CursorMode::Move);
                            } else if (terrainEdit) {
                                if (brush == 0) EXPECT_EQ(mode, CursorMode::TerrainLift);
                                else if (brush == 1) EXPECT_EQ(mode, CursorMode::TerrainLower);
                                else if (brush == 2) EXPECT_EQ(mode, CursorMode::TerrainSoften);
                                else if (brush == 3) EXPECT_EQ(mode, CursorMode::TerrainFlatten);
                                else EXPECT_EQ(mode, CursorMode::TerrainDrop);
                            } else {
                                EXPECT_EQ(mode, CursorMode::Default);
                            }
                        }
                    }
                }
            }
        }
    }
}

// Contract: Verify enum integer indices match the exact SPR loading order in App::LoadAllCursors()
TEST(EditorCameraStartTest, CursorModeEnumValuesMatchSprSlotIndexContract) {
    using igi::CursorMode;

    EXPECT_EQ(static_cast<int>(CursorMode::Default), 0);
    EXPECT_EQ(static_cast<int>(CursorMode::Hover), 1);
    EXPECT_EQ(static_cast<int>(CursorMode::Selected), 2);
    EXPECT_EQ(static_cast<int>(CursorMode::TerrainLift), 3);
    EXPECT_EQ(static_cast<int>(CursorMode::TerrainLower), 4);
    EXPECT_EQ(static_cast<int>(CursorMode::TerrainFlatten), 5);
    EXPECT_EQ(static_cast<int>(CursorMode::TerrainFlattenLine), 6);
    EXPECT_EQ(static_cast<int>(CursorMode::TerrainDrop), 7);
    EXPECT_EQ(static_cast<int>(CursorMode::TerrainSoften), 8);
    EXPECT_EQ(static_cast<int>(CursorMode::Inactive), 9);
    EXPECT_EQ(static_cast<int>(CursorMode::Camera), 10);
    EXPECT_EQ(static_cast<int>(CursorMode::Move), 11);
}

// Contract: When selecting each terrain tool from the palette, verify the cursor updates to the distinct tool icon
TEST(EditorCameraStartTest, TerrainBrushSwitchingGivesDistinctIcons) {
    using igi::CursorMode;
    using igi::ResolveCursorMode;

    // Each terrain brush must have a UNIQUE cursor mode so the icon visually changes
    CursorMode lift    = ResolveCursorMode(false, false, true, 0, -1, -1);
    CursorMode lower   = ResolveCursorMode(false, false, true, 1, -1, -1);
    CursorMode soften  = ResolveCursorMode(false, false, true, 2, -1, -1);
    CursorMode flatten = ResolveCursorMode(false, false, true, 3, -1, -1);
    CursorMode drop    = ResolveCursorMode(false, false, true, 4, -1, -1);

    EXPECT_EQ(lift,    CursorMode::TerrainLift);
    EXPECT_EQ(lower,   CursorMode::TerrainLower);
    EXPECT_EQ(soften,  CursorMode::TerrainSoften);
    EXPECT_EQ(flatten, CursorMode::TerrainFlatten);
    EXPECT_EQ(drop,    CursorMode::TerrainDrop);

    // None of the brushes can share the same cursor mode with each other
    EXPECT_NE(lift, lower);
    EXPECT_NE(lift, soften);
    EXPECT_NE(lift, flatten);
    EXPECT_NE(lower, soften);
    EXPECT_NE(lower, flatten);
    EXPECT_NE(soften, flatten);
}

TEST(EditorCameraStartTest, OrbitPrefersClickedObjectOverStaleSelection) {
    EXPECT_EQ(igi::ResolveOrbitObjectIndex(4, 2, 0, 10), 4);
    EXPECT_EQ(igi::ResolveOrbitObjectIndex(-1, 2, 0, 10), 2);
    EXPECT_EQ(igi::ResolveOrbitObjectIndex(-1, -1, 0, 10), 0);
    EXPECT_EQ(igi::ResolveOrbitObjectIndex(99, 2, 0, 10), 2);
    EXPECT_EQ(igi::ResolveOrbitObjectIndex(-1, -1, -1, 10), -1);
}

TEST(EditorCameraStartTest, AltMouseOrbitKeepsTargetFixedThroughFullYawTurn) {
    const glm::vec3 target(1000.0f, 2000.0f, 3000.0f);
    const glm::vec3 start = target + glm::vec3(0.0f, -4000.0f, 500.0f);
    igi::ObjectOrbitCamera orbit = igi::BeginObjectOrbit(start, target);
    const float start_distance = orbit.distance;

    for (int step = 0; step < 36; ++step) {
        orbit = igi::StepObjectOrbit(orbit, 10.0f, 0.0f);
        const glm::vec3 pos = igi::ObjectOrbitCameraPosition(orbit);
        EXPECT_NEAR(glm::distance(pos, target), start_distance, 0.5f);
        const glm::vec3 forward =
            igi::ObjectOrbitForward(orbit.yaw_degrees, orbit.pitch_degrees);
        const glm::vec3 to_target = glm::normalize(target - pos);
        EXPECT_NEAR(glm::dot(forward, to_target), 1.0f, 0.002f);
    }
}

TEST(EditorCameraStartTest, AltMouseOrbitPitchLooksOverAndUnderWithoutFlipping) {
    const glm::vec3 target(0.0f, 0.0f, 0.0f);
    igi::ObjectOrbitCamera orbit =
        igi::BeginObjectOrbit(glm::vec3(0.0f, -1000.0f, 0.0f), target);
    orbit = igi::StepObjectOrbit(orbit, 0.0f, 200.0f);
    EXPECT_LE(orbit.pitch_degrees, 89.0f);
    orbit = igi::StepObjectOrbit(orbit, 0.0f, -400.0f);
    EXPECT_GE(orbit.pitch_degrees, -89.0f);
    const glm::vec3 pos = igi::ObjectOrbitCameraPosition(orbit);
    EXPECT_GT(glm::distance(pos, target), 0.1f);
}

} // namespace
