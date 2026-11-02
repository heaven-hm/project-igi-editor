/******************************************************************************
 * @file    anim_panel.h
 * @brief   Issue #73: the pure half of the F10 animation panel - the frame-step
 *          arithmetic, the panel's layout, and the text it shows.
 *
 * WHY THIS HEADER EXISTS
 *   The F10 panel is drawn by `source/renderer/renderer_draw.cpp` and clicked by
 *   `source/app_input_mouse.cpp`; both are Windows-only translation units. The
 *   parts that decide *what a frame step is*, *where each button is* and *what
 *   each row says* are pure functions of their inputs, so they live here and are
 *   pinned by `tests/test_editor_quick_actions.cpp` on the host lane. The two
 *   callers keep only the part that needs the editor: the live `AnimPlayback` and
 *   the GL calls.
 *
 * ONE SPEED LADDER, ONE LABEL
 *   The panel's speed readout and its Speed -/+ buttons use the SAME ladder and
 *   the SAME formatter as the F12 / Shift+F12 keybindings
 *   (`igi::AnimTimerSpeedIndex/StepIndex/At/Label`, source/runtime/editor_quick_actions.h),
 *   which drive the selected object's `igi::AnimPlayback`. There is no second
 *   speed mechanism here: the panel reads and writes that one state.
 *
 * ORACLE
 *   The frame step is the engine's own frame, cited at each function below:
 *
 *     BoneObj_AnimEventsCB 0x4D4B60  src/boneobj/boneobj.c:873-941  (the clock)
 *     Anim_SetSpeed        0x4D6580  src/boneobj/anim.c:172-179     (ticks/frame)
 *     BoneObj_UpdateAnimDirection 0x4D6550  src/boneobj/anim.c:163-169 (reverse)
 *     BoneObj_SetFrameRate 0x4D2D30  src/lod/boneobj.c:86-90        (gTicksPerFrame)
 *     Anim_GetTime         0x4D6500  src/boneobj/anim.c:149-153
 *     Anim_GetLength       0x4D64E0  src/boneobj/anim.c:143-147
 *
 *   What has NO engine counterpart is marked INFERRED where it appears: the
 *   engine has no frame INDEX (it counts ticks only) and no panel, so the row
 *   text and the button geometry are the editor's, not retail's.
 *****************************************************************************/
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include "runtime/anim_rules.h"      // kAnimDefaultTicksPerFrame (gTicksPerFrame)
#include "runtime/editor_quick_actions.h"

namespace igi {

// ---------------------------------------------------------------------------
// The frame step: one engine frame of the playhead
// ---------------------------------------------------------------------------
// VERIFIED. One engine frame moves the playhead by `inst->speed` ticks, and
// `inst->speed` is `gTicksPerFrame * speed`:
//
//   BoneObj_AnimEventsCB 0x4D4B60 (src/boneobj/boneobj.c:884-885)
//       old = inst->time; now = inst->speed + inst->time; inst->time = now;
//   Anim_SetSpeed        0x4D6580 (src/boneobj/anim.c:172-179)
//       inst->speed = gTicksPerFrame * speed;
//   BoneObj_SetFrameRate 0x4D2D30 (src/lod/boneobj.c:86-90)
//       gTicksPerFrame = kTicksPerSecond / fps        (160 at the 30 the game
//                                                      installs, src/game/game.c:449)
//
// So a frame step at 1x is 160 ticks and at 2x it is 320: the step is the frame
// the ENGINE would run, which is what makes "step" and "speed" the same clock.
inline float AnimFrameStepTicks(float speed_multiplier, float ticks_per_frame) {
    return ticks_per_frame * speed_multiplier;
}

// VERIFIED. The playhead after one engine frame, with the engine's own clamp and
// wrap rules (BoneObj_AnimEventsCB 0x4D4B60, src/boneobj/boneobj.c:892-937):
//
//   forward,  `!(t < length)`  ->  looping: `t - length`;  one-shot: `length`
//   backward, `!(t > 0.0f)`    ->  looping: `length + t`;  one-shot: `0.0f`
//
// Three quirks are reproduced rather than corrected, because the engine has them:
//   * the wrap is a SINGLE subtraction/addition, not a while loop
//     (boneobj.c:900-905), so a step longer than the whole clip leaves the
//     playhead past the end after one wrap;
//   * the end test is `>=` (`!(t < length)` is true at `t == length`);
//   * a one-shot clamps and keeps running - it is not stopped by the clock.
// `AnimAdvanceTest.LoopWrapIsNotAWhileLoop` pins the same rule for the sampler
// (source/runtime/anim_rules.h) and this function is pinned against the ported
// clock itself in `AnimPlaybackStepTest.PureStepMatchesTheEngineClock`.
//
// `step_ticks` is `AnimFrameStepTicks(...)` and is never negative; the direction
// is the `forward` flag, exactly as ANIM_REVERSE carries it in the engine
// (`BoneObj_UpdateAnimDirection` 0x4D6550 sets it iff `inst->speed < 0`).
inline float AnimStepTargetTick(float tick, float step_ticks, float length, bool looping,
                                bool forward) {
    const float stepped = forward ? (tick + step_ticks) : (tick - step_ticks);
    if (forward) {
        if (!(stepped < length)) return looping ? (stepped - length) : length;
    } else {
        if (!(stepped > 0.0f)) return looping ? (length + stepped) : 0.0f;
    }
    return stepped;
}

// INFERRED (editor display only). The engine counts ticks and has no frame
// index; the clip's own frames ARE 160-tick multiples at 30 fps - measured over
// the shipped corpus, 55733 of 66659 rotation key times are exact multiples of
// 160 (source/runtime/anim_player.h, "What the BEF text does and does NOT
// carry"). So the frame the clock is showing is the tick divided by the engine's
// own ticks-per-frame, floored.
inline int AnimFrameIndexForTick(float tick, float ticks_per_frame) {
    if (!(ticks_per_frame > 0.0f) || !(tick > 0.0f)) return 0;
    return static_cast<int>(std::floor(tick / ticks_per_frame));
}

// INFERRED (editor display only). The last frame index of a clip: its engine
// length in frames. `length` is `Anim_GetLength` 0x4D64E0, which for every
// shipped clip is a whole number of 30 fps frames (anim_player.h, same
// measurement), so this rounds to nearest rather than truncating a
// representable-but-inexact float.
inline int AnimFrameCountForLength(float length, float ticks_per_frame) {
    if (!(ticks_per_frame > 0.0f) || !(length > 0.0f)) return 0;
    const int frames = static_cast<int>(std::lround(length / ticks_per_frame));
    return frames > 0 ? frames : 0;
}

// ---------------------------------------------------------------------------
// The panel's layout
// ---------------------------------------------------------------------------
// INFERRED (the editor's own panel; retail QED has no animation preview). All
// rects are in TOP-DOWN screen pixels - the GLUT mouse convention - the same
// convention `TerrainPalette`, `GraphNodePanel` and `PropPanel` use
// (source/renderer/renderer.h:28-162). The renderer converts to GL bottom-up
// with `gl_y = viewport_height - screen_y`; the mouse handler uses the values
// as they are, so a button is hit-tested exactly where it is drawn.
//
// The panel is a FIXED size on purpose: the hit-test and the draw must agree
// without either one re-deriving the other's row count, and a panel that grows
// and shrinks as playbacks come and go would move its own buttons.
namespace AnimPanel {

inline constexpr int kLeft   = 10;
inline constexpr int kTop    = 80;    // top-down y of the panel's top edge
inline constexpr int kWidth  = 360;

inline constexpr int kTitleH = 18;    // title band
inline constexpr int kBtnTop = kTop + kTitleH;
inline constexpr int kBtnW   = 66;
inline constexpr int kBtnH   = 20;
inline constexpr int kBtnGap = 4;
inline constexpr int kBtnPadX = 6;

inline constexpr int kRowsTop = kBtnTop + kBtnH + 6;
inline constexpr int kRowH    = 16;
// Five playback rows plus the "... and N more" row the existing cap emits
// (`App::BuildAnimStatusString`), which is now the panel's fixed row count as
// well: the renderer draws at most this many lines, and the hit-test needs no
// row count at all because the panel's height never depends on its contents.
inline constexpr int kMaxRows = 6;

inline constexpr int kFooterH   = 16;
inline constexpr int kPadBottom = 6;

// The shared index space between App (hit-test) and Renderer (draw).
enum Index {
    kPlayPause = 0,
    kStepBack,
    kStepForward,
    kSpeedDown,
    kSpeedUp,
    kCount
};

inline int PanelHeight() {
    return kTitleH + kBtnH + 6 + kMaxRows * kRowH + kFooterH + kPadBottom;
}

inline int PanelBottom() { return kTop + PanelHeight(); }

// The control row, left to right. Returns an all-zero rect for an out-of-range
// index so a caller cannot be handed a plausible-looking wrong button.
inline void GetButtonRect(int idx, int& x, int& y, int& w, int& h) {
    if (idx < 0 || idx >= kCount) {
        x = y = w = h = 0;
        return;
    }
    w = kBtnW;
    h = kBtnH;
    y = kBtnTop;
    x = kLeft + kBtnPadX + idx * (kBtnW + kBtnGap);
}

// `draw_text_sm` puts the text BASELINE at the y it is given (it draws at
// `viewport_height - y`, source/renderer/renderer_draw.cpp:707-734), so every
// y below is a baseline inside its own band, not the band's top edge.
inline int TitleBaseline() { return kTop + kTitleH - 5; }
inline int ButtonLabelBaseline() { return kBtnTop + kBtnH - 6; }
inline int RowBaseline(int row) {
    if (row < 0) row = 0;
    if (row >= kMaxRows) row = kMaxRows - 1;
    return kRowsTop + (row + 1) * kRowH - 4;
}
inline int FooterBaseline() { return PanelBottom() - kPadBottom; }

inline bool InPanel(int mx, int my) {
    return mx >= kLeft && mx <= kLeft + kWidth && my >= kTop && my <= PanelBottom();
}

// Button index 0..kCount-1, or -1 for a click that is inside the panel but not
// on a button (still consumed by the caller: the panel is an overlay).
inline int HitTest(int mx, int my) {
    if (!InPanel(mx, my)) return -1;
    for (int i = 0; i < kCount; ++i) {
        int x, y, w, h;
        GetButtonRect(i, x, y, w, h);
        if (mx >= x && mx <= x + w && my >= y && my <= y + h) return i;
    }
    return -1;
}

// The one state-dependent label: the panel's first button is play/pause, so
// draw and any test ask the same function rather than each formatting it.
inline const char* ButtonLabel(int idx, bool playing) {
    switch (idx) {
    case kPlayPause:   return playing ? "Pause" : "Play";
    case kStepBack:    return "Step -1";
    case kStepForward: return "Step +1";
    case kSpeedDown:   return "Speed -";
    case kSpeedUp:     return "Speed +";
    default:           return "";
    }
}

// The footer names the keys that do the same thing as the buttons. There is no
// QED binding for a frame step (retail QED.doc, "General keystrokes", lists
// F12/Shift F12 for AnimTask speed and Ctrl T / Ctrl R for the timer, and no
// step), so the keys are the editor's and the panel is where they are stated.
inline constexpr const char* kKeyHints =
    "Ctrl+T play  F12/Shift+F12 speed  , . step   > = controls";

}  // namespace AnimPanel

// ---------------------------------------------------------------------------
// The panel's text
// ---------------------------------------------------------------------------
// One row per playback. Everything shown is engine state - the clip name, the
// clock (`Anim_GetTime` 0x4D6500, ticks), the length (`Anim_GetLength` 0x4D64E0)
// and the loop flag - except the frame index/count, which are INFERRED display
// (see above), and the speed label, which is the SAME formatter the F12 keys
// report (`AnimTimerSpeedLabel`), so the panel and the status bar cannot
// disagree about what "2x" means.
struct AnimPanelRow {
    std::string name;            // object name (or "#index" / model id)
    std::string clip;            // clip name
    bool  selected = false;      // the object the buttons and the keys act on
    bool  playing = false;
    bool  engine_ready = true;   // AnimPlayback::EngineReady()
    bool  looping = false;
    float tick = 0.0f;           // engine ticks, Anim_GetTime 0x4D6500
    float length = 0.0f;         // engine ticks, Anim_GetLength 0x4D64E0
    float speed = 1.0f;          // AnimPlayback::speed (the ladder's value)
    float ticks_per_frame = kAnimDefaultTicksPerFrame;   // gTicksPerFrame, src/lod/boneobj.c:86-90
};

inline std::string FormatAnimPanelRow(const AnimPanelRow& row) {
    // A clip the engine cannot play is named as such rather than shown at tick
    // 0 pretending to be a frame: AnimPlayback::EngineReady() is false when
    // `BoneObj_Build`/`Anim_Play` refused it (no bones, no position track -
    // boneobj.c:1014 dereferences `desc->pos->keys[count - 1]`).
    if (!row.engine_ready) {
        return std::string(row.selected ? "> " : "  ") + row.name + ": " + row.clip +
               "  [engine refused the clip]";
    }
    char buf[320];
    std::snprintf(buf, sizeof(buf), "%s%s: %s  f%d/%d  %s  %s%s",
                  row.selected ? "> " : "  ", row.name.c_str(), row.clip.c_str(),
                  AnimFrameIndexForTick(row.tick, row.ticks_per_frame),
                  AnimFrameCountForLength(row.length, row.ticks_per_frame),
                  AnimTimerSpeedLabel(row.speed).c_str(),
                  row.playing ? "playing" : "paused", row.looping ? " loop" : "");
    return std::string(buf);
}

// The panel's title: the transport's own state, from the same three values the
// buttons and the keys read.
inline std::string FormatAnimPanelTitle(bool playing, float speed, float step_ticks) {
    char buf[128];
    std::snprintf(buf, sizeof(buf), "Animation Preview - %s - %s - step %g ticks",
                  playing ? "playing" : "paused",
                  AnimTimerSpeedLabel(speed).c_str(), static_cast<double>(step_ticks));
    return std::string(buf);
}

}  // namespace igi
