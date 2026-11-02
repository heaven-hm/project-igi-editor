#pragma once

namespace igi {

// Custom SPR cursor modes matching loaded editor sprites:
// 0: editor\qed\TerrainEditIcon_Pointer.spr     (Default / neutral pointer)
// 1: editor\qed\highlighttool.spr              (Hover over object)
// 2: editor\qed\activetool.spr                 (Selected object in edit mode)
// 3: editor\qed\TerrainEditIcon_Lift.spr       (Terrain Raise brush)
// 4: editor\qed\TerrainEditIcon_Lower.spr      (Terrain Lower brush)
// 5: editor\qed\TerrainEditIcon_Flatten.spr    (Terrain Flatten brush)
// 6: editor\qed\TerrainEditIcon_FlattenLine.spr
// 7: editor\qed\TerrainEditIcon_Drop.spr       (Neutral terrain drop)
// 8: editor\qed\TerrainEditIcon_Soften.spr     (Terrain Soften brush)
// 9: editor\qed\inactivetool.spr               (Inactive tool)
// 10: editor\qed\editor_camera.spr             (Camera look mode with ALT held + LMB)
// 11: editor\qed\editor_move.spr               (Lateral camera move with ALT held)
enum class CursorMode {
    Default = 0,
    Hover = 1,
    Selected = 2,
    TerrainLift = 3,
    TerrainLower = 4,
    TerrainFlatten = 5,
    TerrainFlattenLine = 6,
    TerrainDrop = 7,
    TerrainSoften = 8,
    Inactive = 9,
    Camera = 10,
    Move = 11,
};

constexpr CursorMode ResolveCursorMode(
    bool enableCameraMode,
    bool leftButtonDown,
    bool terrainEditEnabled,
    int editBrush,
    int /*selectedObjectIndex*/ = -1,
    int /*hoverObjectIndex*/ = -1) noexcept {
    if (enableCameraMode) {
        return leftButtonDown ? CursorMode::Camera : CursorMode::Move;
    }
    if (terrainEditEnabled) {
        if (editBrush == 0) return CursorMode::TerrainLift;
        if (editBrush == 1) return CursorMode::TerrainLower;
        if (editBrush == 2) return CursorMode::TerrainSoften;
        if (editBrush == 3) return CursorMode::TerrainFlatten;
        return CursorMode::TerrainDrop;
    }
    return CursorMode::Default;
}

} // namespace igi
