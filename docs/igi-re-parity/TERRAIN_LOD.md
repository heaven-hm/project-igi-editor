# Terrain LOD detail scaling and the fog table — issue #52 (Part A)

Oracle: `/Users/haseeb-mir/Documents/Code/IGI-RE`, the faithful C decompilation of retail
`IGI.exe`. Every function carries `HOOK(0xADDR, Name)`; every claim below cites `file:line`.

**Verdict, in one line.** Retail has no terrain "draw distance": it selects terrain LOD from a
per-frame **cell-size threshold** (`gQCubeMinSize`) derived from a quality preset and the
camera's field of view, and the editor already implemented the threshold's *comparison* and
its *value* for the default preset but hardcoded that value, so it never scaled with the field
of view. That scaling is now wired, with tests.

This document covers **Part A only**. Part B (ACES/AgX tone mapping, HDR, motion blur, depth of
field) has **no engine oracle at all** and is discussed in §7 — it must not be labelled parity.

---

## 1. The engine's terrain LOD rule

`QCube_Begin`, `HOOK(0x4D2540)`, `src/lod/qcube.c:319-345`:

```c
void QCube_Begin(Terrain *w)
{
    ...
    if (gTransContext.fov > kOne)
        d = gQCubeDetail;
    else
        d = x87_round(gQCubeDetail / ci_pow(gTransContext.fov, kHalf));
    gQCubeMinSize = d < 0x46 ? 0x46 : d;
    QCube_Walk(w);
}
```

| # | Rule | Oracle |
| --- | --- | --- |
| 1 | The terrain detail threshold is re-derived **every frame**, not stored per level | `src/lod/qcube.c:338-342` |
| 2 | The field-of-view test is the **strict** `> 1.0`, so `fov == 1.0` (and a NaN) take the unscaled branch | `src/lod/qcube.c:338` |
| 3 | For `fov > 1` the detail is divided by `sqrt(fov)` | `src/lod/qcube.c:341` |
| 4 | The result is floored at `0x46` = **70** | `src/lod/qcube.c:342` |
| 5 | The default global is `gQCubeDetail = 115`, but retail never renders with it — `Profile_ApplyPerformance` always writes a preset | `src/lod/qcube.c:578`; `src/profile/profile.c:404-416` |
| 6 | There are exactly **three** presets: `0x60` (96) low, `0x81` (129) medium, `0xA3` (163) high | `src/profile/profile.c:412` |
| 7 | The preset chain is first-match, `0x10` before `0x20`; **neither bit set is High** | `src/profile/profile.c:412` |
| 8 | The flag word is the player's performance setting, persisted as `GOGfxPerformance` | `src/config/configtypes.h:31`; `src/profile/profile.c:235` |

`gTransContext.fov` is **the tangent of the half horizontal field of view**, not an angle:
`HumanView_CreateHandler` sets `v->fovX = x87_tan(kFovX)` with `kFovX = 0x1.921fb6p-1` = π/4
(`src/viewcam/humanview.c:83,137`), `Human_GetHumanCameraInfoHandler` copies it into the camera
info (`src/humanbase/human.c:599`), and `TransContext_Create` stores it in `ctx->fov`
(`src/transctx/transctx.c:195`). Retail's default is therefore `tan(45°) = 1.0` **exactly**, and
`ctx->scaleX = gScene.halfW / ctx->fov` (`src/transctx/transctx.c:198`) confirms it is a
reciprocal tangent scale. Retail's zoom multiplies it by `0.96` per step
(`src/viewcam/humanview.c:314,340`), so retail reaches the *unscaled* branch when zoomed in and
the *scaled* branch only at field of view wider than the default.

**Direction of the rule:** a wider field of view gives a *smaller* threshold, i.e. **finer**
terrain. The detail budget tracks the screen area the terrain covers.

## 2. What the threshold compares against

`QCube_Walk`, `HOOK(0x4D18F0)`, `src/lod/qcubewalk.c:327-361`:

```c
half = SAR(0x40000000, sh);              /* sh = depth + 1 */
...
size = SAR(mx - half, 0x18 - depth);     /* 0x18 = 24 */
...
if (size < minSize && depth + 1 < 16) {
    /* subdivide */
} else if (depth + 1 > 6) {
    /* emit a terrain cell at level depth + 1 */
}
```

`mx` is the maximum axis-aligned offset from the view to a cell **corner**
(`src/lod/qcubewalk.c:330-336`). With `half = 2^30 >> (depth+1) = 2^(29-depth)`,

```
size = (mx - half) / 2^(24-depth) = (mx - half) / (half / 32)
```

so `size` is a **unitless multiple of (cell half size / 32)** — a Chebyshev distance to the
cell's face, normalised by the cell's own size. Note the metric is Chebyshev (max over axes),
not Euclidean.

## 3. The editor already had the right comparison

`source/level/terrain_lod.cpp:269-316` computes, per candidate cube:

```
max_axis_aligned_dist_to_view = max |cube_pos - view_pos| over the three axes   /* = mx */
cube_half_size                = ROOT_CUBE_HALF_SIZE >> lod_level = 2^(30-level)
delta                         = mx - cube_half_size
v                             = delta >> (25 - lod_level) = (mx - 2*half) / (half/16) = 2 * size
draw_cur_cube = v >= CUBE_ACTIVE_COMPARE_INT || lod_level >= LEAF_CUBE_LOD_LEVEL
```

Because `v = 2 * size`, the editor's test `v >= C` is exactly the engine's `size >= C/2`. The
editor's cube is one level *coarser* than the engine's at the same numeric level (its
`lod_level` L spans twice the engine's `depth + 1 = L`), and that factor of two is precisely why
the constant is `163` and not `326`.

**So `CUBE_ACTIVE_COMPARE = 163.0f` is not an invented number.** It is
`QCubeMinSizeForFov(fov, High)` for every `fov <= 1`, and 163 is `0xA3`, retail's default
preset. The comparison and the value were right; only the scaling was missing.

## 4. The bug this change fixes

The editor never scaled the threshold, so terrain detail did not respond to the field of view at
all. With the editor's own field of view — `FOVY_IN_DEGREE = 70` (`source/common.h:53`), whose
horizontal tangent at 16:9 is `tan(35°) * 16/9 = 1.2448` — the engine's rule asks for

```
163 / sqrt(1.2448) = 146.1  ->  146
```

i.e. **finer** terrain than the constant 163 was producing. At 4:3 the same vertical field of
view gives `tan(35°) * 4/3 = 0.9336 < 1.0`, so the rule does **not** scale and the preset value
163 stands. The threshold is therefore **aspect-dependent**, which a constant cannot express.
This is a real, if modest, parity gap: the editor was drawing terrain ~12 % coarser in cell-size
terms than retail's rule asks for at its default 16:9 field of view.

## 5. What was changed

| File | Change |
| --- | --- |
| `source/level/terrain_lod_settings.h` | **New.** Ports the preset values, the `fov` rule, the floor, the default global, and the fog-table builder. |
| `source/level/terrain_lod.cpp` | Derives `min_size_` per frame from `tan_half_fovx_` and uses it as the subdivision threshold instead of the constant. |
| `source/level/terrain_mesh.cpp` | The cube morphing factor's `minSize - 64` term now uses the same per-frame value. |
| `source/level/terrain.h` | Adds the `min_size_` member. |
| `source/level/terrain_internal.h` | `CUBE_ACTIVE_COMPARE` kept (the morphing ratios are ratios of it) and documented as the `fov <= 1` case; the now-unused `CUBE_ACTIVE_COMPARE_INT` and `..._SUB_CUBE_START_MERGE_DELTA` removed. |
| `tests/test_terrain_lod_settings.cpp` | **New.** 14 tests: presets, the fov branch, the floor, NaN/negative handling, and the fog table. |

`CMakeLists.txt` **was** modified: `tests/test_terrain_lod_settings.cpp` is added to the
`igi_tests` source list (next to `tests/test_terrain_snap_rules.cpp`). The new **header** needs
no CMake change because `source/level/*.*` is globbed (`CMakeLists.txt:76`). The editor's
explicit source list was **not** touched, because no new `.cpp` was added to the editor target.

## 6. The fog interaction — real, and it is the same number

Terrain draw distance and fog are related in the engine through **this same threshold**.
`TerrainMesh_SetupLighting`, `HOOK(0x50FAE0)`, `src/glowmesh/terrainmesh.c:277-292`:

```c
for (i = 0, s = 25; s > -6; i++, s--) {
    long double d = (long double)(int)(1u << (s & 31)) * gQCubeMinSize;
    d = d + d;
    d += 0x40000000 >> i;
    e = ((long double)Float_Log2((float)d) - k130.f) * gTerrainFogScale;
    ... clamp 0..255 ...
    gTerrainFog[i].value = e;
}
```

Three things follow:

1. **`gQCubeMinSize` feeds the fog table directly.** Changing the terrain detail preset changes
   the terrain lightmap fog values. The two are one system, not two.
2. **The table is built at level setup**, in `TerrainMesh_SetupLighting`, so a preset change
   mid-level does *not* rebuild it in retail either. That is why this change does not try to
   rebuild fog on a field-of-view change: the engine does not.
3. **The tail of the table is an x86 shift artifact.** The loop runs `s` from 25 down to `-5`
   but shifts by `s & 31` (`:278`). On x86 `shl` masks the count to 5 bits, so the negative
   shifts **wrap**: `s = -1` becomes a shift of 31, giving `0x80000000`, which as a signed int is
   `INT_MIN` and makes `d` **negative**. Entries 26..30 are therefore
   deterministic-but-meaningless values. The port reproduces them and the test pins them, rather
   than "fixing" a retail global.

Separately, the *depth* fog itself is `exp(-depth * gWorldScale * gTerrainFogScale)`
(`src/glowmesh/terraind3d.c:164`) and is **independent of the detail level** — only the lightmap
table above depends on it. The editor's existing fog work
(`source/runtime/mission_fog.h`, `docs/igi-re-parity/MISSION_FOG.md`) covers that path.

**Not wired:** the editor already ports the log2 half (`igi::LightmapFogValue`,
`source/runtime/mission_fog.h:130`) but does **not** build this table, so
`BuildTerrainFogTable` is a faithful port **with no caller yet**. It is recorded and tested
rather than wired, because wiring it needs a consumer (a terrain lightmap fog channel) that this
change does not add. This is stated plainly rather than presented as finished integration.

## 7. Part B — the modernisation half has NO engine oracle

The rest of issue #52 asks for HDR tone mapping (ACES/AgX), a 3D colour LUT, camera motion blur,
depth of field and sub-tick pose interpolation. **Retail has none of these.** Evidence:

| Grep over all of `/Users/haseeb-mir/Documents/Code/IGI-RE` | Hits |
| --- | --- |
| `tonemap`, `tone_map`, `hdr`, `high.dynamic` | **0** (the only `hdr` matches are a local variable name in Python test files: `tests/test_res_util.py:163`, `tests/test_qvm.py:243`) |
| `motion.?blur`, `depth.?of.?field`, `velocity.?buffer` | **0** |

That is consistent with the renderer the oracle describes: 8-bit D3D7 fixed-function with
palettised terrain (`Palette_Create`, `src/glowmesh/terrainmesh.c:270`), lightmaps clamped to
`0..255` (`src/glowmesh/terrainmesh.c:283-286`), and `IDirect3DDevice7_SetRenderState` calls
(`src/glowmesh/terraind3d.c:299-327`) — no float render target, no accumulation buffer, no tone
curve anywhere.

**And the editor does not already have them either.** Checked, not assumed:

* All four shader tiers (`shaders/21`, `shaders/32`, `shaders/41`, `shaders/45` and their shipped
  copies under `bin/Release/editor/shaders/`) contain only geometry and terrain passes —
  `terrain_mat`, `terrain_lmp`, `terrain_fog`, `terrain_wireframe`, `skydome*`,
  `flat_sky_layer*`. There is **no** post-process, tone-mapping, colour-grading, bloom or
  HDR-resolve shader in any tier.
* `grep -rni "tonemap|tone_map|aces|agx|bloom|motion.blur|hdr" shaders/ bin/Release/editor/shaders/`
  returns **0**.
* No float colour target exists: `RGBA16F`, `RGBA32F` and `GL_HALF_FLOAT` appear **0** times in
  `source/`. The only framebuffer objects in the editor are the object-picking FBOs
  (`source/renderer/renderer_objects_picking.cpp:31-40`), which carry no colour grading.

So there is nothing in the editor to document as a dishonestly-labelled non-retail feature: the
features simply do not exist. If the maintainer wants them, they are **editor-only
modernisation** and should be labelled that way in the issue and in any UI — not presented as
parity with retail, because there is no retail behaviour to be at parity with.

## 8. Verification status

| Claim | Evidence | Status |
| --- | --- | --- |
| The three preset values and the default global | `tests/test_terrain_lod_settings.cpp` | **Proved** |
| The `fov` rule, its strictness, the floor, NaN handling | same suite | **Proved** |
| The editor's 16:9 threshold is 146, its 4:3 threshold 163 | same suite | **Proved** |
| The tests are not vacuous | negative control: reinstating the hardcoded constant fails **4** of the 14 | **Proved** |
| The fog table arithmetic, its dependence on the detail level, and its x86 tail wrap | same suite | **Proved** |
| The suite builds and passes on the real target | 14/14 PASS in the host lane **and** 14/14 PASS in the Windows `igi_tests.exe` under Wine | **Proved** |
| The equivalence `CUBE_ACTIVE_COMPARE == 163 == QCubeMinSizeForFov(1.0)` | algebraic derivation, §3, plus the test at `fov == 1.0` | **Proved by derivation**, not by a retail trace |
| The editor now *renders* finer terrain at 16:9 | — | **Unverified.** The GUI needs GL 4.1 and this host offers 2.1, so no viewport frame can be produced here. The threshold value is unit-tested; its visual effect is not. |
| The fog table is actually consumed by the renderer | — | **Not wired** (no consumer exists); §6 |
| Any retail trace confirming the threshold formula end to end | — | **Not available.** The rule is read from the decompilation, not measured at runtime. |

### The one place the port deliberately differs by up to 1

The engine rounds the scaled detail with `x87_round` (round half to even,
`src/lod/qcube.c:341`); the editor's constant was cast with a truncating `(int)`. The port uses
`std::nearbyint` to follow the engine. At the default field of view the two agree exactly (163
either way); they can differ by 1 unit only at an exact half-way value, which shifts a
subdivision boundary by one cell. Recorded because it is a real, if negligible, difference from
the editor's previous behaviour — and it moves *towards* the oracle, not away from it.
