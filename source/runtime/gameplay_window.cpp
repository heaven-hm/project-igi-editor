// gameplay_window.cpp - Same-window gameplay surface lifecycle implementation
//
// Gameplay renders directly into the existing editor GLUT window instead of a
// separate top-level window. The host keeps its id bookkeeping so callers can
// query availability, but "the gameplay window" is an alias of the editor
// window: display/input callbacks are already routed by App via in_game_mode_
// and the WindowInputRouter focus, so no second set of GLUT callbacks exists.
#include "gameplay_window.h"

#if defined(_WIN32)
#include <windows.h>
#include <freeglut.h>
#endif

namespace igi {

GameplayWindowHost::~GameplayWindowHost() {
    Destroy();
}

bool GameplayWindowHost::Create(
    int editor_window_id,
    int width,
    int height,
    const GameplayWindowCallbacks& callbacks) {
#if !defined(_WIN32)
    (void)editor_window_id;
    (void)width;
    (void)height;
    (void)callbacks;
    return false;
#else
    if (IsCreated()) {
        return true;
    }
    if (editor_window_id <= 0 || !glutGetWindow()) {
        return false;
    }

    // Same-window mode: alias the gameplay surface onto the editor window.
    // The editor's registered callbacks keep firing; App routes them to
    // gameplay presentation/input while the runtime session is active.
    (void)width;
    (void)height;
    (void)callbacks;
    editor_window_id_ = editor_window_id;
    gameplay_window_id_ = editor_window_id;
    return true;
#endif
}

void GameplayWindowHost::Destroy() {
    // The aliased window is the editor's own window; never destroy it.
    gameplay_window_id_ = 0;
    editor_window_id_ = 0;
}

void GameplayWindowHost::NotifyClosed() {
    gameplay_window_id_ = 0;
}

void GameplayWindowHost::Show() {
    // Single shared window: it is already visible.
}

void GameplayWindowHost::Hide() {
    // Leaving gameplay must keep the editor window on screen; do nothing.
}

void GameplayWindowHost::Focus() {
#if defined(_WIN32)
    if (!IsCreated()) return;

    glutSetWindow(gameplay_window_id_);
    glutShowWindow();

    // FreeGLUT's current-window selection does not guarantee OS focus after
    // another top-level window was active. Resolve the native handle by the
    // stable title and explicitly return focus to the shared window.
    HWND shared_window_handle = FindWindowA(nullptr, "IGI Editor");
    if (shared_window_handle != nullptr) {
        SetForegroundWindow(shared_window_handle);
        SetFocus(shared_window_handle);
    }
#endif
}

void GameplayWindowHost::FocusEditor() {
#if defined(_WIN32)
    if (editor_window_id_ == 0) return;

    glutSetWindow(editor_window_id_);
    glutShowWindow();

    HWND editor_window_handle = FindWindowA(nullptr, "IGI Editor");
    if (editor_window_handle != nullptr) {
        SetForegroundWindow(editor_window_handle);
        SetFocus(editor_window_handle);
    }
#endif
}

void GameplayWindowHost::MakeCurrent() const {
#if defined(_WIN32)
    if (IsCreated()) glutSetWindow(gameplay_window_id_);
#endif
}

bool GameplayWindowHost::IsCurrent() const {
#if defined(_WIN32)
    return IsCreated() && glutGetWindow() == gameplay_window_id_;
#else
    return false;
#endif
}

} // namespace igi
