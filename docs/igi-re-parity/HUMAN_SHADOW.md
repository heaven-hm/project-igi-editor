# Human blob shadow parity (#63): the only shadow the engine has

Scope: GitHub issue **#63** — "[PARITY][CORE] Infer light fixtures from emissive
geometry + baked lightmaps". This document covers the half of that issue that has
an engine oracle, and records why the other half does not.

## 0. TL;DR

* **The fixture half is refused.** #63 asks for open-igi's `LightFixtureExtractor`
  (emissive-texture survey + lightmap brightness → inferred lamp positions).
  IGI-RE has **no** `LightFixture` type, **no** `fixture` symbol and **no**
  PointLight/SpotLight/DirectionalLight task type. There is no engine oracle for
  an inferred fixture list, so none is implemented. The only retail light types
  are `AlarmLight` and the authored `Dirlight`/`GlobalLight`/`TerrainLightMap`
  family, which #43 already covers (`docs/igi-re-parity/SMOKE_LIGHTS.md`).
* **The shadow half is real and is now implemented.** The engine's only shadow
  renderer is a flat blob quad under each human. It was not drawn by the editor.
  `source/runtime/human_shadow.{h,cpp}` is the rule, `tests/test_human_shadow.cpp`
  pins it (41 host tests), and `Renderer_Objects::DrawHumanShadowQuads`
  (`source/renderer/renderer_objects_visual.cpp`) draws it in the viewport under
  every Human-derived placement.
* **Not verified:** the rendered pixels. This host offers OpenGL 2.1 and the GUI
  needs 4.1, so the viewport was never seen (see §6).

## 1. The engine rule, with addresses

| What | Oracle |
| --- | --- |
| `Shadow` / `ShadowPoly`, "a blob shadow quad under its owner" | `src/shadow/shadow.h:13-38` |
| The only creator: `Shadow_New` HOOK **0x4E0CC0** | `src/shadow/shadow.c:154-198` |
| The only caller: `Human_CreateHandler` HOOK **0x45EB30** | `src/soldier/human.c:337-373` (call at `:357-358`) |
| Per-frame geometry/fade: `Shadow_MagicObjEnterHandler` HOOK **0x4E09D0** | `src/shadow/shadow.c:84-146` |
| The ground basis: `Matrix_SetDirection` HOOK **0x4B4010** | `src/math/matrix.c:244-277` |
| The ground hit: `Human_ProbeGround` HOOK **0x462390** | `src/humanbase/human.c:1839-1857` |
| The visibility flag: `Human_HideShadow` **0x464230** / `Human_ShowShadow` **0x464240** | `src/viewcam/human.c:782-790` |
| How it is drawn: `PolyList_Draw` HOOK **0x49A9B0** | `src/dxinit/polylist.c:294-417` |

### 1.1 Who gets one

`TaskType_New` copies the parent type's handler row into the child
(`src/task/task.c:397-398`), so every type derived from `Human`
(`src/soldier/human.c:246`) inherits `Human_CreateHandler` and therefore a shadow:

* `HumanSoldier` — `src/soldier/soldier.c:718`; its `Soldier_CreateHandler`
  HOOK 0x45CC50 explicitly calls the Human create handler at `:742`.
* `HumanPlayer` — `src/humanplayer/humanplayer.c:1091`.
* `HumanSoldierPistol` — `src/pistol/pistol.c:550`.
* `HumanSoldierRPG` — `src/rpg/rpg.c:593`.

`HumanAI` is **not** one of them: it is parented to the root type
(`src/aiaction/aiaction.c:194`) and has its own create handler, so the AI
controller is not a body and carries no shadow. The soldiers it drives are
`HumanSoldier` tasks and do.

`Shadow_New` has exactly one caller in the whole engine — no guard, vehicle, door
or explosion creates one.

### 1.2 When it is drawn

```c
if (!*sh->visible || !*sh->enabled)
    return;                                     /* shadow.c:92 */
```

`visible` is `Human::unk7E8`, set to 1 immediately after creation
(`human.c:359`) and written only by `Human_HideShadow`/`Human_ShowShadow`, which
the player's ladder and zipline states call
(`src/weapon/humanplayer.c:289,332,382,394,459,513,586`). `enabled` is
`Human::groundValid`, 1 only when the ground probe found a surface
(`src/humanbase/human.c:1855`).

### 1.3 The fade and the clamp

```c
t = (kOneD - (long double)*sh->height / sh->maxHeight) * kShadowFade;   /* :94 */
if (!(t >= kShadowMinFade)) t = kShadowMinFade;                         /* :95-96 */
else if (t < kZero)         t = kZero;                                  /* :97-98 */
else if (!(kOne >= t))      t = kOne;                                   /* :99-100 */
```

The order is load-bearing. The **first** test is the NaN guard: `!(NaN >= 0.3)`
is true, so a NaN height lands on the **minimum** (0.3), not on 1. The middle
branch is dead code (reaching `t < 0` requires `height > maxHeight`, which the
first branch already clamped) — it is ported as written, not "fixed".

| Constant | Value | Where |
| --- | --- | --- |
| `kShadowFade` | `0.8f` (`ALIAS` of `kZoomIn`) | `src/statgun/mp5sd.c:120-122` |
| `kShadowMinFade` | `0.3f` (`ALIAS` of `kThreeTenths`) | `src/heli/wheel.c:285-286` |
| `kShadowDepth` | `10.0f` (`ALIAS` of `kTen`) | `src/glass/shatter.c:579-581` |
| `gShadowColor` | `{0.9f, 0.9f, 0.9f, 0.0f}` | `src/shadow/shadow.c:201-202` |
| `kShadowSize` | `4096000.0f` | `src/soldier/human.c:189` |
| `kShadowHeight` | `0x1.ccccccp+10f` = `1843.2f` | `src/soldier/human.c:190` |
| texture | `"shadow1.tex"` | `src/soldier/human.c:310` |

### 1.4 The two placement constants are named backwards

`Shadow_New`'s 7th argument lands in `sh->maxHeight`, the fade **denominator**
(`shadow.c:166`, used at `:94`), and its 8th in `sh->size`, the quad
**half-extent** (`shadow.c:167`, used at `:106`). The caller passes
`(kShadowSize, kShadowHeight)` in that order (`human.c:357-358`), so:

* the fade denominator is `4096000` units = **1000 m** at the engine's own
  `kFixed = 4096` units per metre (`src/math/mathvars.h:7`). `4096000` is the
  engine's "far" distance — the same value as `Human::kFarUp`/`kFarDown`
  (`src/humanbase/human.c:192-193`) and `Missile`'s `kForward`
  (`src/missile/missile.c:822`). The fade therefore sits at ≈0.8 for every
  reachable height and only reaches the 0.3 clamp at **625 m**; the clamp is a
  safety net, not a visible effect.
* the quad half-extent is `1843.2` units = **0.45 m**, i.e. a 0.9 m blob — the
  only reading that fits a human. (The opposite reading would be a 2 km shadow.)

### 1.5 The geometry

The four corners are laid out in the ground basis from the ground normal
(`shadow.c:105-140`): ±`size` along `m[0..2]`, ±`size` along `m[3..5]`, and every
corner pushed `+kShadowDepth` along `m[6..8]`, which for a unit normal is the
normal itself — the quad floats 10 units (2.4 mm) off the surface. The corners are
**local**: `shadow.c:141` sets the PolyList's position to the owner's ground
position, and `PolyList_Draw` builds the world matrix as
`rotation * local + position` (`polylist.c:355-359`). So the shadow is centred on
the owner's ground contact.

The two triangles are fixed (`shadow.c:170-195`):
`(v0,v1,v2)` with UVs `(0,0),(1,0),(0,1)` and `(v1,v3,v2)` with `(1,0),(1,1),(0,1)`.
All three colour pointers of a triangle point at the same `sh->color`
(`:174`, `:187`), so the quad is flat-shaded.

### 1.6 How it is drawn

`Shadow_New` passes `flags = 0` (`shadow.c:169`), which selects the default blend
branch of `PolyList_Draw`: `ALPHABLENDENABLE 1` with `SRCBLEND 5` (SRCALPHA) and
`DESTBLEND 6` (INVSRCALPHA) — `polylist.c:368-372`. The render states are
`ZWRITEENABLE 0` (`:260`), `ZFUNC 4` = LESSEQUAL (`:261`) and `ALPHATESTENABLE 1`
with `ALPHAREF 0`, `ALPHAFUNC 5` = GREATER (`:256-258`). For a textured triangle
the stage is `COLOROP 4` = MODULATE(texture, diffuse) with `ALPHAOP SELECTARG1` of
the **texture** alpha (`:400-408`), so the vertex colour scales the texture's RGB
and the blend alpha is the texture's own.

**The asset, measured.** `shadow1.tex` is a 32×32 32-bit texture (v11 header,
image mode 67) with a full mip chain; its RGB is **pure black** and its alpha is a
soft radial gradient (0 at the corners, 199/255 at the centre, mean 78). In the
shipped data the `0.9 × fade` modulation is therefore applied to black and the
visible shadow is the texture's alpha. That is a property of the asset, not of the
rule; the rule is ported as written and would tint a coloured texture.

## 2. What the editor now does

| File | Change |
| --- | --- |
| `source/runtime/human_shadow.h` | new — the constants, the fade, the colour, the basis, the quad, the triangles, the type set and the placement rule, each with its IGI-RE citation |
| `source/runtime/human_shadow.cpp` | new — the port (long double where the original is long double) |
| `tests/test_human_shadow.cpp` | new — 41 host-portable tests |
| `source/renderer/renderer_objects_visual.cpp` | `Renderer_Objects::DrawHumanShadowQuads` — a textured, alpha-blended triangle pair with the engine's own blend/depth/alpha-test states |
| `source/renderer/renderer_objects.h` | the method declaration + the `human_shadow.h` include |
| `source/renderer/renderer_draw.cpp` | builds one placement per Human-derived level object and draws them after the object pass |
| `CMakeLists.txt` | the two new source files registered (see §5) |

### 2.1 The editor's one mapping decision

The engine's ground position is the **collision** probe's hit
(`Human_ProbeGround` HOOK 0x462390). The editor has no collision world; what it
has is the App's terrain query (`draw_params_s::terrain_z_at_world_xy_`, wired to
`Level::GetTerrainZ` in `source/app.cpp`). So:

* a terrain hit → the shadow sits on the terrain, and the height above it
  (`placement_z − terrain_z`) drives the fade, exactly as `Human::groundDist`
  does (`src/humanbase/human.c:1801-1802`);
* a terrain **miss** → the placement's own Z is used and the height is 0. This is
  deliberately **not** the engine's `groundValid = 0` branch
  (`src/humanbase/human.c:1840-1843`): the editor's placements are authored on the
  ground, and treating a terrain hole — the `no_terrain=26` case that
  `source/level/collision_drop_rules.h` documents — as "no ground" would hide the
  shadow on every building floor the collision probe can see and the terrain query
  cannot.
* the ground **normal** is vertical. The editor stores no surface normal; IGI is
  Z-up, and `Matrix_SetDirection`'s Z-aligned branch (`matrix.c:249-257`) makes
  that the identity basis, so a flat-ground shadow is drawn exactly as retail.

The engine's `visible` flag is 1 for a placed human (`human.c:359`);
`Human_HideShadow` is driven by the player's ladder/zipline states, which the
editor does not simulate.

### 2.2 Deliberate, stated differences

* **No fog.** The engine fogs polylist triangles (`FOGENABLE = gFogEnable`,
  `polylist.c:248`); the editor's object pass is unfogged, so the shadow matches
  the pass it sits in. Presentation, not rule.
* **Culling off.** The engine culls CW (`polylist.c:253`); the editor's object
  pass draws with culling off, and the quad's winding must not depend on that.
* **The gizmo/placement is not selectable and does not affect the level data** —
  it is a viewport-only draw, like the rail polylines of #59.

## 3. What is refused (the other half of #63)

| #63 requirement | Verdict |
| --- | --- |
| 1. Survey textures/materials for emissive candidates | **No engine oracle.** No emissive/`LightFixture` concept exists in IGI-RE. |
| 2. Store an inspectable `LightFixtureStore` equivalent | Refused — would be invented data. |
| 3. Inspector for radius/intensity/colour, mark false positives | Refused — the retail light tasks that *do* exist are #43's (`AlarmLight`, `Dirlight`). |
| 4. Seed a light-authoring/baking workflow (#43) | Refused — #43 is authored-light work with its own oracle. |

The refutation is mechanical, not a judgement call: `LightFixture`, `fixture`,
`PointLight`, `SpotLight` and `DirectionalLight` have **0 matches** in IGI-RE's
symbols, and the task-type table (`TaskType_New` call sites) contains no light
fixture type.

## 4. Tests

`tests/test_human_shadow.cpp` — 41 host tests, all passing:

* **constants** — every value above, its literal form (`0x1.ccccccp+10f`) and its
  metric reading (0.45 m, 1000 m);
* **fade** — the curve, the 0.625·maxHeight clamp point, the top clamp for
  negative heights, NaN → 0.3, ±inf, a zero/negative denominator, and the honest
  "effectively constant at retail" values (1.8 m → 0.79856; 100 m → 0.72);
* **colour** — 0.9 × fade on all three channels, at the clamped fades too;
* **basis** — identity for a Z-up normal, orthonormal and in-plane for a slanted
  one, the third axis equal to the normal, scale invariance, and the degenerate
  `(0,0,0)` normal collapsing to a zero basis (not NaN), which IGI-RE's own
  emulator suite covers at 0x4E09D0;
* **quad** — the exact ±size corners lifted by the depth, centred on the owner's
  ground position, opposite corner pairs, both triangles' areas and their exact
  vertex/UV order;
* **placement** — ground hit vs no hit, the airborne fade, the 700 m clamp, and
  that the corners stay local to the ground position;
* **gate and owners** — `HumanShadowDrawn(visible, enabled)`, the five
  Human-derived types, and the `HumanAI` negative;
* **oracle pins** — four tests that read the IGI-RE sources themselves (via
  `IGI_RE_PATH`, default `/Users/haseeb-mir/Documents/Code/IGI-RE`) and assert the
  constants, the fade formula, the call site, the aliased DATA values and the
  blend/alpha stage states are still what was ported. They self-skip when the
  oracle is absent, so the value tests above are the ones that always run.

## 5. Build files touched

`CMakeLists.txt` was changed in **two** places, and both are load-bearing:

1. `add_executable(igi_tests …)` — `tests/test_human_shadow.cpp`. The host lane
   discovers its suites from the `tests/*.cpp` names in this file
   (`tools/host-tests/host_test_runner.py`), so without this the suite never runs.
2. `target_sources(igi_tests PRIVATE …)` — `source/runtime/human_shadow.cpp`. This
   is the host lane's engine source pool; without it the suite does not link.

`igi-editor` picks the new runtime source up through the existing
`file(GLOB SOURCES_RUNTIME CONFIGURE_DEPENDS "source/runtime/*.*")`, and the two
renderer files changed are already in the glob.

## 6. Verification status

**Verified** (command output in the task report):

* the host lane for this suite: 41 tests, 41 passed, exit 0;
* the full host lane, the Windows `igi_tests` build, the headless e2e lane and the
  performance guard (see the report for the exact results);
* `bash scripts/check-build-integrity.sh` — PASS after the header changes;
* the retail asset facts: `shadow1.tex` is a 32×32 RGBA blob (RGB 0, alpha radial
  0…199) and is a member of `<IGI>/common/textures/textures.res` as
  `LOCAL:common/textures/shadow1.tex`, the identity `GetOrLoadGameTexture` builds.

**Not verified** ⏳:

* **The rendered pixels.** This host offers OpenGL 2.1 and the editor's GUI needs
  4.1, so `DrawHumanShadowQuads` was never executed. What is proven is that it
  compiles, that its geometry/colour inputs are the pinned rule, and that its GL
  states are transcribed from `polylist.c` — not that a blob appears under a
  soldier. The same limitation applies to every viewport feature in this repo.
* **The engine's collision ground probe** (`Human_ProbeGround`) is not ported: the
  editor substitutes its terrain query, as §2.1 states.
* **Blend-state equivalence** on real hardware: the state *values* are cited, but
  no frame was compared against the retail game.
