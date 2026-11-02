# IGIED-LEVEL12-STARTUP-CAMERA-20260907

## Symptom

Opening Level 12 could place the editor camera at a stale position saved from
another level/session. The editor then opened away from the authored game start
and the WinchHouse scene was not visible.

## Resolution

`App::LoadLevel` now uses the level-authored start pose for a fresh process and
after a level switch. A persisted camera is used only for an in-session reload
of the same level. The loader records the selected pose source in the log so
the E2E check can distinguish an authored start from a stale saved camera.

## Verification

- Before fix: Level 12 startup used `(127000280,-37829456,175303728)` and the
  startup scene oracle failed (`uniqueRatio 0.004827...`).
- After fix: Level 12 startup used authored `HumanPlayer` position
  `(112255288,-42206448,179544736)`, logged
  `Viewer start source=level-authored-start`, and passed the visible Session 1
  E2E scenario.
- Focused C++ tests: 8/8 passed.
- E2E manifest contract: passed.
- Fresh visible WMI Session 1 corpus sweep: all 14 levels passed startup,
  scene visibility, pause/cursor checks, and clean shutdown; each level logged
  `Viewer start source=level-authored-start`.

## WinchHouse navigation regression

The Level 12 WinchHouse could still appear absent after selecting it because
the developer `goto` command placed the camera at the selected object's origin.
For a large building that is inside the mesh, so the resulting frame showed
only an interior wall or underside. `GotoModel` now computes the geometry
center and transformed bound radius, places the camera outside that bound, and
aims it at the center. The rule is generic for all model sizes and contains no
model-ID exception.

Verification: the Release editor was rebuilt, `editor_camera_start_tests`
passed 3/3, and a fresh WMI Session 1 Level 12 inspection of task `-1#907`
visibly showed the complete WinchHouse from outside.
