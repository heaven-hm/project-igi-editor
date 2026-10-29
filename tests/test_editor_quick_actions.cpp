/******************************************************************************
 * @file    test_editor_quick_actions.cpp
 * @brief   Issue #48: host-portable tests for the pure half of the implemented
 *          keybindings — the animation-timer speed ladder and the camera text
 *          that goes on the clipboard.
 *          Issue #73: the same for the F10 animation panel's frame step, its
 *          layout and its row text (`AnimFrameStep73.*`).
 *
 * WHAT IS AND IS NOT TESTED HERE
 *   `source/runtime/editor_quick_actions.h` holds only the decisions the two
 *   implemented quick actions make: which ladder rung comes next, and the exact
 *   clipboard text.  Those are pure functions of their inputs, so they are
 *   pinned here on the host lane.  `source/runtime/anim_panel.h` holds the same
 *   kind of thing for the F10 panel: the frame-step arithmetic (a transcription
 *   of `BoneObj_AnimEventsCB` 0x4D4B60's clock rule), the layout the renderer
 *   draws and the mouse handler hit-tests, and the text of a row.
 *
 *   What this file does NOT prove (and cannot, on this host): that
 *   `Utils::SetClipboardText` reaches the Windows clipboard, that the handler
 *   reads the live `viewer_`, that `AnimPlayback::speed` reaches the engine, or
 *   that the panel looks like its geometry.  The handler's wiring is asserted
 *   textually by `tests/test_keybindings.cpp` (the event is bound, dispatched,
 *   and not answered by a `Removed()` guard); the engine-side application of
 *   `speed` is the existing, separately tested `AnimPlayback::SyncEngineFlags`
 *   path, and the engine-side frame step is `tests/test_anim_playback.cpp`
 *   (`AnimPlaybackStepTest.*`), which also pins `AnimStepTargetTick` against the
 *   ported engine clock itself.
 *
 * ORACLE
 *   The retail editor's own manual `editor/qed/QED.doc` in an installed IGI
 *   tree, "General keystrokes": "Ctrl P  Copy camera position to clipboard",
 *   "Ctrl R  Reset timer", "Ctrl T  Timer Start / Stop", "F12 / Shift F12
 *   Increment / Decrement AnimTask speed".  The shipped default
 *   `assets/editor/qed/qedkeybindings.qsc` binds those combos to
 *   CameraCopyPosition, TimerReset, TimerStartStop, TimerIncreaseSpeed and
 *   TimerDecreaseSpeed.  The manual documents NO frame step: the `,` / `.` keys
 *   are the editor's own, while the STEP SIZE is the engine's frame
 *   (`BoneObj_AnimEventsCB` 0x4D4B60, src/boneobj/boneobj.c:884-885).
 *****************************************************************************/

#include <gtest/gtest.h>

#include "../source/runtime/anim_panel.h"
#include "../source/runtime/editor_quick_actions.h"

#include <limits>
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

// ===========================================================================
// Issue #73: the F10 panel's frame step and its layout
// ===========================================================================
//
// `source/runtime/anim_panel.h` holds the three pure parts of the panel: the
// frame-step arithmetic (a transcription of the engine's own clock rule), the
// shared layout the renderer draws and the mouse handler hit-tests, and the row
// text.  The ENGINE-BACKED half of the same feature - that a real
// `AnimPlayback` steps exactly this far - is asserted next to the playback
// tests, in `tests/test_anim_playback.cpp` (`AnimPlaybackStepTest.*`), which
// also pins `AnimStepTargetTick` against the ported engine clock itself.
//
// ORACLE.  One engine frame is `inst->time = inst->speed + inst->time` with
// `inst->speed = gTicksPerFrame * speed`:
//   BoneObj_AnimEventsCB 0x4D4B60  src/boneobj/boneobj.c:884-885
//   Anim_SetSpeed        0x4D6580  src/boneobj/anim.c:172-179
// and the end test / wrap / reverse branch the step reproduces are
// `BoneObj_AnimEventsCB`'s (boneobj.c:892-937), documented in
// docs/igi-re-parity/ANIMATION.md §5.

TEST(AnimFrameStep73, OneFrameIsTicksPerFrameTimesSpeed) {
    // 4800/30 = 160 ticks at the 30 fps the game installs
    // (BoneObj_SetFrameRate 0x4D2D30, src/lod/boneobj.c:86-90; src/game/game.c:449).
    EXPECT_FLOAT_EQ(igi::kAnimDefaultTicksPerFrame, 160.0f);
    EXPECT_FLOAT_EQ(igi::kAnimDefaultTicksPerFrame,
                    igi::kAnimTicksPerSecond / igi::kAnimDefaultFps);
    EXPECT_FLOAT_EQ(igi::AnimFrameStepTicks(1.0f, igi::kAnimDefaultTicksPerFrame), 160.0f);
    EXPECT_FLOAT_EQ(igi::AnimFrameStepTicks(2.0f, igi::kAnimDefaultTicksPerFrame), 320.0f);
    EXPECT_FLOAT_EQ(igi::AnimFrameStepTicks(0.5f, igi::kAnimDefaultTicksPerFrame), 80.0f);
    EXPECT_FLOAT_EQ(igi::AnimFrameStepTicks(8.0f, igi::kAnimDefaultTicksPerFrame), 1280.0f);
}

TEST(AnimFrameStep73, ForwardStepBelowTheLengthIsAPlainAddition) {
    // 6400 ticks = 40 frames at 160 ticks/frame.
    EXPECT_FLOAT_EQ(igi::AnimStepTargetTick(0.0f, 160.0f, 6400.0f, false, true), 160.0f);
    EXPECT_FLOAT_EQ(igi::AnimStepTargetTick(1920.0f, 160.0f, 6400.0f, true, true), 2080.0f);
    // One frame short of the length, in both loop modes: still below the end
    // test, so still a plain addition.
    EXPECT_FLOAT_EQ(igi::AnimStepTargetTick(6080.0f, 160.0f, 6400.0f, false, true), 6240.0f);
    EXPECT_FLOAT_EQ(igi::AnimStepTargetTick(6080.0f, 160.0f, 6400.0f, true, true), 6240.0f);
    // The very next frame lands exactly ON the length, which is already past the
    // end: the loop wraps by one duration and the one-shot clamps (asserted in
    // the two tests below).
    EXPECT_FLOAT_EQ(igi::AnimStepTargetTick(6240.0f, 160.0f, 6400.0f, true, true), 0.0f);
}

TEST(AnimFrameStep73, TheEndTestIsGreaterOrEqualAndAOneShotClamps) {
    // `!(inst->time < d->length)` is true AT the length, so landing exactly on it
    // is already past the end: a one-shot is clamped to the length ...
    EXPECT_FLOAT_EQ(igi::AnimStepTargetTick(6240.0f, 160.0f, 6400.0f, false, true), 6400.0f);
    // ... and a playhead already sitting there has nowhere to go, which is the
    // no-op `AnimPlayback::StepFrames` reports.
    EXPECT_FLOAT_EQ(igi::AnimStepTargetTick(6400.0f, 160.0f, 6400.0f, false, true), 6400.0f);
    // Overshooting past the end still clamps to the length, not past it.
    EXPECT_FLOAT_EQ(igi::AnimStepTargetTick(6300.0f, 1280.0f, 6400.0f, false, true), 6400.0f);
}

TEST(AnimFrameStep73, LoopWrapIsOneSubtractionNotAWhileLoop) {
    // `inst->time = inst->time - inst->desc->length` happens ONCE
    // (src/boneobj/boneobj.c:900-905): a step longer than the whole clip leaves
    // the playhead past the end after the single wrap, and that engine quirk is
    // reproduced rather than corrected.  160-tick clip, 1280-tick step (8x).
    EXPECT_FLOAT_EQ(igi::AnimStepTargetTick(0.0f, 1280.0f, 160.0f, true, true), 1120.0f);
    EXPECT_GT(igi::AnimStepTargetTick(0.0f, 1280.0f, 160.0f, true, true), 160.0f);
    // The ordinary wrap: 480-tick clip, one frame from the last frame.
    EXPECT_FLOAT_EQ(igi::AnimStepTargetTick(320.0f, 160.0f, 480.0f, true, true), 0.0f);
}

TEST(AnimFrameStep73, BackwardStepUsesTheEnginesReverseBranch) {
    // Reverse: `!(inst->time > 0.0f)` -> a one-shot clamps at 0 ...
    EXPECT_FLOAT_EQ(igi::AnimStepTargetTick(160.0f, 160.0f, 6400.0f, false, false), 0.0f);
    EXPECT_FLOAT_EQ(igi::AnimStepTargetTick(0.0f, 160.0f, 6400.0f, false, false), 0.0f);
    // ... and a loop adds ONE duration (`length + time`, boneobj.c:901-903).
    EXPECT_FLOAT_EQ(igi::AnimStepTargetTick(80.0f, 160.0f, 480.0f, true, false), 400.0f);
    // A reverse step that stays above 0 is a plain subtraction.
    EXPECT_FLOAT_EQ(igi::AnimStepTargetTick(6400.0f, 160.0f, 6400.0f, false, false), 6240.0f);
}

TEST(AnimFrameStep73, ANonFinitePlayheadTakesTheEndBranchLikeTheEngine) {
    // `!(NaN < length)` is true, so a NaN playhead is treated as past the end
    // (src/boneobj/boneobj.c:892-893) - reproduced, not sanitised.
    const float nan = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FLOAT_EQ(igi::AnimStepTargetTick(nan, 160.0f, 6400.0f, false, true), 6400.0f);
    EXPECT_FLOAT_EQ(igi::AnimStepTargetTick(nan, 160.0f, 6400.0f, false, false), 0.0f);
}

TEST(AnimFrameStep73, FrameIndexAndCountAreTheClipGrid) {
    // INFERRED display, not engine logic: the engine counts ticks only.  A frame
    // is 160 ticks, so the index floors and the count rounds to nearest.
    EXPECT_EQ(igi::AnimFrameIndexForTick(0.0f, 160.0f), 0);
    EXPECT_EQ(igi::AnimFrameIndexForTick(159.0f, 160.0f), 0);
    EXPECT_EQ(igi::AnimFrameIndexForTick(160.0f, 160.0f), 1);
    EXPECT_EQ(igi::AnimFrameIndexForTick(319.0f, 160.0f), 1);
    EXPECT_EQ(igi::AnimFrameIndexForTick(320.0f, 160.0f), 2);
    EXPECT_EQ(igi::AnimFrameCountForLength(6400.0f, 160.0f), 40);
    EXPECT_EQ(igi::AnimFrameCountForLength(4800.0f, 160.0f), 30);
    // A float length that is a hair off the grid must not truncate a frame away.
    EXPECT_EQ(igi::AnimFrameCountForLength(6399.6f, 160.0f), 40);
    // Degenerate inputs are reported as frame 0 rather than dividing by zero.
    EXPECT_EQ(igi::AnimFrameIndexForTick(100.0f, 0.0f), 0);
    EXPECT_EQ(igi::AnimFrameCountForLength(100.0f, 0.0f), 0);
}

TEST(AnimFrameStep73, EveryButtonIsInsideThePanelAndNoneOverlap) {
    for (int i = 0; i < igi::AnimPanel::kCount; ++i) {
        int x = 0, y = 0, w = 0, h = 0;
        igi::AnimPanel::GetButtonRect(i, x, y, w, h);
        EXPECT_GT(w, 0) << "button " << i;
        EXPECT_GT(h, 0) << "button " << i;
        EXPECT_GE(x, igi::AnimPanel::kLeft) << "button " << i;
        EXPECT_LE(x + w, igi::AnimPanel::kLeft + igi::AnimPanel::kWidth) << "button " << i;
        EXPECT_GE(y, igi::AnimPanel::kTop) << "button " << i;
        EXPECT_LE(y + h, igi::AnimPanel::PanelBottom()) << "button " << i;
        // The button row sits below the title band and above the first status row.
        EXPECT_GE(y, igi::AnimPanel::kTop + igi::AnimPanel::kTitleH) << "button " << i;
        EXPECT_LE(y + h, igi::AnimPanel::kRowsTop) << "button " << i;
        for (int j = i + 1; j < igi::AnimPanel::kCount; ++j) {
            int x2 = 0, y2 = 0, w2 = 0, h2 = 0;
            igi::AnimPanel::GetButtonRect(j, x2, y2, w2, h2);
            const bool disjoint = (x + w < x2) || (x2 + w2 < x) || (y + h < y2) || (y2 + h2 < y);
            EXPECT_TRUE(disjoint) << "buttons " << i << " and " << j << " overlap";
        }
    }
    // Out-of-range indices must not be handed a plausible rect.
    int x = 7, y = 7, w = 7, h = 7;
    igi::AnimPanel::GetButtonRect(-1, x, y, w, h);
    EXPECT_EQ(w, 0);
    EXPECT_EQ(h, 0);
    igi::AnimPanel::GetButtonRect(igi::AnimPanel::kCount, x, y, w, h);
    EXPECT_EQ(w, 0);
    EXPECT_EQ(h, 0);
}

TEST(AnimFrameStep73, ThePanelFitsTheSmallestViewportAndItsBandsDoNotOverlap) {
    // The panel is anchored top-left and its height is fixed, so it must fit a
    // small window (640x480) without its rows colliding with its own footer.
    EXPECT_LE(igi::AnimPanel::kLeft + igi::AnimPanel::kWidth, 640);
    EXPECT_LE(igi::AnimPanel::PanelBottom(), 480);
    EXPECT_LE(igi::AnimPanel::kRowsTop + igi::AnimPanel::kMaxRows * igi::AnimPanel::kRowH,
              igi::AnimPanel::PanelBottom());
    // `draw_text_sm` puts the baseline at the y it is given, so every baseline
    // must be inside its own band: a title that rendered into the button row, or
    // a row that rendered into the footer, would be a layout bug that no headless
    // test could see otherwise.
    EXPECT_GT(igi::AnimPanel::TitleBaseline(), igi::AnimPanel::kTop);
    EXPECT_LT(igi::AnimPanel::TitleBaseline(), igi::AnimPanel::kTop + igi::AnimPanel::kTitleH);
    EXPECT_GT(igi::AnimPanel::ButtonLabelBaseline(), igi::AnimPanel::kBtnTop);
    EXPECT_LT(igi::AnimPanel::ButtonLabelBaseline(),
              igi::AnimPanel::kBtnTop + igi::AnimPanel::kBtnH);
    for (int row = 0; row < igi::AnimPanel::kMaxRows; ++row) {
        const int top = igi::AnimPanel::kRowsTop + row * igi::AnimPanel::kRowH;
        EXPECT_GT(igi::AnimPanel::RowBaseline(row), top) << "row " << row;
        EXPECT_LT(igi::AnimPanel::RowBaseline(row), top + igi::AnimPanel::kRowH) << "row " << row;
        if (row > 0) {
            EXPECT_GT(igi::AnimPanel::RowBaseline(row), igi::AnimPanel::RowBaseline(row - 1));
        }
    }
    EXPECT_GT(igi::AnimPanel::FooterBaseline(),
              igi::AnimPanel::PanelBottom() - igi::AnimPanel::kFooterH);
    EXPECT_LE(igi::AnimPanel::FooterBaseline(), igi::AnimPanel::PanelBottom());
}

TEST(AnimFrameStep73, HitTestRoundTripsEveryButtonCentre) {
    for (int i = 0; i < igi::AnimPanel::kCount; ++i) {
        int x = 0, y = 0, w = 0, h = 0;
        igi::AnimPanel::GetButtonRect(i, x, y, w, h);
        const int cx = x + w / 2;
        const int cy = y + h / 2;
        EXPECT_TRUE(igi::AnimPanel::InPanel(cx, cy)) << "button " << i;
        EXPECT_EQ(igi::AnimPanel::HitTest(cx, cy), i) << "button " << i;
        // The corners are part of the button (`<=` on both edges), which is what
        // `TerrainPalette::HitTest` does too (source/renderer/renderer.h:89-96).
        EXPECT_EQ(igi::AnimPanel::HitTest(x, y), i) << "top-left corner of button " << i;
        EXPECT_EQ(igi::AnimPanel::HitTest(x + w, y + h), i) << "bottom-right corner of button " << i;
    }
    // Inside the panel but not on a button: the row area, and the title band.
    EXPECT_TRUE(igi::AnimPanel::InPanel(igi::AnimPanel::kLeft + 4,
                                        igi::AnimPanel::RowBaseline(0)));
    EXPECT_EQ(igi::AnimPanel::HitTest(igi::AnimPanel::kLeft + 4, igi::AnimPanel::RowBaseline(0)), -1);
    EXPECT_EQ(igi::AnimPanel::HitTest(igi::AnimPanel::kLeft + 4, igi::AnimPanel::kTop + 2), -1);
    // Outside the panel entirely - a click there must not be consumed.
    EXPECT_FALSE(igi::AnimPanel::InPanel(igi::AnimPanel::kLeft - 1, igi::AnimPanel::kTop));
    EXPECT_EQ(igi::AnimPanel::HitTest(igi::AnimPanel::kLeft - 1, igi::AnimPanel::kTop), -1);
    EXPECT_FALSE(igi::AnimPanel::InPanel(igi::AnimPanel::kLeft + 2, igi::AnimPanel::PanelBottom() + 1));
    EXPECT_EQ(igi::AnimPanel::HitTest(igi::AnimPanel::kLeft + 2,
                                      igi::AnimPanel::PanelBottom() + 1), -1);
    // A point past the panel's right edge, level with the buttons.
    EXPECT_EQ(igi::AnimPanel::HitTest(igi::AnimPanel::kLeft + igi::AnimPanel::kWidth + 1,
                                      igi::AnimPanel::kBtnTop + 2), -1);
}

TEST(AnimFrameStep73, ButtonLabelsAreDistinctAndOnlyPlayPauseIsStateDependent) {
    EXPECT_STREQ(igi::AnimPanel::ButtonLabel(igi::AnimPanel::kPlayPause, false), "Play");
    EXPECT_STREQ(igi::AnimPanel::ButtonLabel(igi::AnimPanel::kPlayPause, true), "Pause");
    for (int i = 0; i < igi::AnimPanel::kCount; ++i) {
        const std::string stopped = igi::AnimPanel::ButtonLabel(i, false);
        const std::string running = igi::AnimPanel::ButtonLabel(i, true);
        EXPECT_FALSE(stopped.empty()) << "button " << i;
        if (i != igi::AnimPanel::kPlayPause) {
            EXPECT_EQ(stopped, running) << "button " << i << " must not depend on play state";
        }
        for (int j = i + 1; j < igi::AnimPanel::kCount; ++j) {
            EXPECT_NE(stopped, std::string(igi::AnimPanel::ButtonLabel(j, false)))
                << "buttons " << i << " and " << j << " share a label";
        }
    }
    EXPECT_STREQ(igi::AnimPanel::ButtonLabel(-1, false), "");
    EXPECT_STREQ(igi::AnimPanel::ButtonLabel(igi::AnimPanel::kCount, false), "");
    // The footer is what tells the user which keys do the same thing.
    EXPECT_NE(std::string(igi::AnimPanel::kKeyHints).find("Ctrl+T"), std::string::npos);
    EXPECT_NE(std::string(igi::AnimPanel::kKeyHints).find("F12"), std::string::npos);
    EXPECT_NE(std::string(igi::AnimPanel::kKeyHints).find('.'), std::string::npos);
}

TEST(AnimFrameStep73, RowTextShowsTheFrameTheSpeedAndTheState) {
    igi::AnimPanelRow row;
    row.name = "Soldier";
    row.clip = "000_anim_002";
    row.selected = true;
    row.playing = true;
    row.tick = 1920.0f;      // frame 12 of 40 at 160 ticks/frame
    row.length = 6400.0f;
    row.speed = 1.0f;
    row.ticks_per_frame = igi::kAnimDefaultTicksPerFrame;
    EXPECT_EQ(igi::FormatAnimPanelRow(row), "> Soldier: 000_anim_002  f12/40  1x  playing");

    row.playing = false;
    row.looping = true;
    EXPECT_EQ(igi::FormatAnimPanelRow(row), "> Soldier: 000_anim_002  f12/40  1x  paused loop");

    // Not the selected object: no marker, so the row the buttons act on is
    // identifiable in a list of five.
    row.selected = false;
    row.looping = false;
    EXPECT_EQ(igi::FormatAnimPanelRow(row), "  Soldier: 000_anim_002  f12/40  1x  paused");
}

TEST(AnimFrameStep73, RowTextUsesTheSameSpeedLabelAsTheKeys) {
    // One ladder, one label: every rung must read in the panel exactly as the
    // F12 / Shift+F12 status message reads it, or the two would disagree about
    // the same clock.
    igi::AnimPanelRow row;
    row.name = "AI";
    row.clip = "clip";
    row.tick = 0.0f;
    row.length = 6400.0f;
    for (int i = 0; i < igi::kAnimTimerSpeedCount; ++i) {
        row.speed = igi::AnimTimerSpeedAt(i);
        const std::string text = igi::FormatAnimPanelRow(row);
        const std::string label = igi::AnimTimerSpeedLabel(row.speed);
        EXPECT_NE(text.find(label), std::string::npos)
            << "rung " << i << " (" << label << ") missing from: " << text;
        EXPECT_EQ(text.find(label + "  "), text.rfind(label))
            << "the speed label must appear once, as its own field: " << text;
    }
}

TEST(AnimFrameStep73, RowTextNamesAClipTheEngineRefused) {
    // A clip `BoneObj_Build`/`Anim_Play` refuses (no bones, or no position track
    // - boneobj.c:1014 dereferences `desc->pos->keys[count - 1]`) is named as
    // such: showing it at tick 0 would claim a frame that was never posed.
    igi::AnimPanelRow row;
    row.name = "Soldier";
    row.clip = "empty";
    row.engine_ready = false;
    row.tick = 0.0f;
    row.length = 0.0f;
    const std::string text = igi::FormatAnimPanelRow(row);
    EXPECT_NE(text.find("engine refused the clip"), std::string::npos) << text;
    EXPECT_EQ(text.find("f0/"), std::string::npos) << text;
}

TEST(AnimFrameStep73, TitleReportsTheTransportStateAndTheEngineFrameSize) {
    EXPECT_EQ(igi::FormatAnimPanelTitle(true, 2.0f, 320.0f),
              "Animation Preview - playing - 2x - step 320 ticks");
    EXPECT_EQ(igi::FormatAnimPanelTitle(false, 1.0f, 160.0f),
              "Animation Preview - paused - 1x - step 160 ticks");
    EXPECT_EQ(igi::FormatAnimPanelTitle(false, 0.125f, 20.0f),
              "Animation Preview - paused - 0.125x - step 20 ticks");
}

}  // namespace
