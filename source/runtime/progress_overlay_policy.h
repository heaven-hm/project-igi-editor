#pragma once

namespace igi {

// Describes one synchronous progress-overlay presentation pass. Loading a
// level or lightmaps runs synchronously on the main loop thread; the overlay
// repaints + presents the bar at each milestone and then pumps window events
// so the OS does not flag the window as unresponsive.
struct ProgressOverlayState {
    bool overlay_visible = false;   // overlay currently owns the GL surface
    bool redisplay_pending = false; // a pumped event queued a scene repaint
};

// A pending scene repaint must be suppressed while the overlay owns the
// surface: App::OnDisplay would otherwise draw the level scene over the
// overlay's back buffer AND swap it, so the bar is never seen.
inline bool ShouldSuppressSceneRepaint(const ProgressOverlayState& state) {
    return state.overlay_visible;
}

// The overlay frame must be presented (swap) BEFORE pumping window events.
// Pumping first lets a queued scene repaint overwrite the overlay between the
// draw and the swap, which is why the bar appeared to never show up.
inline bool ShouldPresentOverlayBeforePumpingEvents(const ProgressOverlayState& state) {
    return state.overlay_visible;
}

// The overlay is positioned and its projection set from the editor's viewport, so
// a viewport the editor does not know yet (0) means the frame is dropped: there is
// nothing to draw into.  That is not a corner case -- the STARTUP level load runs
// inside `App::Init`, before GLUT's reshape callback is registered
// (source/main.cpp), so `App::Init` must seed the viewport from the window GLUT has
// already created, or the one load every user sees reports nothing at all.
inline bool ProgressOverlayCanPaint(int viewport_width, int viewport_height) {
    return viewport_width > 0 && viewport_height > 0;
}

} // namespace igi
