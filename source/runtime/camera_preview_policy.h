#pragma once

namespace igi {

// Camera-key look/orbit must retain the complete editor scene. Fast preview is
// reserved for navigation driven by movement keys, where it is safe to omit
// secondary overlays for a frame.
inline bool ShouldUseFastScenePreview(
    bool camera_navigating, bool render_gameplay, bool camera_mode_active) {
    return camera_navigating && !render_gameplay && !camera_mode_active;
}

} // namespace igi
