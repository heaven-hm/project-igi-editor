#pragma once

namespace igi {

// Camera-key look/orbit must retain the complete editor scene and TaskTree HUD.
// Fast preview is reserved for navigation driven by movement keys without the
// camera key, where secondary overlays can be omitted for a frame.
inline bool ShouldUseFastScenePreview(
    bool camera_navigating, bool render_gameplay, bool camera_mode_active = false) {
    return camera_navigating && !render_gameplay && !camera_mode_active;
}

} // namespace igi
