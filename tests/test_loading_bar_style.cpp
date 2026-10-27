/******************************************************************************
 * @file    test_loading_bar_style.cpp
 * @brief   The retail loading screen's progress bar: geometry, colours, fill.
 *
 * Every assertion here is a value or a rule from IGI-RE, the verified C
 * decompilation of retail IGI.exe, cited per test:
 *
 *   src/loadscreen/loadscreen.c:63-96    LoadingScreen_New  HOOK 0x48A440
 *       :74  ls->x = (mode->width  - 640) / 2 + 40
 *       :75  ls->y = (mode->height - 480) / 2 + 440
 *       :76  OffscreenBuffer_New(&ls->bar, 560, 10)
 *       :77-78 every bar pixel = 0x7F00 (the track)
 *       :85-86 the picture is drawn at ((w - 640) / 2, (h - 480) / 2)
 *   src/loadscreen/loadscreen.c:110-124  LoadingScreen_Step HOOK 0x48A620
 *       :114 ls->done += steps
 *       :115 filled = ls->bar.w * ls->done / ls->total
 *       :119-121 the first `filled` pixels of each row = 0xFF00 (the fill)
 *   src/d3d/direct3d.c:1092-1156         Direct3D_Blit HOOK 0x494C50
 *       :1111-1114 the 32-bit path takes the low 24 bits as 8-bit R/G/B and
 *                  ORs 0xFF000000 for opaque alpha
 *       :1115-1121 the 16-bit path maps bits 23-16 / 15-8 / 7-0 to R / G / B
 *   src/dxinit/dxinit.c:39-48            OffscreenBuffer_New HOOK 0x498380
 *       allocates width * height * 4 bytes, i.e. 32 bits per pixel
 *   src/res/qvertex.c:189                gColorScale DATA 0x534AB0 = 0.0078125f
 *   src/math/draw.c:12                   COLOR_SCALE = 1/128
 *
 * This suite is deliberately host-portable: it includes only the header under
 * test (no Windows headers, no GL, no logger), so it runs in both the host lane
 * and the Windows igi_tests binary.
 *
 * What is PROVEN here: the numeric geometry, the colour domain and the fill
 * rule.  What is NOT proven: the rendered pixels -- the editor needs OpenGL 4.1
 * and this host offers 2.1, so the draw path cannot be executed here.
 *****************************************************************************/

#include <gtest/gtest.h>

#include "runtime/loading_bar_style.h"

namespace {

using igi::BuildLoadingBarLayout;
using igi::LoadingBarFillWidth;
using igi::LoadingBarFillWidthForPercent;
using igi::LoadingBarLayout;

// ─── 1. the bar's authored size (loadscreen.c:76) ───────────────────────────

TEST(LoadingBarStyle, BarIs560By10) {
    // OffscreenBuffer_New(&ls->bar, 560, 10);   src/loadscreen/loadscreen.c:76
    EXPECT_EQ(igi::kRetailLoadingBarWidth, 560);
    EXPECT_EQ(igi::kRetailLoadingBarHeight, 10);
}

TEST(LoadingBarStyle, BandIs640By480) {
    // The picture and the bar are both positioned against a 640x480 band:
    //   :74/:85 (width - 640) / 2      :75/:86 (height - 480) / 2
    EXPECT_EQ(igi::kRetailLoadingBandWidth, 640);
    EXPECT_EQ(igi::kRetailLoadingBandHeight, 480);
}

TEST(LoadingBarStyle, BarIsHorizontallyCentredInTheBand) {
    // 40 (inset) + 560 (bar) + 40 (margin) == 640, so the bar sits centred with
    // equal margins -- the retail value is insetX = 40 at :74.
    EXPECT_EQ(igi::kRetailLoadingBarInsetX, 40);
    EXPECT_EQ(igi::kRetailLoadingBarMarginX, 40);
    EXPECT_EQ(igi::kRetailLoadingBarInsetX + igi::kRetailLoadingBarWidth +
                  igi::kRetailLoadingBarMarginX,
              igi::kRetailLoadingBandWidth);
}

TEST(LoadingBarStyle, BarSitsThirtyAboveTheBandBottom) {
    // 440 (top offset) + 10 (height) leaves 30 px of band below the bar.
    EXPECT_EQ(igi::kRetailLoadingBarOffsetY, 440);
    EXPECT_EQ(igi::kRetailLoadingBarBottomGap, 30);
    EXPECT_EQ(igi::kRetailLoadingBarOffsetY + igi::kRetailLoadingBarHeight +
                  igi::kRetailLoadingBarBottomGap,
              igi::kRetailLoadingBandHeight);
}

// ─── 2. the layout at the retail resolution (loadscreen.c:74-76) ────────────

TEST(LoadingBarStyle, LayoutAt640x480IsExactlyTheRetailValues) {
    // At 640x480 the band origin is (0, 0), so the bar is at (40, 440).
    const LoadingBarLayout bar = BuildLoadingBarLayout(640, 480);
    EXPECT_EQ(bar.x, 40);    // :74  (640 - 640) / 2 + 40
    EXPECT_EQ(bar.y, 440);   // :75  (480 - 480) / 2 + 440
    EXPECT_EQ(bar.w, 560);   // :76
    EXPECT_EQ(bar.h, 10);    // :76
}

TEST(LoadingBarStyle, LayoutTracksTheCentredBandAt1920x1080) {
    // The bar is a fixed pixel size at every resolution: only the band origin
    // moves with the screen.
    //   x = (1920 - 640) / 2 + 40  = 640 + 40 = 680
    //   y = (1080 - 480) / 2 + 440 = 300 + 440 = 740
    const LoadingBarLayout bar = BuildLoadingBarLayout(1920, 1080);
    EXPECT_EQ(bar.x, 680);
    EXPECT_EQ(bar.y, 740);
    EXPECT_EQ(bar.w, 560);
    EXPECT_EQ(bar.h, 10);
}

TEST(LoadingBarStyle, LayoutTracksTheCentredBandAt1280x720) {
    //   x = (1280 - 640) / 2 + 40  = 320 + 40 = 360
    //   y = (720 - 480) / 2 + 440  = 120 + 440 = 560
    const LoadingBarLayout bar = BuildLoadingBarLayout(1280, 720);
    EXPECT_EQ(bar.x, 360);
    EXPECT_EQ(bar.y, 560);
}

TEST(LoadingBarStyle, LayoutDoesNotScaleTheBarWithTheViewport) {
    // Retail never enlarges the bar: 560x10 at 640x480 and at 4K alike.
    const LoadingBarLayout small = BuildLoadingBarLayout(640, 480);
    const LoadingBarLayout large = BuildLoadingBarLayout(3840, 2160);
    EXPECT_EQ(small.w, large.w);
    EXPECT_EQ(small.h, large.h);
}

TEST(LoadingBarStyle, LayoutKeepsTheBarInsideTheViewport) {
    // The bar must never be clipped at a viewport at or above the band size.
    for (int w : {640, 800, 1280, 1920, 2560}) {
        for (int h : {480, 600, 720, 1080, 1440}) {
            const LoadingBarLayout bar = BuildLoadingBarLayout(w, h);
            EXPECT_GE(bar.x, 0) << w << "x" << h;
            EXPECT_GE(bar.y, 0) << w << "x" << h;
            EXPECT_LE(bar.x + bar.w, w) << w << "x" << h;
            EXPECT_LE(bar.y + bar.h, h) << w << "x" << h;
        }
    }
}

// ─── 3. colours, and the domain they are in (loadscreen.c:77-78, :120) ──────

TEST(LoadingBarStyle, TrackAndFillAreTheRetailPixelWords) {
    // The bar's pixels are 0x7F00 (track) and 0xFF00 (fill).  Only the low 24
    // bits are colour: Direct3D_Blit reads them as 8-bit R/G/B
    // (src/d3d/direct3d.c:1111-1121), so the green channel is 0x7F and 0xFF.
    EXPECT_EQ(igi::kRetailLoadingTrackGreen8, 0x7F);
    EXPECT_EQ(igi::kRetailLoadingFillGreen8, 0xFF);
    EXPECT_EQ(igi::kRetailLoadingChannelMax8, 0xFF);

    // 0x7F00 = R 0x00, G 0x7F, B 0x00 and 0xFF00 = R 0x00, G 0xFF, B 0x00.
    const int trackWord = 0x7F00;
    const int fillWord = 0xFF00;
    EXPECT_EQ((trackWord >> 16) & 0xFF, 0x00);  // R
    EXPECT_EQ((trackWord >> 8) & 0xFF, 0x7F);   // G
    EXPECT_EQ(trackWord & 0xFF, 0x00);          // B
    EXPECT_EQ((fillWord >> 16) & 0xFF, 0x00);   // R
    EXPECT_EQ((fillWord >> 8) & 0xFF, 0xFF);    // G
    EXPECT_EQ(fillWord & 0xFF, 0x00);           // B
}

TEST(LoadingBarStyle, ColoursAreEightBitPerChannelNotOneOver128) {
    // The loading bar is raw pixel data blitted straight to the surface, so it
    // skips the menus' 1/128 QVertex scale (gColorScale DATA 0x534AB0,
    // src/res/qvertex.c:189; COLOR_SCALE, src/math/draw.c:12).  The conversion
    // to the float domain is 8-bit / 255:
    EXPECT_FLOAT_EQ(igi::kRetailLoadingFillGreen, 1.0f);              // 255 / 255
    EXPECT_FLOAT_EQ(igi::kRetailLoadingTrackGreen, 127.0f / 255.0f);  // 0.498039f
    // ... and explicitly NOT the /128 result, which is the bug this guards:
    // 127 / 128 = 0.992188 -- within 1% of full brightness, so the track would
    // be indistinguishable from the fill.  The two must be far apart.
    EXPECT_LT(igi::kRetailLoadingTrackGreen, 0.5f);
    EXPECT_GT(igi::kRetailLoadingTrackGreen, 0.49f);
    EXPECT_GT((127.0f / 128.0f) - igi::kRetailLoadingTrackGreen, 0.49f);
    // The two tones must differ enough to read as a progress bar.
    EXPECT_GT(igi::kRetailLoadingFillGreen - igi::kRetailLoadingTrackGreen, 0.4f);
}

TEST(LoadingBarStyle, BothTonesArePureGreenAndOpaque) {
    // 0x7F00 / 0xFF00 have zero red and zero blue; the alpha byte is supplied
    // by the blit (0xFF000000 at src/d3d/direct3d.c:1114), so the bar is opaque.
    EXPECT_FLOAT_EQ(igi::kRetailLoadingBarRed, 0.0f);
    EXPECT_FLOAT_EQ(igi::kRetailLoadingBarBlue, 0.0f);
}

TEST(LoadingBarStyle, FillIsBrighterThanTrack) {
    // 0xFF (255) vs 0x7F (127): the fill is exactly twice the track's green.
    EXPECT_EQ(igi::kRetailLoadingFillGreen8, 2 * igi::kRetailLoadingTrackGreen8 + 1);
}

// ─── 4. the fill rule (loadscreen.c:114-115) ────────────────────────────────

TEST(LoadingBarStyle, FillIsZeroAtTheStart) {
    // ls->done starts at 0 (src/loadscreen/loadscreen.c:72), and LoadingScreen_New
    // presents the bar once with steps 0 (:93), so nothing is filled.
    EXPECT_EQ(LoadingBarFillWidth(560, 0, 9), 0);
    EXPECT_EQ(LoadingBarFillWidth(560, 0, 100), 0);
}

TEST(LoadingBarStyle, FillIsTheWholeBarWhenDone) {
    EXPECT_EQ(LoadingBarFillWidth(560, 9, 9), 560);
    EXPECT_EQ(LoadingBarFillWidth(560, 100, 100), 560);
    EXPECT_EQ(LoadingBarFillWidthForPercent(560, 100), 560);
}

TEST(LoadingBarStyle, FillIsIntegerDoneOverTotalTruncated) {
    // filled = ls->bar.w * ls->done / ls->total;   :115
    // 560 * 1 / 9 = 62 (truncated from 62.2)
    EXPECT_EQ(LoadingBarFillWidth(560, 1, 9), 62);
    EXPECT_EQ(LoadingBarFillWidth(560, 2, 9), 124);   // 560*2/9 = 124.4
    EXPECT_EQ(LoadingBarFillWidth(560, 3, 9), 186);   // 560*3/9 = 186.6
    EXPECT_EQ(LoadingBarFillWidth(560, 4, 9), 248);   // 560*4/9 = 248.8
    EXPECT_EQ(LoadingBarFillWidth(560, 1, 3), 186);   // 560/3   = 186.6
}

TEST(LoadingBarStyle, FillIsMonotonicAcrossTheNineLevelSteps) {
    // A full level load uses 9 steps: LoadingScreen_New(9), src/game/game.c:399,
    // advanced by nine LoadingScreen_Step(loading, 1) calls at :415-468.
    int previous = 0;
    for (int done = 0; done <= 9; ++done) {
        const int filled = LoadingBarFillWidth(560, done, 9);
        EXPECT_GE(filled, previous) << "done=" << done;
        EXPECT_LE(filled, 560) << "done=" << done;
        previous = filled;
    }
    EXPECT_EQ(previous, 560);
}

TEST(LoadingBarStyle, FillCoversThePercentRange) {
    // The editor drives the bar with a percentage; the rule is unchanged.
    EXPECT_EQ(LoadingBarFillWidthForPercent(560, 0), 0);
    EXPECT_EQ(LoadingBarFillWidthForPercent(560, 10), 56);
    EXPECT_EQ(LoadingBarFillWidthForPercent(560, 25), 140);
    EXPECT_EQ(LoadingBarFillWidthForPercent(560, 50), 280);
    EXPECT_EQ(LoadingBarFillWidthForPercent(560, 75), 420);
    EXPECT_EQ(LoadingBarFillWidthForPercent(560, 100), 560);
}

TEST(LoadingBarStyle, FillNeverExceedsTheBar) {
    // The retail write loop is clamped by construction (:119-121); the copy must
    // not run past the row either.
    EXPECT_EQ(LoadingBarFillWidth(560, 10, 9), 560);
    EXPECT_EQ(LoadingBarFillWidth(560, 1000, 1), 560);
    EXPECT_EQ(LoadingBarFillWidth(560, 5, 1000), 2);  // 2.8 truncated
}

TEST(LoadingBarStyle, FillHandlesDegenerateInputs) {
    EXPECT_EQ(LoadingBarFillWidth(560, 5, 0), 0);    // total 0: no division
    EXPECT_EQ(LoadingBarFillWidth(560, -3, 9), 0);   // done below zero
    EXPECT_EQ(LoadingBarFillWidth(0, 5, 9), 0);      // zero-width bar
    EXPECT_EQ(LoadingBarFillWidth(-10, 5, 9), 0);
}

TEST(LoadingBarStyle, FillStaysCorrectForLargeBarWidths) {
    // Long long arithmetic in the copy means no overflow at wide scales.
    EXPECT_EQ(LoadingBarFillWidth(4096, 1, 2), 2048);
    EXPECT_EQ(LoadingBarFillWidth(100000, 50000, 100000), 50000);
}

}  // namespace

