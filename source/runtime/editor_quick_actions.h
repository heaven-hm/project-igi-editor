/******************************************************************************
 * @file    editor_quick_actions.h
 * @brief   Issue #48: the pure half of the editor's camera-clipboard and
 *          animation-timer keybindings.
 *
 * WHY THIS HEADER EXISTS
 *   The two implemented quick actions of issue #48 are key handlers in
 *   `source/app_input.cpp`, which is a Windows-only translation unit that links
 *   windows.h, GLUT and the whole `App`.  The *decisions* those handlers make -
 *   which speed comes next on the animation-timer ladder, and the exact text
 *   that goes on the clipboard - are pure functions of their inputs, so they
 *   live here and are pinned by `tests/test_editor_quick_actions.cpp` on the
 *   host lane.  The handler keeps only the part that needs the editor: reading
 *   the live camera / the selected object's playback and writing the status bar.
 *
 * ORACLE
 *   The retail editor's own manual, `editor/qed/QED.doc` in an installed IGI
 *   tree ("General keystrokes", last saved 2003-03-07), is the authority for what
 *   these QED commands mean:
 *
 *     "Ctrl P  Copy camera position to clipboard"
 *     "Ctrl R  Reset timer"
 *     "Ctrl T  Timer Start / Stop"
 *     "F12 / Shift F12  Increment / Decrement AnimTask speed"
 *
 *   The shipped default `qedkeybindings.qsc` binds exactly those combos to
 *   `CameraCopyPosition`, `TimerReset`, `TimerStartStop`,
 *   `TimerIncreaseSpeed` and `TimerDecreaseSpeed`, so the retail "timer" and the
 *   retail "AnimTask speed" are one control: the animation playback clock.
 *
 *   IGI-RE is not an oracle here: it decompiles IGI.exe only and contains no QED
 *   editor code (see docs/igi-re-parity/KEYBINDINGS.md).  Nothing in this header
 *   claims game-side behaviour.
 *****************************************************************************/
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace igi {

// ---------------------------------------------------------------------------
// Animation-timer speed ladder
// ---------------------------------------------------------------------------
// This editor's one playback clock is `igi::AnimPlayback`
// (source/animation.h:184): `currentTimeMs` is the timer, `playing` is
// start/stop, `Reset()` rewinds and stops, and `speed` is the multiplier the
// engine is given by `AnimPlayback::SyncEngineFlags`
// (source/runtime/anim_playback.cpp:147-151, `Anim_SetSpeed` 0x4D6580:
// `inst->speed = gTicksPerFrame * speed`).
//
// The ladder doubles and halves rather than scaling by a factor, so repeated
// presses are exactly reversible and the reachable set is small and reportable.
// 1.0f is the engine's own normal speed and the default the editor starts a
// playback with (source/animation.h:189).
inline constexpr float kAnimTimerSpeeds[] = {
    0.125f, 0.25f, 0.5f, 1.0f, 2.0f, 4.0f, 8.0f,
};
inline constexpr int kAnimTimerSpeedCount =
    static_cast<int>(sizeof(kAnimTimerSpeeds) / sizeof(kAnimTimerSpeeds[0]));
inline constexpr int kAnimTimerDefaultSpeedIndex = 3;   // 1.0f

// Ladder entry nearest to `speed`.  A value that is exactly on the ladder is
// found exactly; anything else (the engine accepts any positive multiplier)
// snaps to the closest entry, ties going to the faster one so that a value
// between 1x and 2x steps up to 2x rather than stalling at 1x.
inline int AnimTimerSpeedIndex(float speed) {
    int best = 0;
    float best_diff = std::fabs(speed - kAnimTimerSpeeds[0]);
    for (int i = 1; i < kAnimTimerSpeedCount; ++i) {
        const float diff = std::fabs(speed - kAnimTimerSpeeds[i]);
        if (diff <= best_diff) {   // <= : a tie keeps the faster entry
            best = i;
            best_diff = diff;
        }
    }
    return best;
}

// One rung up (+1) or down (-1) from `index`, clamped at both ends of the
// ladder: the ends are the documented limits, and the handler reports the
// clamp to the user instead of silently doing nothing.
inline int AnimTimerSpeedStepIndex(int index, int direction) {
    if (index < 0) index = 0;
    if (index >= kAnimTimerSpeedCount) index = kAnimTimerSpeedCount - 1;
    if (direction > 0) return std::min(index + 1, kAnimTimerSpeedCount - 1);
    if (direction < 0) return std::max(index - 1, 0);
    return index;
}

inline float AnimTimerSpeedAt(int index) {
    if (index < 0) index = 0;
    if (index >= kAnimTimerSpeedCount) index = kAnimTimerSpeedCount - 1;
    return kAnimTimerSpeeds[index];
}

// "0.125x" .. "8x" - what the status bar reports.
inline std::string AnimTimerSpeedLabel(float speed) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%gx", static_cast<double>(speed));
    return std::string(buf);
}

// ---------------------------------------------------------------------------
// Camera text for the clipboard
// ---------------------------------------------------------------------------
// `viewer_.pos_` is world units and `viewer_.yaw_/pitch_/roll_` are degrees
// (source/app.h:612-625), the same three-plus-three the editor persists as
// QEDSetCameraPosition / QEDSetCameraOrientation (source/config.cpp:505-508),
// so one "three numbers, three decimals" formatter serves both.  Three decimals
// is the precision the editor's own readouts use (source/app_input.cpp:23-27).
inline std::string FormatCameraTripleForClipboard(float a, float b, float c) {
    char buf[96];
    std::snprintf(buf, sizeof(buf), "%.3f, %.3f, %.3f",
                  static_cast<double>(a), static_cast<double>(b),
                  static_cast<double>(c));
    return std::string(buf);
}

}  // namespace igi
