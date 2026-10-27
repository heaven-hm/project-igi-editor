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

}  // namespace igi
