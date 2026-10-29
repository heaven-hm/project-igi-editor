/******************************************************************************
 * @file    terrain_lod_settings.h
 * @brief   The engine's terrain (QCube) LOD detail scaling, ported.
 *
 * The retail engine does NOT pick terrain LOD by a fixed draw distance. It
 * picks it from a per-frame *cell size* threshold, `gQCubeMinSize`, derived
 * from a quality preset and the camera's field of view. The editor had the
 * right threshold VALUE for the default preset (163) but as a compile-time
 * constant, so it never scaled with the field of view. This header carries the
 * engine's rule so the editor can.
 *
 * Oracle: IGI-RE (decompilation of retail IGI.exe).
 *   QCube_Begin        HOOK 0x4D2540  src/lod/qcube.c:319-345
 *   QCube_SetDetail    HOOK 0x4D2C20  src/lod/qcube.c:567-571
 *   gQCubeDetail       DATA 0x54844C  src/lod/qcube.c:578        (INIT 115)
 *   Profile_ApplyPerformance HOOK 0x406390 src/profile/profile.c:404-416
 *   QCube_Walk         HOOK 0x4D18F0  src/lod/qcubewalk.c:101-424 (the size test)
 *   TerrainMesh_SetupLighting HOOK 0x50FAE0 src/glowmesh/terrainmesh.c:235-294
 *   setup_view         (in TerrainMesh_Update HOOK 0x50FE60)
 *                                      src/glowmesh/terrainmesh.c:329-360
 *
 * See docs/igi-re-parity/TERRAIN_LOD.md for the full trace.
 *****************************************************************************/
#pragma once

#include <cmath>
#include <cstdint>

namespace igi {
namespace terrain_lod {

// ---------------------------------------------------------------------------
// 1. The two constants of the subdivision test
// ---------------------------------------------------------------------------

// src/lod/qcube.c:338 -- `if (gTransContext.fov > kOne)`. The comparison is the
// strict `>`, so a tangent of exactly 1.0 takes the UNScaled branch, and so does
// a NaN (the engine's float compare is unordered-false for NaN). Written with
// the same strictness so a NaN cannot silently change the detail level.
constexpr float kFovScaleThreshold = 1.0f;

// src/lod/qcube.c:342 -- `gQCubeMinSize = d < 0x46 ? 0x46 : d`. The floor is
// decimal 70, in units of (cell half size / 32); see the derivation below.
constexpr int kQCubeMinSizeFloor = 0x46;  // 70

// src/lod/qcube.c:578 -- `DATA(54844C) int gQCubeDetail INIT(115)`. The value
// the engine uses when no performance preset has been applied yet. Retail never
// renders with it, because Profile_ApplyPerformance runs at profile load
// (src/profile/profile.c:404-416) and always writes one of the three presets;
// it is kept because it is the documented initial value of the global.
constexpr int kQCubeDetailDefault = 115;

// ---------------------------------------------------------------------------
// 2. The three quality presets
// ---------------------------------------------------------------------------

// src/profile/profile.c:412 --
//
//     QCube_SetDetail(flags & 0x10 ? 0x60 : flags & 0x20 ? 0x81 : 0xA3);
//
// `flags` is `Profile_GetActive()->gfx.perf_flags`, the player's performance
// setting (src/config/configtypes.h:31, persisted by Profile_Save at
// src/profile/profile.c:235 as "GOGfxPerformance"). The chain is first-match,
// so 0x10 wins over 0x20, and neither set gives the high preset. These are
// therefore the ONLY three detail levels retail can ever render terrain at.
enum class TerrainDetailPreset : int {
    High = 0xA3,    // 163 -- neither 0x10 nor 0x20 set: the default
    Medium = 0x81,  // 129 -- perf_flags bit 0x20
    Low = 0x60,     // 96  -- perf_flags bit 0x10
};

// The detail value the engine passes to QCube_SetDetail for a preset.
constexpr int DetailValue(TerrainDetailPreset preset) {
    return static_cast<int>(preset);
}

// ---------------------------------------------------------------------------
// 3. The per-frame rule
// ---------------------------------------------------------------------------

// src/lod/qcube.c:320-345, in full:
//
//     void QCube_Begin(Terrain *w)
//     {
//         ...
//         if (gTransContext.fov > kOne)
//             d = gQCubeDetail;
//         else
//             d = x87_round(gQCubeDetail / ci_pow(gTransContext.fov, kHalf));
//         gQCubeMinSize = d < 0x46 ? 0x46 : d;
//         QCube_Walk(w);
//     }
//
// `gTransContext.fov` is the tangent of the half horizontal field of view, NOT
// an angle: HumanView_CreateHandler sets `v->fovX = x87_tan(kFovX)` with
// `kFovX = 0x1.921fb6p-1` = pi/4 (src/viewcam/humanview.c:83,137), and
// Human_GetHumanCameraInfoHandler copies it into the camera
// (src/humanbase/human.c:599), which TransContext_Create stores in
// `ctx->fov` (src/transctx/transctx.c:195). So retail's default is tan(45deg)
// == 1.0 exactly, and `ctx->scaleX = gScene.halfW / ctx->fov`
// (src/transctx/transctx.c:198) confirms fov is the reciprocal of a tangent
// scale, not a radian angle.
//
// The editor's equivalent field is `view_define_s::tan_half_fovx_`
// (source/common.h:255), assigned as `std::tan(view_define_.fovx_ * 0.5f)`
// (source/app.cpp:1954), and already documented as the frame's
// `gTransContext.fov` in source/renderer/renderer_objects.h:144-147.
//
// Because the threshold only ever DIVIDES by sqrt(fov) for fov > 1, a wider
// field of view yields a SMALLER minimum cell size, i.e. FINER terrain detail.
// That is the engine's rule and it is deliberate: the detail budget tracks how
// much screen area the terrain covers.
inline int QCubeMinSizeForFov(float fov_tangent, TerrainDetailPreset preset) {
    const int detail = DetailValue(preset);
    if (!(fov_tangent > kFovScaleThreshold)) {
        // The engine's `d = gQCubeDetail` branch. Also the NaN branch.
        return detail < kQCubeMinSizeFloor ? kQCubeMinSizeFloor : detail;
    }
    // `ci_pow(fov, kHalf)` is sqrt(fov) (src/lod/qcube.c:341).
    const double scaled = static_cast<double>(detail) / std::sqrt(static_cast<double>(fov_tangent));
    // `x87_round` (src/lod/qcube.c:341) rounds to nearest, halfway to even.
    const int d = static_cast<int>(std::nearbyint(scaled));
    return d < kQCubeMinSizeFloor ? kQCubeMinSizeFloor : d;
}

// The default-preset convenience overload: retail's default performance
// setting (no 0x10/0x20 bit) is High.
inline int QCubeMinSizeForFov(float fov_tangent) {
    return QCubeMinSizeForFov(fov_tangent, TerrainDetailPreset::High);
}

// ---------------------------------------------------------------------------
// 4. What the threshold MEANS, and why 163 was already the right number
// ---------------------------------------------------------------------------

// The editor's `CUBE_ACTIVE_COMPARE = 163.0f` (source/level/terrain_internal.h:41)
// is this same quantity, hardcoded. Proof, so the equivalence is not taken on
// faith -- both sides are unitless multiples of (cell half size / 32):
//
//   engine, src/lod/qcubewalk.c:327,339 --
//       half = SAR(0x40000000, sh)            // cell half size, sh = depth + 1
//       size = SAR(mx - half, 0x18 - depth)   // 0x18 = 24, so 2^(24-depth)
//     with `mx` the max axis-aligned offset of the view to a cell CORNER
//     (src/lod/qcubewalk.c:330-336, `cx = SAR(corner[i][0], sh) + ox`).
//     Using half = 2^30 >> (depth+1) = 2^(29-depth):
//       size = (mx - half) / 2^(24-depth) = (mx - half) / (half / 32)
//     and the test at src/lod/qcubewalk.c:358 is `size < minSize`.
//
//   editor, source/level/terrain_lod.cpp:269-316 --
//       max_axis_aligned_dist_to_view = max |cube_pos - view_pos| over axes
//                                     = mx          (same corner distance)
//       cube_half_size = ROOT_CUBE_HALF_SIZE >> lod_level = 2^30 >> level
//                      = 2^(30-level) = 2 * half
//       delta = mx - cube_half_size = mx - 2*half
//       v     = delta >> (25 - lod_level) = (mx - 2*half) / (half/16)
//             = 2 * size
//     and the test at source/level/terrain_lod.cpp:332 is
//       `v >= CUBE_ACTIVE_COMPARE_INT`, i.e. `size >= CUBE_ACTIVE_COMPARE / 2`.
//
// The editor's cube is one level COARSER than the engine's at the same
// numeric level (its `lod_level` L spans 2x the engine's `depth+1 = L`), which
// is exactly why the constant is 163 and not 326: the factor of two cancels.
// So `CUBE_ACTIVE_COMPARE` is not an invented number, it is
// `QCubeMinSizeForFov(fov, High)` -- for fov <= 1, where the engine does not
// scale. The editor simply never scaled it.
//
// Consequence for the editor: with the editor's 70-degree vertical field of
// view (source/common.h:53), tan_half_fovx_ is ~1.24 at 16:9, so the engine's
// rule asks for a SMALLER threshold (~146) than the constant 163 -- finer
// terrain than the editor has been drawing. At retail's own default
// (tan == 1.0 exactly) the rule returns 163 unchanged, so the constant was
// correct only for a field of view the editor does not use.

// ---------------------------------------------------------------------------
// 5. The fog table, which is derived from the SAME threshold
// ---------------------------------------------------------------------------

// src/glowmesh/terrainmesh.c:277-292 (inside TerrainMesh_SetupLighting):
//
//     for (i = 0, s = 25; s > -6; i++, s--) {
//         long double d = (long double)(int)(1u << (s & 31)) * gQCubeMinSize;
//         d = d + d;
//         d += 0x40000000 >> i;
//         e = ((long double)Float_Log2((float)d) - k130.f) * gTerrainFogScale;
//         ... clamp 0..255 ...
//         gTerrainFog[i].value = e;
//     }
//
// 31 entries, shift 25 down to -5, `d = 2^(s+1) * minSize + 2^(30-i)` and a
// 130 bias; the result is an 8-bit lightmap fog value. This is the ONE place
// where the terrain detail level and the fog meet: changing the detail preset
// changes this table. It is built in TerrainMesh_SetupLighting, i.e. at level
// setup, so a preset change mid-level does NOT rebuild it in retail either.
//
// The editor has the log2 half of this already (`igi::LightmapFogValue`,
// source/runtime/mission_fog.h:130) but does not build the table, so this
// helper is a faithful port with no caller yet -- recorded rather than wired,
// because wiring it needs a consumer this change does not add.
constexpr int kTerrainFogTableSize = 31;

// The engine's clamp bounds, src/glowmesh/terrainmesh.c:283-286 (kZero / k255,
// F32(0x437F0000) at src/glowmesh/terrainmesh.c:137).
constexpr long double kZero = 0.0L;
constexpr long double k255 = 255.0L;

// `Float_Log2` (HOOK 0x499FF0, src/dxinit/spline.c:13-20) is NOT std::log2; it
// is `8 * log2` for powers of two, computed from the exponent field. The editor
// ports it as `igi::FloatLog2` (source/runtime/spline_rules.h:611,
// source/runtime/spline_rules.cpp:1287-1292) and that is the one to pass in.
// `float_log2` is injected so this header stays free of the runtime include.
template <typename FloatLog2Fn>
inline void BuildTerrainFogTable(int qcube_min_size, float terrain_fog_scale,
                                 FloatLog2Fn float_log2, float out[kTerrainFogTableSize]) {
    for (int i = 0, s = 25; s > -6; ++i, --s) {
        // `(int)(1u << (s & 31))` -- the engine's x86 shift masks the count to
        // 5 bits, so the negative shifts wrap instead of shifting out.
        const uint32_t shifted = 1u << (static_cast<uint32_t>(s) & 31u);
        double d = static_cast<double>(static_cast<int32_t>(shifted)) *
                   static_cast<double>(qcube_min_size);
        d = d + d;
        d += static_cast<double>(0x40000000 >> i);

        long double e = (static_cast<long double>(float_log2(static_cast<float>(d))) - 130.0L) *
                        static_cast<long double>(terrain_fog_scale);
        if (kZero > e) {
            e = kZero;
        } else if (k255 < e) {
            e = k255;
        }
        out[i] = static_cast<float>(e);
    }
}

} // namespace terrain_lod
} // namespace igi
