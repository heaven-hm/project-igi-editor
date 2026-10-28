#pragma once

// viewport_size_policy.h - which size the GL viewport must use.
//
// The editor has several independent places that apply a viewport: the GLUT
// reshape callback, the two fullscreen paths, the gameplay surface, and the
// `-w`/`-h` startup seed.  They all write the SAME `window_state_`, so the last
// caller wins for both the GL viewport and every UI hit-test -- and a caller
// that applies a size the window no longer has leaves the rest of the surface
// unpainted (the reported "half screen and half black").
//
// The authoritative size is the presenting window's CLIENT rectangle.  GLUT's
// own idea of the size is only a fallback, because the borderless-fullscreen
// path resizes the Win32 window with a direct `SetWindowPos`, which does not go
// through GLUT and so can leave `glutGet(GLUT_WINDOW_*)` stale.
//
// Kept free of windows.h/GLUT so the fallback order is host-testable.

namespace igi {

// Priority: the real client extent, then GLUT's extent, then the requested
// `-w`/`-h` seed.  Never returns a value below 1 (a zero/negative extent would
// make the caller divide by zero when it derives fovx_ from the aspect ratio).
inline int ResolveViewportExtent(int client_extent, int glut_extent,
                                 int requested_extent) {
    if (client_extent > 0) return client_extent;
    if (glut_extent > 0) return glut_extent;
    if (requested_extent > 0) return requested_extent;
    return 1;
}

} // namespace igi
