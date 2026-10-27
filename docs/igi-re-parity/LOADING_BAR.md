# The retail loading bar in the editor — `source/runtime/loading_bar_style.h`

Status: **complete and reached**, with the rendered pixels explicitly NOT verified
(this host gives the editor no visible surface — see §5).

The engine's loading progress is a `LoadingScreen` widget, and the editor draws
that widget's bar for its own long operations. Every constant is cited to IGI-RE
(the verified C decompilation of retail `IGI.exe`) by function + HOOK address +
file:line; the header `source/runtime/loading_bar_style.h` carries the same
citations next to the code.

## 1. What the retail bar is

```
src/loadscreen/loadscreen.c:18-23   struct LoadingScreen { total, done, x, y, Bitmap bar; }
src/loadscreen/loadscreen.c:63-96   LoadingScreen_New  HOOK 0x48A440
    :74  ls->x = (mode->width  - 640) / 2 + 40
    :75  ls->y = (mode->height - 480) / 2 + 440
    :76  OffscreenBuffer_New(&ls->bar, 560, 10)
    :77-78 every bar pixel = 0x7F00  (the track)
src/loadscreen/loadscreen.c:110-124 LoadingScreen_Step HOOK 0x48A620
    :114 ls->done += steps
    :115 filled = ls->bar.w * ls->done / ls->total      (integer, truncating)
    :119-121 the first `filled` pixels of every row = 0xFF00  (the fill)
src/loadscreen/loadscreen.c:98-108  LoadingScreen_Delete HOOK 0x48A5D0
src/d3d/direct3d.c:1092-1156        Direct3D_Blit HOOK 0x494C50 (8-bit channels)
```

* **One flat 560×10 bitmap**, positioned inside a screen-centred 640×480 band. No
  border, no panel, no gradient, no alpha, no text — and *not* the pause menu's
  `MenuScreen_DrawFrame` (HOOK 0x421CD0, `src/menuscreen/menuscreen.c:168-229`),
  which is a different widget.
* **Not resolution-scaled.** Only the band origin moves with the window
  (`SCREEN_X`/`SCREEN_Y`, `src/menuscreen/menuscreen.h:141-142`), so the bar is
  560×10 physical pixels at every size.
* **Colours are 8-bit channels**, not the menus' 1/128 domain: `0x7F00` is
  R 0 / G 0x7F / B 0 and `0xFF00` is R 0 / G 0xFF / B 0, taken verbatim by the blit
  (`src/d3d/direct3d.c:1111-1121`). The editor therefore divides by 255
  (`kRetailLoadingTrackGreen` = 0.498, `kRetailLoadingFillGreen` = 1.0), not by 128.
* **Driven by steps, not a timer**: `LoadingScreen_New(9)` for a full level load
  (`src/game/game.c:399`, advanced by nine `LoadingScreen_Step(loading, 1)` calls at
  `:415-468`), 5 for a restart (`:1077`), 1 for a mission transition (`:576`).
* **No font at all**: `LoadingScreen_New` loads only the loading picture
  (`loading.pic`, a sprite via `Text_Load` HOOK 0x4B6DB0, `src/res/text.c:12-35`) and
  never a `.fnt`. There is therefore **no engine oracle for text on the bar**; the
  editor's own title/stage label is editor chrome.

## 2. How the editor uses it

`App::DrawProgressOverlay(title, pct, stage)` (`source/app_ui.cpp`) clears to black
(the retail teardown's own clear, `LoadingScreen_Delete` :100-108), draws the track
over the full retail rect, draws the fill over the retail rect's first `filled`
pixels, and then draws the editor's own label above the bar. Geometry comes from
`BuildLoadingBarLayout(viewport)`, colours from the constants above, and the fill
from `LoadingBarFillWidthForPercent`, which is `:115` with `total == 100`.

## 3. The editor's progress ladders (and the gap they closed)

`source/runtime/loading_bar_style.h` §5 holds three ladders — a level load, a level
save and a reset's restore — as `LoadingBarStage { percent, stage }` data used by
`source/app_level.cpp` and `source/app_input.cpp`, with `static_assert`s and host
tests for range and monotonicity.

The gap that mattered: the load's ladder used to start at 10% *after* the level's
pristine full-folder backup, and — worse — the **startup** load runs inside
`App::Init`, before GLUT's reshape callback is registered, so
`window_state_.viewport_*` was still 0 and `DrawProgressOverlay`'s `vw <= 0` guard
dropped **every** frame. The one load every user sees reported nothing at all.

Fixed by:

* `App::Init` seeds the viewport from the window GLUT already created
  (`OnWindowResize(glutGet(GLUT_WINDOW_WIDTH), glutGet(GLUT_WINDOW_HEIGHT))`) before
  it calls `LoadLevel`, and `DrawProgressOverlay` asks
  `igi::ProgressOverlayCanPaint` (`source/runtime/progress_overlay_policy.h`) rather
  than testing the two fields inline;
* the load ladder gained `preparing` (2%) and `backing up level` (5%) before
  `reading level`, so the prelude is reported;
* the level save (`App::SaveCurrentLevel`) and a reset's restore
  (`App::ResetLevel`) now report too, through the same ladders.

## 4. What else already reported

Verified by reading the call sites, not by trusting a report: six load milestones
(now eight), both model-import entry points (`CommitPropertyText`'s automatic
add in `source/app_editor.cpp` and the `AddModelToRes` binding in
`source/app_input.cpp`, each with the `AddModelToLevelRes` progress callback), the
per-object and bulk lightmap bakes (`source/app_editor.cpp`), and the lightmap
write-back (`source/app_level.cpp`).

## 5. Verification

| check | command | result |
| --- | --- | --- |
| bar geometry/colour/fill | `IGI_HOST_TESTS=tests/test_loading_bar_style.cpp bash scripts/run-host-tests.sh` | 26 passed, 0 failed |
| ladder data | same suite (`LevelLoadLadder*`, `LevelSaveLadder*`, `LevelResetLadder*`, `EveryLadderStageFillsTheBarByTheRetailRule`, `TheFirstLoadStageIsAlreadyVisible`) | included above |
| overlay viewport rule | Windows suite (`ProgressOverlayPolicyTest.TheOverlayCannotPaintWithoutAViewport`) | passed |
| **the startup load reaches the bar** | i686 editor under Wine, `-level 1 -w 1280 -h 720`, game root a writable copy, `QEDLogLevel(0)` in the staged `qedconfig.qsc` | **8 frames**, listed in `loading_bar_style.h` §5: `2% preparing … 100% done`, every one `viewport 1280x720 bar 360,560 560x10`, fill 11/28/56/140/336/420/504/560 |

`bar 360,560` is exactly `BuildLoadingBarLayout(1280, 720)` — the retail 640×480
band at that window size — and the fills are the integer truncation of
`560 * pct / 100`.

**Not verified: the rendered pixels.** On this host Wine never maps a visible window
for the editor (the X root has no window and the macOS screen is unchanged across a
run), so the evidence above proves which frames were presented, at which retail rect
and with which fill — not what the bar looked like. The label above the bar has no
engine oracle at all (§1) and is the editor's own.
