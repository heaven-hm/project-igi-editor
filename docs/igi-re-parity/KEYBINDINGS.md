# Keybindings — issue #48 (25 stubbed event bindings) and task-11 (5 adjacent silent no-ops)

Status: **complete** for the 25 bindings listed by issue #48, and for the five
adjacent silent no-ops decided under **task-11** (second table below).
Scope of change: `source/app_input.cpp`, `source/app_view.cpp`, `source/config.cpp`,
`source/config.h`, `source/app.h`, `source/runtime/editor_quick_actions.h`,
`assets/editor/qed/qedkeybindings.qsc`, `tests/test_keybindings.cpp`,
`tests/test_editor_quick_actions.cpp`, `CMakeLists.txt`, this document.

Issue #73 adds the editor's frame-step keys (`source/app_input_keyboard.cpp`) and
routes all four `Timer*` handlers plus the F10 panel's buttons through one set of
`App` methods; see "Issue #73: the same transport on the F10 panel" below.

## TL;DR

`source/app_input.cpp` contained 25 lines of the form

```cpp
if (Check("<Name>")) { Logger::Get().Log(LogLevel::INFO, "[Keybind] <Name> not implemented"); }
```

Every one of the 25 is now decided: **8 implemented**, **17 removed** from the
shipped default bindings. No `[Keybind] ... not implemented` line survives
(`grep -c` = 0), and every removed binding is answered by a status-bar message
instead of silently doing nothing, because an already-deployed copy of
`qedkeybindings.qsc` in a user's QED folder can still bind it.

The first pass over #48 implemented 2 and removed 23. The second pass (this
document's current state) moved six of those removals to **implemented**: the
retail editor's own manual, `editor/qed/QED.doc` in an installed IGI tree
("General keystrokes"), defines them exactly, and the editor already had every
facility they need —

* `Ctrl P` — "Copy camera position to clipboard" → `Utils::SetClipboardText`
  already exists (`source/utils.cpp:445`);
* `Ctrl R` — "Reset timer", `Ctrl T` — "Timer Start / Stop", `F12 / Shift F12`
  — "Increment / Decrement AnimTask speed" → the editor's one playback clock
  `igi::AnimPlayback` already has `playing`, `Reset()` and a `speed` the engine
  applies (`source/animation.h:184`, `source/runtime/anim_playback.cpp:147-151`).

The first pass removed those six on the stated grounds that "no editor behaviour"
and "no playback timer" existed. That was not checked against the code: the
clipboard and the playback clock were both already there, so the correct action
was to wire the keys to them. `CameraSnapToObjectWithRadius` stays removed, but
for the accurate reason — the behaviour exists and is reachable, it is the
*event name* that was redundant (see below).

The 17 removals are not "we did not get to it": each one names a feature this
editor does not have, and the oracle cannot supply the behaviour either. **IGI-RE
is a decompilation of `IGI.exe` only** (`README.md`: "A decompilation of Project
I.G.I. … the whole game rewritten as readable C"; 5,376/5,376 functions of the
game and engine code) — it contains **no QED editor code at all** (`grep -rln QED
src/ docs/` → nothing). The 25 bindings are all QED editor commands, so for them
IGI-RE can only describe the *game-side* objects an editor feature would drive,
never the editor command itself. Where that game-side evidence exists it is cited
below; where a binding's meaning could only be guessed, it was removed rather
than invented.

**Oracle note.** For a QED *editor command*, the authoritative source is the
retail editor's own manual shipped with the game, `editor/qed/QED.doc`
(Word 97, "updated 23.09.99", author Stein Pedersen — the QED author). It is a
retail artifact read from the installed tree, not a web source, and it is cited
by its "General keystrokes" table below. IGI-RE remains the authority for *game*
behaviour, and nothing here claims any.

## The dispatcher

`App::DispatchEventBindings()` — `source/app_input.cpp:70`. `Check(name)`
(`:46-52`) looks the name up in `Config::eventBindings_` and requires an exact
modifier match (`Utils::IsKeyBindingPressedExact`).

Removed bindings go through one helper so the reason reaches the user:

```cpp
// source/app_input.cpp:92
auto Removed = [&](const char* name) -> bool {
    if (!Check(name)) return false;
    status_message_ = std::string(name) +
        " was removed (#48): no editor handler and no IGI-RE counterpart — "
        "see docs/igi-re-parity/KEYBINDINGS.md";
    return true;
};
```

`status_message_` is the editor's status bar (`source/app.h:207`, drawn at
`source/app.cpp:744` and `:1091`), the same feedback channel the rest of the
dispatcher already uses (`source/app_input.cpp:290`, `:395`, `:541`).

Bindings are loaded by `Config` from `GetQEDDirectory() / "qedkeybindings.qsc"`
(`source/config.cpp:136`, parse at `:345-373`, file loop at `:444-452`). The
shipped default is `assets/editor/qed/qedkeybindings.qsc`; `bin/Release/editor/qed/…`
is an untracked build copy. The help window (`H`) lists this file verbatim
(`source/app_ui.cpp:98-153`), so removing a line also removes it from the help
list — the editor stops advertising the binding.

## Decision table

| # | Binding | Decision | Editor handler (now) | Game-side (IGI-RE) evidence |
|---|---|---|---|---|
| 1 | `CameraResetRadius` | **implemented** | `source/app_input.cpp:123-146` | none — editor-only, see below |
| 2 | `CameraCopyPosition` | **implemented** | `source/app_input.cpp:158` (`Utils::SetClipboardText`) | `editor/qed/QED.doc`: "Ctrl P  Copy camera position to clipboard" |
| 3 | `CameraCopyOrientation` | **implemented** | `source/app_input.cpp:174` | same command on the camera's angles; combo `<Ctrl><Shift><O>` from the shipped default |
| 4 | `CameraSnapToObjectWithRadius` | **removed** (redundant, not missing) | `source/app_input.cpp` (status-bar guard) | none — the wider framing is already the F11 handler's own Shift path |
| 5 | `CameraStrafe` | **removed** | `source/app_input.cpp` (status-bar guard) | none — strafing already exists as `CameraStrafeLeft/Right` |
| 6 | `AnimTaskIncreaseKeyframeInterpolation` | **removed** | `source/app_input.cpp` (status-bar guard) | no QED anim-task timeline/keyframe editor; no binding semantics in IGI-RE |
| 7 | `AnimTaskDecreaseKeyframeInterpolation` | **removed** | `source/app_input.cpp` (status-bar guard) | same |
| 8 | `AnimTaskToggleCameraRelative` | **removed** | `source/app_input.cpp` (status-bar guard) | `QED.doc`: "Ctrl L  Toggle target relative/chase camera" — this editor has no chase camera |
| 9 | `AnimTaskGoToCursor` | **removed** | `source/app_input.cpp` (status-bar guard) | no QED anim-task timeline to go to a cursor in |
| 10 | `AnimTaskToggleWindowHidden` | **removed** | `source/app_input.cpp` (status-bar guard) | no QED anim-task window to hide |
| 11 | `AnimTaskStartRecording` | **removed** | `source/app_input.cpp` (status-bar guard) | no keyframe recorder exists |
| 12 | `AnimTaskGoToTop` | **removed** | `source/app_input.cpp` (status-bar guard) | no QED anim-task timeline |
| 13 | `AnimTaskToggleSyncPlayback` | **removed** | `source/app_input.cpp` (status-bar guard) | no sync-playback mode |
| 14 | `TimerIncreaseSpeed` | **implemented** | `source/app_input.cpp` (`AnimTimerSpeedStepIndex` + `AnimPlayback::speed`) | `QED.doc`: "F12 / Shift F12  Increment / Decrement AnimTask speed" |
| 15 | `TimerDecreaseSpeed` | **implemented** | same | same |
| 16 | `TimerReset` | **implemented** | `source/app_input.cpp` (`AnimPlayback::Reset`) | `QED.doc`: "Ctrl R  Reset timer" |
| 17 | `TimerStartStop` | **implemented** | `source/app_input.cpp` (`AnimPlayback::Pause`/`Resume`) | `QED.doc`: "Ctrl T  Timer Start / Stop" |
| 18 | `Manipulate` | **removed** | `source/app_input.cpp` (status-bar guard) | none — the gesture is not event-driven, see below |
| 19 | `ManipulatePositionXY` | **removed** | `source/app_input.cpp` (status-bar guard) | none — already implemented via Shift+LMB |
| 20 | `ManipulatePositionXZ` | **removed** | `source/app_input.cpp` (status-bar guard) | none — already implemented via Ctrl+LMB |
| 21 | `ManipulateOrientationAlpha` | **removed** | `source/app_input.cpp` (status-bar guard) | none — already implemented via `keyRotateAlpha` |
| 22 | `ManipulateOrientationBeta` | **removed** | `source/app_input.cpp` (status-bar guard) | none — already implemented via `keyRotateBeta` |
| 23 | `ManipulateOrientationGamma` | **removed** | `source/app_input.cpp` (status-bar guard) | none — already implemented via `keyRotateGamma` |
| 24 | `ManipulateOrientationReset` | **removed** | `source/app_input.cpp` (status-bar guard) | none — already implemented via `keyResetOri` (Space) |
| 25 | `ToggleObjects` | **implemented** | `source/app_input.cpp:693-710` | none — editor viewport option, see below |

"Editor handler (now)" for a removed binding is the `Removed("…")` guard that
reports the removal in the status bar; the binding itself is gone from
`assets/editor/qed/qedkeybindings.qsc` (see the comment blocks in that file for
the removed key combos). Line numbers in the "implemented" rows are indicative —
the guard and the handler both live in `App::DispatchEventBindings`.

## Implemented

### 1. `CameraResetRadius` — `<Shift><Home>`

`source/app_input.cpp:123-146`. The editor's only camera radius is the
object-orbit distance, `igi::ObjectOrbitCamera::distance`
(`source/runtime/editor_camera_start.h:39-44`, initialiser `1.0f`). Entering orbit
mode derives that distance from the current camera position
(`BeginObjectOrbit`, `:64-84`), and nothing else ever changes it
(`StepObjectOrbit`, `:85-92`, only moves yaw/pitch). The handler therefore
re-uses the stored orbit target and resets the distance to the editor's default
radius, then re-derives the eye position:

```cpp
// source/app_input.cpp:20 (constant), :57-68 (helper), :139 (call site)
static constexpr float kDefaultOrbitRadius = 40960.0f;   // 10 m, 4096 wu = 1 m
...
ApplyOrbitRadius(kDefaultOrbitRadius);   // sets orbit_distance_ + eye position
```

**Correction made under task-11.** The task-8 version of this handler used the
struct's own initialiser, `igi::ObjectOrbitCamera{}.distance` = `1.0f`. That value
is a *degenerate* fallback, not a viewing distance: `BeginObjectOrbit`
(`source/runtime/editor_camera_start.h:64-84`) only falls back to it when the
camera is already within 0.1 world units of the target, and 1 wu is 0.24 mm
(4096 wu = 1 m), i.e. the camera would end up inside the object. The reset now
uses `kDefaultOrbitRadius` = 10 m, a documented editor-side constant chosen to
frame a typical 1–20 m object. This is an editor-only design value: no IGI-RE
evidence exists for it (see below) and none is claimed.

If no orbit is active it says so: `"CameraResetRadius: no orbit target (F11
snaps the camera to an object first)"` — `CameraSnapToObject` is `<F11>` and is
what activates orbit mode (`source/app_input_mouse.cpp:230-247`).

**IGI-RE: unverified by design.** This is an editor-tool concept, and IGI-RE does
not cover the editor. Its nearest module, `src/editcam/editcamera.c`, is the
*game's* `EditCamera` task type: it attaches a camera to a target
(`EditCamera_AttachRotation` `:212`, `EditCamera_AttachPosition` `:273`) and has
no radius concept at all (`grep -rn radius src/editcam/` → no hits). So the
constant comes from the editor's own struct, and no game behaviour is claimed.

### 2. `ToggleObjects` — `<Ctrl><Shift><Z>`

`source/app_input.cpp:693-710`. "Toggle objects in editor" is the editor's own
draw-part switch, applied exactly like the View menu's *Show all*
(`source/main.cpp:439-450`, `MENU_SHOW_ALL`): toggle
`Renderer::DRAW_OBJECTS | DRAW_BUILDINGS | DRAW_PROPS` together, and report
`"Objects: ON"` / `"Objects: OFF"` in the status bar. `DRAW_OBJECTS` is a real
gate — `source/renderer/renderer_objects.cpp:760-768` short-circuits the whole
object pass, with `BUILDINGS`/`PROPS` consulted as the per-kind fallback, and it
also gates picking (`source/renderer/renderer_objects_picking.cpp:190-197`). The
same flags are already toggled from the menu (`source/main.cpp:342-346`) and from
the debug command manager (`source/debug_command_manager.cpp:104`).

**IGI-RE: not applicable.** A viewport draw toggle is an editor option with no
game-side counterpart; no IGI-RE claim is made.

### 3. `CameraCopyPosition` — `<Ctrl><P>`, and `CameraCopyOrientation` — `<Ctrl><Shift><O>`

`QED.doc`, "General keystrokes": **"Ctrl P  Copy camera position to clipboard"**.
That settles the meaning the first pass called ambiguous — it is the clipboard,
not the selected task. The editor already owns a clipboard
(`Utils::SetClipboardText`, `source/utils.cpp:445`; the same call the text editor
uses at `source/app_editor.cpp:136`), and its live camera is `viewer_.pos_`
(`source/app.h:613`), so the handler copies that and nothing else. The
orientation sibling copies `viewer_.yaw_/pitch_/roll_` (`source/app.h:618-620`,
degrees), which is the editor's own camera orientation — the same three values it
persists as `QEDSetCameraOrientation` (`source/config.cpp:505-508`).

The text is three numbers at three decimals (`"%.3f, %.3f, %.3f"`), produced by
`igi::FormatCameraTripleForClipboard` in
`source/runtime/editor_quick_actions.h` and pinned by
`tests/test_editor_quick_actions.cpp`. The formatting is the only part of this
that is host-testable; that `SetClipboardText` reaches the Windows clipboard is
**unverified on this host** (it is a `windows.h` path, and the GUI cannot run
here — see the verified/unverified section).

**IGI-RE: not applicable.** A QED editor clipboard command has no game-side
counterpart; no IGI-RE claim is made.

### 4. `TimerStartStop` — `<Ctrl><T>`, `TimerReset` — `<Ctrl><R>`, `TimerIncreaseSpeed` — `<F12>`, `TimerDecreaseSpeed` — `<Shift><F12>`

`QED.doc`, "General keystrokes": **"Ctrl R  Reset timer"**, **"Ctrl T  Timer
Start / Stop"**, **"F12 / Shift F12  Increment / Decrement AnimTask speed"** — so
in the retail editor the "timer" and the "AnimTask speed" are the same control:
the animation playback clock. The first pass removed these four because "no
animation playback timer exists in this editor"; that was wrong. The editor's one
playback clock is `igi::AnimPlayback` (`source/animation.h:163-184`: "This is the
editor's ONE playback path"), and it has exactly the three things the four keys
need:

| Key | `AnimPlayback` member | Engine effect |
|---|---|---|
| `TimerStartStop` | `playing` via `Pause()`/`Resume()` (`source/animation.h:204-205`) | the clock stops/starts advancing |
| `TimerReset` | `Reset()` (`source/animation.h:202`, impl `source/runtime/anim_playback.cpp:154-161`) | rewinds to the clip's first frame and stops |
| `TimerIncreaseSpeed` / `TimerDecreaseSpeed` | `speed` (`source/animation.h:189`) | applied by `SyncEngineFlags` → `SetSpeed` (`source/runtime/anim_playback.cpp:147-151`, `Anim_SetSpeed` 0x4D6580: `inst->speed = gTicksPerFrame * speed`) |

Scope: the **selected object's** playback. That is the editor's own animation
preview model — the property editor shows the selected object's clip and play
state (`App::ComputePropAnimUiState`, `source/app.cpp:4610-4630`) and starts it
from the animation list (`App::ToggleAnimationForObject`, `source/app.cpp:4539`,
called at `source/app_input_mouse.cpp:250`). Playbacks also exist without any user
action: the level load auto-plays one per eligible AI NPC
(`source/app_level.cpp:664-676`). If nothing animatable is selected the key says
so in the status bar rather than doing nothing.

The speed ladder is `{0.125, 0.25, 0.5, 1, 2, 4, 8}` (1× is the engine's normal
speed and `AnimPlayback`'s own default), doubling/halving so presses are exactly
reversible, and it clamps at both ends with a `(limit)` note. The ladder and the
label live in `igi::AnimTimerSpeedIndex/StepIndex/At/Label`
(`source/runtime/editor_quick_actions.h`) and are pinned by
`tests/test_editor_quick_actions.cpp`; the handler keeps only the live-state part.

**Not implemented here:** the AnimTask *keyframe interpolation* pair (`F8` /
`Shift F8`, `AnimTaskIncrease/DecreaseKeyframeInterpolation`) — that needs a
keyframe editor, which this editor does not have, so those two stay removed. The
retail manual documents the interpolation pair and the speed pair as separate
controls; only the speed pair has an owner here.

**IGI-RE: not applicable.** These are QED transport controls; IGI-RE contains no
QED code and no game-side claim is made. The engine call the speed ultimately
reaches (`Anim_SetSpeed` 0x4D6580) is game-side and is already cited where it is
used, `source/runtime/anim_playback.cpp:148`.

## Issue #73: the same transport on the F10 panel, plus a frame step

`source/runtime/editor_quick_actions.h` and the four `Timer*` handlers above are
now the shared implementation of the animation transport: the F10 animation
panel's five buttons (`App::AnimPanelClick`, `source/app.cpp`) call the same
`App::ToggleSelectedAnimPlayback` / `ResetSelectedAnim` /
`AdjustSelectedAnimSpeed` / `StepSelectedAnim` methods the keys call, so the panel
and the keybindings cannot become two sources of truth for one clock. The panel's
layout, its text and the frame-step arithmetic are pure and live in
`source/runtime/anim_panel.h` (pinned by `tests/test_editor_quick_actions.cpp`);
the engine-backed stepping is `AnimPlayback::StepFrames`
(`source/runtime/anim_playback.cpp`, pinned by `tests/test_anim_playback.cpp`).

| Binding | Combo | Handler | Oracle |
|---|---|---|---|
| `TimerStartStop` | `<Ctrl><T>` | panel **Play/Pause** button; `App::ToggleSelectedAnimPlayback` | `QED.doc`: "Ctrl T  Timer Start / Stop" |
| `TimerReset` | `<Ctrl><R>` | `App::ResetSelectedAnim` | `QED.doc`: "Ctrl R  Reset timer" |
| `TimerIncreaseSpeed` / `TimerDecreaseSpeed` | `<F12>` / `<Shift><F12>` | panel **Speed -/+** buttons; `App::AdjustSelectedAnimSpeed` | `QED.doc`: "F12 / Shift F12  Increment / Decrement AnimTask speed" |
| frame step back / forward | `,` / `.` | panel **Step -1 / Step +1** buttons; `App::StepSelectedAnim` | **editor-only key, engine-side step.** `QED.doc`, "General keystrokes", documents no frame step; the *step size* is the engine's own frame — `inst->time = inst->speed + inst->time` with `inst->speed = gTicksPerFrame * speed` (`BoneObj_AnimEventsCB` 0x4D4B60, `src/boneobj/boneobj.c:884-885`; `Anim_SetSpeed` 0x4D6580, `src/boneobj/anim.c:172-179`), i.e. 160 ticks at the default 1× |

The `,` / `.` keys only act while the F10 panel is up (`show_anim_debug_`), which
is the panel those keys are documented in; the panel's footer line names them,
along with `Ctrl+T` and `F12`/`Shift+F12`. A step works while the playback is
PAUSED — that is the point of it — and it is one engine frame, so at 2× it
advances 320 ticks, exactly the frame the running clock would have taken.
Stepping a one-shot onto its last frame stops the playback, because that is the
engine's own end test (`HumanPlayer_AnimDone` 0x489F50,
`src/loadscreen/human.c:196-204`).

**What is verified here:** the pure arithmetic and layout (`AnimFrameStep73.*`),
and that a real `AnimPlayback` lands exactly where the pure rule says for every
rung of the speed ladder, in both loop modes and both directions
(`AnimPlaybackStepTest.PureStepMatchesTheEngineClock`). **What is not:** the
panel's appearance — the GUI cannot run on this host (OpenGL 4.1 required, 2.1
available), so the button row is asserted as geometry and text, never as a
picture.

**Panel precedence, stated because it is not visible headlessly.** The F10 panel
occupies the top-left strip that the property editor also uses
(`PropPanel::kLeft .. +kWidth`), and the property editor is drawn after it and
consumes clicks in that strip first (`source/app_input_mouse.cpp`). So while the
property editor is open it keeps those clicks — its own "Animation Control"
section is the transport there — and the F10 panel's buttons act when it is not
covering them. The keys (`Ctrl T`, `F12` / `Shift F12`, `,` / `.`) are unaffected
in either state, which is why the panel's footer names them.

## Removed

### Camera (2)

| Binding | Was bound to | Why removed |
|---|---|---|
| `CameraSnapToObjectWithRadius` | `<Shift><F11>` | **Redundant, not missing.** The wider-radius framing already happens: the F11 special-key handler reads the Shift modifier itself and passes it to `MakeF11ObjectCameraPose(..., shiftHeld)` (`source/app_input_keyboard.cpp:466-471`), and that path returns before `DispatchEventBindings` ever runs — so the event name could not add anything, and `Shift+F11` keeps working after the binding is gone. The first pass's stated reason ("no editor behaviour") was wrong; the behaviour is there and is reachable. |
| `CameraStrafe` | `<Alt><Shift><Space>` | Strafing is already implemented as continuous held bindings `CameraStrafeLeft`/`CameraStrafeRight` (`source/app_view.cpp:225-236`), which are untouched. A bare "strafe" modifier has no implementation path. |

### AnimTask (8) and Timer (4)

Removed: `AnimTaskIncreaseKeyframeInterpolation` `<F8>`,
`AnimTaskDecreaseKeyframeInterpolation` `<Shift><F8>`,
`AnimTaskToggleCameraRelative` `<F6>`, `AnimTaskGoToCursor` `<Ctrl><G>`,
`AnimTaskToggleWindowHidden` `<Ctrl><Shift><H>`,
`AnimTaskStartRecording` `<Ctrl><Q>`, `AnimTaskGoToTop` `<Ctrl><R>`,
`AnimTaskToggleSyncPlayback` `<Ctrl><Shift><S>`.

These name a QED animation-task editor window: a keyframe list, an interpolation
setting, a recorder, a playback sync toggle, a hidden-window toggle and a
camera-relative toggle. **This editor has none of that.** There is no
animation-task window, no timeline, no recorder and no keyframe interpolation
editor anywhere in `source/`: the only `keyframe` hits are renderer/level data
paths (`source/renderer/renderer_objects.h`, `source/level/level_objects_serialize.cpp`,
`source/level/level.cpp`, `source/mcp/mcp_tools_cutscene.cpp`).

`AnimTaskToggleCameraRelative` is separately checkable because `QED.doc` defines
it — "Ctrl L  Toggle target relative/chase camera" — and this editor has no
target-relative/chase camera mode: `TaskNewCameraRelative` (`<Shift><Insert>`)
only chooses where a *new task* is created (`source/app_input.cpp:283`), and
`grep -rniE 'chase|camera_relative' source/` finds only AI movement speeds.

The **timer half of this group is not removed — it is implemented** under the
`Timer*` names above, because `QED.doc` documents `F12`/`Shift F12` as the
AnimTask *speed* control and this editor does have a playback clock
(`igi::AnimPlayback`). Only the *keyframe interpolation* pair (`F8`/`Shift F8`)
stays removed: that needs a keyframe editor, and the editor has none. The first
pass's claim that no playback timer exists anywhere in `source/` was wrong — see
the implemented section above.

Two of them were also unreachable in practice because their combos collided with
live bindings, which the removals also fixes:

* `AnimTaskGoToTop` `<Ctrl><R>` and `TimerReset` `<Ctrl><R>` were the same combo
  (the timer keeps it — `QED.doc` documents `Ctrl R` as "Reset timer").
* `AnimTaskToggleCameraRelative` `<F6>` collided with `ToggleTaskNoteDisplay` `<F6>`.

The three config fields that were already wired for this group
(`source/config.cpp:367-369`: `keyStartRecording`, `keyGoToCursor`,
`keySyncPlayback`) have no readers (`grep` over `source/` excluding
`config.{h,cpp}` → 0 hits) and are now unreferenced; they are left in place as
inert config surface, not silently repurposed.

**IGI-RE: no evidence either way.** The game side of animation tasks exists
(`src/graphtask/graph.c`, `src/graphtask/graphtask.h`, `src/animcam/`,
`src/graphnode/`), but a decompilation of `IGI.exe` cannot define what a QED
*editor window* shortcut should do; no editor command names or editor key tables
appear anywhere in IGI-RE. Removing the advertisement is the only honest option.

### Manipulate (7)

Removed: `Manipulate` `<LeftMouseButton>`, `ManipulatePositionXY`
`<LeftMouseButton><Shift>`, `ManipulatePositionXZ` `<LeftMouseButton><Ctrl>` and
`<LeftMouseButton><Shift><Ctrl>`, `ManipulateOrientationAlpha`
`<LeftMouseButton><A>`, `ManipulateOrientationBeta` `<LeftMouseButton><B>`,
`ManipulateOrientationGamma` `<LeftMouseButton><G>`, `ManipulateOrientationReset`
`<LeftMouseButton><Space>`.

These are the one group where the *behaviour is already implemented* — the event
names were simply never consulted, because the manipulation gesture reads live
modifier state and held keys instead of the binding table. Deleting the stubs
therefore loses nothing, and keeping the bindings would have kept advertising
names that cannot affect anything (rebinding `ManipulateOrientationAlpha` to
another combo would change nothing, while the real key is `keyRotateAlpha`).

Where the behaviour lives, all verified by reading the code:

| Gesture | Implementation |
|---|---|
| Drag the marker with LMB | `source/app_input_mouse.cpp:846-852` → `UpdateMarkerManipulation()` (`source/app_view.cpp:666`) |
| Shift+LMB → move in the view plane (`MoveXY`) | `source/app_view.cpp:686`, applied `:738-744` |
| Ctrl+LMB (and Shift+Ctrl+LMB) → move in the vertical plane (`MoveXZ`) | `source/app_view.cpp:685,687`, applied `:745-752` |
| A / B / G while dragging → rotate alpha/beta/gamma | `source/app_input_keyboard.cpp:1524-1526` sets `MK_MANIP_A/B/G`; `source/app_view.cpp:688-690`, applied `:753-765` |
| Space while dragging → reset orientation | `source/app_input_keyboard.cpp:1543-1545` sets `MK_MANIP_SPACE`; `source/app_view.cpp:804-807` |
| S / O while dragging → snap to ground / on top of the nearest object | `source/app_input_mouse.cpp:333-336`; `source/app_view.cpp:766-802` |

The configurable keys are `keyRotateAlpha`/`keyRotateBeta`/`keyRotateGamma`,
`keySnapGround`, `keySnapObject`, `keyResetOri`
(`source/config.cpp:142-147`; defaults `A`, `B`, `G`, `S`, `O`, `Space`).
`ManipulatePositionSnapToGround` and `ManipulatePositionSnapToObject` are **kept**
— they have real handlers (`source/app_input.cpp:159` and `:171`).

**IGI-RE: not applicable.** Marker manipulation is an editor gesture; the game
has no equivalent (the closest game-side notion, `EditCamera_AttachPosition`
`src/editcam/editcamera.c:273`, positions a *camera task*, not an editor marker).
No game behaviour is claimed.

## Verified / unverified

Verified (task-8, re-run unchanged by task-11 unless noted):

* `python3 tools/igi-re-parity/tu-check.py source/app_input.cpp` → `OK (source/app_input.cpp)`,
  `checked 1 translation unit(s), 0 failed` — the cross (MinGW) build compiles
  the changed dispatcher, including the new
  `#include "runtime/editor_camera_start.h"`.
* `grep -c "not implemented" source/app_input.cpp` → `0`; `grep -c "\[Keybind\]"` → `0`;
  `grep -c 'Removed("' source/app_input.cpp` → `23` (task-11 raises this to 26).
* `IGI_HOST_NO_CACHE=1 IGI_HOST_TESTS=tests/test_keybindings.cpp bash scripts/run-host-tests.sh`
  → `PASS test_keybindings 7 passed 0 failed`, `OK: 7 tests passed in 1 suites`,
  exit 0. The 7 cases are repository invariants: no stub line survives, every
  removed name is guarded by a status-bar message, removed names are absent from
  the shipped default, implemented names are present in both the file and the
  dispatcher, the default file is well-formed with unique names, every one of the
  25 names appears in this document, and the decision table still covers exactly
  25 bindings (2 implemented / 23 removed). Registered additively in
  `CMakeLists.txt:354`; the host runner discovered and ran it, which is also what
  proves the registration line is valid.
* The shipped `qedkeybindings.qsc` parses cleanly: 80 lines containing
  `SetEventBinding(` (78 active bindings + the 2 pre-existing commented-out
  `TestEvent` lines), no malformed line, no duplicate event name. The duplicate
  check ignores comment lines the way the editor's own readers do
  (`source/app_ui.cpp:123-128`).

Verified (task-11):

* `python3 tools/igi-re-parity/tu-check.py source/app_input.cpp source/app_view.cpp source/config.cpp`
  → `OK (source/app_input.cpp)`, `OK (source/app_view.cpp)`, `OK (source/config.cpp)`,
  `checked 4 translation unit(s), 0 failed` — the cross (MinGW) build compiles all
  three changed translation units. `source/config.h` and `source/app.h` are headers
  covered by those same TUs.
* `IGI_HOST_NO_CACHE=1 IGI_HOST_TESTS=tests/test_keybindings.cpp bash scripts/run-host-tests.sh`
  → `PASS test_keybindings 15 passed 0 failed (15 tests, exit 0)`,
  `OK: 15 tests passed in 1 suites`. The 8 new cases are the task-11 invariants:
  no combo is bound to two different events; the duplicate-combo decisions are
  pinned (`AddModelToRes` = `<Ctrl><Shift>M`, `ToggleAutoSave` = `<Ctrl><Shift><A>`);
  every advertised binding has a `Check`/`CheckCont` handler or a visible
  `Removed()` message; every advertised event that `config.cpp` routes to a named
  field has a live consumer; the two deleted config fields stay deleted; the
  `CameraStrafeFree` flag and its "Strafe lock" message stay gone; the plain-drag
  no-op names its mode; the doc records the five task-11 decisions. All content
  assertions run on comment-stripped text, so a comment that merely *names* a
  deleted identifier cannot satisfy or break them.
* Two of the new tests failed on their first run and the code, not the test, was
  wrong in one case: `TaskFind` (`<Ctrl><F>`) was advertised with no handler at all
  (row 6 above). The other four first-run failures were test bugs (comment text
  matching, and an off-by-one in the `data_.` prefix length of the routing
  parser), fixed in `tests/test_keybindings.cpp`.
* The shipped default after task-11: **75 active bindings, 75 distinct names, 75
  distinct combos, 0 malformed lines** (plus the 2 commented-out `TestEvent`
  lines), checked with the same comment-aware parser the editor's readers use.
* `grep -c 'Removed("' source/app_input.cpp` → `26` (23 of #48 + 3 task-11).
* `grep -rn 'camera_strafe_free_\|keyAdjustCameraRadius\|keyLookDown' source/` → 3
  hits, all of them the explanatory `//` comment lines at
  `source/app_input.cpp:114`, `:149`, `:191` — the identifiers no longer exist in
  any code line, which is why the tests strip comments before asserting their
  absence.

Verified (issue #48, second pass — the six newly implemented bindings):

* **Oracle read first.** `textutil -convert txt -stdout editor/qed/QED.doc` from the
  installed retail tree, "General keystrokes" table: `Ctrl P` "Copy camera position
  to clipboard", `Ctrl R` "Reset timer", `Ctrl T` "Timer Start / Stop",
  `F12 / Shift F12` "Increment / Decrement AnimTask speed". The retail
  `editor/qed/qedkeybindings.qsc` binds those combos to `CameraCopyPosition`,
  `TimerReset`, `TimerStartStop`, `TimerIncreaseSpeed`, `TimerDecreaseSpeed`, which
  is what identified the six removals of the first pass that should have been
  implementations.
* `python3 tools/igi-re-parity/tu-check.py source/app_input.cpp` → `OK
  (source/app_input.cpp)`, `checked 1 translation unit(s), 0 failed` — the changed
  dispatcher compiles under the cross (MinGW) compiler, including the new
  `#include "runtime/editor_quick_actions.h"`.
* `cmake --build build-mingw --target igi_tests igi-editor --parallel` → exit 0.
* `bash scripts/check-build-integrity.sh` → **PASS** — "all 382 objects are
  consistent with their dependencies, their PCH, and the ninja dependency log".
* `IGI_E2E_STAGE=$PWD/scratch/e2e-48 bash scripts/e2e-headless.sh --fast` → exit 0,
  `smoke, tests` tiers, `2 assertions run, 0 failed`, levels 1–14.
* `IGI_HOST_NO_CACHE=1 bash scripts/run-host-tests.sh` (full lane) → 1405/1409
  passed. The two suites for this change are green: **`test_keybindings` 17 passed
  0 failed**, **`test_editor_quick_actions` 9 passed 0 failed**. The only 3 failures
  in the lane are `SmokeParticles` (another agent's in-progress `#43` work in
  `source/runtime/smoke_sim.cpp`), untouched by this change.
* The shipped default after this pass: **81 active bindings, 81 distinct names, 0
  duplicate combos, 0 malformed lines** (75 + the 6 re-added), checked with the same
  comment-aware parser the editor's readers use.
* `grep -c 'Removed("' source/app_input.cpp` → `20` distinct names (17 removed of
  #48 + the 3 task-11 adjacent); `grep -c '\[Keybind\]\|not implemented'` → 2, both
  inside explanatory `//` comments (`source/app_input.cpp:119`, `:154`), which the
  suite strips before asserting.
* **What the tests actually pin.** `tests/test_editor_quick_actions.cpp` proves the
  pure decisions: the ladder is strictly increasing with `1x` at its default index,
  every rung is found exactly, off-ladder speeds snap to the nearest rung with ties
  going faster, stepping is reversible in the middle and clamps at both ends, every
  rung has a distinct status-bar label, and the clipboard text is three
  fixed-decimal numbers that round (not truncate) and keep their sign.
  `tests/test_keybindings.cpp` proves the wiring: the six events are advertised in
  the shipped default with their documented combos, each has a live handler and no
  `Removed()` guard, the handlers go through those helpers and through
  `AnimPlayback::Pause`/`Reset`, every advertised binding still has a handler or a
  visible message, and no combo is bound twice.

Unverified (and why):

* **No runtime/GUI test of the implemented bindings.** `CameraResetRadius`,
  `ToggleObjects`, `TaskFind`, the two camera copies and the four timer keys were
  not exercised in a running editor: they need the GL window and a loaded level,
  and this host offers OpenGL 2.1 where the editor needs 4.1. Their code paths are
  compile-verified and read-verified only.
* **The clipboard is not proven to receive the text.** `Utils::SetClipboardText`
  is a `windows.h` path (`source/utils.cpp:445-460`) that cannot run on this host;
  what is proven is the text handed to it (unit-tested) and that the handler calls
  it (asserted textually in `test_keybindings`). The e2e lane exercises level
  loading and the test tiers, not key presses.
* **The timer's effect on the engine is not re-proven here.** `AnimPlayback::speed`
  reaching the engine through `SyncEngineFlags` → `SetSpeed` is pre-existing
  behaviour covered by `tests/test_anim_playback.cpp`; this change only sets the
  field and reports it.
* **No runtime/GUI test of the three task-8/task-11 bindings.** `CameraResetRadius`,
  `ToggleObjects` and `TaskFind` were not exercised in a running editor: they need
  the GL window and a loaded level, and this host is running the cross-build gate.
  Their code paths are compile-verified and read-verified only.
* **No IGI-RE differential evidence for any of the 25, or for the task-11 items**,
  by construction: all of them are QED editor commands and IGI-RE decompiles
  `IGI.exe` only. Nothing in this change alters game behaviour, so there is nothing
  to differentially test; every task-11 row above is marked editor-only for that
  reason.
* **Stale deployed config is untested at runtime.** A user whose QED folder already
  holds a `qedkeybindings.qsc` containing the removed names gets the status-bar
  message (that is the purpose of the `Removed()` guard), and the parser is
  tolerant by construction (see "Config-key deletion: parser tolerance" above),
  but that path was not executed: `ParseKeyBinding` is Windows-only
  (`VkKeyScanA`) and the loader reads a real QED directory.
* **The `ManipulationMode::None` message was not seen on screen.** The branch is
  read-verified and pinned by a test; no GL run was made, so "the user sees this
  string in the status bar" rests on the status bar being the same
  `status_message_` the other handlers already use (`source/app.cpp:744`, `:1091`).

## Adjacent findings (outside the 25) — decided under task-11

The five items below are the same defect class issue #48 calls out ("users bind
muscle memory to keys that quietly do nothing") but were outside its literal list
of 25, so they were left untouched in task-8 and are decided here.

**Evidence standard for this table.** These are editor-only behaviours: they
describe QED's own camera, its own status bar and its own config file. There is
**no IGI-RE oracle and there cannot be one** — IGI-RE decompiles `IGI.exe` only
and contains no QED editor code (`grep -rln QED src/ docs/` → nothing) — so the
evidence for every row is the editor's own existing behaviour and configuration,
cited by `file:line`, plus the statement that **no game-side claim is made and no
game behaviour changes**. No IGI-RE justification is manufactured for any of them.

| # | Item | Decision | Handler (now) | Evidence (editor's own code) | Editor-only? | What the user sees now |
|---|---|---|---|---|---|---|
| 1 | `CameraAdjustRadius` (`<Alt><Space>`) | **removed** — binding, config field and routing branch | `source/app_input.cpp:112-122` `Removed("CameraAdjustRadius")` | the editor has no radius adjuster: `Config::keyAdjustCameraRadius` had **0 readers**, and the only radius, `igi::ObjectOrbitCamera::distance`, is *derived* by `BeginObjectOrbit` (`source/runtime/editor_camera_start.h:64-84`) and only reset by `CameraResetRadius`. Any rate/direction/preset ladder would have been invented | yes — no game-side claim | pressing the old combo prints `CameraAdjustRadius was removed (#48): no editor handler and no IGI-RE counterpart — see docs/igi-re-parity/KEYBINDINGS.md`; `H` no longer lists it |
| 2 | `CameraLookDown` (`<Ctrl><Home>`) | **removed** — binding, config field and routing branch | `source/app_input.cpp:147-156` `Removed("CameraLookDown")` | `Config::keyLookDown` had **0 readers**; camera pitch is owned by the mouse (`source/app_view.cpp:43`, clamped to ±89 at `:256`) and there is no keyboard look control to wire it to. A step size would have been invented | yes — no game-side claim | pressing the old combo prints the same "was removed (#48)" message naming `CameraLookDown`; `H` no longer lists it |
| 3 | `CameraStrafeFree` (`<Alt><Space>`) | **removed** — binding, `camera_strafe_free_` flag and the message | `source/app_input.cpp:189-196` `Removed("CameraStrafeFree")` | the flag had no reader anywhere (`source/app.h` was its only other mention), so `"Strafe lock: ON/OFF"` reported a state change that never happened — a lying status message. Strafing itself is unconditional (`source/app_view.cpp:225-236`), so there was no lock to release | yes — no game-side claim | the message is gone; the combo is unbound, and a stale user file gets "CameraStrafeFree was removed (#48)…" in the status bar. `CameraStrafeLeft/Right` (`<Alt><Left>`/`<Alt><Right>`) still strafe |
| 4 | Duplicate combos: `<Alt><Space>` and `<Ctrl><Shift><A>` | **fixed** — collision 1 disappears with items 1+3; collision 2 rebinds the loser | `assets/editor/qed/qedkeybindings.qsc` | one combo can only fire one handler, so whichever loses is a silent no-op. `ToggleAutoSave` keeps `<Ctrl><Shift><A>` ("A" is its mnemonic and it is the older binding); `AddModelToRes` moves to `<Ctrl><Shift>M` ("M" for model). Verified: 0 duplicate combos remain (77 active bindings, 77 distinct combos) | yes — no game-side claim | `Ctrl+Shift+A` toggles auto-save; `Ctrl+Shift+M` adds the missing model to the level `.res`. Both are reachable and `H` lists both |
| 5 | `ManipulationMode::None` (plain LMB drag) | **deliberate no-op, made visible** — not implemented | `source/app_view.cpp:703-719` sets `status_message_`; the `else` branch at `:721-728` still skips the undo push | the two drag planes this editor defines are already taken (`Shift` = view plane, `Ctrl` = vertical, `source/app_view.cpp:685-687`); a default plane would be an invented design decision, so the gesture stays a no-op — but it now names the mode that did nothing and the modifiers that work. No undo state is pushed because nothing changed | yes — no game-side claim | dragging with no modifier prints `Manipulate (None): drag moves nothing — Shift = view plane, Ctrl = vertical, A/B/G = rotate, Space = reset orientation, S/O = snap to ground / nearest object` |
| 6 | `TaskFind` (`<Ctrl><F>`) — **found by the new invariant, not one of the five** | **implemented** (was a silent no-op) | `source/app_input.cpp:297-310` | the new "every advertised binding does something visible" test flagged `TaskFind` as advertised in `assets/editor/qed/qedkeybindings.qsc:44` with no handler anywhere, while its three siblings (`TaskFindTextInTask` `:313`, `TaskFindByTaskID` `:320`, `TaskFindByTaskNote` `:327`) all open the find bar. The state already exists (`find_open_`, `find_mode_`, `find_query_`, `source/app.h:365-369`) and its default mode is `FindMode::TaskNameTypeId`, so the generic find opens that mode — four lines copied from the siblings, no new state, nothing invented | yes — no game-side claim (IGI-RE has no QED task-tree find bar) | `Ctrl+F` opens the find bar in its default mode (find by task name/type/id), like `Ctrl+Shift+X` / `Ctrl+Shift+I` / `Ctrl+Shift+N` do for the other modes |

### Task-11 consequences for the #48 decision table

* `CameraAdjustRadius` and `CameraLookDown` were the two rows whose
  `source/app_input.cpp` line said *"handled by existing named binding"* while
  nothing read the field. They are now removed with the same `Removed()` guard as
  the 23 of #48, and their dead config fields are deleted, so
  `source/app_input.cpp` no longer claims a handler that does not exist.
* `camera_strafe_free_` (`source/app.h`) is deleted; the three task-11 removals are
  listed explicitly in `tests/test_keybindings.cpp` (`kAdjacentRemovals`), so a
  binding removed without a recorded decision still fails the suite.
* The shipped default now has **75 active bindings** (was 78 after task-8: −3
  removed, −1 rebound from the duplicate) and **no duplicate combo**: 75 distinct
  names, 75 distinct combos, 0 malformed lines, plus the 2 pre-existing
  commented-out `TestEvent` lines.

### Config-key deletion: parser tolerance (verified by reading, not by running)

Deleting `Config::keyAdjustCameraRadius` / `Config::keyLookDown` cannot break a
user's existing `qedkeybindings.qsc`, and the key's absence from the shipped file
cannot break startup:

* the router is an `if / else if` chain on `eventName` with **no else/error
  branch** (`source/config.cpp:345-373`), and every parsed binding is stored
  unconditionally into `data_.eventBindings_[eventName]` at `:373`, so a name with
  no named-field branch is still stored and simply has no mirror field;
* the loader reads whatever lines the file contains
  (`while (std::getline(kbFile, line)) ParseLine(line)`, `source/config.cpp:444-449`)
  and a missing file is a `WARNING` only (`:450-452`);
* a stale user file that still binds a removed name therefore lands in the event
  map and is answered by the `Removed()` guard — a visible message, not a crash.

**Not verified at runtime**: this path needs the Windows QSC/VK parser
(`ParseKeyBinding` uses `VkKeyScanA`, `source/config.cpp:126`) and a real QED
directory, so the check above is a read verification. `tu-check.py` compiles the
changed TUs and the host suite pins the invariants; neither executes the parser.

