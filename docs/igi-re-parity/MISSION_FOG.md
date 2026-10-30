# Per-mission authored fog (#66)

Status: implemented in `source/runtime/mission_fog.{h,cpp}`, tested in
`tests/test_mission_fog.cpp` and `tests/test_mission_fog_wiring.cpp`. Wired into
the editor: `Level::LoadFogInfo` (`source/level/level.cpp`) resolves the authored
keyframe through the QSC bridge in `source/level/fog_qsc.h`, so there is ONE fog
path rather than two (see §15). Every constant, offset and line number below is
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
   in the shipped data, and its haze term is zero in retail.** The first half is
   arithmetic (measured above). The second half is now verified rather than
   assumed, and it CORRECTS an earlier claim in this document: `Fog_Compute`'s
   `*out` is not undefined. `Fog_Compute` calls `Haze_GetFog(color, out, ...)` as
   its second statement (`light.c:62`), and `Haze_GetFog` writes `*density = 0`
   before accumulating (`haze.c:188`), so `*out` is the accumulated HazeLayer
   density — 0 whenever no layer is visible. `TerrainMesh_DrawTriangle` declaring
   `float k = 0, fog[3], out;` (`terrainmesh.c:876`) and calling
   `Fog_Compute(&out, ...)` at `:960` is therefore fine: the callee assigns it,
   and `:961` reads it. No shipped level places a HazeLayer at all (no
   `OBJECTS.QVM` of the fourteen contains the string "Hazinglayers"), so for
   retail data the haze term is exactly 0 and this variant is
   `(Float_Log2(depth) - 130) * scale / 65536`, which the table above shows is
   below one 8-bit step at every shipped density. The `/65536` still implies the
   intended caller supplied a `×65536`; that inference is unchanged, but it is no
   longer needed to explain a missing value.

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

## 10. Renderer wiring — applied, and the parse deduplicated

`renderer.cpp` needs no change at all: `Renderer::SetFogIntensity` /
`ApplyFogIntensity` (`renderer.cpp:184-208`) and the `fog_intensity_` slider keep
multiplying whatever base distance the level supplies, by
`2 * 0.5^(intensity/100)`.

The change belongs at the call site, and it is now there in two steps:

1. `19d226a4` replaced the invented `120.0f` at `source/level/level.cpp:528` with
   the engine's derived `488.28125f` — the e-folding distance
   `1 / (gWorldScale * density * 0.0005)` game units, `= 488.28125 / density`
   metres at `kFixed = 4096` (`terraind3d.c:164`, `direct3d.c:532`,
   `globallight.c:115,125`).
2. The inline parse that constant sat in is gone (§15): `LoadFogInfo` now calls
   `igi::ResolveAuthoredFogFromQsc` (`source/level/fog_qsc.h`) →
   `igi::ResolveMissionFog` → `igi::LinearFogFarMetresForAuthoredDensity`, so the
   keyframe selection, the colour and the distance all come from this module.

`tests/test_mission_fog_wiring.cpp` reproduces the pre-deduplication code verbatim
and requires bit-identical values on all fourteen retail levels, so step 2 changed
no number. The `120.0f` → `488.28125f` change in step 1 was a real behaviour change
to the editor's fog thickness (4.069× thinner on every level); it is the part that
cannot be verified visually on this host.

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
* The `light.c` `Fog_Compute` haze term is 0 for every shipped level (no
  `Hazinglayers` task anywhere, §4.1), so its contribution to the terrain vertex
  fog is unexercised by real data — not undefined, as an earlier revision of this
  document claimed.
* `HazeGetFog` is reconstructed and unit-tested against hand-computed values, but
  no shipped level places a `HazeLayer`, so it is unexercised by real data.
* The `gTerrainFogScale` → D3D `FOGDENSITY` handoff (`terraind3d.c:302`) is read
  but not executed; a live Direct3D trace would be needed to confirm the shader
  consumes it as the exponential coefficient.
* The deduplicated `LoadFogInfo` cannot be executed on this host at all: the GUI
  needs OpenGL 4.1 (this Mac offers 2.1) and no headless mode loads a level
  through `Level::Load`. The Windows-lane test in §15 drives the same code path
  (`QVM_Decompile` → `QSC::Load` → the bridge → the resolver) but not the renderer
  call itself, and `--smoke-level` deliberately skips `LoadFogInfo`.

## 12. Known gaps in IGI-RE found while doing this

* **`Fog_Compute`'s `*out` is written by its callee, not left uninitialised**
  (corrected 2026-10-29). `haze.c:188` sets `*density = 0` before accumulating, so
  `TerrainMesh_DrawTriangle`'s `out` is defined; the earlier "uninitialised"
  reading of `terrainmesh.c:876,960` was wrong. What remains true is that the
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

## 15. Deduplication: ONE fog path (2026-10-29)

`Level::LoadFogInfo` (`source/level/level.cpp`) used to carry its own copy of the
"which keyframe, and what are its fog colour and density" parse while
`source/runtime/mission_fog.cpp` implemented the same rule — two implementations
of one engine rule, which is how the invented `120.0f` survived in one of them.
The inline copy is gone:

* `source/level/fog_qsc.h` (new) is the only bridge from a parsed QSC to this
  module: `FogTokenFromQscArg` (one token per QSC argument, in order, so declared
  parameter *k* is token *k+3*), `FogTaskObjectFromFunc`, `FogTaskObjectsFromQsc`
  and `ResolveAuthoredFogFromQsc`. Floats are written with `%.9g` of the float
  value — `FLT_DECIMAL_DIG` — because the QSC parser stores every number as a
  double (`level_common.cpp` `ParseArgs`) and this module re-parses with `strtof`;
  `ArgTokenFromArg`'s `%.7g` is for re-serialising source text and can land an ulp
  away, which would move the fog distance with it.
* `igi::LinearFogFarMetresForAuthoredDensity(density, fallback)` (new) holds the
  distance rule. `kEFoldingMetresPerAuthoredDensity = 488.28125f` is kept as an
  exact literal with a `static_assert` because the float form
  `1.0f / (0.001f * 0.0005f * 4096.0f)` evaluates to **488.2812f** — one ulp low —
  and would move every level's fog distance in the last place.
* `LoadFogInfo` now reads: resolve → take set 0's colour and density → distance →
  `SetupFog(color, distance * WORLD_UNITS_PER_METER)`, and logs the result
  (`"[Level] authored GlobalLightKeyframe fog: ..."`).

`tests/test_mission_fog_wiring.cpp` (new) is the acceptance test: it reproduces
the pre-deduplication loop verbatim and requires **bit-identical** colour and
distance — not `EXPECT_FLOAT_EQ`'s four-ulp tolerance — on all fourteen retail
levels, plus LEVEL1/LEVEL4's shipped `objects.qsc` text. 8 tests; the corpus table
it prints shows legacy and wired distances equal to the last digit (LEVEL1
258.349884 m, LEVEL14 51.398026 m, …). `tests/support/mission_fog_corpus.h` (new)
holds the shared QVM bytecode walk so both fog suites read the corpus one way.

The end-to-end half (`objects.qvm` → `QVM_Decompile` → `objects.qsc` →
`QSC::Load` → the bridge → the resolver, all fourteen levels) is compiled only on
Windows: `QSC::Load` needs `File_LoadText`/`File_FreeBuf` from
`source/common.cpp`, which is a Windows-only translation unit
(`#error "unsupported platform"`), so a host-native binary cannot link it. The
host half drives the same rules through the QVM walk, and the Windows half
cross-checks the bridge's tokens against that walk token for token, so the two
halves cannot drift apart silently.

---

## 16. The OBJECT fog is a DIFFERENT light set, and it was not applied at all (2026-10-30)

The user's report was "the fog and the smoke and the lighting should be exactly how
the level is". Three separate defects came out of it. This section covers the first
two; §17 covers the third.

### 16.1 The engine fogs objects with "object category 1", proved by address arithmetic

`publish()` fills five sets (globallight.c:127-134):

| global | address | type | per-set stride |
|---|---|---|---|
| `gFogColors` | `0xB818E0` (`addr.c:1991`) | `Vec3f[5]` | 12 |
| `gFogDensities` | `0xB46D00` (`addr.c:1970`) | `float[5]` | 4 |

The **object** pass does not index them; it reads two plain globals:

| global | address | reader |
|---|---|---|
| `gFogColor` (`float[3]`) | `0xB818EC` (`addr.c:1992`, `render/rendervars.h:18`) | `render.c:775-777` (`set_fog`), `dxinit/polylist.c:243-244` |
| `gFogDensity` (`float`) | `0xB46D04` (`addr.c:1971`, `dxinit/dxinitvars.h:8`) | `render.c:780`, `polylist.c:245`, `Fog_SetRange` (`d3drender.c:117-118`) |

`0xB818E0 + 12 == 0xB818EC` and `0xB46D00 + 4 == 0xB46D04`. So
**`gFogColor` IS `gFogColors[1]` and `gFogDensity` IS `gFogDensities[1]`** — the
object pass fogs with *object category 1*, while the terrain fogs with set 0 through
`TerrainMesh_SetFog` / `TerrainMesh_SetAmbient` (globallight.c:124-125). The same
aliasing identifies the object pass's D3D ambient material colour: `ALIAS(gLightColor,
gAmbient, 12)` (`src/nodereg/water.c:544`) makes `gLightColor == gAmbient[1]`, and
`polylist.c:249-250` sets `D3DRENDERSTATE_AMBIENT` from it — which is the oracle for
`app_level.cpp` reading "Ambient color object category 1" rather than a guess.

On the shipped corpus the two sets happen to carry the SAME numbers (every level
links them: `link = 0,1,2,3` in the daytime keyframe), so this is a *correctness*
fix — the editor now reads the set the engine reads — rather than a change of
thickness. `ObjectFogCorpusTest.EveryLevelAuthorsBothTheTerrainAndTheObjectFog`
prints both columns for all fourteen levels and they are equal on every one.

### 16.2 The object fog was dead state

`Renderer_Objects` carried `fog_color_`, `fog_far_` and `fog_enabled_`, assigned by
`SetFogParams` / `SetFogEnabled` and **read by nothing**; the object fragment shader
(`renderer_objects.cpp`'s `OBJ_FRAG_SRC`) had no fog term at all, and the draw
carried the comment `// Fog is terrain-only — objects/buildings are never fogged.`
Retail fogs them (`render.c:773-785`), so buildings, props and characters did not
fade into the haze that the terrain did.

Fixed by giving the object shader the engine's own exponential — the expression
`terraind3d.c:164` uses for the terrain, with the object set's density:

```
r = 1 / exp(vz * gWorldScale * gFogDensity)
```

with `vz` the view-space z in game units, computed in the shader as
`dot(v_fragPos - u_cameraPos, u_viewDir)` (all three in world units), and
`u_fogFar` = the e-folding distance `2e6 / density` world units. Both GLSL tiers
(3.30 and the 1.20 fallback) got the same six uniforms and the same arithmetic.

**Named divergence.** D3D7 could only fog with a linear ramp (`render.c:781` sets
`FOGVERTEXMODE = D3DFOG_LINEAR`), so the engine linearises per batch with
`Fog_SetRange` (`HOOK 0x49B2D0`, `d3drender.c:108-131`), which solves for the ramp
through the exponential's value at the near and far side of a sphere around the
object. The editor applies the exponential itself — what that ramp approximates,
without the approximation. `FogSetRangeForDepth` is the ported linearisation and
`ObjectFogTest.FogSetRangeMatchesTheExponentialAtBothEdges` proves the two agree at
both edges, so the port is not dead code and its relationship to what renders is
pinned.

### 16.3 The `link` inheritance was missing from the resolver

`GlobalLightKeyframe_ParamsChanged` (`HOOK 0x5282C0`, globallight.c:392-403) resolves
each set's `Link setting` before anything reads the sets, sweeping the whole table
five times so chains resolve:

```c
for (n = 5; n; n--)
    for (i = 0; i < 5; i++) {
        int l = k->sets[i].link;
        if (l >= 0 && l < 5) k->sets[i].set.color = k->sets[l].set.color;
    }
```

`ResolveMissionFog` did not do this: it read the raw tokens. A level that authors
`link` and leaves the linked set at placeholder zeros would therefore have produced
a different fog from retail. The sweep is now implemented (copying ambient, fog and
density together, as one `LightColor`) and
`ObjectFogTest.LinkSettingsAreResolvedBeforeAnythingReadsTheSets` pins it, including
a four-deep chain and the "negative link means none" case.

## 17. The terrain fog was a clip-space length in the wrong unit (2026-10-30)

`Renderer_Terrain`'s fog pass is the only fog the terrain had, and it could not be
seen. `shaders/*/terrain_fog.frag` read

```glsl
float distance = length(vs_out_pos_in_view);
float fog_near = g_fog_far * FOG_NEAR_FACTOR;      // FOG_NEAR_FACTOR = 0.333333
float visibility = (g_fog_far - distance) / (g_fog_far - fog_near);
```

and the vertex shader set `vs_out_pos_in_view = (g_mvp_objects * v4).xyz`.
`g_mvp_objects` is `mat_proj_persp * mat_view * mat_scale` (renderer.cpp:287), so
that varying is the **clip-space** position, and `length(clip.xyz)` is not a distance
in any unit: it is `clip.w * sqrt(x_ndc² + y_ndc² + z_ndc²)`. Meanwhile `g_fog_far`
was handed over as `fog_far_meters * WORLD_UNITS_PER_METER` (level.cpp), i.e. world
units. Two consequences, both measured:

1. **Unit mismatch.** `clip.w` is the view depth in world units *
   `RENDERER_MODEL_SCALE_DOWN` (`source/common.h:54` = 0.001f), so the shader's
   "distance" is 1000x smaller than the `g_fog_far` it was compared against. For
   LEVEL1 (`g_fog_far` = 1 058 201 world units, `fog_near` = 352 734) the terrain
   would need a view depth of ~86 km before the ramp even began; the far plane is
   `RENDER_Z_FAR = 409600` in the same scaled units (100 km), so the strongest fog
   reachable anywhere in the scene was ~8 %, at the horizon.
2. **An invented curve.** `FOG_NEAR_FACTOR = 0.333333` was a tuned constant with no
   oracle (the file said "tune this"), and the engine's terrain fog is not linear at
   all.

Both are replaced by the engine's own rule, `td_project` in
`src/glowmesh/terraind3d.c:164-167`:

```c
r = kOne / x87_exp(vz * gWorldScale * gTerrainFogScale);
tl->specular = fistp(r * 255) << 24;
```

The fragment shader now reads `float depth = 1.0 / gl_FragCoord.w` — the clip-space w
the shared `Matrices` block produces, which is `vz * gWorldScale` — and computes
`1 - exp(-depth * g_fog_scale)`. Because the depth is already in `vz * gWorldScale`
units, **`g_fog_scale` IS the engine's `gTerrainFogScale`** (authored density *
0.0005, clamped 0..16384 by `TerrainMesh_SetFog`, `terrainmesh.c:302-320`), so the
shader performs no unit conversion at all. `ubo_fog_s` therefore carries
`{color_, scale_}` and `Renderer::ApplyFogIntensity` writes
`1 / (effective_far * RENDERER_MODEL_SCALE_DOWN)`;
`ObjectFogTest.TerrainShaderCoefficientIsTheEngineTerrainFogScale` proves that
reciprocal reproduces `gTerrainFogScale` for eight authored densities, and the
object pass (which works in world units) gets `effective_far` unchanged through its
own uniforms.

Checked against the shipped corpus, the restored fog reaches the documented retail
figures: LEVEL1 is 32.1 % at 100 m, 68.7 % at 300 m and 97.9 % at 1 km; LEVEL14 is
85.7 % at 100 m; LEVEL5 is 2.4 % at 100 m.

## 18. Verification status of the 2026-10-30 changes

| Claim | Evidence | Status |
|---|---|---|
| `gFogColor`/`gFogDensity` are set 1 | address arithmetic, `mission_fog.h` kObjectLightSet; corpus prints both sets equal on all 14 levels | **Proved (static)** |
| The object pass is now fogged, with set 1's colour and density | `ObjectFogTest.EachPassReadsItsOwnAuthoredSet`, `ObjectFogCorpusTest.EveryLevelAuthorsBothTheTerrainAndTheObjectFog` | **Proved at the resolver** |
| `Fog_SetRange` is the ramp through the exponential at both edges | `ObjectFogTest.FogSetRangeMatchesTheExponentialAtBothEdges` (4 cases, 8 edge comparisons) | **Proved** |
| `link` inheritance | `ObjectFogTest.LinkSettingsAreResolvedBeforeAnythingReadsTheSets` | **Proved** |
| The terrain coefficient equals `gTerrainFogScale` | `ObjectFogTest.TerrainShaderCoefficientIsTheEngineTerrainFogScale` | **Proved** |
| Document order of the keyframes is chronological on all 14 shipped levels, and the smallest Time is unique | `ObjectFogCorpusTest.TheFirstKeyframeInDocumentOrderIsTheSmallestTime` (prints all 14) | **Proved for the corpus** |
| The fog LOOKS like retail in the viewport | — | **NOT verified.** The GUI needs OpenGL 4.1 and this host offers 2.1, so no frame was rendered. The shader arithmetic is checked by construction and by the coefficient test, not by a picture. |
| The blob shadow is fogged | — | **NOT done**, and now a named divergence: `renderer_objects_visual.cpp`'s shadow quad is the one surface left unfogged (see its comment). |
| Smoke particles are fogged | — | **NOT verified.** Retail draws particles through the same D3D device with `FOGENABLE = gFogEnable`, but IGI-RE does not reconstruct which fog start/end are live at that call, so nothing was changed. |

### 18.0 The terrain fog table really is a no-op, measured

`BuildTerrainFogTable` (`source/level/terrain_lod_settings.h:212`) was reported as "a
faithful, tested port with no caller", and the brief suggested that wiring it might be
the user's complaint. It cannot be: recomputing the engine's own loop
(`terrainmesh.c:277-292`, `gQCubeMinSize = 163` = retail's default preset) over all
fourteen authored densities gives a **maximum table value of 0.166 of 255 for LEVEL1
and 0.836 of 255 for the thickest shipped level (LEVEL14)** — not one 8-bit step on
any shipped level, so wiring it would change no pixel. The reason is in §4.2: the
table multiplies `(Float_Log2(d) - 130)` (≈133 at the top entry) by
`gTerrainFogScale` (0.000945 for LEVEL1), which is a per-unit exponent, not a 0..1
factor. It stays unwired, deliberately, with the number recorded here instead of being
presented as the fix.


### 18.1 The one trap this work found the hard way

`FogSetRangeForDepth`'s third argument is **gFogDensity**, i.e. the AUTHORED
"Fog density object category 1" **times** `kGlobalLightFogScale` — what
`MissionFog::object_fog_scale()` returns — and not the authored number.
`publish()` writes `gFogDensities[i] = s->color.fogDensity * kFogScale`
(globallight.c:115,133-134), and every reader of `gFogDensity`
(`render.c:780`, `polylist.c:245`, `d3drender.c:117-118`) consumes that scaled
value. Passing the authored density inflates the exponent by 1/0.0005 = **2000x**,
which collapses the ramp into the engine's `diff == 0` branch.

The first version of the test did exactly that, and the two platforms disagreed
about the result: MinGW i686 has an 80-bit `long double`, so `1/exp(5850)` stays a
tiny non-zero number and the ramp came out `start = -inf, end = 2050`, while the
arm64 host's 64-bit `long double` underflowed it to exactly 0 and took the
engine's `{0, 0}` branch. `FogRangeIsDegenerate` now rejects a non-finite span, so
no caller can divide by one, and the test passes the scaled density and is
platform-independent (96/96 in the Windows lane, 23/23 on the host).


## 19. The skydome was tinted with the fog colour, and the engine forbids that

Item 4 of the brief ("sky / skydome / flat sky, if the level's look depends on it")
turned up one more invention. `Renderer_Skydome::BakeVertices` blended every vertex
toward the scene fog colour —

```cpp
float fog_a = glm::pow(glm::max(0.0f, 1.0f - s), 1.5f);   // s = sin(pitch)
vb[v_idx].color_ = glm::mix(sky_col, fog_color_, fog_a);   // horizon ring
vb[v_idx].color_ = fog_color_;                             // the skirt below it
```

— with the stated intent "blend horizon with fog color so sky matches the game's
hazy look". The engine does the opposite:

* `SkyDome_Render` (`HOOK 0x52CF80`, `src/terredit/skydome.c:242`) sets
  `D3DRENDERSTATE_FOGENABLE = 0` before drawing the dome. The sky is never fogged.
* `SkyDome_Fill` (`HOOK 0x52CB60`, `skydome.c:94-135`) takes every vertex colour from
  the dome's five authored colours (`d->color[0..4]`, i.e. the level's `FlatSky`
  top/middle1/middle2/bottom1/bottom2), with the horizon angle choosing which pair
  and the rows below the horizon using `d->color + 2`. There is no fog term anywhere
  in it.
* The keyframe's "Sky color" (declared parameters 41..43) is **dead data**:
  `Sky_SetTint` (`HOOK 0x52D400`, `skydome.c:321-326`) copies it into
  `gSkyUnusedTint`, and nothing in IGI-RE reads that global. `MissionFog::sky_color`
  is therefore resolved but deliberately unused — it is not a second sky source.

So the sky's horizon and skirt showed the *fog* colour where retail shows the
level's own bottom gradient colour. The blend and the skirt fill are removed; the
dome now carries only the authored gradient, and `Renderer_Skydome::SetFogColor` (and
the `fog_color_` member it wrote) is gone rather than left dead.

**Not verified:** no frame was rendered, so this is a code-level parity fix, not an
observed one. The engine's rule is unambiguous (an explicit `FOGENABLE = 0`), which
is what makes the change safe to make blind.

