#pragma once

namespace igi {

// Fast scene preview is used during active camera navigation (movement keys or
// active camera-key drag/orbit) to skip CPU-heavy skinned meshes and overlays
// for smooth interactive frame rates. When stationary in camera mode, the full
// editor scene and overlays are retained.
inline bool ShouldUseFastScenePreview(
    bool camera_navigating, bool render_gameplay, bool camera_mode_active = false,
    bool camera_mode_moving = false) {
    if (render_gameplay) return false;
    if (camera_mode_active && !camera_mode_moving) return false;
    return camera_navigating;
}

} // namespace igi
