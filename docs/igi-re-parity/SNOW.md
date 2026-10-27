# Snow particles audit (#57)

Status: **audit + extracted rules; the snow half is WIRED, the `snow.tex` binding
included. See §9.**

* Audit: this document.
* Retail rules: `source/runtime/rain_rules.{h,cpp}` — the **snow-specific**
  additions are the `R7` block (header) and the `R7 - snow is rain's snow flag`
  block (implementation).
* Tests: `tests/test_rain_rules.cpp`, the `R7` section plus
  `RainRulesCorpus.LevelsSevenAndTwelveAuthorSnow`, and
  `tests/test_weather_texture.cpp` for the texture binding.
* Rain's own audit: [`RAIN.md`](RAIN.md) — this file is the snow half and
  repeats only what differs.

Every claim about engine behaviour is cited to IGI-RE
(`/Users/haseeb-mir/Documents/Code/IGI-RE`) as function + address + file:line.
open-igi (C#) is **not** an authority and was not consulted; where the editor's
own code cites it, that is recorded as a defect (§5). No frame was rendered:
the editor needs GL 4.1+ and this host offers GL 2.1, so **nothing visual or
performance-related is verified here**.

---

## 1. Why the issue's premise is wrong

The triage entry for #57 records **"no IGI-RE symbol"**, and that observation is
correct but is not a dead end — it is the answer. There is no `Snow` symbol, no
`snow.c`, no snow task type and no snow parameter table anywhere in IGI-RE. Snow
is the **same particle system as rain**, switched by one field:

```c
/* src/dxinit/rain.c:144-153, Rain_New HOOK 0x498960 */
Rain *Rain_New(void *world, char *texture, int rain, float height, float depth)
{
    ...
    r->snow = rain == 0;
```

`Rain::snow` is one `int` in the `Rain` task (`src/dxinit/rain.c:72`, the struct
at `rain.c:59-83`). The authored selector is the `Is Rain` `bool8` parameter
(`RainEffect_Init` HOOK `0x436780`, `src/fxobj/rain.c:77`), read out of
`RainEffect::unk24` and passed as `Rain_New`'s third argument
(`RainEffect_Changed` HOOK `0x436A50`, `fxobj/rain.c:141`). Because
`r->snow = rain == 0`, **the parameter is inverted**: `TRUE` selects rain
streaks, `FALSE` selects snow flakes.

So #57 is not "port the Snow system". It is "port the branches that read
`r->snow`", and every one of them is a conditional inside a `Rain_*` function:

| Rule | Site | Function (HOOK) | IGI-RE file:line |
|---|---|---|---|
| the flag itself | `r->snow = rain == 0` | `Rain_New` `0x498960` | `dxinit/rain.c:153` |
| speed | `speed = r->snow ? kSnowSpeed : kRainSpeed` | `Rain_Update` `0x499CF0` | `dxinit/rain.c:572` (statics at `122-123`) |
| spawn z | `d->z = kLayer - speed` | `Rain_Update` `0x499CF0` | `dxinit/rain.c:631` |
| sway + tile wrap | `if (r->snow) { … }` | `Rain_Update` `0x499CF0` | `dxinit/rain.c:592-617` |
| quad geometry | `if (r->snow == 1) { … }` | `Rain_FillVB` `0x499090` | `dxinit/rain.c:337-364` |
| billboard matrix | `if (r->snow == 1) { … } else { … }` | `Rain_FillVB` `0x499090` | `dxinit/rain.c:382-392` |
| texture + primitive | `if (r->snow == 1) { … } else { … }` | `Rain_DrawCell` `0x499430` | `dxinit/rain.c:430-459` |
| texture load | `Game_AddTexture("snow.tex")` | `RainEffect_Init` `0x436780` | `fxobj/rain.c:82` |

The module exposes this exact list as machine-readable data
(`igi::rain::SnowRuleDifferences()`), and a test asserts every row is cited.

**Everything else is shared** and must not be duplicated for snow: the same
`RainDrop drops[80]` pool (`rain.c:65`), the same spawn accumulator
(`rain.c:619-636`), the same camera-anchored 12-tile lattice and 9×9 draw window
(`rain.c:220-303`), the same `kLayer = 24576` (6 m) wrap band (`rain.c:590-591`),
the same `kTop = 73728` (18 m) three-layer draw band (`rain.c:464-468`), the same
per-cell `Coll_DropPointsMat` ground probe (`rain.c:257-269`), the same 60-tick
`fade` ramp (`rain.c:574-583`), the same four vertex groups selected by tile
parity (`rain.c:404,408`), and the same blend/clip render states.

### 1.1 What is *not* snow

Two near-misses are recorded so they are not mistaken for the snow path:

* `RainObj_*` (`src/cutscene/rain.c`, HOOK `0x4F65E0`…, task type
  `"RainDynCubeObj"`, `cutscene/rain.c:568`) — a world-space DynCube volume with
  an eleven-parameter schema. A different feature; out of scope for #57 exactly
  as it is for #58 (RAIN.md N6).
* `gRaining` (`src/game/game.c:1112`, `AT 0x57B1A0` per `addr.c:835`) — a
  `signed char` set by `Game_SetRaining` (`0x416110`, `game.c:516-522`) and read
  by `Game_IsRaining` (`0x416FB0`, `game.c:1058-1062`). Its only writer is the
  **cutscene** `RainDynCubeObj_SetCallback(Game_SetRaining)` (`game.c:461`); it
  has nothing to do with `RainEffect`, with `Rain::snow`, or with
  `gRainEnabled` (`rain.c:692`, the global that actually gates `Rain_DrawCell`
  at `rain.c:410-411`).

---

## 2. The snow rules

Labels `S1`…`S7` are used here and in the `R7` block of
`source/runtime/rain_rules.h`.

**S1 — snow is selected by an inverted authored boolean.**
`r->snow = rain == 0` (`rain.c:153`), fed from `RainEffect::unk24`
(`fxobj/rain.c:141`), the `Is Rain` `bool8` parameter (`fxobj/rain.c:77`).
`RainEffect_Create` defaults it to `1` (`fxobj/rain.c:89`), i.e. **rain**, not
snow. API: `SnowFromAuthoredIsRain`.

**S2 — speed, and everything that follows from it.**
`speed = r->snow ? kSnowSpeed : kRainSpeed` with `kSnowSpeed = 204.8f` and
`kRainSpeed = 2048.0f` (`rain.c:122-123`, consumed at `rain.c:572`). Snow falls
at exactly **one tenth** of rain: 204.8 units/tick = **0.05 m/tick** against
0.5 m/tick. There is still no gravity and no acceleration — the sole vertical
motion is `d->z = d->z - speed` (`rain.c:587-589`). API:
`SpeedUnitsPerTick`, `SpawnZUnits`, `LifetimeTicks`.

Two rules are *derived from* the speed rather than branching on `snow`:

* **S2a — spawn rate.** The accumulator is `acc = speed + r->spawn; while (acc >
  kSpawnStep) { … }` with `kSpawnStep = 307.2f` (`rain.c:619-636`, `689`), so the
  steady-state rate is `speed / kSpawnStep`: **6.6667 particles/tick for rain,
  0.6667 for snow** — snow fills the 80-slot pool **ten times more slowly**.
  The per-tick count is not constant, because the test is a strict `>`: rain
  alternates 6/7 and snow alternates 0/1, and snow's **first flake is spawned on
  the second `Rain_Update`**, not the first (tick 1 reaches only `acc = 204.8`,
  which is not `> 307.2`). API: `SpawnRatePerTick`, plus `SpawnCountForTick`.
* **S2b — lifetime.** `if (z < 0) d->z = z + kLayer` with `kLayer = 24576.0f`
  (`rain.c:590-591`, `681`), so the wrap band is always 6 m and a particle's
  lifetime is `kLayer / speed` = **12 ticks (rain) / 120 (snow)**. The authored
  `Traceline start`/`Traceline end` do **not** size this band; they are scaled by
  4096 and used only to size the ground raycast (`fxobj/rain.c:138-141`,
  `rain.c:159-160,258,263-265`). API: `LifetimeTicks`, `WrapZ`.

**S3 — spawn position.**
`d->z = kLayer - speed` (`rain.c:631`) is the same statement for both modes, so
the mode's speed changes it: a snow flake is created at `24576 - 204.8 =
24371.2` units (**5.95 m**, one speed step below the 6 m band top) where a rain
streak is created at `22528` units (5.5 m). The horizontal spawn is shared and
mode-independent: `x = Random_Float() * kTileF; d->y = Random_Float() * kTileF`
(`rain.c:627-629`), i.e. uniform in `[0, 10895.36)` units. API: `SpawnZUnits`.

**S4 — the horizontal random walk ("sway").**
`rain.c:592-617`, snow only:

```c
if (d->timer-- < 0) {                                   /* 595 */
    d->timer += Random_Int(gDxRainRandom, 30) + 30;     /* 596 */
    d->dx = (Random_Float(gDxRainRandom) - kHalfF) * kSway;  /* 597 */
    d->dy = (Random_Float(gDxRainRandom) - kHalfF) * kSway;  /* 598 */
}
x = d->dx + d->x;  d->x = x;                            /* 600-601 */
d->y = d->dy + d->y;                                    /* 602 */
```

* `kSway = 136.53334f` (`rain.c:691`) and `Random_Float` is in `[0, 1)`
  (`math/random.c:22-26`), so each component is in **[-68.26667, +68.26667)
  units** and is a **per-tick displacement**, held constant until the next
  re-roll. There is no acceleration and no easing.
* `Random_Int(r, n)` is `Random_Next() % n` (`math/random.c:28-31`), so
  `Random_Int(30) + 30` is **30..59 ticks**.
* The re-roll test is the post-decrement `d->timer-- < 0`, and the add runs
  *after* the decrement, so the stored timer becomes
  `(old - 1) + (Random_Int(30) + 30)` = `Random_Int(30) + 28` = **28..57**.
* `d->timer = 0` at spawn (`rain.c:630`), so a fresh flake's **first** sway roll
  happens on its **second** update (update 1 takes `0 → -1` without rolling).
  Until then the flake is displaced by whatever `dx`/`dy` its drop slot already
  held — `Rain_Update` never initialises them (see §7).

API: `SnowSwayDisplacementUnits`, `SnowSwayReloadTicks`,
`SnowSwayTimerAfterReload`, `SnowSwayFirstRollTick`, `SnowSwayHalfRangeUnits`.

**S5 — the tile wrap.**
`rain.c:603-616`. When the sway pushes a coordinate out of the `[0, kTileF]`
square, the engine adjusts it by **one** `kTileF = 10895.36f` (`rain.c:690`) and
XORs a bit of `RainDrop::kind` — bit 0 for x (`rain.c:605`), bit 1 for y
(`rain.c:612`) — which selects the vertex group the flake is written into
(`rain.c:333-334`). The tests are strict (`!(x >= 0)` and `x > kTileF`), so
`x == 0` and `x == kTileF` do not wrap, and the adjustment is a single
conditional add/subtract, **not a modulo**. API: `SnowWrapTileX`,
`SnowWrapTileY`, `kSnowWrapKindBitX`, `kSnowWrapKindBitY`.

**S6 — the flake quad and its material.**
* `size = r->fade * kFlakeSize` (`rain.c:309`) with `kFlakeSize = 81.92f`
  (`rain.c:685`), and the quad is built as `vx ± size`, `vy ± size` in camera
  space (`rain.c:338-342`), so the flake is **163.84 units = 0.04 m across**
  (half-extent 0.02 m). `fade` scales it, so the flake grows from zero as the
  effect fades in (`rain.c:574-583`).
* Six vertices per flake: four corners plus `v[4] = v[1]; v[5] = v[2]`
  (`rain.c:344-363`), `v += 6` (`rain.c:364`), `mult = 6` and
  `D3DPT_TRIANGLELIST` (`rain.c:446-447`). Rain is the other branch: two vertices
  (`rain.c:368-372`, `v += 2`), `mult = 2` and `D3DPT_LINELIST`
  (`rain.c:457-458`).
* Every quad vertex is `0xFFFFFFFF` — opaque white (`rain.c:353`). Rain's top
  vertex is `0x00FFFFFF` (alpha byte 0, fully transparent) and its bottom vertex
  is `r->color` (`rain.c:366-367`), which is the alpha rule of RAIN.md R6.
* The per-particle vertex budget explains the 0x780-vertex buffer
  (`rain.c:140`): four groups of 480 vertices (`rain.c:328`), and
  `80 flakes × 6 vertices = 480` fits one group exactly.

API: `SnowFlakeWidthUnits`, `SnowFlakeSizeUnits`, `SnowVerticesPerParticle`,
`RainVerticesPerParticle`, `SnowFlakeDiffuse`, `RainStreakTopDiffuse`.

**S7 — `snow.tex` sampling, and the billboard matrix.**
* `RainEffect_Init` loads the texture once, unconditionally, for both modes:
  `gRainTexture = Game_AddTexture("snow.tex")` (`fxobj/rain.c:82`).
  `Game_AddTexture` (`0x416930`, `game/game.c:835-848`) interns a
  `GameTexture { char name[0x18]; char tex[0x24]; }` (`game.c:167-170`), and
  `Game_LoadTextures` (`0x416130`, `game.c:524-535`) loads every registered
  texture from `LOCAL:common/textures/<name>`.
* `RainEffect_Changed` passes `gRainTexture + 0x18` — the embedded `Texture`
  (`res/restypes.h:13-25`) — as `Rain_New`'s `texture` argument for **both**
  modes (`fxobj/rain.c:141`); `Rain_New` stores it in `Rain::texture`
  (`rain.c:154`) for both. Only the snow branch dereferences it:
  `SetTexture(device, 0, *(IDirectDrawSurface7 **)(r->texture + 0x20))`
  (`rain.c:431-432`), i.e. `Texture::surface` at offset 0x20 of the embedded
  `Texture`. The rain branch binds `NULL` (`rain.c:449`).
* Stage-0 texture-argument count is `k = 4` when the pointer is non-null and
  `k = 3` when it is null (`rain.c:433-438`).
* The flake quad is camera-facing in two places: the corner positions are the
  drop's local position rotated by the camera basis
  (`rain.c:338-340`), and `Rain::matrix` is filled from that same basis times
  `gWorldScale` (`rain.c:382-388`). Rain instead uses a pure scale matrix
  (`rain.c:390-392`).

API: `SnowTextureName`, `SnowTextureStageArgCountWithTexture`,
`SnowTextureStageArgCountWithoutTexture`.

**S8 — the shared per-cell draw.**
`Rain_DrawCell` sets one world matrix per 2×2 cell block and then walks three
`kLayer`-tall layers, calling `DrawPrimitiveVB(device, prim, r->vb, q * 480,
n * mult, 0)` (`rain.c:461-484`). Snow differs only in `prim`, `mult` and the
bound texture (S6/S7); the layer walk, the `h` clip
(`h = max(cell ground z) - it->z - kTopF`, `rain.c:403`) and the `gRainEnabled`
gate (`rain.c:410-411`) are shared.

---

## 3. The authored schema (unchanged from RAIN.md R7)

Snow does not add, remove or rename a parameter. The five-parameter
`ParamTable_Add` sequence in `RainEffect_Init` (`fxobj/rain.c:76-81`) is the
whole schema; only parameter 0 selects the mode:

| # | QSC name | Type | Struct field | Snow meaning |
|---|---|---|---|---|
| 0 | `Is Rain` | `bool8` | `unk24` | **FALSE ⇒ snow** (`rain.c:153`) |
| 1 | `Traceline start` | `Real32` | `unk28` | ground-probe start height, ×4096 (`fxobj/rain.c:138-141`) |
| 2 | `Traceline end` | `Real32` | `unk2C` | ground-probe depth (`fxobj/rain.c:160`) |
| 3 | `Is Active` | `VarString` | `expr` | re-evaluated every tick (`fxobj/rain.c:102-103`) |
| 4 | `Rain Alpha` | `Real32` | `level` | `trunc(a * 255)` into the vertex byte (`rain.c:646-649`) |

Snow therefore inherits the whole of RAIN.md R7, including the inverted
`Is Rain`, the `Is Active` expression, the `Rain Alpha`-only-rebuild heuristic
(`fxobj/rain.c:131-146`) and the fact that the declared 5..150 / 0..1 ranges are
editor-UI metadata that nothing reads back.

---

## 4. Retail data: which levels author snow

The engine's own default is rain (`fxobj/rain.c:89`), and of the three shipped
levels that have a decompiled QSC text form only LEVEL4 declares a `RainEffect`
at all — as rain. **Two levels author snow**, and they are only visible through
the retail binary `OBJECTS.QVM`, which the editor's own decompiler
(`source/level/qvm_decompiler.cpp`) renders to QSC. Decompiling the retail
files gives:

| Level | Authored `Task_New` | Mode | Active | Alpha |
|---|---|---|---|---|
| LEVEL4 | `Task_New(-1, "RainEffect", "", TRUE, 50.0, 20.0, "FALSE\n", 0.059999998658895493)` | rain | no | 0.06 |
| LEVEL7 | `Task_New(-1, "RainEffect", "", FALSE, 50.0, 20.0, "1", 0.059999998658895493)` | **snow** | yes | 0.06 |
| LEVEL12 | `Task_New(-1, "RainEffect", "", FALSE, 50.0, 20.0, "1", 0.3125)` | **snow** | yes | 0.3125 (default) |

(Command used, from this worktree: a host probe built by
`scripts/run-host-tests.sh` linking `source/level/qvm_parser.cpp` +
`source/level/qvm_decompiler.cpp` and calling `QVM_Parse` then
`QVM_DecompileToString` on
`/Users/haseeb-mir/Downloads/IGI/MISSIONS/location0/LEVEL{4,7,12}/OBJECTS.QVM`.
The permanent form of that check is
`RainRulesCorpus.LevelsSevenAndTwelveAuthorSnow` in `tests/test_rain_rules.cpp`.)

Consequences pinned by the tests:

* Both snow levels are **active** (`Is Active` is the literal `"1"`), so snow is
  the weather a player actually sees on LEVEL7 and LEVEL12.
* Both keep the `RainEffect_Create` defaults for the traceline band (50.0 /
  20.0), so the band values do **not** distinguish snow from rain and do not
  size the 6 m wrap band (S2b).
* LEVEL7's authored alpha 0.06 becomes vertex byte `trunc(0.06 × 255) = 15`;
  LEVEL12's 0.3125 becomes `79` (`rain.c:646-649`, RAIN.md R6).
* The retail LEVEL4/LEVEL2/level1 `objects.qsc` text files contain **no**
  snow declaration; the per-level authored table in
  `tests/test_level_weather.cpp:113-119` agrees (levels 7 and 12 are the snow
  entries).

---

## 5. What the editor does instead

> **Status note (revision after the wiring).** The table in §5.1 records the
> renderer as it stood when this audit was written: a vertex-shader particle field
> with sinusoidal drift and no texture. `source/renderer/renderer_rain.cpp` has
> since been rewritten onto `rain_rules` (the pool, the tick, the tile wrap, the
> sway, the four vertex groups, the layer walk and the fade are the engine's now),
> and the `snow.tex` binding — S-D8 — is wired (§9). The rows still true are S-D3
> (the editor's **rain** buffer is still the invented 1200-drop field) and S-D10
> (no `Rain_ClipCells` outcodes, and the per-layer `n` clip of `rain.c:465-484` is
> not applied). The rest describe code that no longer exists and are kept only as
> the record of what the audit found.

The editor has no snow *system* either — it takes the same shortcut as the
engine, by treating `Is Rain` FALSE as "snow". Entry points:

* `source/renderer/renderer_rain.{h,cpp}` — the GPU particle field
  (`Renderer_Rain::SetParams`, `Draw`, `DrawSnow`, and the `SNOW_VERT_SRC` /
  `SNOW_FRAG_SRC` shaders).
* `source/renderer/weather_math.h` — the editor's particle constants.
* `source/runtime/weather_particle_style.h` — the editor's snow length.
* `source/runtime/level_weather.{h,cpp}`, `source/runtime/weather_mode.cpp` —
  the `RainEffect` parser and mode override.
* `source/app_level.cpp:140-152` — the wiring and the "SNOW"/"RAIN" label.

### 5.1 Diverges from retail (snow-specific defects)

| # | Rule | Editor behaviour | Editor file:line | Oracle |
|---|---|---|---|---|
| S-D1 | S1 | `is_snow = !is_rain` — correct | `runtime/level_weather.cpp:76-77,87` | `r->snow = rain == 0`, `rain.c:153` |
| S-D2 | S2 speed | per-flake, per-**second** speed `(0.025 + seed.z*0.040) * fallRange`; for the retail 50/20 band ≈1.75..4.55 m/s | `renderer/renderer_rain.cpp:67`; `renderer/weather_math.h:15-16` | constant 204.8 units (0.05 m) **per tick**, no gravity, `rain.c:122,572,587-589` |
| S-D3 | S2a spawn count | a fixed 900-flake vertex buffer, no accumulator, no pool, so the count is constant instead of ramping to 80 | `renderer/weather_math.h:8`; `renderer/renderer_rain.cpp:176-190` | `drops[80]` + accumulator at 0.6667/tick, `rain.c:65,619-636` |
| S-D4 | S4 sway | a **deterministic sinusoid**, `sin(u_time*0.45 + seed.x*17) * 0.35 m` (and a cosine on y), i.e. a smooth periodic path with no re-roll | `renderer/renderer_rain.cpp:70-73`; `renderer/weather_math.h:22` | constant per-tick `(dx, dy)` re-rolled every 30..59 ticks, components in (-68.267, +68.267) units, `rain.c:592-617,691` |
| S-D5 | S5 wrap | the box is wrapped with `mod()` in the shader, so flakes slide continuously and never jump a tile or change vertex group | `renderer/renderer_rain.cpp:70-73`; `renderer/weather_math.h:9` | a single ±`kTileF` step that XORs the `kind` bit, `rain.c:603-616` |
| S-D6 | S6 extent | 0.045 m half-extent ⇒ a 0.09 m quad; also used as the "ground visible particle length" | `renderer/weather_math.h:12`; `runtime/weather_particle_style.h:8-10` | `kFlakeSize = 81.92` half-extent ⇒ **0.04 m across**, `rain.c:341-342,685` |
| S-D7 | S6 material | hardcoded `vec4(0.92, 0.97, 1.0, clamp(u_alpha*1.75, 0.10, 0.42))`, one constant alpha per quad | `renderer/renderer_rain.cpp:84-91`; `renderer/weather_math.h:19-20,28-30` | opaque-white vertex diffuse, alpha byte `trunc(a*255)`, `rain.c:353,646-649` |
| S-D8 | S7 texture | **WIRED — see §9.** `snow.tex` is resolved as `LOCAL:common/textures/snow.tex` out of the mounted pack, its UVs are the engine's and the shader reproduces stage 0's MODULATE / SELECTARG2 pair | `source/renderer/renderer_objects_texture.cpp` (`GetOrLoadGameTexture`); `source/renderer/renderer_rain.cpp` (`SNOW_VERT_SRC`, `SNOW_FRAG_SRC`, `DrawSnow`) | `Game_AddTexture("snow.tex")` `fxobj/rain.c:82`, bound at `rain.c:431-432` |
| S-D9 | S7 billboard | the flake quad is axis-aligned in world space with a `z + a_corner.x * u_halfExtent` skew, not a camera-facing billboard | `renderer/renderer_rain.cpp:77-79` | corners are the local position rotated by the camera basis, `rain.c:338-342,382-388` |
| S-D10 | S8 vertex budget | one draw call, `GL_TRIANGLES`, 6 vertices per flake, no four-group split | `renderer/renderer_rain.cpp:280,317` | four 480-vertex groups selected by tile parity, `rain.c:328,404,408,446-447` |
| S-D11 | S6 fade | no fade: snow appears and disappears instantly | (absent) | `fade` ramps by 1/60 per tick and scales the flake, `rain.c:309,574-583` |
| S-D12 | S2b lifetime | the wrap band is the authored `camera.z + start .. camera.z - end` (70 m for retail 50/20), not the fixed 6 m | `renderer/renderer_rain.cpp:34,248-249` | fixed `kLayer` 6 m wrap, 120 ticks, `rain.c:590-591,681` |

S-D2, S-D4, S-D5, S-D6, S-D7 and S-D9 are the snow-specific core: the editor's
snow is a smooth sinusoidal GPU field with a boosted alpha and no texture, and
the oracle's is a 6 m wrap band of 0.04 m textured billboards that walk in a
straight line for 30..59 ticks at a time.

### 5.2 Matches

| # | Rule | Editor | Oracle |
|---|---|---|---|
| S-M1 | S1 selection and inversion | `is_snow = !is_rain` (`runtime/level_weather.cpp:87`) | `r->snow = rain == 0` (`rain.c:153`) |
| S-M2 | S2 no gravity | the editor's flake z is a linear function of time | no acceleration term anywhere in `Rain_Update` (`rain.c:586-618`) |
| S-M3 | S6 both modes are the same feature | one `Renderer_Rain` with an `is_snow_` flag (`renderer/renderer_rain.h:36`) | one `Rain` task with a `snow` int (`rain.c:72`) |
| S-M4 | S2b traceline band is not the wrap band | the editor uses the authored band for its box (a defect, S-D12) but does not claim it is a lifetime | `height`/`fallDist` size only the raycast (`rain.c:159-160,258`) |

---

## 6. The extracted module

The snow rules live in the existing `source/runtime/rain_rules.{h,cpp}` module,
in the `R7` block. **No file was renamed and no existing function, constant or
test changed**; the snow API is additive.

| Rule | API |
|---|---|
| S1 | `SnowFromAuthoredIsRain` |
| S2 | `SpeedUnitsPerTick`, `LifetimeTicks`, `WrapZ` |
| S2a | `SpawnRatePerTick`, `SpawnCountForTick`, `kDropCapacity`, `kSpawnStep` |
| S3 | `SpawnZUnits`, `kLayerUnits` |
| S4 | `SnowSwayDisplacementUnits`, `SnowSwayReloadTicks`, `SnowSwayTimerAfterReload`, `SnowSwayFirstRollTick`, `SnowSwayHalfRangeUnits`, `SnowSwayTicksMin/Max`, `kSwayUnits` |
| S5 | `SnowWrapTileX`, `SnowWrapTileY`, `SnowWrap`, `kSnowWrapKindBitX/Y`, `kTileUnitsF` |
| S6 | `SnowFlakeWidthUnits`, `SnowFlakeSizeUnits`, `SnowVerticesPerParticle`, `RainVerticesPerParticle`, `SnowFlakeDiffuse`, `RainStreakTopDiffuse` |
| S7 | `SnowTextureName`, `SnowTextureStageArgCountWithTexture`, `SnowTextureStageArgCountWithoutTexture` |
| all | `SnowRuleDifferences` (the citation table), `SnowRuleDifference` |

As with the rain half, **nothing calls the module**: `Renderer_Rain` computes
positions in a vertex shader from `u_time` and a per-flake seed, so its state
lives on the GPU. The module's value is that every snow number above is
executable and regression-guarded, and that a future rewrite has a cited
reference implementation. The D3D render states, the `SortRender_Add` key, the
`Rain_ClipCells` outcodes and the device-restore vertex-buffer path are
deliberately not ported — they are per-back-end plumbing with no GL analogue.

---

## 7. What is unverified

* **No frame was rendered.** The editor needs GL 4.1+; this host offers GL 2.1.
  Every visual statement above is read from source. No performance claim is made.
* **`dx`/`dy` are never initialised at spawn.** `Rain_Update` sets `d->timer = 0`
  and `d->z`, `d->kind`, `d->x`, `d->y` on a new drop (`rain.c:626-632`) but not
  `dx`/`dy`, and the first sway roll only happens on the drop's second update
  (S4). A flake's first tick therefore displaces by whatever the slot held.
  `Task_New` may or may not zero the allocation in IGI-RE, so the module does not
  model the pre-roll displacement at all. This is the one snow behaviour that is
  recorded but **not** ported.
* **The exact pool-saturation tick is float-rounding dependent.** The steady
  rate is exact (S2a), but the tick on which `dropCount` first reaches 80 falls
  out of the float accumulator and its strict `>` test; the engine computes it
  with x87 excess precision, which the module's `float` does not reproduce
  bit-for-bit. Only the rate is pinned, not the saturation tick.
* **`Rain::snow` is an `int`, not a bool**, and `Rain_New`'s third parameter is
  an `int` (`rain.c:144`); only 0 and 1 are reachable from the `bool8` schema.
  The module exposes the boolean form.
* **Tick rate.** `Rain_Update` runs from a task handler; IGI-RE does not pin the
  call rate, so any "m/s at 60 Hz" figure is an illustration.
* **The `snow.tex` image itself was not inspected.** The engine binds
  `Texture::surface` (`rain.c:432`) and the editor never loads the file, so the
  flake's actual pixels are not verified here; only the name, the load path
  (`game.c:529-532`) and the binding are.
* **The editor's divergences are read from its source**, not demonstrated by
  running it (no frame can be rendered here).
* **Only the host lane was run** for this change
  (`tests/test_rain_rules.cpp`, 48 tests, including 4 corpus tests). The Windows
  `igi_tests` binary was **not** built or run here; that suite is the gate and
  the Lead runs it.
* **Retail corpus coverage is the decompiled QVM path only.** LEVEL7 and LEVEL12
  are the two levels whose `OBJECTS.QVM` authors `Is Rain` FALSE; the other
  eleven were not individually re-checked for snow in this task, and the
  decompiled text corpus (`objects.qsc`) covers only LEVEL4, LEVEL2 and level1,
  none of which authors snow.

---

## 8. Recommended follow-up (for the Lead, not done here)

1. **S-D8 is DONE** — see §9. `LOCAL:common/textures/snow.tex` is resolved through
   the engine's own identity and pack and bound in the snow fragment shader.
2. **S-D6/S-D7 (0.09 m → 0.04 m, boosted alpha → `trunc(a*255)`) are one-line
   constant changes**, but they must be done *after* S-D11 (fade) and S-D7
   (vertex-diffuse alpha), exactly as RAIN.md §6.3 says for rain: at LEVEL7's
   authored 0.06 the faithful alpha byte is 15/255, which will look like a
   regression until the fade and the material are faithful.
3. **S-D4/S-D5/S-D9 are one decision.** The editor's snow motion is a shader
   sinusoid in world space; the oracle's is a CPU random walk in a tile-local
   frame with a camera-facing billboard. Bringing snow to parity means replacing
   the `SNOW_VERT_SRC` position model in `source/renderer/renderer_rain.cpp` —
   outside this task's write scope. **Done since:** the snow position model *is*
   the engine's random walk now (it comes from `rain_rules`), so this item now
   describes the RAIN half only.
4. **The editor's snow shader cites OpenIGI in its comments**
   (`renderer_rain.cpp:74-76`, `weather_particle_style.h:5-7`). OpenIGI is not
   an authority; those comments should point at `fxobj/rain.c` / `dxinit/rain.c`
   instead, so the next reader does not treat the C# port as the reference.

---

## 9. The `snow.tex` binding (WIRED)

The engine loads the flake texture once, for both weather modes, and binds it in
the snow branch only. Every step of that chain is now in the editor:

| Step | Engine | Editor |
|---|---|---|
| register the name | `gRainTexture = Game_AddTexture("snow.tex")`, `RainEffect_Init` HOOK 0x436780, `src/fxobj/rain.c:82`; the intern table is `src/game/game.c:835-848` | `igi::rain::SnowTextureName()` (`source/runtime/rain_rules.cpp`), passed by `Renderer::Draw` (`source/renderer/renderer_draw.cpp`) |
| build the member identity | `path = "LOCAL:common/textures/"; strcat(path, name)`, `Game_LoadTextures` HOOK 0x416130, `src/game/game.c:524-534` | `igi_res::GameTextureMemberName` (`source/renderer/res_chunks.h`) |
| the archive that answers it | `Resource_Load("LOCAL:common/textures/textures.res")`, `src/game/game.c:430`; `Texture_LoadFile` then `Resource_Load`s the member name, `src/file/texture.c:231-252` | `igi_res::kGameTexturePackName`; `<IGI>/common/textures/textures.res` indexed with `RES_BuildIndex` |
| name matching | `String_ToUpper` HOOK 0x4B5AF0 (`src/res/resource.c:98-104`) + `Resource_IsLoaded` HOOK 0x4B5B90 (`:136-138`) compare the folded full name, newest resource first | the index key is `igi_res::NormalizeResourceName(member)`; lookup with `RES_FindIndexEntry` |
| decode | `Texture_LoadFile` dispatches on `LOOP` + version, `LoopHeader11` at `src/file/texture.c:24-36`, fields consumed at `:239-243` | `Tex_LoadFromMemory` (`source/level/level_common.cpp`) |
| bind + stage 0 | `SetTexture(device, 0, texture->surface)`, `SET_TSS(0,1,4)` COLOROP = MODULATE, `SET_TSS(0,4,4)` ALPHAOP = MODULATE, args TEXTURE/DIFFUSE, `k = 4`; with `r->texture == NULL` the ops are SELECTARG2 (`k = 3`), `src/dxinit/rain.c:430-438` | `Renderer_Objects::GetOrLoadGameTexture`; the fragment shader computes `u_diffuse * texel` with `texel = 1` when the handle is 0 |
| uv | `v0..v3` carry `(0,0) (1,0) (0,1) (1,1)` and `v4 = v1`, `v5 = v2`, `src/dxinit/rain.c:344-363` | `v_uv = (a_corner + 1) / 2` in `SNOW_VERT_SRC` / `SNOW_VERT_SRC_120` |
| filtering | `SET_TSS(0,0x10,2)` MAGFILTER = LINEAR, `SET_TSS(0,0x11,1)` MINFILTER = POINT; MIPFILTER unset, `src/dxinit/rain.c:444-445` | `glTexParameteri` in `Renderer_Rain::DrawSnow` (MAG `GL_LINEAR`, MIN `GL_NEAREST`, wrap `GL_REPEAT`) |

**Verified:** `tests/test_weather_texture.cpp` (7 tests) reads the retail pack with
the product's own reader and asserts that `LOCAL:common/textures/snow.tex` is a
member, that its bytes decode as a `LOOP` v11 16x16 4-bpp image (`IMAGE_MODE_67`),
and that the identity, the UV mapping and the colour domain are the engine's.

**The colour domain, with the evidence.** The particles are `D3DFVF_DIFFUSE`
vertices (`Rain_CreateVB`, `src/dxinit/rain.c:139`), so their diffuse words are raw
8-bit `D3DCOLOR` and `Rain_SetAlpha` packs `trunc(alpha * 255)` into the top byte
(`rain.c:646-649`) — a full 0..255 range. The 1/128 domain belongs to the menus:
`gColorScale` DATA 0x534AB0 = `0.0078125f` (`src/res/qvertex.c:189`) with
`COLOR_SCALE = 1/128` (`src/math/draw.c:12`), consumed by the QVertex/Grid draw
path, which `Rain_*` never touches. The editor therefore converts with `/255`
(`renderer_rain.cpp`, the `u_diffuse` upload).

**Unverified:** the rendered pixels. This host offers OpenGL 2.1 and the editor
needs 4.1, so nothing here was seen on screen; the binding is proven by the
corpus test and the citations above, not by a frame.
