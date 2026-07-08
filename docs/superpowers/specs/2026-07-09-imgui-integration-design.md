# ImGui Integration — Phase 1: Backend Plumbing — Design

**Status**: Approved. Phase 1 of a multi-phase "full UI overhaul" (custom
hand-rolled pause menu / overlays → Dear ImGui). This spec covers only
Phase 1. Phases 2-4 are outlined at the end for context and get their own
specs when we get to them.

## 1. Goal

Wire Dear ImGui into the editor so a real ImGui window can be shown,
interacted with, and rendered on top of the existing GL scene — with zero
changes to existing rendering, input handling, or UI behavior. This phase
proves the integration works; it does not replace any existing UI.

Phase 1 is a prerequisite for everything after it: the pause menu rebuild
(Phase 2), overlay migration (Phase 3), and dead-code cleanup (Phase 4) all
depend on this plumbing existing and being verified first.

## 2. Constraint: No Official GLUT Backend

The editor windows via **freeglut** (`glutCreateWindow`/`glutMainLoop` in
`source/main.cpp`), not GLFW/SDL/Win32. Dear ImGui ships official backends
for those, but not GLUT. Two options were considered:

- **Migrate off GLUT to GLFW** — would let us use ImGui's official,
  well-tested `imgui_impl_glfw` backend, but touches window creation, the
  entire input callback chain (`app_input_mouse.cpp`,
  `app_input_keyboard.cpp`), fullscreen handling, and the DPI-awareness
  code path (`main.cpp:630-636`) that GLUT-specific behavior currently
  depends on. Much larger blast radius than the ImGui work itself.
- **Keep GLUT, write a thin custom input glue layer** (chosen) — pair the
  official `imgui_impl_opengl3` render backend (which only needs a live GL
  context, not any particular windowing system) with a small hand-written
  adapter that feeds GLUT's existing callbacks into `ImGuiIO`. Zero changes
  to window creation or existing input plumbing.

Decision: keep GLUT, write the custom glue. Lower risk, smaller diff.

## 3. Vendoring

`third_party/imgui/` is added as a plain vendored copy, matching how
`glm-1.0.1/`, `tinygltf/`, and `assimp/` are already checked into
`third_party/` (no git submodules are used anywhere in this repo).

Included:
- ImGui core: `imgui.cpp/.h`, `imgui_draw.cpp`, `imgui_tables.cpp`,
  `imgui_widgets.cpp`, `imgui_demo.cpp` (demo kept in Phase 1 only, for the
  F10 proof-of-life window — see §6).
- Official backend: `imgui_impl_opengl3.cpp/.h`.

Branch: latest **stable** (not docking). The editor has a single GLUT
window; nothing in Phase 1-4 requires multi-viewport or OS-level dockable
panels. If a future phase needs docking, that's a separate, additive
upgrade — not blocking now.

`imgui_impl_opengl3` is built with `IMGUI_IMPL_OPENGL_LOADER_GLEW` defined,
since `gl_helper.cpp` already loads all GL entry points via GLEW
(`GL_Init()`). This avoids linking ImGui's own bundled loader alongside
GLEW.

## 4. Custom GLUT Input Glue

New file: `source/imgui_glut_backend.cpp/.h`. Pure additive code — does not
modify any existing input handler. Responsibilities:

- Translate GLUT mouse button/state into `ImGuiIO::AddMouseButtonEvent`.
- Translate `glutMouseWheelFunc` into `ImGuiIO::AddMouseWheelEvent`.
- Translate motion (`OnMotion`, used for both `glutMotionFunc` and
  `glutPassiveMotionFunc`) into `ImGuiIO::AddMousePosEvent`.
- Translate `glutKeyboardFunc`/`glutKeyboardUpFunc` (regular keys) and
  `glutSpecialFunc`/`glutSpecialUpFunc` (arrows, F-keys, etc.) into
  `ImGuiIO::AddKeyEvent`.
- Update `ImGuiIO::DisplaySize` on `OnReshape`.

## 5. Main-Loop Integration

All changes below are additive lines in existing functions — no existing
call is removed or reordered relative to itself.

- `ImGui::CreateContext()` + `ImGui_ImplOpenGL3_Init()` run once, immediately
  after `GL_Init()` succeeds in `main()` (`main.cpp:695`).
- Each existing GLUT callback (`OnMouse`, `OnMouseWheel`, `OnMotion`,
  `OnKeyboard`, `OnKeyboardUp`, `OnSpecial`, `OnSpecialUp`, `OnReshape`)
  additionally forwards to the new `imgui_glut_backend` glue, alongside its
  existing `g_app.Input_On...()` call.
- `ImGui::NewFrame()` is called at the top of `OnDisplay()` (`main.cpp:137`),
  before `g_app.OnDisplay()` runs.
- `App::OnDisplay()` calls `glutSwapBuffers()` from three call sites
  (`app.cpp:505`, `app.cpp:802`, `app_ui.cpp:359` — normal frame path,
  loading/progress path, and one more). A new helper,
  `GL_SwapBuffersWithImGui()` in `gl_helper.cpp`, does `ImGui::Render()` +
  `ImGui_ImplOpenGL3_RenderDrawData()` then `glutSwapBuffers()`. All three
  call sites are switched to call this helper instead of `glutSwapBuffers()`
  directly. This avoids restructuring `App::OnDisplay()`'s three separate
  render paths into one.
- `ImGui_ImplOpenGL3_Shutdown()` + `ImGui::DestroyContext()` run in
  `App::Shutdown()` (`app.cpp:186`).

## 6. Input Coexistence

In each forwarding callback, if `ImGui::GetIO().WantCaptureMouse` (for mouse
callbacks) or `WantCaptureKeyboard` (for keyboard callbacks) is true, the
existing `g_app.Input_On...()` call for that event is skipped — so
clicking/typing into an ImGui window doesn't simultaneously move the 3D
camera, rotate the view, or trigger game hotkeys.

Until Phase 2 adds real panels, the only ImGui content is the demo window
below, so this gating is inert during normal use and only activates while
the demo window is open.

**Proof of life**: pressing **F10** toggles `ImGui::ShowDemoWindow()`. This
is the Phase 1 acceptance check — confirms rendering, input routing,
resizing, and non-interference with existing camera/pause-menu controls all
work. The demo window and its F10 toggle are deleted in Phase 2 once real
panels exist; noting that here so it isn't mistaken for permanent scope.

## 7. Build System

`CMakeLists.txt` gains a new glob (`SOURCES_IMGUI`) covering
`third_party/imgui/*.cpp/.h` and `source/imgui_glut_backend.*`, added to the
`igi-editor` target and given a `source_group("imgui", ...)`, matching the
existing pattern for `level`/`renderer`/`cli` groups. No new PCH exclusion
is planned; one will be added only if a build-memory issue like the ones
already documented for `renderer_objects*.cpp` turns up.

## 8. Testing / Verification

No automated test harness exists for rendering/input in this codebase
(`igi_tests.exe` covers parsers/data, not the GL/input loop). Verification
is manual, per this repo's existing convention for renderer-touching
changes:

1. Build succeeds (`-A Win32`, matching the project's 32-bit target).
2. Launch the editor, load a level.
3. Press F10 → ImGui demo window appears, is draggable/resizable, and its
   widgets respond.
4. While the demo window is focused and being interacted with, confirm
   mouse drags do **not** also rotate the 3D camera, and keyboard input does
   **not** also trigger game hotkeys (`WantCaptureMouse`/`WantCaptureKeyboard`
   gating works).
5. Press F10 again to close it, confirm normal camera/keyboard control is
   completely unaffected (unchanged from before this change).
6. Resize the window / toggle fullscreen (Alt+Enter) — ImGui's
   `DisplaySize` follows correctly.

## 9. Later Phases (context only, not designed yet)

- **Phase 2**: Rebuild the pause menu (fog enable/intensity, other settings
  currently hit-tested by hand in `app_input_mouse.cpp`) as ImGui windows.
  Demo window/F10 toggle removed here.
- **Phase 3**: Migrate remaining hand-rolled overlays (progress bar, help
  text, autocomplete) to ImGui.
- **Phase 4**: Delete the now-dead hand-rolled pixel hit-testing and overlay
  drawing code that Phases 2-3 made obsolete.

Cursor sprites (`App::LoadAllCursors`, `app_ui.cpp`) are explicitly **out of
scope** for the whole overhaul — those are in-world/game cursor icons, not
editor chrome, and ImGui does not replace them.
