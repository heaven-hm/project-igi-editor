# Rain renderer audit (#58)

Status: **audit + extracted rule module, not wired.**

> **Snow is not a separate system.** This document covers rain and snow
> together, because they are one task in the engine: `Rain_New`'s third argument
> becomes `r->snow = rain == 0` (`src/dxinit/rain.c:153`) and every snow
> difference is a branch on that field. The snow-specific rules, their
> citations, the retail levels that author snow (LEVEL7 and LEVEL12) and the
> audit of the editor's snow path are in **[`SNOW.md`](SNOW.md)** (#57). There
> is no `Snow` symbol in IGI-RE; the module's snow API is the `R7` block of
> `source/runtime/rain_rules.h`, not a parallel module.

* Audit: this document.
* Snow half: [`SNOW.md`](SNOW.md) (#57) — read it for anything snow-specific.
* Extracted, tested retail rules: `source/runtime/rain_rules.{h,cpp}`
  (**no editor code calls it**), tests in `tests/test_rain_rules.cpp`.
* The editor's live rain path (`source/renderer/renderer_rain.cpp` +
  `source/runtime/level_weather.cpp` + `source/runtime/weather_math.h`) was read
  and is cited below, but **not modified** — it is owned elsewhere.

Every claim about engine behaviour is cited to IGI-RE
(`/Users/haseeb-mir/Documents/Code/IGI-RE`) as function + address + file:line.
open-igi (C#) is not an authority and was not consulted. No frame was rendered:
the editor needs GL 4.1+ compatibility and this host offers GL 2.1, so **nothing
visual or performance-related is verified here**.

---

## 1. What the oracle's rain actually is

The triage entry for #58 names `Rain_New` `0x498960` and `Rain_Init` `0x498AB0`.
Both exist (`symbols/names.tsv:3070,3072`) and both live in
`src/dxinit/rain.c`. They are the whole `Rain_*` family:

| Symbol | Address | File:line |
|---|---|---|
| `Rain_New` | `0x498960` | `dxinit/rain.c:144-167` |
| `Rain_Free` | `0x498A70` | `dxinit/rain.c:169-175` |
| `Rain_Init` | `0x498AB0` | `dxinit/rain.c:180-190` |
| `Rain_Restore` | `0x498B00` | `dxinit/rain.c:192-201` |
| `Rain_Render` | `0x498B80` | `dxinit/rain.c:203-216` |
| `Rain_UpdateGrid` | `0x498BD0` | `dxinit/rain.c:220-278` |
| `Rain_Draw` | `0x498F10` | `dxinit/rain.c:280-303` |
| `Rain_FillVB` | `0x499090` | `dxinit/rain.c:307-393` |
| `Rain_DrawCell` | `0x499430` | `dxinit/rain.c:397-486` |
| `Rain_ClipCells` | `0x499910` | `dxinit/rain.c:495-555` |
| `Rain_Close` | `0x499CE0` | `dxinit/rain.c:557-561` |
| `Rain_Update` | `0x499CF0` | `dxinit/rain.c:565-637` |
| `Rain_SetFalling` | `0x499F90` | `dxinit/rain.c:640-644` |
| `Rain_SetAlpha` | `0x499FA0` | `dxinit/rain.c:646-649` |
| `Rain_Enable` / `Rain_IsEnabled` | `0x499FD0` / `0x499FE0` | `dxinit/rain.c:652-662` |

Three facts settle the issue's framing:

1. **There is no `Snow` symbol anywhere in IGI-RE.** Rain and snow are the same
   task: `Rain_New`'s third argument is stored as `r->snow = rain == 0`
   (`rain.c:153`). Snow is rain with `snow == 1`. The only other weather particle
   system in the oracle is `RainObj_*` (`src/cutscene/rain.c`, HOOK `0x4F65E0`…,
   task type `"RainDynCubeObj"`, `cutscene/rain.c:568`), a world-space DynCube
   volume with an eleven-parameter schema — a *different feature*, not ported
   here and not what #58 asked about.
   **This is the answer to #57 as well, not a dead end**: the snow branches are
   enumerated, cited and ported in `SNOW.md` and in the `R7` block of
   `source/runtime/rain_rules.h`. The triage note "no IGI-RE symbol" is true of
   the *name* and false of the *behaviour*.
2. **The task is owned by a level-authored `RainEffect` object**, not by the
   level's sky or weather globals. `RainEffect_Init` (`0x436780`,
   `fxobj/rain.c:60-84`) registers the task type, the five-parameter schema and
   the snow texture; `RainEffect_Changed` (`0x436A50`, `fxobj/rain.c:131-146`)
   constructs the `Rain`.
3. **`Rain_Init` is trivial**: it seeds the RNG, registers the task type, hooks
   `Rain_Render` into handler slot 4 and adds a device-restore callback
   (`rain.c:180-190`). Everything interesting is in `Rain_Update`,
   `Rain_UpdateGrid` and `Rain_DrawCell`.

---

## 2. The extracted rules

`R#` labels are used in the audit table and in `source/runtime/rain_rules.h`.

**R1 — particle count and how it is chosen.**
The pool is a fixed `RainDrop drops[80]` (`rain.c:65`). Drops are *created*, never
recycled through a free list: `Rain_Update` runs an accumulator,
`acc = speed + r->spawn; r->spawn = acc; while (acc > kSpawnStep) { … }`
(`rain.c:619-636`), emitting one drop per whole `kSpawnStep = 307.2` units
(`rain.c:689`) and stopping dead at `if (r->dropCount == 80) return;`
(`rain.c:624-625`). Steady state is `speed / kSpawnStep` drops per tick —
**6.667 per tick for rain, 0.667 for snow** — so the pool saturates in 12 ticks
(rain) or 120 ticks (snow) and never shrinks.

**R2 — spawn volume relative to the camera.**
A camera-anchored lattice of `kTile = 10895.3603515625`-unit tiles
(`rain.c:678`), snapped by `ix = floor((cam.x - kGridOrigin) * kTileInv + 0.5)`
with `kGridOrigin = 59924.48193359375` (`rain.c:223-224,680`). The cell array is
`RainCell cells[24*24]` (`rain.c:67`) but it is a **2×2 mirror of a 12×12
fundamental block** — `rain.c:249,253` add `kTwoTiles = 130744.32421875`
(= 12 × kTile) for `i < mx`, and `rain.c:266-269` copy each cell into
`c[12]`, `c[24*12]`, `c[24*12+12]`. `Rain_Draw` then walks a **9×9 window
starting at array cell `(cellX + 1, cellY + 1)`** (`rain.c:282,288-289`).

Derived (and pinned by `RainRules.DrawnWindowIsTheNineTilesAfterTheCameraTile`):
the drawn world tiles are exactly the nine consecutive tiles
`(ix+1 … ix+9, iy+1 … iy+9)`, i.e. a **23.9400 m × 23.9400 m** block that begins
one tile (2.66 m) ahead of the camera's snapped tile in +x and +y. Within a tile
a drop's local x/y are uniform in `[0, kTileF]` (`rain.c:627-629`) and are added
to the tile origin at draw time (`rain.c:391,461-462`).

> **Unverified, and flagged as odd.** The window's `+1` tile offset and its
> one-sidedness in +x/+y fall out of the decompiled constants and indices; I
> could not render a frame, so I cannot say whether the retail camera's world
> axes or some other transform makes that look symmetric. Treat the derivation
> as read-from-code, not as an observed behaviour.

**R3 — velocity and gravity per tick.**
There is **no gravity and no acceleration**. `speed = r->snow ? 204.8 : 2048.0`
(`rain.c:122-123,572`) and the only motion is `d->z = d->z - speed`
(`rain.c:587-589`). That is 0.5 m/tick for rain and 0.05 m/tick for snow. Snow
adds a horizontal random walk: every `Random_Int(30) + 30` ticks (i.e. 30..59,
`rain.c:595-596`) it re-rolls `dx, dy = (Random_Float() - 0.5) * kSway` with
`kSway = 136.53334` (`rain.c:597-598,691`), so each component lies in
`(-68.267, +68.267)` units, and wraps across tile edges flipping the `kind` bits
(`rain.c:603-616`).

**R4 — lifetime and recycling.**
A drop that falls below `z = 0` is wrapped, not destroyed:
`if (z < 0) d->z = z + kLayer` with `kLayer = 24576.0` (`rain.c:590-591,681`).
The band is therefore always **6 m tall** and a drop's lifetime is
`kLayer / speed` = **12 ticks (rain) / 120 ticks (snow)**. The authored
"Traceline start"/"Traceline end" do **not** size this band: they are scaled by
4096 (`fxobj/rain.c:138-141`) and become `height` / `fallDist = height + depth`
(`rain.c:159-160`), which are used *only* to size the ground raycast
(`rain.c:258,263-265`) — `c->z` is overwritten with the collision hit
(`rain.c:265`).

Vertically the drops are drawn in **three 6 m layers** (`Rain_DrawCell`,
`rain.c:464-468`) under `kTop = 73728.0` (`rain.c:687`) from a camera z snapped
to the `kLayer` lattice (`rain.c:283`, `kLayerOrigin = 36864.0`,
`kLayerInv = 1/24576`, `rain.c:683-684`). Derived and pinned
(`RainRules.VerticalBandIsEighteenMetresAroundTheCamera`): the band is exactly
`kTop` = **18 m tall**, sits **6..12 m below** the camera at its bottom and
**6..12 m above** at its top, and slides in 6 m steps as the camera crosses a
layer boundary.

**R5 — splash / impact.**
**None.** There is no splash geometry, no impact particle and no impact sound
anywhere in `Rain_*`. The only ground interaction is:
* `Coll_DropPointsMat` finds the ground height of each cell, once, when the cell
  is (re)acquired (`rain.c:257-269`), and
* `Rain_DrawCell` clips drops that have fallen *below* that surface:
  `h = max(cell ground z) - it->z - kTopF` (`rain.c:403`) and per layer it draws
  only the drops with `list->z[n] <= h` (`rain.c:465-483`).

**R6 — texture and material.**
* **Rain** binds **no texture**: `SetTexture(device, 0, NULL)` (`rain.c:449-450`),
  stage-0 colour/alpha come from the vertex diffuse (`rain.c:451-455`), and it is
  drawn as `D3DPT_LINELIST`, 2 vertices per drop (`rain.c:457-458`). The two
  vertices are `(d->z + len)` and `d->z` with `len = fade * kStreak`,
  `kStreak = 2048.0` = **0.5 m** (`rain.c:309-311,671`). The **top** vertex
  diffuse is `0xFFFFFF` (ARGB alpha byte 0, i.e. fully transparent) and the
  **bottom** is `r->color` (`rain.c:366-367`), so the streak fades along its
  length.
* **Snow** binds `r->texture`, which `RainEffect_Changed` passes as
  `gRainTexture + 0x18` where `gRainTexture = Game_AddTexture("snow.tex")`
  (`fxobj/rain.c:82,141`; consumed at `rain.c:431-432`), and is drawn as a
  camera-facing `D3DPT_TRIANGLELIST` quad, 6 vertices, `kFlakeSize = 81.92`
  half-extent = **0.04 m across** (`rain.c:309`, `kFlakeSize` at `rain.c:685`),
  with a vertex diffuse of `0xFFFFFFFF` (`rain.c:353`).
* `Rain_SetAlpha` (`0x499FA0`, `rain.c:646-649`) is the alpha rule:
  `r->color = 0xFFFFFF - ((unsigned)fistp(alpha * kAlphaScale) << 24)` with
  `kAlphaScale` = `kMinus255` = **-255.0f** (`polylist.c:432,434`, `addr.c:286`).
  `fistp` is x87 FISTP in the *current* rounding mode (`dxinit.h:22-29`), and the
  game sets that to **round toward zero** via `FPU_SetTruncate`
  (`dxinit.c:75-80`, HOOK `0x498460`) called once from `AppMain_WinMain`
  (`win/appmain.c:212`). Derived and pinned (`RainRules.AlphaByteMatchesTheLiteralBitTrick`):
  the trick yields a white ARGB colour whose alpha byte is exactly
  `trunc(alpha * 255)` — e.g. the default `0.3125` → **79**.
* **Fade**: `Rain_Update` ramps `fade` by `kFadeStep = 1/60`
  (`rain.c:574-583`, `kFadeStep` at `rain.c:665`), so a full fade is 60 ticks;
  `fade` scales both the streak length and the flake size (`rain.c:309,311`) and
  at `fade == 0` `Rain_Render` returns early and clears `hasLast`
  (`rain.c:209-212`).

**R7 — every authored parameter a level can set.**
The authoritative schema is the `ParamTable_Add` sequence in `RainEffect_Init`
(HOOK `0x436780`, `fxobj/rain.c:76-81`), whose names the shipped retail data
repeats verbatim in `MISSIONS/location0/LEVEL4/objects.qsc:55`:

| # | QSC name | Type | Struct field | Range (`RainEffect_Init`) | Default (`RainEffect_Create`, `fxobj/rain.c:86-96`) |
|---|---|---|---|---|---|
| 0 | `Is Rain` | `bool8` | `unk24` | — | `1` (TRUE) |
| 1 | `Traceline start` | `Real32` | `unk28` | `gRainSizeRange` 5..150 | `50.0` |
| 2 | `Traceline end` | `Real32` | `unk2C` | `gRainSizeRange` 5..150 | `20.0` |
| 3 | `Is Active` | `VarString` | `expr` | — | `""` |
| 4 | `Rain Alpha` | `Real32` | `level` | `gRainLevelRange` 0..1 | `0.3125` |

Semantics that the names hide:

* **`Is Rain` is inverted.** `Rain_New` does `r->snow = rain == 0`
  (`rain.c:153`), so TRUE = streaks, FALSE = flakes.
* **`Is Active` is an expression, not a boolean.** `RainEffect_Run`
  (`0x436990`, `fxobj/rain.c:98-106`) re-evaluates the `VarString` **every tick**
  and feeds the result to `Rain_SetFalling` — a level can turn precipitation on
  and off from script state.
* **`Rain Alpha` is the only field whose change does not rebuild the object.**
  `RainEffect_Changed` frees and rebuilds only when
  `EQ_OR_NAN(t->level, t->lastLevel)` — i.e. when the level is *unchanged*
  (`fxobj/rain.c:131-142`; `EQ_OR_NAN` at `fxobj/fxobj.h:62` is true when equal).
  A change to any other field rebuilds; a level-only change just calls
  `Rain_SetAlpha`. It is a heuristic proxy for "which field changed", and it is
  quoted here so nobody re-derives it as a bug.
* The declared ranges are **editor-UI metadata, not load-time clamps**:
  `ParamTable_Add` (`0x4F1A80`, `src/param/param.c:155-170`) stores the range
  pointer in the entry's `extra` slot and nothing in `src/param/param.c` reads it
  back. The ported module therefore exposes the ranges as constants but does not
  clamp.
* **Engine enable**: drawing is gated by the single global `gRainEnabled`
  (`rain.c:410-411` inside `Rain_DrawCell`; `Rain_Enable` `0x499FD0`,
  `rain.c:652-656`), initialised to 1 (`rain.c:692`).

**R8 — task lifetime.** `Rain_Render` runs from handler slot 4
(`rain.c:187`) and `RainEffect_Run` (`fxobj/rain.c:98-106`) drives `Rain_Update`
once per task tick. IGI-RE does **not** pin that to a fixed rate, so any
per-second figure below is an illustration at 60 Hz, not an engine constant.

---

## 3. Audit of the editor

Editor-side entry points, all read, none modified:

* `source/renderer/renderer_rain.{h,cpp}` — the GPU particle field
  (`Renderer_Rain::Init/Draw/DrawSnow`).
* `source/renderer/weather_math.h` — the editor's particle constants.
* `source/runtime/weather_particle_style.h`, `weather_visibility.h`,
  `weather_mode.{h,cpp}` — the editor's own policy layer.
* `source/runtime/level_weather.{h,cpp}` — the `RainEffect` parser.
* `source/app_level.cpp:478-489,137-141,237`, `source/renderer/renderer.h:783-788`,
  `source/renderer/renderer.cpp:163-164`, `source/renderer/renderer_draw.cpp:461-468`.

### 3.1 Diverges from retail (defects)

| # | Rule | Editor behaviour | Editor file:line | Oracle |
|---|---|---|---|---|
| D1 | R1 pool | 1200 rain drops / 900 snow flakes, a static vertex buffer, no spawn accumulator, no pool cap | `renderer/weather_math.h:7-8`; `renderer/renderer_rain.cpp:147-163,176-190` | `drops[80]` + accumulator, `Rain_Update` `0x499CF0` `dxinit/rain.c:65,619-636` |
| D2 | R2 footprint | a 50 m box centred on the camera, wrapped with `mod()`, so it slides continuously | `renderer/weather_math.h:9`; `renderer/renderer_rain.cpp:35` | 9×9 tile window (23.94 m) from tile `(ix+1, iy+1)`, snapped to a 12-tile lattice, `Rain_UpdateGrid` `0x498BD0` `dxinit/rain.c:223-236,249,282,288-289` |
| D3 | R3 velocity | per-drop, per-*second* speed `(0.08 + seed.z*0.10) * fallRange`; ≈5.6..12.6 m/s for the retail 50/20 band | `renderer/renderer_rain.cpp:33`; `renderer/weather_math.h:13-14` | constant 2048 units (0.5 m) **per tick**, no gravity, `Rain_Update` `0x499CF0` `dxinit/rain.c:122-123,572,587-589` |
| D4 | R3 snow motion | sinusoidal drift `sin(u_time*0.45 + seed.x*17) * 0.35 m` | `renderer/renderer_rain.cpp:70-73`; `renderer/weather_math.h:22` | random walk, re-rolled every 30..59 ticks, components in ±68.267 units, `Rain_Update` `0x499CF0` `dxinit/rain.c:595-598,691` |
| D5 | R4 lifetime | the wrap range is the authored band (`camera.z + start` .. `camera.z - end`), i.e. 70 m for retail 50/20; lifetime = band / speed, per drop | `renderer/renderer_rain.cpp:34,248-249` | fixed `kLayer` 6 m wrap, 12/120 ticks, `Rain_Update` `0x499CF0` `dxinit/rain.c:590-591,681` |
| D6 | R4 vertical band | band height is `start + end` (70 m retail) and is anchored to the *authored* values | `renderer/renderer_rain.cpp:248-249` | fixed 18 m (`kTop`), 6..12 m either side of the camera, `Rain_DrawCell` `0x499430` `dxinit/rain.c:283,464-468,687` |
| D7 | R4 ground probe | none: drops are never tested against terrain | `renderer/renderer_rain.cpp:34` (pure `mod` of time) | per-cell `Coll_DropPointsMat` probe of `height + depth`, `Rain_UpdateGrid` `0x498BD0` `dxinit/rain.c:258,263-265` |
| D8 | R6 alpha | `clamp(alpha * 1.25, 0, 0.28)` for rain and `clamp(alpha * 1.75, 0.10, 0.42)` for snow — a boost and a clamp, applied as a float | `renderer/weather_math.h:17-30`; `renderer/renderer_rain.cpp:49,89` | vertex byte `trunc(alpha * 255)`, `Rain_SetAlpha` `0x499FA0` `dxinit/rain.c:646-649` with `kAlphaScale` `polylist.c:432,434` |
| D9 | R6 material | hardcoded colours `(0.8,0.85,0.9)` / `(0.92,0.97,1.0)`, one constant alpha per quad; both drawn as `GL_TRIANGLES`, 6 vertices per particle | `renderer/renderer_rain.cpp:43-51,84-91,280,317` | white RGB from the vertex diffuse, alpha 0 at the streak's top and `trunc(alpha*255)` at its bottom; rain is `D3DPT_LINELIST`, 2 vertices, untextured, `Rain_FillVB` `0x499090` / `Rain_DrawCell` `0x499430` `dxinit/rain.c:366-367,449-458` |
| D10 | R6 snow texture | `snow.tex` is never loaded or bound anywhere in the editor | (absent; `grep -rn snow.tex source/` is empty) | `Game_AddTexture("snow.tex")`, `RainEffect_Init` `0x436780` `fxobj/rain.c:82`, sampled at `dxinit/rain.c:431-432` |
| D11 | R6 extents | streak 0.08 m, snow flake 0.09 m across | `renderer/weather_math.h:10,12`; `runtime/weather_particle_style.h:9-11` | streak 0.5 m (`kStreak` 2048), flake 0.04 m across (`kFlakeSize` 81.92), `dxinit/rain.c:671,685` |
| D12 | R7 `Is Active` | parsed once as a boolean; a non-literal token makes the object unusable | `runtime/level_weather.cpp:77,81-83` | re-evaluated every tick as a `VarString` expression, `RainEffect_Run` `0x436990` `fxobj/rain.c:102-103` |
| D13 | R7 alpha default | `LevelWeatherSettings::alpha` defaults to `0.0` | `runtime/level_weather.h:18` | `RainEffect_Create` sets `level = 0.3125`, `0x436950` `fxobj/rain.c:92` |
| D14 | R7 object selection | takes the **first** `RainEffect` with ≥8 tokens and returns "no weather" if that one is inactive, ignoring any later active object | `runtime/level_weather.cpp:72-89` | one `Rain` per authored `RainEffect` task; each runs, `RainEffect_Create`/`Changed` `0x436950`/`0x436A50` `fxobj/rain.c:86-146` |
| D15 | R7 alpha substitution | the editor's weather-mode override replaces a missing/zero authored alpha with `0.5` (rain) or `0.25` (snow) and a missing band with 10 m / 2 m | `runtime/weather_mode.cpp:14-18`; `renderer/renderer_rain.h:37-38` | no such substitution exists; the authored value is the only source (`fxobj/rain.c:92-93,143`) |

D14 and D12 are **latent**: every retail level authors at most one `RainEffect`
and every retail `Is Active` is a literal (`tests/test_level_weather.cpp:93-101`),
so neither can be triggered by retail data today. They are still defects against
the schema.

### 3.2 Not implemented (feature gaps)

| # | Rule | Editor status | Oracle |
|---|---|---|---|
| N1 | R4/R5 terrain clipping: drops that fall below the ground are not drawn | not implemented — the editor has no ground interaction at all | `Rain_DrawCell` `0x499430` `dxinit/rain.c:403,465-483` |
| N2 | R6 fade in/out over 60 ticks, scaling streak length and flake size | not implemented — precipitation appears and disappears instantly | `Rain_Update` `0x499CF0` `dxinit/rain.c:574-583`; `Rain_Render` `0x498B80` `dxinit/rain.c:209-212`; `Rain_FillVB` `0x499090` `dxinit/rain.c:309,311` |
| N3 | R6 streak alpha gradient (transparent top vertex) | not implemented — one constant alpha per quad | `Rain_FillVB` `0x499090` `dxinit/rain.c:366-367` |
| N4 | R6 four vertex groups selected by tile parity (`kind`, and the `q` index) | not implemented — one draw call for all particles | `Rain_FillVB` `0x499090` `dxinit/rain.c:404,408`; `Rain_Update` `0x499CF0` `dxinit/rain.c:603-616,632` |
| N5 | R2 `Rain_CreateVB` re-creates the vertex buffer on device restore (0x780 vertices) | not implemented — irrelevant to a GL 3.3 core path, listed for completeness only | `Rain_CreateVB` `dxinit/rain.c:133-142`; `Rain_Restore` `0x498B00` `dxinit/rain.c:192-201` |
| N6 | R8 `RainObj_*` world-space rain volume (`RainDynCubeObj`, 11 authored parameters) | not implemented, and **out of scope for #58** | `src/cutscene/rain.c:568-596` |

N4 and N5 are batching/back-end details with no rule the editor could diverge
from; they are recorded so the family is covered, not as work items.

### 3.3 Matches

| # | Rule | Editor | Oracle |
|---|---|---|---|
| M1 | R7 parameter order and `Is Rain` inversion | `arg_tokens[3]` = "Is Rain" → `is_snow = !is_rain` (`runtime/level_weather.cpp:76-77,87`), which is exactly `r->snow = rain == 0` | `fxobj/rain.c:77,141`; `rain.c:153` |
| M2 | R5 no splash / no impact particle | the editor has none | none anywhere in `Rain_*` |
| M3 | R5 shelter/occlusion gating | **invented, not a match**: the editor suppresses precipitation when the camera is inside a Building bound (`weather_visibility.h:16-31`; `renderer_draw.cpp:463-467`; `renderer_objects_mesh.cpp:554-574`). The oracle has no per-building shelter test — only the global `gRainEnabled` | `rain.c:410-411,652-662,692` |
| M4 | R7 `Traceline start` / `Traceline end` are heights in metres scaled by 4096 | the editor multiplies them by `WORLD_UNITS_PER_METER` (`renderer/renderer_rain.cpp:248-249`) | `fxobj/rain.c:138-141` |
| M5 | Global enable | the editor's `WeatherMode::Off` / `Config::weatherEnabled` is the analogue of `gRainEnabled` | `rain.c:652-662,692` |

M3 is deliberately in neither the "defect" nor the "gap" column: it is a
behaviour the oracle does not have at all, so it can be neither faithful nor
unfaithful. It is listed so the shelter rule is not mistaken for parity work.

---

## 4. The extracted module (not wired)

`source/runtime/rain_rules.{h,cpp}` ports the rules that are pure arithmetic and
that the editor has (or should have) a corresponding path for:

| Rule | API |
|---|---|
| R1 | `kDropCapacity`, `kSpawnStep`, `SpawnCountForTick` |
| R2 | `PlaceGrid`, `DrawTileX`, `DrawTileY`, `kTileUnits`, `kGridOriginUnits` |
| R3 | `SpeedUnitsPerTick`, `SnowSwayHalfRangeUnits`, `SnowSwayTicksMin/Max` |
| R4 | `LifetimeTicks`, `WrapZ`, `LayerBaseZUnits`, `FallDistanceUnits`, `ProbeTopUnits` |
| R6 | `AlphaByte`, `RainStreakLengthUnits`, `SnowFlakeSizeUnits`, `DropKindCount` |
| R7 | `ParseRainEffect` + `AuthoredRainEffect` (the five-parameter schema, the oracle defaults, the `Is Rain` inversion, the `Is Active` expression) |
| snow (the module's R7 block) | `SnowFromAuthoredIsRain`, `SpawnRatePerTick`, `SpawnZUnits`, `SnowSwayDisplacementUnits`, `SnowSwayReloadTicks`, `SnowSwayTimerAfterReload`, `SnowSwayFirstRollTick`, `SnowWrapTileX/Y`, `SnowFlakeWidthUnits`, `SnowVerticesPerParticle`, `SnowFlakeDiffuse`, `SnowTextureName`, `SnowRuleDifferences` — see [`SNOW.md`](SNOW.md) |

**It is not called by anything.** `Renderer_Rain` is a GPU vertex-shader field
that computes positions from `u_time` and a per-drop seed
(`renderer/renderer_rain.cpp:31-40`); its state lives in the shader, not in C++,
so wiring the CPU-side rules in would mean rewriting the weather renderer, which
is outside this task's write scope. The module's value now is that the audit's
numbers are executable and regression-guarded, and that a future rewrite has a
cited reference implementation.

Explicitly **not** ported, with reasons:

* The D3D render states, the `SortRender_Add` ordering key and the outcode
  clipping in `Rain_ClipCells` (`rain.c:495-555`) — they are per-back-end
  plumbing, and the editor's camera-relative band makes them inapplicable.
* `Rain_CreateVB` / `Rain_Restore` (device-lost handling) — no GL analogue here.
* The `RainObj_*` world-space volume (`src/cutscene/rain.c`) — a different
  feature (N6).

---

## 5. What is unverified

* **No frame was rendered.** The editor needs GL 4.1+ compatibility; this host
  offers GL 2.1. Every visual statement above is read from source, not observed.
  No performance claim is made.
* **The 9×9 / `+1`-tile draw window** (R2) is a derivation from the decompiled
  indices and constants. It is pinned by a test, but the derivation itself has
  not been checked against a running game, and the window's one-sidedness in
  +x/+y is unexplained.
* **`Rain::falling` is never initialised** by `RainEffect_Create`
  (`fxobj/rain.c:86-96`) or `Rain_New` (`rain.c:144-166`) in IGI-RE. The module's
  default (`true`) is a choice, not evidence. `Task_New` may or may not zero the
  allocation.
* **Tick rate.** `Rain_Update` runs from a task handler; IGI-RE does not pin the
  call rate, so the "30 m/s at 60 Hz" figures are illustrations. The
  per-tick-vs-per-second divergence (D3) holds regardless of the rate.
* **The declared parameter ranges** (5..150, 0..1) are stored by `ParamTable_Add`
  and never read back in `src/param/param.c`; whether the QSC loader or the
  editor UI clamps is not established, so the module does not clamp.
* **The editor's divergences are read from its source**, not demonstrated by
  running it (no frame can be rendered here).
* **Only the host lane was run** (`tests/test_rain_rules.cpp`, 30 tests). The
  Windows `igi_tests` binary was not built or run for this change.
* Retail corpus coverage is one level: only `LEVEL4/objects.qsc` ships a
  decompiled text form, so only its declaration and instance are cross-checked
  in `tests/test_rain_rules.cpp`. The per-level authored values are already
  covered by `tests/test_level_weather.cpp:91-119`.

---

## 6. Recommended follow-up (for the Lead, not done here)

1. **D1, D2, D3, D5, D6 are one decision, not five.** The editor's rain is a
   per-frame GPU field with invented constants; the oracle's is an 80-drop pool
   with a 6 m wrap band and a 23.94 m ground-projected footprint. Bringing it to
   parity means replacing the shader's position model, which is
   `source/renderer/renderer_rain.cpp` — outside this task's write scope.
2. **D7/N1 (terrain interaction) is the cheapest visible win**: the editor
   already has the level's collision/height data for the camera path, and
   `LayerBaseZUnits` + the probe extent are ported and tested.
3. **D8/D11 (alpha and extents) are one-line constant changes** in
   `renderer/weather_math.h`, but they would make the editor's rain nearly
   invisible at retail alphas (`trunc(0.06 * 255)` = 15/255), which suggests the
   boost was added to compensate for a missing fade/streak geometry. Fix D9/N3
   first, or the constant change will look like a regression.
4. **D12/D14 are parser bugs** in `source/runtime/level_weather.cpp` and can be
   fixed without touching the renderer. `ParseRainEffect` in `rain_rules.h`
   already returns the faithful shape if you want to swap the parser.
