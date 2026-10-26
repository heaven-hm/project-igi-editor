# Intro/outro cutscenes (#80)

Status: the editor-side half is implemented in `source/runtime/cutscene_rules.{h,cpp}`
and tested in `tests/test_cutscene_rules.cpp`. Every constant, offset, argument
count and line number below is cited to IGI-RE
(`/Users/haseeb-mir/Documents/Code/IGI-RE`), the faithful C decompilation of
retail `IGI.exe`. No web source and no other port is used as authority. Nothing
here is a rendered result: the editor's GUI cannot run on the development host
(it needs OpenGL 4.1; the host offers 2.1), so the evidence is the host-lane unit
and corpus tests quoted in §10.

**Verdict, in one line.** The triage row's cited entry points are a red herring —
`MenuManager_RequestScreen` (`src/game/menumanager.c:216-224`) is a six-line menu
screen switcher and has nothing to do with cameras — and the #74 menu work that
landed beside it (`42a689bb`, `41d4d33a`) contains no cutscene content at all
(§1). The real retail cutscene subsystem is the `CutScene` task
(`src/cutscene/cutscene.c`) driving a chain of `EditCamera` keys
(`src/editcam/editcamera.c`), plus an `IntroScreen` .avi player
(`src/game/introscreen.c`) that is pure mission flow. The editor already had the
CutScene task's own 21 authored tokens right, and SCamera/SCameraLens, but it had
**no** EditCamera key layout and **no** rule to order, time or interpolate the
**120 authored camera keys that ship in the three retail levels that have a
cutscene**. That rule is what this change ports. The .avi intro/outro screens are
deliberately not ported, with reasons in §8.

---

## 1. What the landed #74 work actually contains

The triage entry for #80 (`docs/igi-re-parity/TRIAGE_REMAINING_ISSUES.md:51`)
records the entry points as `MenuManager_Init` `0x4170A0`,
`MenuManager_Create` `0x417260` and `MenuManager_RequestScreen` `0x4174A0`. Two
large commits landed next to it:

| Commit | Subject |
|---|---|
| `42a689bb` | build the editor's menu from the retail menu system (#74) |
| `41d4d33a` | resolve retail menu labels from the language .RES archives (#74, .RES half) |

`git show --stat` on both shows only `source/menu/*`, `source/app_menu.cpp`,
`CMakeLists.txt` and the matching docs and tests — 17 files, none of them a
camera or a cutscene. A grep for `cutscene`, `intro`, `outro` and `movie` across
`source/menu/`, `source/app_menu.cpp`, `source/runtime/config_qvm.cpp`,
`docs/igi-re-parity/MENU_UI.md` and `docs/igi-re-parity/MENU_TEXT_RES.md`
returns **no matches**: the landed work is the in-game pause menu and its
language labels, and it touches no camera.

Reading the cited function settles it:

```c
/* src/game/menumanager.c:215-224 */
ScriptValue *MenuManager_RequestScreen(ScriptValue *out, MenuManager *mm, void *args)
{
    if (Script_GetArgInt(args, 1))
        MenuManager_ApplyLanguage(mm);
    mm->screen = Script_GetArgInt(args, 0);
    mm->nextState = 0;
    mm->state = 6;
    return Game_ReturnOne(out, 1, 1.0);
}
```

It sets a screen index and a state. There is no camera, no key and no timeline in
it. The `MenuManager` block is the menu system, which #74 already ported
(`docs/igi-re-parity/MENU_SYSTEM.md`).

## 2. Where the retail cutscene actually lives

| IGI-RE file | Address | What it is |
|---|---|---|
| `src/cutscene/cutscene.c` | `CutScene_Open` `0x4F4B00` | the `CutScene` task: the 14-parameter table, the 0…20 time range |
| `src/cutscene/cutscene.c` | `CutScene_CollectKeys` `0x4F4880` | flattens the key tree into a timeline |
| `src/cutscene/cutscene.c` | `CutScene_Update` `0x4F5400` | tick → seconds, key selection, the finished snap |
| `src/cutscene/cutscene.c` | `CutScene_Interpolate` `0x4F55D0` | the camera pose at a time |
| `src/editcam/editcamera.c` | `EditCamera_Init` `0x507F10` | the `EditCamera` key: the 16-row parameter table |
| `src/editcam/editcamera.c` | `EditCamera_Create` `0x508290` | a key's defaults |
| `src/game/introscreen.c` | `IntroScreen_Init` `0x415070` | the intro/outro **.avi** screens (§8) |
| `src/game/game.c:465` | — | `CutScene_SetTicksPerSecond(30)` |

So a cutscene in level data is a `CutScene` task with a chain of `EditCamera`
children. That is the whole authored data model, and it is what §3 and §4 port.

## 3. The CutScene task's authored parameters

`CutScene_Open` is `src/cutscene/cutscene.c:227-266`, `HOOK(0x4F4B00, ...)` at
`:267`. It builds a 14-entry table (`cutscene.c:249`). The authored names and
types below are the retail declaration, which ships inside every `OBJECTS.QVM`
that authors a cutscene and is also decompiled to text in
`MISSIONS/location0/level1/objects.qsc:9-10`. The two agree 1:1 in order.

| # | Engine field (`cutscene.c:250-263`) | Authored name | Type | Token |
|---|---|---|---|---|
| 0 | `pos` | Position | `ObjectPos` | 3-5 |
| 1 | `matrix` | Orientation | `Real32x9` | 6-8 |
| 2 | `runExpr` | Run | `VarString` | 9 |
| 3 | `resetExpr` | Reset | `VarString` | 10 |
| 4 | `speedExpr` | Time delta (seconds) | `VarString` | 11 |
| 5 | `startTime` | Start time (seconds) | `Real32` | 12 |
| 6 | `isRun` | Initial run | `bool8` | 13 |
| 7 | `timeScale` | Time scale | `Real32` | 14 |
| 8 | `screenSize` | Viewport height factor | `Real32` | 15 |
| 9 | `fadeIn` | Viewport height factor fade in time | `Real32` | 16 |
| 10 | `fadeOut` | Viewport height factor fade out time | `Real32` | 17 |
| 11 | `unk210` | Time of day | `Real32` | 18 |
| 12 | `runScript` | Start expression | `VarString` | 19 |
| 13 | `stopScript` | Stop expression | `VarString` | 20 |

**"Orientation" is three arguments, not nine.** The declared type is the
`Real32x9` alias, but `ParamReal32x9_Parse` (`src/physobj/paramops.c:423-435`)
reads three Euler angles and builds the matrix with `Matrix_SetEuler`, and
`Param_Three` (`paramops.c:398`) is the argument count registered for it. So the
call consumes three script arguments and `Run` really does sit at token 9. This
is the same trap `docs/igi-re-parity/SCAMERA_PARAMS.md` documents for SCamera,
and it is why the editor's existing indices (`kCutSceneRunArgumentIndex = 9`
through `kCutSceneStopExpressionArgumentIndex = 20`, in
`source/mission_state_loader.cpp:23-34`) are already correct. They were **not**
changed.

`CutScene_CreateHandler` (`cutscene.c:269-297`) is the defaults: `screenSize`
1.0, `timeScale` 1.0, `isRun` 1, `startTime` 0, `unk210` 0, `tick` 0, `key` -1.
`gCutSceneTimeRange` is `{0.0f, 20.0f}` (`cutscene.c:231-232`) and bounds
`fadeIn`, `fadeOut` and `unk210`.

## 4. The EditCamera key

`EditCamera_Init` is `src/editcam/editcamera.c:69-109`, `HOOK(0x507F10, ...)` at
`:110`. It is `ParamTable_New(17)` (`editcamera.c:83`) filled by **16**
`ParamTable_Add` calls (`editcamera.c:88-107`) — the original reserves one slot
more than it uses, and this port does not invent a seventeenth parameter.

The authored declaration names all sixteen, at
`MISSIONS/location0/level1/objects.qsc:24-25` (and `LEVEL2/objects.qsc:21-22`):

| # | Engine field | Authored name | Type | Token |
|---|---|---|---|---|
| 0 | `pos` | Position | `ObjectPos` | 3-5 |
| 1 | `angles[0]` | Alpha | `Angle` | 6 |
| 2 | `angles[1]` | Beta | `Angle` | 7 |
| 3 | `angles[2]` | Gamma | `Angle` | 8 |
| 4 | `fov` | FOV | `Real32` | 9 |
| 5 | `duration` | Duration | `Real32` | 10 |
| 6 | `posAttach` | Link task ID | `Int32` | 11 |
| 7 | `posFollow` | Update link continously | `bool8` | 12 |
| 8 | `rotAttach` | Target task ID | `Int32` | 13 |
| 9 | `rotFollow` | Update target continously | `bool8` | 14 |
| 10 | `smooth` | Smooth to next | `bool8` | 15 |
| 11 | `unk98` | Time of day (-1 means use default) | `Real32` | 16 |
| 12 | `filterName` | FILTER | `EnumString32` | 17 |
| 13 | `filterLevel` | Noise | `Real32` | 18 |
| 14 | `color` | Filter color | `RGB` | 19-21 |
| 15 | `shake` | Camera shake | `Real32` | 22 |

Twenty declared arguments, so **23 tokens** (indices 0…22). Every one of the 120
authored `EditCamera` instances in the three levels that have a cutscene has
exactly 23 (§9).

Two of the names are load-bearing rather than cosmetic:

- **"Time of day (-1 means use default)"** is `unk98`, and it documents in the
  data what `CutScene_Interpolate` does at `cutscene.c:488-492`:
  `w[i] = k[i]->key->unk98; if (!(w[i] >= 0.0f)) w[i] = c->unk210;`. The `!(x >= 0)`
  form means a **NaN also inherits**, and every retail key authors -1, i.e. "take
  the cutscene's Time of day".
- **"Noise"** is `filterLevel`, not a filter selector; **"FILTER"** is the
  `EnumString32` name.

`EditCamera_Create` (`editcamera.c:112-132`) is the defaults a newly authored key
starts from: `smooth` 1, `posAttach` -1, `rotAttach` -1, `posFollow` 1,
`rotFollow` 1, `fov` 1.0, `unk98` -1.0, `filterLevel` 0.5, `color` and `shake`
zero, and `filterName` memcpy'd from `"CAMERAFILTER_TYPE_NONE"` for 23 bytes into
a `char[0x23]` — **with no NUL terminator**, so the field is a fixed-width
string, not a C string.

## 5. The timeline rule

### 5.1 Flattening — `CutScene_CollectKeys` `0x4F4880`

```c
/* src/cutscene/cutscene.c:163-179 */
void CutScene_CollectKeys(CutKey *keys, int *count, EditCamera *k, float *time, float scale)
{
    if (k->task.type != EditCamera_GetType())
        return;
    do {
        keys[*count].key  = k;
        keys[*count].time = *time;
        *time = scale * k->duration + *time;
        (*count)++;
        child = Task_FindChild(&k->task, EditCamera_GetType());
        if (child)
            CutScene_CollectKeys(keys, count, (EditCamera *)child, time, scale);
        k = (EditCamera *)Task_FindNext(&k->task, EditCamera_GetType());
    } while (k && k->task.type == EditCamera_GetType());
}
```

Three things fall out of this, and the port reproduces all three:

1. A key's `start_time` is the running sum of `scale * duration` over every key
   visited **before** it, and `end` — the value `*time` holds when the walk
   returns — includes the **last** key's own duration.
2. The walk is **depth first**: a key's own `EditCamera` child subtree is visited
   before the key's next sibling, and that subtree consumes the child's siblings
   through its own loop.
3. There is **no bounds check** on `keys[*count]` against the caller's
   `CutKey keys[100]` (`cutscene.c:419`). A cutscene with more than 100 keys
   overruns the engine's stack frame.

The port returns an empty timeline instead of a truncated one when the capacity
would be exceeded, and the test asserts the boundary at exactly 100 (§9). Every
retail key is a flat sibling of the others — all 120 across the three levels sit
at one depth under their `CutScene` — so the recursion is exercised only by a
synthetic tree, which `CollectKeysVisitsAChildSubtreeBeforeTheNextSibling`
builds on purpose.

### 5.2 Index clamping — `CutScene_GetKey` `0x4F4910`

```c
/* src/cutscene/cutscene.c:184-191 */
CutKey *CutScene_GetKey(CutKey *keys, int count, int i)
{
    if (i < 0) i = 0;
    if (i >= count) i = count - 1;
    return &keys[i];
}
```

Clamped at **both** ends, which is what makes the first and last key hold their
pose rather than extrapolate.

### 5.3 Angle unwrapping — `CutScene_UnwrapAngles` `0x4F4940`

Per axis, walking the timeline in order, each key's angle is brought within π of
the previous key's by adding or subtracting 2π (`cutscene.c:205-208`). `prev`
starts from the **first** key's own angles, so the first key is never touched.
This is what makes a 6.0 → -6.0 authored pair interpolate the short way round
instead of spinning.

### 5.4 Selecting the window — `CutScene_Update` `0x4F5400`

```c
/* src/cutscene/cutscene.c:427-460 */
float t = (long double)c->tick / gCutSceneTicksPerSec;
...
for (i = 0; i < count && !found; i++) {
    if (keys[i].time > t) found = 1;
    else                   cur = i;
}
if (t > end) {
    c->isFinished = 1;
    c->key = -1;
    c->tick = (int)round_ll((long double)gCutSceneTicksPerSec * end);
}
k[0] = CutScene_GetKey(keys, count, cur - 1);
k[1] = CutScene_GetKey(keys, count, cur);
k[2] = CutScene_GetKey(keys, count, cur + 1);
k[3] = CutScene_GetKey(keys, count, cur + 2);
if (!k[0]->key->smooth) k[0] = k[1];
if (!k[1]->key->smooth) k[2] = k[1];
if (!k[2]->key->smooth) k[3] = k[2];
```

- `t` is `tick / 30`, and `30` is `CutScene_SetTicksPerSecond(30)` at
  `src/game/game.c:465`.
- `cur` starts at **-1**, so before the first key's start time `CutScene_GetKey`
  clamps every slot to key 0 and the scene holds the first pose.
- `cur` is the **last** key whose `time <= t`. A zero-duration key therefore
  shares its start time with the next key and the **later** key holds the pose.
  The retail data contains such keys (level1 cutscene 1200 keys 3 and 5), and the
  corpus test asserts the engine's resolution rather than a nicer one.
- `finished` is set when `t > end`, and `cur` is **not** moved, so the finished
  pose is still the last key's window. The engine also rewinds `tick` to
  `round_ll(30 * end)`, i.e. it snaps the clock to the end of the timeline.
- The smooth collapse runs **in order**, and the third test reads `k[2]` as
  possibly rewritten by the second. So a non-smooth middle key becomes its own
  left **and** right tangent, and a non-smooth `k[1]` drags `k[3]` with it.

### 5.5 Interpolating — `CutScene_Spline` `0x4F6290`

```c
/* src/cutscene/cutscene.c:631-654, with Hermite_Init/Hermite_Eval from
   src/terrdraw/flare.c:46-61 */
half = (p3 - p1) * kHalfD;
d = t2 - t0;  m1 = x87_eq(d, 0) ? 0 : (t1 - t0) * 2 / d * ((p2 - p0) * kHalfD);
d = t3 - t1;  m2 = x87_eq(d, 0) ? 0 : (t2 - t1) * 2 / d * half;
Hermite_Init(h, p1, p2, m1, m2);
d = t2 - t1;  u  = x87_eq(d, 0) ? 0 : (t - t1) / d;
return Hermite_Eval(h, u);
```

A Catmull-Rom-style Hermite through `p1` (time `t1`) and `p2` (time `t2`) whose
tangents are scaled for **uneven** key times. `Hermite_Init`/`Hermite_Eval` are
`h[0]=m0; h[1]=p0; h[2]=p0+p0-(p1+p1)+m0+m1; h[3]=p1*3.0-p0*3.0-(m0+m0)-m1;` and
`uu=u*u; return u*h[0] + (uu*u*h[2] + uu*h[3]) + h[1];`.

Two consequences worth stating, because they are what a preview scrub feels:

- At `t == t1` exactly, `u` is exactly 0 and the result is exactly `p1`
  whatever the smooth flags did to the tangent neighbours. So **every key holds
  its authored pose at its own start time**, and the corpus test asserts this for
  all 120 retail keys.
- All three divisions are guarded by `x87_eq(d, 0)` (`src/dxinit/dxinit.h:31-34`),
  which is `!(a < b) && !(a > b)` and therefore reports **NaN equal to
  everything**. A NaN time difference takes the zero branch instead of
  propagating. The port transcribes the predicate rather than substituting `==`.

### 5.6 The fade and the letterbox factor

```c
/* src/cutscene/cutscene.c:549-567, 587 */
if (!x87_eq(c->fadeIn, 0.0f) && c->fadeIn > t) fadeIn = t / c->fadeIn;
if (x87_eq(c->fadeOut, 0.0f))                 out = fadeOut;
else { d = end - t; out = !(d >= c->fadeOut) ? d / c->fadeOut : fadeOut; }
mix = clamp(min(fadeIn, fadeOut), 0, 1);
screen = (1.0f - mix) + mix * c->screenSize;
```

`screen` is the "Viewport height factor" the wide-screen filter is built from
(`cutscene.c:590-627`): at `mix == 1` it is 1 (no letterbox) and at `mix == 0` it
is `screenSize`. The port returns both the mix and the factor.

### 5.7 The colour mix

```c
/* src/cutscene/cutscene.c:504-527 */
d = k[2]->time - k[1]->time; span = d;
if (x87_eq(d, 0.0f)) { color = k[1]->key->color; }
else {
    if (c->bandFilter && x87_eq(level, 0.0f)) {
        f = !(span > kHalf) ? 1.0f : clamp((t - k[1]->time) / (span - kHalf), 0, 1);
    } else {
        f = (t - k[1]->time) / span;
    }
    color[i] = (k[2]->key->color[i] - k[1]->key->color[i]) * f + k[1]->key->color[i];
}
```

The band-filter arm needs a live `c->bandFilter`, which the editor has no
equivalent of. Rather than assume it away, the port implements both arms and
takes a `band_filter_active` flag, so the difference is testable; the editor
passes `false`.

### 5.8 Shake

```c
/* src/cutscene/cutscene.c:569-575 */
pos.x += (kHalf - Random_Float(gCutSceneRandom)) * shake * kUnits;
pos.y += ...
pos.z += ...
s4 = (kHalf - Random_Float(gCutSceneRandom)) * shake * kShakeAngle;
s5 = ...
s6 = ...
Matrix_SetEuler(&shk, s6, s5, s4);
```

Six draws in that order, `kUnits` 4096.0 and `kShakeAngle` `0x1.1171bap-1f`
(`cutscene.c:144-145`), with `s6` landing on the X angle and `s4` on Z. The
amplitude is itself interpolated (`shake = SPLINE(shake)`, `cutscene.c:501`) and
the port exposes it as `CutSceneCameraState::shake`. The port takes the six draws
as a caller argument instead of owning a generator, because the engine's
`gCutSceneRandom` is a **shared global** that other subsystems have already
consumed by the time a cutscene runs — a preview cannot reproduce that sequence,
and pretending to would be a false claim. The order and the scale factors are the
engine's.

## 6. The module

`source/runtime/cutscene_rules.h` / `.cpp`, namespace `igi`.

| Group | API |
|---|---|
| constants | `kCutScenePi`, `kCutSceneNegPi`, `kCutSceneTwoPi`, `kCutSceneHalfD`, `kCutSceneHalf`, `kCutSceneUnits`, `kCutSceneShakeAngle`, `kCutSceneFilterDepth`, `kCutSceneWideScreenUnknown`, `kCutSceneTimeRangeMin/Max`, `kCutSceneTicksPerSecond`, `kCutSceneKeyCapacity`, `kCutSceneCameraAspect`, `kCutSceneCameraPriority`, `kCutSceneKeyTokenCount`, `kCutSceneScreenTokenCount` |
| tables | `CutSceneKeyParamTable()` (16), `CutSceneScreenParamTable()` (14), `CutSceneParamDesc` |
| parse | `ParseCutSceneKey`, `ParseCutSceneScreen` |
| data | `CutSceneKeyParams`, `CutSceneScreenParams`, `CutSceneKey`, `CutSceneKeyNode` |
| timeline | `CollectCutSceneKeys`, `CutSceneKeyAt`, `UnwrapCutSceneAngles` |
| time | `CutSceneTimeFromTick`, `CutSceneTickFromTime` |
| selection | `SelectCutSceneKeys` → `CutSceneKeyWindow` |
| interpolation | `CutSceneSpline`, `CutSceneFadeMix`, `CutSceneShakeDelta` |
| preview | `EvaluateCutScene` → `CutSceneCameraState` |

Notes on the shape:

- **Rotation is returned as Euler angles, not a matrix.** The caller builds the
  matrix with `igi::IgiEulerRotationMatrix` (`source/renderer/igi_transform.h`),
  which is `Matrix_SetEuler` `0x4B38E0` (`src/math/matrix.c:99-110`) = Rz·Ry·Rx,
  as `docs/igi-re-parity/SCAMERA_PARAMS.md` already establishes. Duplicating
  matrix code here would be a second source of truth. The angles are `float`,
  because the engine stores the spline result into `float ax, ay, az`
  (`cutscene.c:477`, `:496-498`) before `Matrix_SetEuler(&rot, ax, ay, az)`
  (`cutscene.c:528`) — a `double` here would hand the caller a marginally
  different matrix than the engine builds.
- **Every expression keeps the engine's arithmetic width.** Parameters are
  `double`, the spline's `half`/`d` and the fade's `d` are `long double`, `f` in
  the colour mix holds a `double`, `span` is a `float`, and the engine's locals
  (`ax`, `fov`, `shake`, `level`, `color`, `mix`, `screen`) are `float`. This is
  transcribed deliberately, not incidentally: the same expression evaluated at a
  different width is a different number.
- **The shake amplitude is part of the state.** `cutscene.c:501` splines it like
  any other field, so `CutSceneCameraState::shake` carries it and
  `CutSceneShakeDelta` takes it as an argument.
- **String arguments are unquoted.** `LevelObject::argTokens` keeps a string
  argument's quotes (`source/level/level_objects.cpp:703` strips them at the use
  site), so `filter_name` and the five expression strings are unquoted the way
  `TaskSchemaNS::UnquoteToken` does (`source/level/task_schema.cpp:440-444`).
  `filter_name` has to be comparable against the `"CAMERAFILTER_TYPE_*"`
  constants. (The Smoke port keeps its raw token; this module does not, and says
  so rather than leaving it implied.)
- **`EvaluateCutScene` does not unwrap.** `CutScene_Update` unwraps the
  freshly-collected keys on every frame (`cutscene.c:435`), so the caller of the
  preview API calls `UnwrapCutSceneAngles` once on the timeline it built. Both
  corpus and unit tests do.
- **A truncated call is rejected, not defaulted.** `ParseCutSceneKey` /
  `ParseCutSceneScreen` return false when fewer than 23 / 21 tokens are present,
  leaving the output untouched, rather than silently filling a partial key from
  the engine defaults.

## 7. Time and rounding

`t = (long double)tick / 30` (`cutscene.c:427`), and the reverse is
`c->tick = (int)round_ll((long double)30 * seconds)` (`cutscene.c:445` and
`:663`). `round_ll` is `fistpll` with the x87 control word's rounding mode
(`src/mission/mission.h:113-119` and the identical copies in nine other headers),
which the engine never changes from the default round-to-nearest-even, and the
`(int)` cast truncates the low 32 bits of the result. The port's
`CutSceneTickFromTime` is the same transcription as `igi::SmokeRoundInt`
(`source/runtime/smoke.cpp`), and the test pins `0.5 → 0`, `1.5 → 2` (ties to
even), `13.02 → 391` and the NaN/out-of-range "integer indefinite" case.

## 8. Out of scope, and why

The mandate was to port what **this editor** needs — the authored camera/screen
data model and what it must know to place and preview a cutscene — not the retail
game's mission-flow state machine. Left out, each with its reason:

| Left out | Where | Why |
|---|---|---|
| `CutScene_RunHandler` `0x4F51D0` | `cutscene.c:371-413` | run/reset expression gating, `speedExpr` tick advance, run/stop scripts — mission flow |
| `CutScene_RegisterVarsHandler` `0x4F4FB0`, `UnregisterVarsHandler` `0x4F5050`, `DeleteHandler` `0x4F50E0`, `LeaveWorldHandler` `0x4F6520` | `cutscene.c` | the script symbol namespace and filter teardown |
| `CutScene_InputInfoHandler` `0x4F64F0`, `SetCamera` `0x4F65B0`, `SetFilterParent` `0x4F65C0` | `cutscene.c` | the world's camera and filter parent, which exist only while a level runs |
| LineFilter, BandFilter, HandiCamFilter, WideScreenFilter | `cutscene.c:590-627` | runtime draw state; only the `screen_factor` they are built from is modelled |
| `EditCamera_AttachPosition` `0x508940`, `EditCamera_AttachRotation` `0x508620` | `editcamera.c` | a key with `posAttach`/`rotAttach >= 0` takes its pose from **another live task** (a heli, a train). The flags are parsed and exposed; resolving them needs the running world, so the preview reports the authored pose and the caller decides. 36 of the 120 retail keys set at least one of the two (15 `posAttach`, 26 `rotAttach`), so this is a real limitation, stated rather than hidden |
| the shake RNG | `cutscene.c:569-574` | see §5.8 |
| `src/game/introscreen.c` | `IntroScreen_New` `0x4154B0`, `IntroScreen_Run` `0x415380` | the intro/outro **.avi** screens. `IntroScreen_New` selects a hard-coded movie list (`eidos`/`innerloop`/`vp`/`intro_eu` or `intro_us`, else `outro`, `introscreen.c:228-231`) and `IntroScreen_Run` ends with `Flow_SetRequest(3)`. Nothing about it is authored in level data and it is pure flow, so the "intro/outro movie" half of #80 is not editor work |
| `MenuManager_*` | `src/game/menumanager.c` | the menu system, already ported by #74 (`docs/igi-re-parity/MENU_SYSTEM.md`) |

## 9. The retail corpus

The authored data is in `MISSIONS/location0/{LEVEL2,LEVEL4,level1}/objects.qsc`
and the compiled `OBJECTS.QVM` beside it. Measured by the corpus tests:

| Level | CutScenes | Keys per cutscene |
|---|---|---|
| `LEVEL2` | 2 | 22, 9 |
| `LEVEL4` | 2 | 36, 5 |
| `level1` | 5 | 7, 2, 30, 3, 6 |
| **total** | **9** | **120** |

- Every one of the 120 keys has exactly 23 tokens, and every `CutScene` call has
  exactly 21 header tokens plus one nested call per key. Both are asserted
  against the editor's own `QVM_Parse` + `QVM_DecompileToString` of the retail
  `OBJECTS.QVM`.
- Every key is a flat sibling at one depth, so the depth-first recursion is never
  exercised by retail data — it is covered by a synthetic tree instead.
- Each timeline's `end` equals the independently recomputed sum of
  `timeScale * duration` over its keys, to 1e-3.
- `level1` ships both `OBJECTS.QVM` and `objects.qsc`, i.e. two renderings of the
  same data. The test builds the timeline from both and compares task ids, key
  counts, `timeScale`, `screenSize`, `end`, every key's `start_time` and every
  key's position and duration. That is what proves the token reader is aligned
  with the argument list rather than off by a token.
- Cutscene 1200, the worked example: `Time scale` 1, `Viewport height factor`
  0.7, durations 6.0 + 3.0 + 2.5 + 0 + 4.5 + 0 + 2.6, so `end` is 18.6 s and
  keys 1 and 4 start at 6.0 s and 11.5 s.
- The corpus exercises the awkward branches rather than only the easy ones:
  **61** of the 120 keys are `Smooth to next` FALSE (so the ordered collapse is
  live), **31** have a zero duration (so the hold-key scan is live), **40** have
  a non-zero `Camera shake`, and **36** set a link or target task id (§8).

The authored declarations themselves are checked against the port's tables by
parsing `Task_DeclareParameters("CutScene", ...)` and
`Task_DeclareParameters("EditCamera", ...)` out of the decompiled `OBJECTS.QVM`:
all 14 and all 16 name/type pairs match, in order.

## 10. Verified, and not

**Verified by execution** (host lane, `IGI_GAME_PATH=/Users/haseeb-mir/Downloads/IGI`):

```
$ IGI_HOST_NO_CACHE=1 IGI_HOST_OUT_DIR=build-mingw/ht-cut \
  IGI_HOST_LOG_DIR=build-mingw/log-cut \
  IGI_HOST_TESTS=tests/test_cutscene_rules.cpp bash scripts/run-host-tests.sh
   PASS  test_cutscene_rules                  42 passed    0 failed  (42 tests, exit 0)
   tests run             : 42
   tests passed          : 42
   tests failed          : 0
   tests skipped         : 0 (in 0 suite(s))
OK: 42 tests passed in 1 suites
[exit code: 0]
```

34 unit tests and 8 corpus tests; the corpus tests **ran** rather than skipping
(0 skips), so the retail data was read.

- **Proved:** both authored parameter tables against the shipping declarations;
  the 23-token and 21-token argument layouts on all 120 retail keys and 9 retail
  cutscenes; the timeline accumulation including the last key's duration and the
  depth-first child order; the 100-key capacity boundary; index clamping at both
  ends; the ±π unwrap; `tick/30` and `fistpll` round-to-nearest-even including
  ties and the integer-indefinite case; the last-key-at-or-before scan including
  the zero-duration hold keys; the ordered smooth collapse; the Hermite spline's
  endpoints, its zero-tangent linear case, its uneven-time tangent scaling and
  its zero-span guards; the fade ramps, the mix clamp and the letterbox factor;
  the splined shake amplitude; the six-draw shake order and scale; the colour mix
  and both band-filter arms; the finished flag and that it does not move `cur`;
  and, on the retail data, that every key holds its authored pose at its own
  start time.
- **Not verified — no rendered result.** The editor's GUI needs OpenGL 4.1 and
  this host provides 2.1, so nothing here was observed on screen. The module is
  not wired into a draw path; whether the ported rule produces the retail
  *appearance* is unverified. It is pure logic plus a documented API.
- **Not verified — anything requiring the live world.** `posAttach`/`rotAttach`
  resolution (`EditCamera_AttachPosition` `0x508940`,
  `EditCamera_AttachRotation` `0x508620`), the four screen filters, and the
  run/reset/speed expression gating are out of scope by §8 and untested.
- **Not verified — the shake sequence.** The draw order and the scale factors are
  transcribed and unit-tested, and the amplitude that feeds them is splined and
  exposed (`CutSceneCameraState::shake`), but the engine's shared
  `gCutSceneRandom` state at the moment a cutscene runs is not reproducible, so
  no draw sequence is claimed.
- **Not verified — bit-exactness of the arithmetic widths.** Each expression is
  transcribed at the engine's own width (`double` parameters, `long double` for
  the spline's divisions and the fade's `d`, `float` for the engine's locals and
  for the colour difference), because the engine's x87/D3D build computes at
  those widths. That transcription is a reading of the source, not a differential
  test against the executable: no bit-for-bit comparison was run, so a
  last-place difference in a preview value is not excluded.
- **Not verified — the decompiler's fidelity.** The corpus tests read the QVM
  through the editor's `QVM_DecompileToString`. For `level1` that path is
  cross-checked against the shipped `objects.qsc`, which is what makes it
  trustworthy; `LEVEL2` and `LEVEL4` have no such second rendering in this
  checkout, so their timelines rest on the QVM path alone.
- **Not checked in this task:** the Windows `igi_tests` binary (the gate the
  project requires before a push) and the MCP tool tests, which the host lane
  does not build. This work is left uncommitted in the working tree.

## 11. Known divergences

| Divergence | Engine | Port | Why |
|---|---|---|---|
| NaN angle unwrapping | `while (!(x - prev >= kNegPi)) x += kTwoPi;` cannot terminate on a NaN, and the engine's own comment says "a NaN hangs, like the original" (`cutscene.c:205-208`) | a non-finite angle is left exactly as authored | a level editor must not hang on authored data. Every finite input takes the identical path |
| More than 100 keys | writes past `CutKey keys[100]` (`cutscene.c:170-173`) | returns an empty timeline and reports it | a stack overrun is not a behaviour worth reproducing; an empty timeline cannot be mistaken for a correct preview |
| A truncated authored call | not applicable — the engine reads its own compiled data | `ParseCutSceneKey`/`ParseCutSceneScreen` return false | a half-filled key silently previewing wrong is worse than a refusal |

## 12. Files

| Path | What |
|---|---|
| `source/runtime/cutscene_rules.h`, `cutscene_rules.cpp` | the cutscene camera timeline port |
| `tests/test_cutscene_rules.cpp` | 34 unit + 8 corpus tests |
| `CMakeLists.txt` | purely additive: one test file in the `igi_tests` sources and one runtime source in `target_sources(igi_tests PRIVATE ...)` |
| `docs/igi-re-parity/CUTSCENES.md` | this document |
