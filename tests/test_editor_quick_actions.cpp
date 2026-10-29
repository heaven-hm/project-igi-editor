/******************************************************************************
 * @file    test_editor_quick_actions.cpp
 * @brief   Issue #48: host-portable tests for the pure half of the implemented
 *          keybindings — the animation-timer speed ladder and the camera text
 *          that goes on the clipboard.
 *
 * WHAT IS AND IS NOT TESTED HERE
 *   `source/runtime/editor_quick_actions.h` holds only the decisions the two
 *   implemented quick actions make: which ladder rung comes next, and the exact
 *   clipboard text.  Those are pure functions of their inputs, so they are
 *   pinned here on the host lane.
 *
 *   What this file does NOT prove (and cannot, on this host): that
 *   `Utils::SetClipboardText` reaches the Windows clipboard, that the handler
 *   reads the live `viewer_`, or that `AnimPlayback::speed` reaches the engine.
 *   The handler's wiring is asserted textually by `tests/test_keybindings.cpp`
 *   (the event is bound, dispatched, and not answered by a `Removed()` guard);
 *   the engine-side application of `speed` is the existing, separately tested
 *   `AnimPlayback::SyncEngineFlags` path (tests/test_anim_playback.cpp).
 *
 * ORACLE
 *   The retail editor's own manual `editor/qed/QED.doc` in an installed IGI
 *   tree, "General keystrokes": "Ctrl P  Copy camera position to clipboard",
 *   "Ctrl R  Reset timer", "Ctrl T  Timer Start / Stop", "F12 / Shift F12
 *   Increment / Decrement AnimTask speed".  The shipped default
 *   `assets/editor/qed/qedkeybindings.qsc` binds those combos to
 *   CameraCopyPosition, TimerReset, TimerStartStop, TimerIncreaseSpeed and
 *   TimerDecreaseSpeed.
 *****************************************************************************/

#include <gtest/gtest.h>

#include "../source/runtime/editor_quick_actions.h"

#include <string>

namespace {

// ---------------------------------------------------------------------------
// The speed ladder
// ---------------------------------------------------------------------------

TEST(EditorQuickActions48, LadderIsStrictlyIncreasingAndHasTheDefault) {
    ASSERT_GE(igi::kAnimTimerSpeedCount, 3);
    for (int i = 1; i < igi::kAnimTimerSpeedCount; ++i) {
        EXPECT_LT(igi::AnimTimerSpeedAt(i - 1), igi::AnimTimerSpeedAt(i))
            << "the ladder must be strictly increasing at index " << i;
    }
    EXPECT_FLOAT_EQ(igi::AnimTimerSpeedAt(igi::kAnimTimerDefaultSpeedIndex), 1.0f)
        << "the default index must be the engine's normal speed (1.0)";
    for (int i = 0; i < igi::kAnimTimerSpeedCount; ++i) {
        EXPECT_GT(igi::AnimTimerSpeedAt(i), 0.0f) << "index " << i;
    }
}

TEST(EditorQuickActions48, EveryLadderEntryIsFoundExactly) {
    for (int i = 0; i < igi::kAnimTimerSpeedCount; ++i) {
        const float speed = igi::AnimTimerSpeedAt(i);
        EXPECT_EQ(igi::AnimTimerSpeedIndex(speed), i)
            << "a speed that is exactly on the ladder (" << speed
            << ") must not snap to another rung";
    }
}

TEST(EditorQuickActions48, OffLadderSpeedsSnapToTheNearestRungTiesFaster) {
    // The engine accepts any positive multiplier, so the index lookup must be
    // total.  A value between two rungs snaps to the closer one; an exact tie
    // goes to the faster rung so that stepping up never stalls.
    EXPECT_EQ(igi::AnimTimerSpeedIndex(1.4f), igi::AnimTimerSpeedIndex(1.0f));
    EXPECT_EQ(igi::AnimTimerSpeedIndex(1.6f), igi::AnimTimerSpeedIndex(2.0f));
    EXPECT_EQ(igi::AnimTimerSpeedIndex(3.0f), igi::AnimTimerSpeedIndex(4.0f));   // tie -> faster
    EXPECT_EQ(igi::AnimTimerSpeedIndex(0.0f), 0) << "a stopped/zero speed floors at the slowest rung";
    EXPECT_EQ(igi::AnimTimerSpeedIndex(1000.0f), igi::kAnimTimerSpeedCount - 1);
    EXPECT_EQ(igi::AnimTimerSpeedIndex(-5.0f), 0);
}

TEST(EditorQuickActions48, SteppingIsReversibleInTheMiddleOfTheLadder) {
    for (int i = 1; i + 1 < igi::kAnimTimerSpeedCount; ++i) {
        const int up = igi::AnimTimerSpeedStepIndex(i, 1);
        EXPECT_EQ(up, i + 1) << "one rung up from index " << i;
        EXPECT_EQ(igi::AnimTimerSpeedStepIndex(up, -1), i)
            << "a step up then down must return to index " << i;
    }
}

TEST(EditorQuickActions48, SteppingClampsAtBothEndsOfTheLadder) {
    const int last = igi::kAnimTimerSpeedCount - 1;
    EXPECT_EQ(igi::AnimTimerSpeedStepIndex(last, 1), last)
        << "the fastest rung is a limit, not a wrap";
    EXPECT_EQ(igi::AnimTimerSpeedStepIndex(0, -1), 0)
        << "the slowest rung is a limit, not a wrap";
    EXPECT_EQ(igi::AnimTimerSpeedStepIndex(0, 0), 0);
    EXPECT_EQ(igi::AnimTimerSpeedStepIndex(last, 0), last);
    // Out-of-range indices are clamped, not read out of bounds.
    EXPECT_EQ(igi::AnimTimerSpeedStepIndex(-3, 1), 1);
    EXPECT_EQ(igi::AnimTimerSpeedStepIndex(last + 5, -1), last - 1);
    EXPECT_FLOAT_EQ(igi::AnimTimerSpeedAt(-1), igi::AnimTimerSpeedAt(0));
    EXPECT_FLOAT_EQ(igi::AnimTimerSpeedAt(last + 1), igi::AnimTimerSpeedAt(last));
}

TEST(EditorQuickActions48, SpeedLabelIsTheStatusBarText) {
    EXPECT_EQ(igi::AnimTimerSpeedLabel(1.0f), "1x");
    EXPECT_EQ(igi::AnimTimerSpeedLabel(2.0f), "2x");
    EXPECT_EQ(igi::AnimTimerSpeedLabel(8.0f), "8x");
    EXPECT_EQ(igi::AnimTimerSpeedLabel(0.5f), "0.5x");
    EXPECT_EQ(igi::AnimTimerSpeedLabel(0.125f), "0.125x");
    // Every rung must be reportable and distinct, or the status bar would lie.
    for (int i = 0; i < igi::kAnimTimerSpeedCount; ++i) {
        for (int j = i + 1; j < igi::kAnimTimerSpeedCount; ++j) {
            EXPECT_NE(igi::AnimTimerSpeedLabel(igi::AnimTimerSpeedAt(i)),
                      igi::AnimTimerSpeedLabel(igi::AnimTimerSpeedAt(j)))
                << "rungs " << i << " and " << j << " render the same label";
        }
    }
}

// ---------------------------------------------------------------------------
// The clipboard text
// ---------------------------------------------------------------------------

TEST(EditorQuickActions48, CameraTripleTextIsThreeFixedDecimals) {
    EXPECT_EQ(igi::FormatCameraTripleForClipboard(1.0f, 2.5f, -3.25f),
              "1.000, 2.500, -3.250");
    EXPECT_EQ(igi::FormatCameraTripleForClipboard(0.0f, 0.0f, 0.0f), "0.000, 0.000, 0.000");
}

TEST(EditorQuickActions48, CameraTripleTextRoundsRatherThanTruncates) {
    // A camera position is a float; the text must round to the nearest third
    // decimal, not chop it, and must keep the sign.  0.0005f is a hair above
    // 0.0005 and 1.9999f is a hair below 2.0, so truncation would print
    // "0.000" and "1.999" where rounding gives "0.001" and "2.000".
    EXPECT_EQ(igi::FormatCameraTripleForClipboard(0.0005f, -0.0005f, 1.9999f),
              "0.001, -0.001, 2.000");
    EXPECT_EQ(igi::FormatCameraTripleForClipboard(40960.0f, -40960.0f, 0.125f),
              "40960.000, -40960.000, 0.125");
}

TEST(EditorQuickActions48, CameraTripleTextIsCopyPasteableAndUnlabelled) {
    // Three numbers, comma separated: the same shape the editor's own
    // QEDSetCameraPosition/QEDSetCameraOrientation carry (source/config.cpp:505-508).
    const std::string text = igi::FormatCameraTripleForClipboard(1.0f, 2.0f, 3.0f);
    EXPECT_EQ(text.find("x"), std::string::npos);
    EXPECT_EQ(text.find("yaw"), std::string::npos);
    EXPECT_EQ(text.find('\n'), std::string::npos);
    int separators = 0;
    for (const char c : text) if (c == ',') ++separators;
    EXPECT_EQ(separators, 2) << "expected exactly three numbers: " << text;
}

}  // namespace
