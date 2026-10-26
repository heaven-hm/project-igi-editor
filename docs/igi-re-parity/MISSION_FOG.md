# Per-mission authored fog (#66)

Status: implemented in `source/runtime/mission_fog.{h,cpp}`, tested in
`tests/test_mission_fog.cpp`. Every constant, offset and line number below is
cited to IGI-RE (`/Users/haseeb-mir/Documents/Code/IGI-RE`), which is the only
evidence source used. Nothing here is taken from open-igi or from web research.

---

## 1. The issue's premise is half right — and that matters

The task brief told me the issue's `FlatSky` premise was simply wrong: that
IGI-RE's `FlatSky` "carries a sky gradient, a clear colour and three UNLABELLED
`unk` fields", and that deriving fog from it would be inventing a rule.

**The first half of that is not what the data says.** `FlatSky`'s ParamTable
(`src/flatwater/flatsky.c:89-92`) registers its fields under real authored names,
and two of them are fog fields:

| Struct field | Authored name | Type | Range |
|---|---|---|---|
| `unk74` | **Fog Amount** | `Real32` | `kSkyRange01` (0..1) |
| `unk6C` | **Z Pos** | `Real32` | — |
| `unk70` | **Distance** | `Real32` | — |
| `clear[3]` | **Fog Color** | `RGB` | — |

and retail LEVEL1 authors real values on its `TheSky` instance
(`LEVEL1/objects.qsc:91`):

```
Task_New(-1, "FlatSky", "TheSky", 0.94166702032089233, 4.0, 100.0,
         0.20000000298023224, 0.28846201300621033, 0.40000000596046448, TRUE, 15.0, ...)
         /* Fog Amount,  Z Pos,  Distance,  Fog Color RGB,  Snap, Angle */
```

So `FlatSky` is a genuine, per-level, authored fog source, and it is *not* three
unlabelled unknowns. The premise's error is narrower than stated.

**The second half still holds, for a better reason.** Those four fields
parameterise a flat-sky **ground fog band**, not the depth-based scene fog.
`FlatSky_Render` (`flatsky.c:115-270`) reads them at `:126-128`:

```c
a  = s->unk70 * k4096;                       /* Distance, in game units  */
b  = s->unk6C * k4096;                       /* Z Pos                    */
ce = a - (a - kNear) * s->unk74;             /* Fog Amount: near edge    */
c  = ce;
d  = kOne / (ce - a);
```

and builds a horizon quad from `a`, `b`, `ce`, coloured by `clear` (packed to
RGB bytes at `flatsky.c:309-311`). It is drawn as part of the sky, at a fixed
`Distance` from the camera with a `Z Pos` vertical offset — it has no depth
argument at all, so it cannot be the scene fog. The scene fog is `GlobalLight`'s,
which is what this module implements.

**Conclusion for the Lead:** the issue named a real fog field, but the wrong one
for this purpose. Implementing the `FlatSky` ground band is a *separate,
legitimate feature* (and one the editor does not currently render), not a
substitute for per-mission scene fog. I did not implement it, per the brief; if
you want it, it should be its own task, and §2 below is the scene-fog mechanism it
would sit alongside.

The engine's per-mission scene fog is `GlobalLight` + `GlobalLightKeyframe`.

## 2. Where the authored fog lives

`GlobalLight_Open` (`HOOK 0x527C90`, `src/animcam/globallight.c:265-285`) builds
the QSC field names, using `"terrain"` for set 0 and `"object category %d"` for
sets 1..4:

```c
crt_sprintf(gFogNames[i],        "Fog color %s",   set);
crt_sprintf(gFogDensityNames[i], "Fog density %s", set);
```

So the authored fields are literally **"Fog color terrain"** and **"Fog density
terrain"**. `publish()` (`globallight.c:117-140`) converts the authored density
into the value the renderer stores, with `static const float kFogScale =
0x1.0624dep-11f` (`globallight.c:115`) = **0.0005**:

```c
TerrainMesh_SetFog(&g->sets[0].color.fog, g->sets[0].color.fogDensity * kFogScale);   /* :125 */
gFogDensities[i] = s->color.fogDensity * kFogScale;                                   /* :131 */
```

`TerrainMesh_SetFog` (`HOOK 0x50FD20`, `src/glowmesh/terrainmesh.c:302-320`)
clamps each colour channel to 0..1 and the scale to 0..16384 (negative → 0), and
stores it in `gTerrainFogScale` (`AT 0xA7DA18`). **`gTerrainFogScale` is the
per-mission fog density the renderer uses.**

### Parameter order

`GlobalLightKeyframe` declares 51 parameters ending in `Time` (`Real32`), in this
order (`globallight.c:264-282`, reproduced verbatim in the shipped
`Task_DeclareParameters` line of every `objects.qsc`):

| Parameter | Field |
|---|---|
| 0 | Link All Sliders (`PushButton`) |
| 1..3 | Ambient color terrain (RGB) |
| 4..6 | **Fog color terrain** (RGB) |
| 7 | **Fog density terrain** (`Real32`) |
| 8 | Link setting terrain (`Int32`) |
| 9..16, 17..24, 25..32, 33..40 | the same 8 fields for object categories 1..4 |
| 41..43 / 44..46 / 47..49 | Sky color / Water ambient / Water color (RGB) |
| 50 | Time (`Real32`) |

`LevelObject::argTokens` is `[0]=task id, [1]=type, [2]=name, [3]=parameter 0`, so
declared parameter *k* is at token index *k + 3*. That is the same offset
`Level::LoadFogInfo` (`source/level/level.cpp:450-521`) and `app_level.cpp:408-450`
already use.

## 3. The falloff, and the trap

### 3.1 `Float_Log2` is a bit trick, not a logarithm

`HOOK 0x499FF0`, `src/dxinit/spline.c:13-20`:

```c
unsigned Float_Log2(int bits)
{
    if (bits > 0x3F800000)
        return (unsigned)(bits - 0x3F800000) >> 20;
    return 0;
}
```

It reinterprets the float's own bits. It is `8*exponent + floor(8*mantissa)`,
i.e. **roughly** `8*log2(x)`, and it returns 0 for every `x <= 1` and every
negative input. Two details matter and both are reproduced:

* The comparison is **signed** (`int bits`). A negative float has the sign bit
  set, so as a signed int it is negative and the function returns 0. An unsigned
  compare would instead return a large value.
* The mantissa is treated **linearly**. On exact powers of two the trick and
  `8*log2` agree exactly (`Float_Log2(2)=8`, `(4096)=96`, `(2^20)=160`,
  `(2^30)=240`), which is precisely why a test built only on powers of two cannot
  detect a substitution. Between them they differ by up to **1.68** (just below
  the mantissa=0.5 boundary: `Float_Log2(1.4999)=3` where `8*log2` says 4.68).
  Against the bias of 130 that is ~1.3%.

`tests/test_mission_fog.cpp` therefore pins `Float_Log2(1.1) == 0` (real `log2`
says 1), `Float_Log2(2.2) == 8` (real says 9) and `Float_Log2(1.4999) == 3`, and
the red/green proof in §9 uses exactly those.

### 3.2 The three variants disagree, by construction

| Site | Expression | Output range |
|---|---|---|
| `src/sector/light.c:55-72` (`Fog_Compute`, HOOK 0x4DA790) | `(Float_Log2(depth) - 130) * scale * (1/65536) + *out`, clamped 0..1 | 0..1 |
| `src/glowmesh/terrainmesh.c:282` | `(Float_Log2(d) - 130) * gTerrainFogScale`, clamped 0..255 | 0..255 lightmap |
| `src/flatwater/flatwater.c:421` | `(Float_Log2(depth) - 130) * gTerrainFogScale * 2000 * 0.00390625`, clamped 0..1 | 0..1 water |

Constants: `kFogBias = 130.0f`, `kFogScale = 1.5258789e-05f` (= 1/65536),
`kFovBase = 0.17f`, `kFovScale = 0.83f` — all `DATA(534B1C)` at
`src/sector/light.c:172-175`. `k2000 * k256th = 2000 * 0.00390625 = 7.8125`.

`Fog_Compute` also scales depth by `gTransContext.fov * 0.83 + 0.17` when that
factor is below 1 (`light.c:63-64`).

The bias of 130 is meaningful: `Float_Log2(depth) = 130` corresponds to
`depth = 1.25 * 2^16 = 81920` game units = **exactly 20.0 m** at `kFixed = 4096`
(`src/math/mathvars.h:7`). So fog onset at ~20 m is consistent across all three
sites and corroborates the reading.

### 3.3 A fourth, exponential path is the one that renders

`src/glowmesh/terraind3d.c:164`:

```c
r = kOne.f / x87_exp(vz * gWorldScale * gTerrainFogScale);
```

then `terraind3d.c:292-295` packs the colour and `:302` sets D3D render state
`0x26` (`D3DRENDERSTATE_FOGDENSITY`) to `gTerrainFogScale` — note it passes the
**authored scale**, not the exponent coefficient.

`gWorldScale = 0.001f`, set once at `src/d3d/direct3d.c:532` (declared
`AT 0xBCABEC`). This constant is the missing factor in the open question.

## 4. Resolution of the open question

The Lead's arithmetic was: with LEVEL1's real numbers `scale = 1.89 * 0.0005 =
0.000945`, so `(8*log2(depth) - 130) * 0.000945 / 65536` is ~1e-6 even at
`depth = 2^20`. That is **correct**, and here are the measured numbers:

| Path | LEVEL1 @ 2^20 units (256 m) | LEVEL1 @ 2^30 (262 km) | Visible? |
|---|---|---|---|
| `light.c` × 1/65536 | 4.33e-7 | 1.59e-6 | no — never, at any depth |
| `terrainmesh.c:282` lightmap | 0.0287 of 255 | 0.104 of 255 | no — effectively all-zero |
| `flatwater.c:421` × 7.8125 | 0.2215 | 0.8121 | yes |
| `terraind3d.c:164` exponential | see below | — | yes |

So the honest answer has three parts:

1. **The `/65536` variant cannot reach a visible range at any authored density
   in the shipped data, and it is also the one whose input is undefined.**
   `TerrainMesh_DrawTriangle` declares `float k = 0, fog[3], out;`
   (`terrainmesh.c:876`) and calls `Fog_Compute(&out, ...)` at `:960` **without
   ever assigning `out`** — verified by scanning every line of that function
   (`terrainmesh.c:866-1000`): `out` occurs exactly twice, the declaration and the
   call. The `/65536` in `Fog_Compute` exists to undo a `×65536` that the intended
   caller was supposed to supply. The reconstruction therefore cannot tell us what
   `*out` is, and this module takes it as an explicit parameter instead of reading
   uninitialised stack. This is a genuine gap in IGI-RE, not a tuning problem.

2. **The lightmap-table variant is not "broken", it is an 8-bit quantity.** For
   LEVEL1's 0.000945 the table is ~0.1 out of 255, i.e. zero. Reaching a
   meaningful value needs an authored density around **1400** at `depth = 2^30`
   (which gives 77/255). The engine's own clamp is `gTerrainFogScale <= 16384`
   (`terrainmesh.c:318`), which caps the authored density at **32,768,000** — so
   the range is available, but no shipped level is anywhere near it. The most
   likely reading is that the lightmap fog table is a legacy path that the
   exponential renderer superseded, and it is left at zero in retail.

3. **The per-mission density does reach a visible range — through the
   exponential path.** With `gWorldScale = 0.001f` (`direct3d.c:532`) the
   coefficient is `0.001 * 0.000945 = 9.45e-7` per game unit, i.e.
   **0.00387 per metre** for LEVEL1:

   | depth | LEVEL1 fog |
   |---|---|
   | 100 m | **32.1%** |
   | 300 m | **68.7%** |
   | 1 km | **97.9%** |

   The e-folding distance is `1 / (kWorldScale * scale)` =
   **488.28125 / authored_density metres**, i.e. 258.35 m for LEVEL1. Across all
   fourteen shipped levels it ranges from **51.4 m** (LEVEL14, density 9.5) to
   **4069 m** (LEVEL5, density 0.12) — a sensible, well-populated range.

**Conclusion: the Lead's reading is right about the log2/`/65536` path and about
the lightmap table at the shipped densities, and wrong only in that the density is
not the whole story — `gWorldScale` is the factor that makes the authored number
render.** No constant was tuned to reach this conclusion; the four paths were
evaluated separately and only the exponential one is in range.

## 5. What this means for the editor's existing fog (a real defect)

`Level::LoadFogInfo` (`source/level/level.cpp:450-521`) already reads the right
authored fields (`arg_idx` 7/8/9 = fog colour, 10 = density) and picks the
keyframe with the smallest `Time`, which is correct. But it converts density to a
renderer fog distance with an **invented constant** (`level.cpp:509-510`):

```cpp
// Factor 120: density=0.12 (level1 haze) → fog_far=1000m; density=0.5 → 240m.
bestFogFarMeters = (curFogDensity > 0.0f) ? (1.0f / curFogDensity) * 120.0f : fog_far_meters;
```

The functional form `∝ 1/density` is right — the engine's e-folding distance is
also `∝ 1/density`. The constant is not: the engine's is **488.28125**, so the
editor's fog distance is **4.069× too short** at every density. For LEVEL1 the
editor currently uses 63.5 m where the engine's exponential e-folds at 258.35 m.

The comment's own example is also mis-attributed: "density=0.12 (level1 haze)" is
LEVEL5's density; LEVEL1 authors **1.89** (see §6), for which the formula gives
63.5 m.

Also worth recording: the renderer is **linear** fog (`fog_base_far_`), while the
engine is **exponential**. `488.28125 / density` is the honest bridge (the
e-folding distance), but it is a bridge, not an equivalence — matching the engine
exactly would need exponential fog in the shader. That decision is the Lead's,
not this module's.

## 6. The 14-level inventory, extracted from `OBJECTS.QVM`

Produced by `tests/test_mission_fog.cpp` (`MissionFogCorpusTest.AllFourteenLevelsHaveAuthoredFog`),
which parses each level's real `OBJECTS.QVM` with the editor's own `QVM_Parse` and
walks the bytecode to the authored `GlobalLightKeyframe` call. Terrain set only.

```
  level  density   scale       fog RGB                    time   e-fold(m)  @100m  @300m  @1km
      1    1.890  0.00094500  (0.5769, 0.5385, 0.4808)   12.0      258.3  32.1%  68.7%  97.9%
      2    0.180  0.00009000  (0.5385, 0.5769, 0.5962)   12.0     2712.7   3.6%  10.5%  30.8%
      3    4.790  0.00239500  (0.2692, 0.3269, 0.3846)   12.0      101.9  62.5%  94.7%  100.0%
      4    1.200  0.00060000  (0.5769, 0.5385, 0.4808)   12.0      406.9  21.8%  52.2%  91.4%
      5    0.120  0.00006000  (0.3462, 0.6731, 0.8846)   12.0     4069.0   2.4%   7.1%  21.8%
      6    0.180  0.00009000  (0.5385, 0.5769, 0.5962)   12.0     2712.7   3.6%  10.5%  30.8%
      7    0.790  0.00039500  (0.1538, 0.2692, 0.3846)   12.0      618.1  14.9%  38.5%  80.2%
      8    0.400  0.00020000  (0.5192, 0.6346, 0.7115)   12.0     1220.7   7.9%  21.8%  55.9%
      9    3.000  0.00150000  (0.2692, 0.3269, 0.3846)   12.0      162.8  45.9%  84.2%  99.8%
     10    0.180  0.00009000  (0.5385, 0.5769, 0.5962)   12.0     2712.7   3.6%  10.5%  30.8%
     11    0.115  0.00005750  (0.0962, 0.1731, 0.3077)   12.0     4245.9   2.3%   6.8%  21.0%
     12    0.400  0.00020000  (0.0385, 0.1346, 0.2500)   12.0     1220.7   7.9%  21.8%  55.9%
     13    0.600  0.00030000  (0.1923, 0.1154, 0.1346)   12.0      813.8  11.6%  30.8%  70.7%
     14    9.500  0.00475000  (0.0000, 0.2115, 0.2331)   12.0       51.4  85.7%  99.7%  100.0%
```

All fourteen levels author a distinct, non-zero density and a normalised colour.
Every level authors **two** keyframes: the daytime state above and a `Time = 100`
night state with density 0; the resolver selects the smallest `Time`, so the
daytime state is the one used. LEVEL1's `Fog density terrain = 1.89` matches the
`.qsc` exactly.

### How the bytecode walk works

A retail `OBJECTS.QVM` identifier table is exactly
`["Task_DeclareParameters", "Task_New", "TRUE", "FALSE"]` (4 entries), which is
how a boolean reaches a task: `PUSHIIB 3` is `FALSE`, `PUSHIIB 2` is `TRUE`.
`PUSHB`/`PUSHW` are immediate integers (so the `Link setting` values 2 and 3 are
`PUSHB 2` / `PUSHB 3`, **not** booleans). `PUSHF` carries a float,
`PUSH0`/`PUSH1` are the literals 0 and 1, `MINUS` negates the previous push, and
`PUSHSIB`/`PUSHSIW` index the string table.

The declaration call and the authored instance both push the type string, so they
are told apart structurally: after the type comes the instance **name** string,
and after that a declaration has another parameter-name string while an instance
has a value. The instance's 51 parameters then follow as a flat push sequence.

## 7. Cross-check against the readable `.qsc` text

LEVEL1 and LEVEL4 are the only levels that ship a decompiled `objects.qsc`
alongside the binary `OBJECTS.QVM`, so they are the only ones where the bytecode
walk can be checked against human-readable authored data.

`MissionFogCorpusTest.Level1AndLevel4MatchTheDecompiledText` parses
`Task_New(..., "GlobalLightKeyframe", ...)` out of the text and requires **every
token from the type onwards to be equal** to the bytecode-derived token, compared
as floats where both parse as numbers. Both levels, both keyframes, all 53 tokens:
**exact match**.

`MissionFogCorpusTest.Level1FogMatchesTheShippedNumbers` additionally pins LEVEL1's
full-precision values (`0.57692301273345947`, `0.53846198320388794`,
`0.48076900839805603`, density `1.8899999856948853`, `Time = 12.0`), which is what
would catch a mis-aligned token index landing on a neighbouring field.

Note: the decompiled `.qsc` is a **nested** `Task_New` tree, so a parent call's
body contains its children. A parser that resumes its scan after a call's matching
`)` silently skips every nested call and finds only the 4 top-level ones — which
is what my first version did, and the corpus test caught it.

## 8. `HazeLayer` fog boxes

`HazeLayer` (`src/haze/haze.h:56-63`, `_Static_assert(sizeof(HazeLayer) == 0x90)`
at `:73`) is a fog box: `density`, `bottom`, `top` (doubles), `color[3]`,
`visible`. Fields are written by `HazeLayer_Set` / `set_fields`
(`src/haze/haze.c:40-53`).

`Haze_GetFog` (`HOOK 0x4DA300`, `haze.c:174-239`) integrates each visible box
along the eye→point ray: a vertical `frac` fade over `bottom..top` using
`kFade = 0.3` (`haze.c:33`), a segment length `v` scaled by `len/s`, then
`*density += v * l->density * frac` and a density-weighted colour average guarded
by `if (!(v < 0 || v > 0)) continue;`. `Vec3_FastLength`
(`HOOK 0x4B2A10`, `src/file/vec.c:140-181`) is
`max + (√2−1)·mid + (√3−√2)·min` — exact on axis-aligned and equal-component
vectors, over-estimating by up to ~11% otherwise.

Three divergences are named in the code rather than silently resolved:

1. **The engine never initialises its colour weight `wsum`** (`haze.c:177` declared, `:188` the asm; the
   decompiler emits a bare `__asm__("" : "=m"(wsum))` with the comment "the
   original never sets the starting weight either"). This module uses `0.0f`,
   which makes the first contributing layer's colour exact and later ones a
   density-weighted average — the evident intent.
2. **The engine's `else` branch is `Error_Show("Fog layer logic error"); for(;;);`**
   (`haze.c:224-226`) — an infinite loop. That branch contributes nothing here
   instead of hanging the editor.
3. **No token parser is provided for `HazeBox`.** `HazeLayer_Open`
   (`haze.c:106-114`) registers the task type with **no ParamTable**, so IGI-RE
   does not reconstruct the authored parameter order, and no retail level declares
   a `Hazinglayers` task to read one from. A field-by-position parser would be
   invented, so `mission_fog.h` deliberately exposes only the struct and
   `HazeGetFog`, and documents that the caller must supply a verified layout.
   `HazeLayer_Set` also takes the box's min/max corners but stores neither (they
   only place the `DynCube_AddBox` volume), so they are absent from the struct.

No retail level uses `HazeLayer`, so this code is reconstructed and unit-tested
but **unexercised by any shipped data**.

## 9. The module

`source/runtime/mission_fog.h` / `.cpp` — pure and host-portable (no GL, no
Windows, no `app_internal.h`), in the same shape as the existing
`source/runtime/level_weather.{h,cpp}`, so the editor can feed it
`LevelObject::{type,argTokens}` directly.

* `ResolveMissionFog(const std::vector<FogTaskObject>&)` → `MissionFog`, choosing
  the smallest-`Time` keyframe and exposing all five light sets, the sky colour,
  the water colours, and `terrain_fog_scale()` with the engine's 0..16384 clamp.
* `FloatLog2` — the signed bit trick.
* `ExponentialFogTransmittance` / `ExponentialFogAmount` — `terraind3d.c:164`.
* `ExponentialFogDistanceForFraction` / `ExponentialFogEFoldingDistance` — the
  engine's fog distance, `488.28125 / density` metres.
* `FogComputeAmount` — `light.c:55-72`, with the haze term explicit.
* `LightmapFogValue` (0..255) and `FlatWaterFogAmount` (0..1).
* `FastLength3` and `HazeGetFog`.

### Tests

`tests/test_mission_fog.cpp`, 16 tests in 2 suites: 13 unit tests plus 3 corpus
tests (the inventory, the `.qsc` cross-check, and the LEVEL1 pin).

Red/green proof that `Float_Log2`'s bit trick is load-bearing: with the body of
`FloatLog2` temporarily replaced by `8.0 * std::log2(value)`,

```
[  FAILED  ] MissionFogTest.FloatLog2IsTheBitTrickAndNotLog2
[  FAILED  ] MissionFogTest.FlatWaterCarriesTheMissingGain
    tests passed          : 14
```

and after restoring the file byte-identically (`diff` empty →
`RESTORED-IDENTICAL`), 16/16 pass. The first version of this test used only
powers of two and **passed under `std::log2`** — the discriminating values in §3.1
were added because of that.

## 10. Renderer wiring — diff supplied, NOT applied

The task says not to wire this into the renderer, because the renderer cannot be
verified behaviourally on this host. The change I would make, for the Lead:

```diff
--- a/source/renderer/renderer.cpp
+++ b/source/renderer/renderer.cpp
@@ -176,8 +176,13 @@ void Renderer::SetupClearColor(const glm::vec4 &color) {
-void Renderer::SetupFog(const glm::vec4 &color, float fog_far) {
-  fog_base_color_ = color;
-  fog_base_far_ = fog_far;
+void Renderer::SetupFog(const glm::vec4 &color, float fog_far) {
+  // Unchanged: the manual Fog Intensity slider keeps overriding the base.
+  fog_base_color_ = color;
+  fog_base_far_ = fog_far;
   ApplyFogIntensity();
 }
```

i.e. **no change is required in `renderer.cpp` at all.** The whole change belongs
at the call site in `source/level/level.cpp:509-510`, replacing the invented
constant with the engine's derived distance, and feeding the authored colour that
is already being read correctly:

```diff
--- a/source/level/level.cpp
+++ b/source/level/level.cpp
@@
-			// Factor 120: density=0.12 (level1 haze) → fog_far=1000m; density=0.5 → 240m.
-			bestFogFarMeters = (curFogDensity > 0.0f) ? (1.0f / curFogDensity) * 120.0f : fog_far_meters;
+			// The engine's fog is exponential with coefficient
+			// gWorldScale * gTerrainFogScale (terraind3d.c:164), so its
+			// e-folding distance is 1 / (0.001 * density * 0.0005) game units
+			// = 488.28125 / density metres at kFixed = 4096. The previous
+			// 120.0f had the right 1/density shape but an invented constant,
+			// making every level's fog 4.069x too thick.
+			bestFogFarMeters = (curFogDensity > 0.0f)
+				? (1.0f / curFogDensity) * 488.28125f : fog_far_meters;
```

`Renderer::SetFogIntensity` / `ApplyFogIntensity` (`renderer.cpp:184-208`) and the
`fog_intensity_` slider are untouched, so the manual override keeps working
exactly as it does today: it multiplies the derived base distance by
`2 * 0.5^(intensity/100)`. The derived base is what changes.

This is a behaviour change to the editor's fog thickness on every level, so it is
the Lead's call, and it cannot be verified visually on this host.

## 11. Verified vs unverified

**Verified (observed command output):**

* `python3 tools/igi-re-parity/tu-check.py source/runtime/mission_fog.cpp tests/test_mission_fog.cpp`
  → `checked 3 translation unit(s), 0 failed`, exit 0 (i686 MinGW cross-compile).
* `IGI_HOST_NO_CACHE=1 IGI_HOST_TESTS=tests/test_mission_fog.cpp bash scripts/run-host-tests.sh`
  → `OK: 16 tests passed in 1 suites`, exit 0.
* The 14-level inventory and the LEVEL1/LEVEL4 `.qsc` cross-check, both read from
  the real retail `OBJECTS.QVM` and `.qsc` files on disk.
* Red/green proof of the `Float_Log2` bit trick (§9).
* LEVEL1's authored values against `LEVEL1/objects.qsc` lines 82-83, and LEVEL4's
  against `LEVEL4/objects.qsc` line 63, token for token.

**Not verified:**

* No rendered frame. Nothing here proves the fog *looks* like the retail game;
  only that the arithmetic matches the reconstructed engine.
* The `light.c` `Fog_Compute` `*out` term is undefined in IGI-RE (§4.1), so the
  haze contribution to the terrain vertex fog cannot be verified at all.
* `HazeGetFog` is reconstructed and unit-tested against hand-computed values, but
  no shipped level places a `HazeLayer`, so it is unexercised by real data.
* The `gTerrainFogScale` → D3D `FOGDENSITY` handoff (`terraind3d.c:302`) is read
  but not executed; a live Direct3D trace would be needed to confirm the shader
  consumes it as the exponential coefficient.
* The proposed `level.cpp` change in §10 is a diff only; it was not applied, built
  or rendered.

## 12. Known gaps in IGI-RE found while doing this

* **`Fog_Compute`'s `*out` is uninitialised at its only call site**
  (`terrainmesh.c:876` declaration, `:960` call, no assignment between). The
  `/65536` implies the intended caller supplied `×65536`.
* **`gFogDensity` (`AT 0xB46D04`, `BSS(B46D04)` at `src/dxinit/d3drender.c:366`)
  is read in three places but never assigned anywhere in the decompiled source**:
  `d3drender.c:117-118` (`kOne / x87_exp(gWorldScale * n * gFogDensity)`),
  `polylist.c:245` (`SET_RS(0x26, f2u(gFogDensity))`), and
  `render/render.c:780` (`SRS(D3DRENDERSTATE_FOGDENSITY, ...)`). It is a distinct
  global from `gTerrainFogScale`, and with no writer reconstructed its value is
  unknown. This may be the D3D global fog that `gTerrainFogScale` supersedes.
* **`HazeLayer_Open` registers no ParamTable**, so the authored parameter order
  for `Hazinglayers` is unreconstructable from IGI-RE (§8.3).
* **`wsum` is never initialised in `Haze_GetFog`** (§8.1).
* The `else` branch of `Haze_GetFog` is an infinite loop (§8.2).

## 13. `fog-presets.json` — out of scope, and my recommendation

The issue's requirement 2 describes a JSON override store. That file format does
not exist in IGI-RE and no engine code reads one, so it is not implemented here.

My recommendation: **do not build it.** The authored per-mission fog is real data
that is already in every level, and §6 shows it produces a well-spread, sensible
range without any preset table. A JSON store would be a second, competing source
of truth for the same value, and the existing manual Fog Intensity slider already
provides the user override the store would be for. If a per-level *artistic*
override is wanted later, it is a feature request for the editor, not a parity
fix. The decision is the Lead's.

## 13b. One follow-up worth its own task

`FlatSky`'s ground fog band (§1) is real authored data that the editor does not
currently render at all: `Fog Amount`, `Z Pos`, `Distance` and `Fog Color` are
present in every level and are drawn as a horizon quad by `FlatSky_Render`
(`flatsky.c:115-270`). That is a *visual* parity gap in its own right, separate
from scene fog, and it is not implemented here. It would make a clean follow-up
task, with `flatsky.c:126-128` and the LEVEL1 values in §1 as its evidence.

## 14. Files

| File | Change |
|---|---|
| `source/runtime/mission_fog.h` | new — module interface, cited constants |
| `source/runtime/mission_fog.cpp` | new — resolver, engine maths, haze |
| `tests/test_mission_fog.cpp` | new — 16 tests, incl. the retail corpus |
| `docs/igi-re-parity/MISSION_FOG.md` | new — this document |
| `CMakeLists.txt` | `tests/test_mission_fog.cpp` added after `tests/test_magic_object_expand.cpp`; `source/runtime/mission_fog.cpp` added to the `igi_tests` source pool after `source/runtime/level_weather.cpp` |
