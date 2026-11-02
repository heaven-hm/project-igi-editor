#pragma once

#include <cstdint>

namespace igi {

constexpr bool ShouldPickSceneHover(bool pointerChanged,
                                    bool leftButtonDown,
                                    bool cameraNavigating = false) noexcept {
    return pointerChanged && !leftButtonDown && !cameraNavigating;
}

// Scene picking performs a full-color pass plus a synchronous readback. Keep
// hover responsive without allowing rapid mouse motion to issue that blocking
// GPU operation every frame. Camera look/move never picks: that GPU sync is
// what makes turning the view feel like it is running in slow motion.
constexpr bool ShouldPickSceneHover(bool pointerChanged,
                                    bool leftButtonDown,
                                    int64_t nowMs,
                                    int64_t lastPickMs,
                                    int64_t minIntervalMs = 33,
                                    bool cameraNavigating = false) noexcept {
    return ShouldPickSceneHover(pointerChanged, leftButtonDown, cameraNavigating) &&
           (lastPickMs < 0 || nowMs - lastPickMs >= minIntervalMs);
}

// A level can reference models that are not present in its own model archive.
// Resolving every such model from other levels during the normal scene pass
// grows the cross-level caches until the 32-bit editor runs out of memory.
// Keep the useful magenta foreign-model preview, but load it only on demand.
constexpr bool ShouldResolveMissingSceneModel(bool modelMissingInLevel,
                                              bool selected,
                                              bool hovered) noexcept {
    return !modelMissingInLevel || selected || hovered;
}

// One hover pick is NOT cheap: Renderer_Objects::PickObjectAtScreen renders the
// whole scene a second time into a full-viewport FBO and then blocks on a 1x1
// glReadPixels (source/renderer/renderer_objects_picking.cpp:374-412; the pick's
// own IGI_PROFILE block is there because a call was measured at ~23 ms).
//
// Issuing that on the motion event itself pays for a cursor position the user
// has already left, and the 33 ms throttle still allows ~30 calls/s - more than
// a second of GPU-blocked work per second. That is why the view stutters while
// the mouse is moving; it is not a camera-math problem: App::ProcessInput's
// movement is identical with and without the camera key (viewer_s::clip_to_z_ is
// never set after construction, so the velocity/friction branch is dead code).
//
// So: defer the hover pick until the pointer has SETTLED (no motion event for
// `settleMs`), then pick once for the settled position. Camera navigation never
// picks at all.
constexpr bool ShouldPickSceneHoverOnSettle(bool pointerMovedSinceLastPick,
                                            bool leftButtonDown,
                                            int64_t nowMs,
                                            int64_t lastMotionMs,
                                            int64_t lastPickMs,
                                            int64_t settleMs = 120,
                                            int64_t minIntervalMs = 33,
                                            bool cameraNavigating = false) noexcept {
    if (!pointerMovedSinceLastPick || leftButtonDown || cameraNavigating) return false;
    // Still in flight: the position will change again before it is worth a pick.
    if (lastMotionMs >= 0 && nowMs - lastMotionMs < settleMs) return false;
    return lastPickMs < 0 || nowMs - lastPickMs >= minIntervalMs;
}

} // namespace igi
