#pragma once

/******************************************************************************
 * @file    source/runtime/loading_bar_style.h
 * @brief   The retail loading screen's progress bar: its geometry, its colours,
 *          and the rule that drives the fill, exactly as the engine draws them.
 *
 * Everything here comes from ONE place: the engine's loading-screen code in
 * IGI-RE, the verified C decompilation of retail IGI.exe.  Each constant names
 * the function, the HOOK address and the file:line that carries it.
 *
 *   src/loadscreen/loadscreen.c:18-23   struct LoadingScreen { total, done,
 *                                       x, y, Bitmap bar; }
 *   src/loadscreen/loadscreen.c:63-96   LoadingScreen_New  HOOK 0x48A440
 *   src/loadscreen/loadscreen.c:110-124 LoadingScreen_Step HOOK 0x48A620
 *   src/loadscreen/loadscreen.c:98-108  LoadingScreen_Delete HOOK 0x48A5D0
 *   src/d3d/direct3d.c:1092-1156        Direct3D_Blit HOOK 0x494C50 (the pixel
 *                                       format the bar's words live in)
 *   src/dxinit/dxinit.c:39-48           OffscreenBuffer_New HOOK 0x498380
 *   src/transctx/offscreen.c:22         "32 bit pixels in memory"
 *
 * ── what the retail bar IS ──────────────────────────────────────────────────
 * The loading screen is a 640x480 picture (`menusystem/loading.pic`, or
 * `loading_us.pic` on a USA build) with a single flat two-tone green bar under
 * it.  There is NO border, NO panel, NO gradient, NO alpha and NO text:
 *
 *   - the bar is one 560x10 bitmap (:76), every pixel of which is set to
 *     0x7F00 at construction (:77-78) and then overwritten left-to-right with
 *     0xFF00 as loading advances (:119-121).  So the "track" is the mid green
 *     and the "fill" is the full green;
 *   - it is drawn by a plain blit at (:122), not through the QVertex colour
 *     pipeline, so it does NOT use the menus' 1/128 colour domain (see §2);
 *   - `LoadingScreen_New` loads a sprite for the picture (`Text_Load` HOOK
 *     0x4B6DB0, src/res/text.c:12-35, which allocates a `Sprite` and wraps it
 *     in a `Text`, src/res/restypes.h:58-66) and draws it with `Text_Draw`
 *     (:83-87).  It never loads a `.fnt`, so **the retail loading bar has no
 *     font at all** -- there is no engine oracle for text on it;
 *   - it does NOT call `MenuScreen_DrawFrame` (HOOK 0x421CD0,
 *     src/menuscreen/menuscreen.c:168-229).  The pause menu's green frame, its
 *     green->black wash and its 0.66-alpha black fill are a DIFFERENT widget
 *     and must not be reused here.
 *
 * ── how the editor may use it ───────────────────────────────────────────────
 * The editor's own strings stay the editor's (the retail bar draws none), so
 * `App::DrawProgressOverlay` keeps its title/stage text and only takes the
 * game's bar geometry, colours and fill rule from this header.
 *
 * The bar is NOT resolution-scaled in retail: the engine positions it in
 * fixed-pixel offsets from the centre of a 640x480 band
 * (cf. `SCREEN_X`/`SCREEN_Y`, src/menuscreen/menuscreen.h:141-142, which are
 * `(width >> 1) - 320 + x` / `(height >> 1) - 240 + y` at every resolution).
 * So neither does the editor scale it.
 *****************************************************************************/

namespace igi {

// ─────────────────────────────────────────────────────────────────────────────
// 1. Geometry -- LoadingScreen_New (HOOK 0x48A440, src/loadscreen/loadscreen.c:63-96)
//
// The bar is positioned relative to the 640x480 loading picture:
//     ls->x = (mode->width  - 640) / 2 + 40;      // :74
//     ls->y = (mode->height - 480) / 2 + 440;     // :75
//     OffscreenBuffer_New(&ls->bar, 560, 10);     // :76
// and the picture itself is drawn at
//     pic->x = (mode->width  - 640) / 2;          // :85
//     pic->y = (mode->height - 480) / 2;          // :86
// so the bar's offsets are measured from a screen-centred 640x480 band.
// ─────────────────────────────────────────────────────────────────────────────

constexpr int kRetailLoadingBandWidth = 640;   // :74, :85
constexpr int kRetailLoadingBandHeight = 480;  // :75, :86
constexpr int kRetailLoadingBarWidth = 560;    // :76
constexpr int kRetailLoadingBarHeight = 10;    // :76
constexpr int kRetailLoadingBarInsetX = 40;    // :74
constexpr int kRetailLoadingBarOffsetY = 440;  // :75

// 40 + 560 + 40 == 640, so the bar is horizontally centred in the band (and
// therefore on screen) with equal side margins.
constexpr int kRetailLoadingBarMarginX = kRetailLoadingBandWidth - kRetailLoadingBarWidth -
                                        kRetailLoadingBarInsetX;  // 40
static_assert(kRetailLoadingBarInsetX == kRetailLoadingBarMarginX,
              "retail: the loading bar has equal left/right margins in the 640x480 band");

// 480 - 440 - 10 == 30, so the bar's bottom edge sits 30 px above the band's
// bottom edge.
constexpr int kRetailLoadingBarBottomGap = kRetailLoadingBandHeight -
                                           kRetailLoadingBarOffsetY -
                                           kRetailLoadingBarHeight;  // 30

// ─────────────────────────────────────────────────────────────────────────────
// 2. Colours -- and the domain they are in
//
// The bar's pixels are written as 32-bit words whose low 24 bits are literal
// 8-bit R, G, B channels:
//
//   * track, at construction (:77-78):
//         for (i = 0; i < 560 * 10; i++)
//             ls->bar.pixels[i] = 0x7F00;
//   * fill, as loading advances (:119-121):
//         for (x = 0; x < filled; x++)
//             p[x] = 0xFF00;
//
// `Bitmap::pixels` is `uint32_t *` (:11-15) and `OffscreenBuffer_New`
// (HOOK 0x498380, src/dxinit/dxinit.c:39-48) allocates `width * height * 4`
// bytes, i.e. 32 bits per pixel; src/transctx/offscreen.c:22 notes the same
// ("32 bit pixels in memory").
//
// The blit that puts those words on screen is `Direct3D_Blit` (HOOK 0x494C50,
// src/d3d/direct3d.c:1092-1156), and it consumes them as 8-bit channels:
//   * 32-bit target, masks R 0xFF0000 / G 0xFF00 / B 0xFF (:1111-1114):
//         ((unsigned *)buf)[i] = a->pixels[i] | 0xFF000000;
//     -- the low 24 bits are taken verbatim, the top byte is forced opaque.
//   * 16-bit RGB565 target, masks R 0xF800 / G 0x7E0 / B 0x1F (:1115-1121):
//         (((((p >> 19) & 0x1F) << 6) + ((p >> 10) & 0x3F)) << 5) + ((p >> 3) & 0x1F)
//     -- bits 23-16 become R, 15-8 become G, 7-0 become B.
//
// So 0x7F00 is R 0x00, G 0x7F, B 0x00 and 0xFF00 is R 0x00, G 0xFF, B 0x00.
// **These are 8-bit channel values, so the conversion to the float domain is a
// divide by 255 -- NOT the 1/128 scale the menus use.**  The 1/128 domain
// (`gColorScale` DATA 0x534AB0 = 0.0078125f, src/res/qvertex.c:189;
// COLOR_SCALE = 1/128, src/math/draw.c:12) applies to QVertex colours that the
// engine multiplies by the scale as it queues them; the loading bar never
// enters that pipeline -- it is raw pixel data blitted straight to the surface.
// Dividing 127 by 128 instead of 255 would render the track at 0.992 instead of
// 0.498, i.e. almost as bright as the fill and with the fill's contrast gone.
// ─────────────────────────────────────────────────────────────────────────────

constexpr int kRetailLoadingTrackGreen8 = 0x7F;  // 127, from 0x7F00 at :77-78
constexpr int kRetailLoadingFillGreen8 = 0xFF;   // 255, from 0xFF00 at :120
constexpr int kRetailLoadingChannelMax8 = 0xFF;  // 8-bit channel full scale

// The float-domain equivalents: pure green at two brightnesses.
constexpr float kRetailLoadingTrackGreen = 127.0f / 255.0f;  // 0.498039f
constexpr float kRetailLoadingFillGreen = 255.0f / 255.0f;   // 1.0f

// Both tones are pure green: 0x7F00 and 0xFF00 have zero red and zero blue, and
// the alpha byte is never set by the loading screen -- `Direct3D_Blit` supplies
// 0xFF itself (:1114), so the bar is fully opaque.
constexpr float kRetailLoadingBarRed = 0.0f;
constexpr float kRetailLoadingBarBlue = 0.0f;

// ─────────────────────────────────────────────────────────────────────────────
// 3. The fill rule -- LoadingScreen_Step (HOOK 0x48A620,
//    src/loadscreen/loadscreen.c:110-124)
//
//     ls->done += steps;                          // :114
//     filled = ls->bar.w * ls->done / ls->total;  // :115
//
// `ls->total` is the `steps` argument given to `LoadingScreen_New` (:73) and
// `ls->done` starts at 0 (:72), so the bar is driven by 0..N *steps*, not by a
// 0..1 fraction: the fractional width is integer `done / total` truncated at
// :115.  The engine picks 9 steps for a full level load (`LoadingScreen_New(9)`,
// src/game/game.c:399, advanced by 9 `LoadingScreen_Step(loading, 1)` calls at
// :415-468), 5 for a level restart (`LoadingScreen_New(5)`, src/game/game.c:1077)
// and 1 for a mission transition (`LoadingScreen_New(1)`, src/game/game.c:576).
//
// `done` may exceed `total` in principle; the loop at :119-121 clamps by
// construction (it only writes `x < filled` over a 560-wide row), so mirror the
// truncation but also clamp to the bar width for a defensive copy.
// ─────────────────────────────────────────────────────────────────────────────

constexpr int LoadingBarFillWidth(int barWidth, int done, int total) noexcept {
    if (barWidth <= 0 || total <= 0 || done <= 0) return 0;
    const int filled = static_cast<int>((static_cast<long long>(barWidth) * done) /
                                        total);  // :115
    if (filled > barWidth) return barWidth;
    return filled;
}

// The editor reports progress as a percentage, which is the same rule with
// total == 100.  Kept separate so call sites read the way the retail code does.
constexpr int LoadingBarFillWidthForPercent(int barWidth, int percent) noexcept {
    return LoadingBarFillWidth(barWidth, percent, 100);
}

// ─────────────────────────────────────────────────────────────────────────────
// 4. Layout
//
// The retail origin: a 640x480 band centred on screen, then the bar's own
// offsets inside it (:74-76, :85-86).  This is identical at every resolution --
// the bar does not grow with the screen.
// ─────────────────────────────────────────────────────────────────────────────

struct LoadingBarLayout {
    int x = 0;  // left, in screen pixels (top-left origin, as the engine's blit)
    int y = 0;  // top
    int w = 0;
    int h = 0;
};

constexpr LoadingBarLayout BuildLoadingBarLayout(int viewportWidth,
                                                int viewportHeight) noexcept {
    const int bandX = (viewportWidth - kRetailLoadingBandWidth) / 2;    // :74, :85
    const int bandY = (viewportHeight - kRetailLoadingBandHeight) / 2;  // :75, :86
    LoadingBarLayout bar;
    bar.x = bandX + kRetailLoadingBarInsetX;   // :74
    bar.y = bandY + kRetailLoadingBarOffsetY;  // :75
    bar.w = kRetailLoadingBarWidth;            // :76
    bar.h = kRetailLoadingBarHeight;           // :76
    return bar;
}

// ─────────────────────────────────────────────────────────────────────────────
// 5. The editor's own progress ladder
//
// Retail advances the bar in DISCRETE STEPS, not with a timer: a full level load
// is `LoadingScreen_New(9)` (src/game/game.c:399) advanced by nine
// `LoadingScreen_Step(loading, 1)` calls (:415-468); a restart uses 5 (:1077) and
// a mission transition 1 (:576).  Each step rewrites the fill from the same
// integer rule (:115), so the bar moves in jumps between real work.
//
// The editor's loads are synchronous as well, so it advances the bar the same
// way: ONE presentation per phase of its own work, never a timer.  The ladders
// below are those phases, and every `percent` is the `done` value of the
// percentage form of the retail rule (`LoadingBarFillWidthForPercent`).
//
// The rule the ladders exist to keep: no long editor operation may run with the
// window frozen and no bar.  The three operations that can take seconds are a
// level load (source/app_level.cpp), a level save (including the recalculated
// lightmaps written back into the game's `lightmaps.res`) and a reset's restore
// from the pristine level backup (source/app_input.cpp).  Model imports and the
// lightmap bakes already report through `App::DrawProgressOverlay`.
//
// A phase that turns out to be fast is still reported: the alternative is a bar
// that skips a value, and a skipped value is invisible while a missing phase is
// not.
// ─────────────────────────────────────────────────────────────────────────────

struct LoadingBarStage {
    int percent = 0;        // 0..100, i.e. `done` with `total == 100`
    const char* stage = "";  // the stage text of the editor's own overlay label
};

// A level load, in order.  `preparing` and `backing up level` cover
// `App::LoadLevel`'s prelude -- the previous level's asset teardown and the
// pristine full-folder backup of the level being loaded, which ran with no bar at
// all before these two stages existed -- and `reading level` is the first stage
// that was there before.
inline constexpr LoadingBarStage kLevelLoadPreparing{2, "preparing"};
inline constexpr LoadingBarStage kLevelLoadBackingUp{5, "backing up level"};
inline constexpr LoadingBarStage kLevelLoadReading{10, "reading level"};
inline constexpr LoadingBarStage kLevelLoadModels{25, "models & terrain"};
inline constexpr LoadingBarStage kLevelLoadTracks{60, "tracks"};
inline constexpr LoadingBarStage kLevelLoadSnapping{75, "snapping objects"};
inline constexpr LoadingBarStage kLevelLoadFinalizing{90, "finalizing"};
inline constexpr LoadingBarStage kLevelLoadDone{100, "done"};

inline constexpr LoadingBarStage kLevelLoadStages[] = {
    kLevelLoadPreparing, kLevelLoadBackingUp, kLevelLoadReading,   kLevelLoadModels,
    kLevelLoadTracks,    kLevelLoadSnapping,   kLevelLoadFinalizing, kLevelLoadDone,
};
constexpr int kLevelLoadStageCount =
    static_cast<int>(sizeof(kLevelLoadStages) / sizeof(kLevelLoadStages[0]));

// A level save: serializing the level's `objects.qsc`/`.qvm`, compiling an edited
// AI script, and repacking the recalculated lightmaps into the game's
// `lightmaps.res` (`App::SaveCurrentLevel`, source/app_level.cpp).  The write-back
// reports its own per-object progress between `lightmaps` and `done`.
inline constexpr LoadingBarStage kLevelSavePreparing{0, "preparing"};
inline constexpr LoadingBarStage kLevelSaveWriting{10, "writing level"};
inline constexpr LoadingBarStage kLevelSaveCompiling{55, "compiling objects"};
inline constexpr LoadingBarStage kLevelSaveLightmaps{80, "lightmaps"};
inline constexpr LoadingBarStage kLevelSaveDone{100, "done"};

inline constexpr LoadingBarStage kLevelSaveStages[] = {
    kLevelSavePreparing, kLevelSaveWriting, kLevelSaveCompiling,
    kLevelSaveLightmaps, kLevelSaveDone,
};
constexpr int kLevelSaveStageCount =
    static_cast<int>(sizeof(kLevelSaveStages) / sizeof(kLevelSaveStages[0]));

// A reset's restore from the pristine level backup (`App::ResetLevel`,
// source/app_input.cpp): the whole level folder is staged, swapped in and then
// reloaded, and the reload runs the level-load ladder above -- which is why this
// ladder stops at 20 rather than 100.
inline constexpr LoadingBarStage kLevelResetPreparing{0, "preparing reset"};
inline constexpr LoadingBarStage kLevelResetRestoring{10, "restoring backup"};
inline constexpr LoadingBarStage kLevelResetReloading{20, "reloading level"};

inline constexpr LoadingBarStage kLevelResetStages[] = {
    kLevelResetPreparing, kLevelResetRestoring, kLevelResetReloading,
};
constexpr int kLevelResetStageCount =
    static_cast<int>(sizeof(kLevelResetStages) / sizeof(kLevelResetStages[0]));

// A ladder is well-formed when its percentages are in range and never go
// backwards: the bar's fill follows `done` monotonically (:115), so a stage that
// went backwards would visibly rewind it.
template <int N>
constexpr bool LoadingBarLadderIsMonotonic(const LoadingBarStage (&stages)[N]) noexcept {
    for (int i = 0; i < N; ++i) {
        if (stages[i].percent < 0 || stages[i].percent > 100) return false;
        if (i > 0 && stages[i].percent < stages[i - 1].percent) return false;
    }
    return true;
}

static_assert(LoadingBarLadderIsMonotonic(kLevelLoadStages),
              "the level-load ladder must be in range and never go backwards");
static_assert(LoadingBarLadderIsMonotonic(kLevelSaveStages),
              "the level-save ladder must be in range and never go backwards");
static_assert(LoadingBarLadderIsMonotonic(kLevelResetStages),
              "the level-reset ladder must be in range and never go backwards");
static_assert(kLevelLoadStages[kLevelLoadStageCount - 1].percent == 100,
              "a level load ends with the bar full");
static_assert(kLevelSaveStages[kLevelSaveStageCount - 1].percent == 100,
              "a level save ends with the bar full");
static_assert(kLevelResetStages[kLevelResetStageCount - 1].percent < 100,
              "a reset hands the bar over to the level load's own ladder");

// ── how this was verified (2026-10-28) ──────────────────────────────────────
// The i686 editor runs under Wine on this host through the GL 2.1 fallback
// (docs/igi-re-parity/GL21_FALLBACK.md), so the overlay's own frames are
// observable in the editor's log with DEBUG logging on (`QEDLogLevel(0)` in the
// staged `qedconfig.qsc`; `App::DrawProgressOverlay` logs one DEBUG line per
// presented frame).  A startup run (`igi1ed.exe -level 1 -w 1280 -h 720`, game
// root a writable copy) reported exactly this, in order:
//
//   progress overlay: Loading Level 1  2% (preparing)         viewport 1280x720 bar 360,560 560x10 filled  11
//   progress overlay: Loading Level 1  5% (backing up level) viewport 1280x720 bar 360,560 560x10 filled  28
//   progress overlay: Loading Level 1 10% (reading level)    viewport 1280x720 bar 360,560 560x10 filled  56
//   progress overlay: Loading Level 1 25% (models & terrain) viewport 1280x720 bar 360,560 560x10 filled 140
//   progress overlay: Loading Level 1 60% (tracks)           viewport 1280x720 bar 360,560 560x10 filled 336
//   progress overlay: Loading Level 1 75% (snapping objects) viewport 1280x720 bar 360,560 560x10 filled 420
//   progress overlay: Loading Level 1 90% (finalizing)       viewport 1280x720 bar 360,560 560x10 filled 504
//   progress overlay: Loading Level 1 100% (done)            viewport 1280x720 bar 360,560 560x10 filled 560
//
// `bar 360,560` is `BuildLoadingBarLayout(1280, 720)`: (1280-640)/2 + 40 = 360 and
// (720-480)/2 + 440 = 560, i.e. the retail 640x480 band at this window size, and
// every `filled` is the integer truncation of `560 * pct / 100` (:115).  The
// startup load is the load this ladder's first two stages exist for: it runs
// inside `App::Init`, before GLUT's reshape callback, and used to present NOTHING
// because the viewport was still 0 (see `ProgressOverlayCanPaint`,
// source/runtime/progress_overlay_policy.h).
//
// **Not verified: the rendered pixels.**  Wine on this host never maps a visible
// window for the editor (the X root has no window and the macOS screen is
// unchanged across a run), so what is proven is that each frame was presented at
// the retail rect with the retail fill -- not what the bar looked like on screen.

}  // namespace igi
