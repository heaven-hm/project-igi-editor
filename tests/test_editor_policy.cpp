#include <gtest/gtest.h>

#include "../source/runtime/auto_save_policy.h"
#include "../source/runtime/editor_hover_policy.h"
#include "../source/runtime/editor_history.h"
#include "../source/runtime/viewport_size_policy.h"

#include <vector>

// The viewport must be the size the window ACTUALLY has.  The regression these
// pin: App::Init's borderless-fullscreen path sizes the window to the monitor
// (1680x1050 on the reporting machine) with a direct SetWindowPos, and main.cpp
// then stamped the `-w`/`-h` default (1280x720) over the viewport.  The window
// stayed monitor-sized, only 1280x720 of it was ever painted, and the rest was
// black -- reported as "half screen and half black".
TEST(ViewportSizePolicyTest, RealClientSizeBeatsAStaleGlutSize) {
    EXPECT_EQ(igi::ResolveViewportExtent(/*client=*/1680, /*glut=*/1280,
                                        /*requested=*/1280),
              1680);
}

TEST(ViewportSizePolicyTest, GlutIsTheFallbackWhenTheClientRectIsUnavailable) {
    // GetClientRect cannot be called before the window exists / on non-Win32.
    EXPECT_EQ(igi::ResolveViewportExtent(/*client=*/0, /*glut=*/1680,
                                        /*requested=*/1280),
              1680);
}

TEST(ViewportSizePolicyTest, RequestedSizeIsOnlyASeed) {
    EXPECT_EQ(igi::ResolveViewportExtent(0, 0, 1280), 1280);
    EXPECT_EQ(igi::ResolveViewportExtent(-1, -1, 720), 720);
}

TEST(ViewportSizePolicyTest, NeverResolvesBelowOne) {
    // ApplyViewportSize derives fovx_ from width/height, so a zero would divide
    // by zero; a window reported as 0x0 (minimized) must not reach it.
    EXPECT_EQ(igi::ResolveViewportExtent(0, 0, 0), 1);
    EXPECT_EQ(igi::ResolveViewportExtent(-5, -5, -5), 1);
}

TEST(AutoSavePolicyTest, TimerRequiresEveryEligibilityCondition) {
    for (bool editor_mode : {false, true}) {
        for (bool enabled : {false, true}) {
            for (bool paused : {false, true}) {
                for (int level : {-1, 0, 1, 14}) {
                    const bool expected = editor_mode && enabled && !paused && level > 0;
                    EXPECT_EQ(igi::ShouldRunAutoSave(editor_mode, enabled, paused, level), expected)
                        << "editor=" << editor_mode << " enabled=" << enabled
                        << " paused=" << paused << " level=" << level;
                }
            }
        }
    }
}

TEST(AutoSavePolicyTest, ExternalLaunchRequiresEnabledEditorMode) {
    EXPECT_FALSE(igi::ShouldSaveBeforeExternalGameLaunch(true, false));
    EXPECT_TRUE(igi::ShouldSaveBeforeExternalGameLaunch(true, true));
    EXPECT_FALSE(igi::ShouldSaveBeforeExternalGameLaunch(false, true));
}

TEST(AutoSavePolicyTest, GracefulExitRequiresEnabledEditorAndValidLevel) {
    EXPECT_TRUE(igi::ShouldSaveBeforeEditorExit(true, true, 1));
    EXPECT_TRUE(igi::ShouldSaveBeforeEditorExit(true, true, 14));
    EXPECT_FALSE(igi::ShouldSaveBeforeEditorExit(true, false, 1));
    EXPECT_FALSE(igi::ShouldSaveBeforeEditorExit(false, true, 1));
    EXPECT_FALSE(igi::ShouldSaveBeforeEditorExit(true, true, 0));
    EXPECT_FALSE(igi::ShouldSaveBeforeEditorExit(true, true, -1));
}

TEST(EditorHoverPolicyTest, SkipsExpensiveScenePickingWhileObjectDragIsActive) {
    EXPECT_FALSE(igi::ShouldPickSceneHover(
        /*pointer_changed=*/true,
        /*left_button_down=*/true));

    EXPECT_TRUE(igi::ShouldPickSceneHover(
        /*pointer_changed=*/true,
        /*left_button_down=*/false));

    EXPECT_FALSE(igi::ShouldPickSceneHover(
        /*pointer_changed=*/false,
        /*left_button_down=*/false));
}

TEST(EditorHoverPolicyTest, ThrottlesRepeatedScenePickingDuringMouseMotion) {
    EXPECT_FALSE(igi::ShouldPickSceneHover(true, false, 100, 80, 33));
    EXPECT_TRUE(igi::ShouldPickSceneHover(true, false, 113, 80, 33));
    EXPECT_FALSE(igi::ShouldPickSceneHover(true, false, 112, 80, 33));
    EXPECT_TRUE(igi::ShouldPickSceneHover(true, false, 0, -1, 33));
}

TEST(EditorHoverPolicyTest, SkipsScenePickingWhileCameraIsNavigating) {
    EXPECT_FALSE(igi::ShouldPickSceneHover(true, false, true));
    EXPECT_TRUE(igi::ShouldPickSceneHover(true, false, false));
    EXPECT_FALSE(igi::ShouldPickSceneHover(true, false, 200, 0, 33, true));
}

TEST(EditorHoverPolicyTest, ResolvesMissingSceneModelsOnlyOnDemand) {
    EXPECT_TRUE(igi::ShouldResolveMissingSceneModel(false, false, false));
    EXPECT_TRUE(igi::ShouldResolveMissingSceneModel(true, true, false));
    EXPECT_TRUE(igi::ShouldResolveMissingSceneModel(true, false, true));
    EXPECT_FALSE(igi::ShouldResolveMissingSceneModel(true, false, false));
}

TEST(EditorHistoryTest, PushUndoInvalidatesRedoAndKeepsLatestEntries) {
    std::vector<int> undo{1, 2};
    std::vector<int> redo{9};
    igi::PushEditorUndo(undo, redo, 3, 3);
    EXPECT_EQ(undo, (std::vector<int>{1, 2, 3}));
    EXPECT_TRUE(redo.empty());

    igi::PushEditorUndo(undo, redo, 4, 3);
    EXPECT_EQ(undo, (std::vector<int>{2, 3, 4}));
}

TEST(EditorHistoryTest, ZeroCapacityDropsNewUndoState) {
    std::vector<int> undo{1};
    std::vector<int> redo{2};
    igi::PushEditorUndo(undo, redo, 3, 0);
    EXPECT_TRUE(undo.empty());
    EXPECT_TRUE(redo.empty());
}

TEST(EditorHistoryTest, UndoAndRedoMoveCurrentStateBetweenStacks) {
    std::vector<int> undo{10};
    std::vector<int> redo;
    int restored = 0;
    ASSERT_TRUE(igi::MoveEditorUndoToRedo(undo, redo, 20, restored));
    EXPECT_EQ(restored, 10);
    EXPECT_TRUE(undo.empty());
    EXPECT_EQ(redo, (std::vector<int>{20}));

    ASSERT_TRUE(igi::MoveEditorRedoToUndo(undo, redo, restored, restored));
    EXPECT_EQ(restored, 20);
    EXPECT_EQ(undo, (std::vector<int>{10}));
    EXPECT_TRUE(redo.empty());
}

TEST(EditorHistoryTest, EmptyUndoAndRedoDoNotChangeTheRestoreTarget) {
    std::vector<int> undo;
    std::vector<int> redo;
    int restored = 17;
    EXPECT_FALSE(igi::MoveEditorUndoToRedo(undo, redo, 20, restored));
    EXPECT_FALSE(igi::MoveEditorRedoToUndo(undo, redo, 20, restored));
    EXPECT_EQ(restored, 17);
}

TEST(EditorHistoryTest, ClearDropsHistoryAtDocumentBoundary) {
    std::vector<int> undo{1, 2};
    std::vector<int> redo{3};
    igi::ClearEditorHistory(undo, redo);
    EXPECT_TRUE(undo.empty());
    EXPECT_TRUE(redo.empty());
}
