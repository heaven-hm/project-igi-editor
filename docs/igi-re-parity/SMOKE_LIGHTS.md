# Smoke and alarm-light FX (#43)

Status: implemented in `source/runtime/smoke.{h,cpp}` and
`source/runtime/alarm_light.{h,cpp}`; tested in `tests/test_smoke_rules.cpp` and
`tests/test_alarm_light_rules.cpp`. Every constant, offset, argument count and
line number below is cited to IGI-RE (`/Users/haseeb-mir/Documents/Code/IGI-RE`),
the faithful C decompilation of retail `IGI.exe`. No web source and no other port
is used as authority. Nothing in this document is a rendered result: the editor's
GUI cannot run on the development host (it needs OpenGL 4.1; the host offers 2.1),
so the evidence is the host-lane unit and corpus tests quoted in §12.

**Verdict, in one line.** Both rules are fully recoverable and are ported as pure
logic: the Smoke emitter's 21 authored parameters, its emission accumulator, its
per-particle geometry, its direction/blend/sprite/group derivation and its
attachment transform; and the AlarmLight's 8 authored parameters, its on/off
state machine with its one-tick sound lag, and the reflector's blink — a triangle
wave from 0 to 128 on the red channel with a period of exactly 30 ticks. Two
findings are load-bearing and are reproduced rather than corrected: the authored
names **"Minimum Velocity"** and **"Maximum Velocity"** are wired to the struct
fields `speedMax` and `speedMin`, and the authored **"On model"** is the model the
runtime installs when the lamp turns **off**. The emission accumulator's first
tick also loses one particle relative to its steady state.

---

## 1. Smoke — the authored parameter table

`Smoke_Init` is `src/fxobj/smoke.c:125-193`, `HOOK(0x437510, Smoke_Init)` at
`:194`. It builds a 21-entry table (`smoke.c:166`) and registers four sprites
(`smoke.c:189-192`). The authored names and types come from the retail
declaration, which is itself in the shipping data (§10); the two agree 1:1 in
order.

| # | Engine field (`smoke.c:167-187`) | Authored name | Type | Range (`smoke.c:131-154`) |
|---|---|---|---|---|
| 0 | `origin` | Position | `ObjectPos` | — |
| 1 | `pitch` | Alpha | `Angle` | 0 … `0x1.921fb6p+1f` |
| 2 | `yaw` | Gamma | `Angle` | ±`0x1.921fb6p+1f` |
| 3 | `count` | Number of Particles | `Int32` | 1 … 255 |
| 4 | `radius` | Radius | `Real32` | 0 … 5 |
| 5 | `spread` | Maximum Random Angle | `Angle` | — |
| 6 | **`speedMax`** | **Minimum Velocity** | `Real32` | 0 … 14.6484375 |
| 7 | **`speedMin`** | **Maximum Velocity** | `Real32` | 0 … 14.6484375 |
| 8 | `unk104` | Colour | `RGB` | — |
| 9 | **`fadeDelay`** | **Life Time** | `Real32` | 0 … 4 |
| 10 | **`fadeTime`** | **Fade Time** | `Real32` | 0 … 4 |
| 11 | `blend` | Fade Mode | `Int32` | 0 … 2 |
| 12 | `sprite` | Sprite index | `Int32` | 0 … 3 |
| 13 | `size` | Particle Size | `Real32` | 0 … 5 |
| 14 | `grow` | Particle Size Delta | `Real32` | −150 … 150 |
| 15 | `unk120` | Minimum Rotation Speed | `Angle` | — |
| 16 | `unk11C` | Maximum Rotation Speed | `Angle` | — |
| 17 | `level` | Intensity | `Real32` | 0 … 1 |
| 18 | `rate` | Initial generate factor value | `Real64` | 0 … 150 |
| 19 | `expr` | Generate factor | `VarString` | — |
| 20 | `enabled` | Move Particles | `Bool8` | — |

The argument counts are the script types', not the engine's:
`Param_ObjectPos()` is an alias of `Real64x3` and consumes **three** doubles
(`src/physobj/paramops.c:185`), `Param_RGB()` is `Real32x3` and consumes **three**
floats (`paramops.c:182`), and every other type here consumes one token. The
authored list is therefore **25 tokens**; with the editor's
`[0]=task id, [1]=type, [2]=name` prefix (`source/level/level_objects.h:116`) an
instance has 28 `argTokens`.

**The two name/field mismatches are real and are reproduced.** Slot 6 is the
authored *"Minimum Velocity"* but writes `s.speedMax`, and slot 7 is the authored
*"Maximum Velocity"* but writes `s.speedMin` (`smoke.c:173-174`). The emission
call then passes `t->speedMin * kSpeed, t->speedMax * kSpeed` in that order
(`smoke.c:333`), i.e. authored-maximum first. `Particle2D_Emit` interpolates
`(b - a) * Random_Float + a` (`src/graphtask/particle.c:616`), which runs
backwards over the pair; because `Random_Float` is uniform on [0,1) the sampled
speed is still uniform on [authored min, authored max], so the swap is invisible
in the distribution but changes the argument order at the call site. The port
keeps the engine order and exposes it as `SmokeEmitSpeedArguments`.

Likewise slot 9 is *"Life Time"* and writes `fadeDelay`, which becomes the
particle's **`delay`** argument, while slot 10 is *"Fade Time"* and writes
`fadeTime`, which becomes its **`frames`** argument (`smoke.c:331-333`). So the
authored "Life Time" is the delay before the fade starts, not the lifetime.

### 1.1 Defaults

`Smoke_Create` is `smoke.c:212-247`, `HOOK(0x437B90, Smoke_Create)` at `:248`.
Every field is assigned except `radius`, and the table's own default template is
an uninitialised stack `Smoke s` (`smoke.c:127`, used at `:171`), so the engine's
default `radius` is indeterminate. The port sets it to `0.0f` and documents that
as a port choice, not a recovery.

Notable values: `speedMax = 4.5f`, `speedMin = 6.0f` (`:224-225`), `spread =
0x1.921fb6p-1f` (`:226`), `rate = 30.0` (`:227`), `acc = 0.0f` (`:228`),
`count = 100`, `fadeTime = 1.0f`, `fadeDelay = 0.5f`, colour `(2,2,2)`,
`grow = 0x1.555556p+1f`, `size = 0.5f`, `sprite = 3`, `level = 1.0f`,
`lastLevel = 1.0f`, **`enabled = 0`** (`:243`), `moved = 0`, and
`Random_Init(gSmokeRandom)` (`:246`).

### 1.2 The random source

`Random_Init` 0x4B4720 (`src/math/random.c:5-9`, `HOOK` at `:10`) seeds
`0xC8FB1A07`, `0x39EA7756`, `0x7FF13CCD`. `Random_Next` 0x4B4740
(`random.c:12-18`) is `c ^= a >> 7; b ^= c; a += b; return a;`. `Random_Float`
0x4B4770 (`random.c:21-24`) is `(Random_Next() & 0xFFFF) / 65536.0L`.

Because `Smoke_Create` calls `Random_Init`, every emitter created in a session
replays the same sequence — the port reproduces that, and its output is checked
against an independent 32-bit reimplementation of those three lines (IGI-RE ships
no test for `random.c`).

---

## 2. The emission accumulator — a corrected reading

`Smoke_Run` is `smoke.c:298-338`, `HOOK(0x437DB0, Smoke_Run)` at `:339`. Per tick:

1. if `moved && enabled`, translate the live particles by `pos - lastPos`
   (`:303-312`);
2. `moved = 1; lastPos = pos;` (`:313-314`);
3. `CALL_CHILDREN` (`:315`);
4. if the authored expression has a body, `rate = VarString_EvalReal(&expr, t)`
   (`:316-317`);
5. `acc = t->rate + t->acc; t->acc = acc;` (`:318-319`);
6. `while (acc > kRate) { …emit…; acc = t->acc - kRate; t->acc = acc; }`
   (`:320-337`).

`acc` is a `long double` local (`:300`) while `t->acc` is a **float** field, so
the comparison uses the untruncated value and the running remainder is re-derived
from the truncated field — the port models both. `kRate` is `30.0f`
(`smoke.c:96`), so the comparison is against 30.

**The loop emits first and subtracts second, and the test is strictly `>`.**
That has two consequences, and only the second is the interesting one:

- a tick whose running total is *exactly* 30 emits nothing;
- the accumulator carries the remainder, so **the steady state is exactly
  `rate / 30` particles per tick** — which is what the function's own comment
  claims (`smoke.c:297`). The only loss is the **first** tick: `Smoke_Create`
  starts `acc` at zero (`:228`), so a rate that is an exact multiple of 30 emits
  one fewer particle on its very first tick.

Measured with an independent implementation of the loop (all values are
first-tick, then steady state):

| rate | tick 1 | ticks 2+ | note |
|---|---|---|---|
| 30 | 0 | 1 | the engine default and the most common authored value |
| 45 | 1 | 1, 1, 2, 1, 2, … | averages 1.5 = 45/30 |
| 60 | 1 | 2 | |
| 150 | 4 | 5 | LEVEL2's fast emitter |
| 1 | 0 | 0 for 30 ticks, then 1 | one particle per second |

An earlier reading of this loop concluded that a rate of exactly 30 emits nothing
*for ever*, and that 150 emits four particles a tick rather than five. That was
wrong: it counted only the first tick. The tests assert both the first tick and
the steady state so the distinction cannot silently regress.

---

## 3. Per-particle emission

Inside the loop (`smoke.c:320-334`), in this exact order:

```c
float angle = Random_Float(gSmokeRandom) * kTwoPi;              /* :320 */
float r     = Random_Float(gSmokeRandom) * t->radius * kUnit;   /* :321 */
long double f = Random_Float(gSmokeRandom);                     /* :322 */
long double x = (t->unk11C - t->unk120) * f + t->unk120;        /* :323 */
float c = t->level;                                             /* :324 */
p.z = t->pos.z;                                                 /* :327 */
p.x = x87_cos(angle) * r + t->pos.x;                            /* :328 */
p.y = x87_sin(angle) * r + t->pos.y;                            /* :329 */
n1 = round_int(t->fadeTime * kRate / c);                        /* :331 */
n2 = round_int(t->fadeDelay * kRate);                           /* :332 */
Particle2D_Emit(ps, &p, &t->dir, t->spread,
                t->speedMin * kSpeed, t->speedMax * kSpeed,
                c, x * kPerFrame, 0, kOneF, n2, n1);            /* :333-334 */
```

Three draws are consumed per particle, in the order angle → radius → rotation
fraction, and `p.z` is copied rather than randomised — so a disc of particles is
laid out in the emitter's XY plane. `x` (the authored *"Maximum Rotation Speed"*
scaled by the authored *"Minimum Rotation Speed"*) is passed as the particle's
`spin`, multiplied by `kPerFrame` (`0x1.111112p-5f`, `smoke.c:97`) because
`Particle2D_Add` stores per-frame values. `n2` is the `delay` and `n1` the
`frames` argument of `Particle2D_Add`, whose `fade` is
`frames ? 1.0L / frames : 1.0L` (`particle.c:563`).

`n1` divides by the authored **"Intensity"**, so an intensity of 0 gives an
infinite frame count; `round_int` is `fistpll` under the default x87 control word
(`src/fxobj/fxobj.h:53-58`), which yields the "integer indefinite"
`0x8000000000000000` for out-of-range and NaN inputs, whose `(int)` truncation is
0 — i.e. a one-frame fade. The port reproduces `fistpll` including that case.

**Divergence, named.** The engine uses the x87 instructions `fsin`/`fcos`
(`src/math/x87.h:19-28`); the port uses `std::sin`/`std::cos` on `long double`.
Both are within about an ulp of the true value for the argument range here, but
they are not bit-identical, so a particle's exact position may differ in the last
bits from retail. Nothing else in the emission path diverges.

---

## 4. Direction, blend, sprite, group, attachment

`Smoke_Changed` is `smoke.c:344-409`, `HOOK(0x438000, Smoke_Changed)` at `:410`.

```c
Matrix_RotateZ(&rz, &id, t->yaw);      /* :361 */
Matrix_RotateX(&rx, &rz, t->pitch);    /* :362 */
t->dir.z = rx.m[8]; t->dir.x = rx.m[2]; t->dir.y = rx.m[5];   /* :363-365 */
```

so **`dir = (sin(yaw)·sin(pitch), −cos(yaw)·sin(pitch), cos(pitch))`**, a unit
vector that is `+Z` at zero pitch and swings with yaw.

The blend flags are a three-way switch (`:366-379`): `0 → 0x1080000`,
`1 → 0x1000008`, `2 → 0x1000010`; the `default` arm passes stack garbage in the
original, and the port returns 0. The sprite switch (`:380-393`) maps
`0 → gSmokeFireball`, `1 → gSmokeBlood`, `2 → gSmokeSmoke2`, default
`→ gSmokeSmoke1`, i.e. `fireball.spr`, `blood1.spr`, `smoke2.spr`, `smoke1.spr`
(`:189-192`). Then `size[0] = size[1] = t->size * kUnit` (`:394`),
`scale = unk104 * kScale` (`kScale = 63.0f`, `smoke.c:99`), and
`Particle2D_New(Level_GetDynamic(), Game_GetWorld(), sprite, &zero, &scale, size,
kLife, blend, 0, t->grow * kPerFrame, 0, t->count, 0)` with `kLife = 409600.0f`
(`smoke.c:102`), followed by `Particle2D_SetAutoDelete(ps, 0)` (`:405`).

**The engine's own comment on this function is wrong.** `smoke.c:343` says
"rebuilds the emitter unless only level changed", but the guard is
`if (t->ps && EQ_OR_NAN(t->level, t->lastLevel))` (`:347`) with
`EQ_OR_NAN(a,b) = !(a<b || a>b)` (`fxobj.h:62`) — true when the two are *equal*
or either is NaN. The emitter is therefore rebuilt when the level is
**unchanged**, which is the opposite of the comment. The port documents the code,
not the comment.

`Smoke_Move` (`smoke.c:197-209`, `HOOK(0x437AD0)` at `:210`) follows the parent:
`v = M · local` in the engine's summation order and `pos = anchor_pos + v`.
`Smoke_UpdateLocal` (`smoke.c:413-438`, `HOOK(0x438240)` at `:439`) inverts it:
with no moving ancestor the local offset is zero and the matrix identity, else
`z = origin.z − p->pos.z` in `long double`, `lx`/`ly` taken unrotated, and
`v.x = m[6]·z + m[3]·ly + m[0]·lx`, `v.y = m[7]·z + m[4]·ly + m[1]·lx`,
`v.z = m[8]·z + m[5]·ly + m[2]·lx`.

---

## 5. AlarmLight — the authored parameter table and the model-slot inversion

`AlarmLight_Init` is `src/fxobj/alarm.c:92-123`, `HOOK(0x4397C0, AlarmLight_Init)`
at `:124` — note this is in `fxobj/alarm.c`, **not** in `elevator/alarmlight.c`,
which holds the task itself.

| # | Engine field (`alarm.c:109-115`) | Authored name | Type |
|---|---|---|---|
| 0 | `place` | Position | `ObjectPos` |
| 1 | `placeMatrix` | Orientation | `Real32x9` |
| 2 | `cond` | On expression | `VarString` |
| 3 | `offModel + 0x00` | On model | `String16` |
| 4 | `offModel + 0x10` | Off model | `String16` |
| 5 | `offModel + 0x20` | Destroyde model | `String16` |
| 6 | `onSound + 0x00` | On sound | `String16` |
| 7 | `onSound + 0x20` | Explode sound | `String16` |

`Param_ObjectPos()` is again three doubles and `Param_Real32x9()` is three floats
— but not three arbitrary floats: `ParamReal32x9_Parse` reads **three Euler
angles** and runs them through `Matrix_SetEuler`
(`src/physobj/paramops.c:423-435`), so the authored "Orientation" is `(ax, ay,
az)` and the stored matrix is `RotZ(az)·RotY(ay)·RotX(ax)` (`src/math/matrix.c:99-109`, `HOOK(0x4B38E0)` at `:110`). The authored list is therefore **12 tokens** (15 `argTokens`).

The three models are the three consecutive 16-byte fields at offsets 0x00, 0x10
and 0x20 (`elevator/elevatortypes.h:11-13`) — the loop is
`ParamTable_Add(…, l.offModel + 0x10 * i, …)` (`alarm.c:112-113`). **So the
authored "On model" is param 3 and lands at offset 0x00**, which is the field
IGI-RE calls `offModel`; the authored "Off model" is param 4 and lands at 0x10,
which IGI-RE calls `onModel`.

`AlarmLight_Run` (`src/elevator/alarmlight.c:44-68`, `HOOK(0x43A270, AlarmLight_Run)`
at `:69`) settles it:

```c
if (wasOn) {
    if (!al->isOn)
        RigidDynCubeObj_SetModel(al, VirModel_Find(al->offModel));   /* :59-61 */
} else if (al->isOn) {
    RigidDynCubeObj_SetModel(al, VirModel_Find(al->onModel));        /* :62-64 */
}
```

Offset 0x00 is installed on the **on → off** transition and offset 0x10 on the
**off → on** transition. Combined with the table, **the value the level calls "On
model" is the model shown when the lamp turns OFF, and vice versa.** This is
proven positionally, not by name-matching, and it is reproduced rather than
"fixed": the retail LEVEL9 data (§10) authors `344_03_1` as "On model" and
`344_01_1` as "Off model", so a faithful port installs `344_01_1` when the lamp
comes on. IGI-RE's own field names describe the runtime use, which is why they
look inverted against the authored labels.

`AlarmLight_Changed` (`alarmlight.c:94-111`, `HOOK(0x43A450)` at `:112`) installs
`al->onModel` — offset 0x10, the authored "Off model" — unconditionally, i.e.
whenever the editor changes the task's parameters.

---

## 6. The AlarmLight state machine

`AlarmLight_Create` (`alarmlight.c:16-41`, `HOOK(0x43A0E0)` at `:42`) zeroes
`place`, sets `placeMatrix` to the identity (`:26-28`), clears all five strings,
sets **`isOn = 0`** (`:35`) and **`isDestroyed = 0`** (`:36`), inits `cond` and
registers the task on `Game_GetTaskList3()`.

`AlarmLight_Run`'s order of operations is what matters:

```c
char wasOn = al->isOn;                                    /* :46 */
if (wasOn && !al->isDestroyed) {                          /* :48 */
    if (!al->sound) al->sound = Sound_Play(al, al->onSound);
} else if (al->sound) { Task_Delete(al->sound); al->sound = NULL; }
if (al->isDestroyed) return;                              /* :55-56 */
if (VarString_HasExpr(&al->cond))
    al->isOn = (char)round_ll(VarString_EvalReal(&al->cond, al));  /* :57-58 */
…model swap on the transition…                            /* :59-65 */
CALL_CHILDREN(&al->obj.task, 1);                          /* :66 */
if (al->obj.attached) HANDLER(1, attached->type)(al->obj.attached);  /* :67-68 */
```

Four consequences the port encodes:

1. **The sound lags the lamp by one tick in both directions**, because the sound
   branch decides from `wasOn`, the value at entry. The model swap, by contrast,
   is immediate. A destroyed lamp still tears its sound down.
2. **A destroyed lamp returns before the expression, the model swap and its
   children** — the lamp state is frozen and `CALL_CHILDREN` never runs.
3. `isOn` is assigned from a **real**, not a bool: `round_ll` is `fistpll`
   (`src/animcam/animcam.h:67-73`) and the result is then cast to `char`, so it
   truncates to eight bits. A condition evaluating to 256 switches the lamp back
   off. The port takes a `double` and reproduces the truncation.
4. The last line runs the **attached** reflector directly, which is how the
   visible blink advances (below).

`AlarmLight_Destroy` (`alarmlight.c:122-130`, `HOOK(0x43A5F0)` at `:131`) spawns
`Explosion_Spawn(&al->obj.pos, 1, 0, 0.05f, 4096.0f, 0.0f, 2048.0f, 5, 2, 0,
"1009_01_1", al->deadSound, 0x15, 2, 20480.0f, 0)` (signature
`src/camera/camera.h:15-16`), sets `isDestroyed = 1` and installs `deadModel`
(offset 0x20, the authored "Destroyde model"). `AlarmLight_Hit` (`:120`) and a
non-zero-falloff `AlarmLight_Blast` (`:140`) both route to it. The port exposes
the spawn arguments as a struct whose field names are `Explosion_Spawn`'s own
(`camera/camera.h:15-16`), keeping `unk` where the decompilation has no name.

---

## 7. The reflector — the visible blink

`AlarmLightReflector_Create` (`alarm.c:126-150`, `HOOK(0x439A50)` at `:151`)
sets **`t->angle = 0.0f`** (`:149`) and `t->light = a->owner` (`:147`).

`AlarmLightReflector_Run` (`alarm.c:154-192`, `HOOK(0x439B70)` at `:193`): the
**entire body, including the increment, is inside
`if (l->isOn && !l->isDestroyed)`** (`:158`). So the reflector only turns while
the lamp is on and alive:

```c
float a = t->angle + kTurn;      /* :161 */
t->angle = a;                    /* :164 */
Matrix_SetRotX(&r, a);           /* :165 */
…r = RotX(a) · local, then r · placeMatrix…      /* :166-190 */
CALL_CHILDREN(&t->obj.task, 1);  /* :191 — outside the guard */
```

`kTurn = 0x1.aceeap-4f` (`alarm.c:71`) = `0.10471975803375244`, which is
π/30 to float precision: `π / kTurn = 29.999999165173982`, and as a float that
is exactly `30.0f`. The port exposes both the increment and the period.

`AlarmLightReflector_Draw` (`alarm.c:197-204`, `HOOK(0x439EC0)` at `:205`)
returns immediately if the lamp is destroyed and draws the flare only when it is
on. `AlarmLightReflector_DrawFlare` (`alarm.c:208-244`, `HOOK(0x439F10)` at
`:245`) is the blink itself:

```c
float m1 = l->placeMatrix.m[1], m4 = l->placeMatrix.m[4], m7 = l->placeMatrix.m[7];  /* :220 */
dx = gTransContext.pos.x - l->obj.pos.x;  … dy, dz                                  /* :221-223 */
if (!(dz * m7 + dy * m4 + dx * m1 > kZero)) return;                                  /* :225-226 */
TransContext_Project(scr, &w, &p);
if (!(w > kZeroF)) return;                                                           /* :229-230 */
size[0] = size[1] = 6144.0f;                                                         /* :232-233 */
k = crt_fmod(t->angle, kPi) * kInvPi - kHalfF;                                       /* :235 */
if (k < kZeroF || k != k) k = -k;                                                    /* :236-237 */
QVertex_DrawQuadEx(…, (k + k) * kFlareScale, 0.0f, 0.0f, w, 0.5f, 0, 0x1040008, 0);  /* :238-239 */
```

Three things are worth stating precisely:

- **The facing test uses the AUTHORED orientation, not the turned reflector.**
  It reads `l->placeMatrix.m[1]/m[4]/m[7]` — row 1 of the light's stored
  "Orientation" — and dots it with the camera offset, and it returns when the dot
  is not strictly positive. A lamp whose authored row 1 points away from the
  camera therefore never draws its flare, however it turns.
- **The colour is a triangle wave on red.** The 10th argument of
  `QVertex_DrawQuadEx` is `r`, the 11th `g`, the 12th `b`
  (`src/res/qvertex.c:74-86`, and `ORIG(0x4B53B0, …)` at `alarm.c:31-33`), so
  `(k+k)*128` is red and green and blue are 0. With
  `k = fmod(angle, π)/π − 0.5` and the absolute value, red ramps 0 → 128 → 0 with
  a period of π in angle, i.e. **30 `AlarmLight_Run` calls ≈ 1 second**, matching
  `kTurn`. `kFlareScale = 128.0f` (`alarm.c:77`).
- **A NaN angle stays NaN.** The `k != k` term is meant to take the magnitude of
  a negative `k`, but negating a NaN leaves it NaN, so a NaN angle propagates into
  the colour. The port reproduces this rather than sanitising it.

`kPi` is the double `0x1.921fb54442d1bp+1` (`alarm.c:74`), `kInvPi` the float
`0x1.45f306p-2f` (`:75`) and `kHalfF` the float `0.5f` (`:76`), so the arithmetic
mixes precisions; the port keeps that mix.

---

## 8. The module

`source/runtime/smoke.h`:

- constants `kSmokeTwoPi`, `kSmokeUnit`, `kSmokeTickRate`, `kSmokePerFrame`,
  `kSmokeSpeedScale`, `kSmokeColourScale`, `kSmokeOne`, `kSmokeParticleLife`,
  `kSmokeParamCount` (21), `kSmokeTokenCount` (25), `kSmokeFirstParam` (3);
- `SmokeParamTable()` / `SmokeParamDesc` — the table of §1 with the authored name,
  the script type, the token count, the optional range, and a comment naming the
  engine field;
- `SmokeParams` / `ParseSmokeParams` / `ResolveSmokeTasks` — the editor's
  `argTokens` to a typed struct, following `mission_fog.{h,cpp}`'s positional
  convention. Field names follow the **authored** names, with the engine field
  documented per row;
- `SmokeRandom` — `Random_Init`/`Next`/`Float`;
- `SmokeRoundInt` (the `fistpll` + `(int)` pair), `SmokeAdvanceAccumulator`
  (one `Smoke_Run` tick, including the float field truncation),
  `SmokeEmitParticles`, `SmokeEmitSpeedArguments`, `SmokeSampleSpeed`,
  `SmokeDelayFrames`, `SmokeFadeFrames`;
- `SmokeDirection`, `SmokeBlendFlags`, `SmokeSpriteFile`, `SmokeGroupSize`,
  `SmokeGroupColour`, `SmokeGroupGrow`, `SmokeMove`, `SmokeUpdateLocal`,
  `SmokeTranslateDelta`.

`source/runtime/alarm_light.h`:

- constants `kAlarmLightTurn`, `kAlarmLightPi`, `kAlarmLightInvPi`,
  `kAlarmLightHalf`, `kAlarmLightFlareScale`, `kAlarmLightFlareSize`,
  `kAlarmLightFlareAlpha`, `kAlarmLightParamCount` (8), `kAlarmLightTokenCount`
  (12), `kAlarmLightFirstParam` (3);
- `AlarmLightModelSlot` (A/B/C = offsets 0x00/0x10/0x20) with the named
  transition aliases of §5, `AlarmLightParamTable()`, `AlarmLightParams` (with
  `ModelForSlot`), `ParseAlarmLightParams`, `ResolveAlarmLightTasks`;
- `AlarmLightRoundLL`, `AlarmLightState`, `AlarmLightTick`, `AlarmLightRun`;
- `AlarmLightReflectorAngle`, `AlarmLightFlareRed`,
  `AlarmLightFlarePeriodTicks`, `AlarmLightFlareRedAfterTicks`,
  `AlarmLightFlareFacesCamera`, `kAlarmLightFlareGreen/Blue`;
- `AlarmLightEulerMatrix` (the `Matrix_SetEuler` port),
  `AlarmLightExplosion` / `AlarmLightDestroyExplosion`.

Both modules are pure logic: no renderer, no engine handle, no allocation. They
are compiled into the host test lane by a purely additive `CMakeLists.txt` change.

**No renderer wiring is included.** The port answers "what should be drawn and
when", not "draw it": nothing in `source/renderer/`, `source/level/level.cpp` or
`source/common.*` was touched, and no GUI verification was possible on this host
(§12).

---

## 9. The retail corpus

Read from the shipping `MISSIONS/location0/LEVEL*/OBJECTS.QVM` with the editor's
own `QVM_Parse`, and cross-checked against the decompiled `objects.qsc` where one
exists.

**Smoke: 74 instances, all parsing with 25 parameter tokens.**

| LEVEL | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 | 14 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Smoke | 3 | 4 | 0 | 0 | 0 | 5 | 12 | 9 | 0 | 23 | 2 | 8 | 3 | 5 |
| AlarmLight | 0 | 0 | 0 | 0 | 0 | 3 | 2 | 7 | 2 | 0 | 0 | 0 | 0 | 2 |

Every one of the 74 uses sprite index 3 (`smoke1.spr`), and every one has
`min_velocity <= max_velocity` as authored.

Worked values the tests assert exactly:

- **LEVEL2 `objects.qsc:1044`** — `(126014848, −38164756, 175416512)`, Alpha 0,
  Gamma 0, count 170, radius 1, spread 1, velocities 5.5 / 7.0, colour (2,2,2),
  Life Time 1.5, Fade Time 3.4000000953674316, Fade Mode 2, sprite 3, size
  0.30000001192092896, size delta 2.6666669845581055, rotation 0 / 2.0,
  intensity 0.25, rate 30.0, no expression, Move Particles false.
- **LEVEL2 `objects.qsc:1045`** — radius 5.0, spread 0.78539800643920898,
  colour `(0.30769199132919312, 0.17307700216770172, 0)`, Fade Time
  1.3999999761581421, intensity 0.05000000074505806 → **45 delay frames, 840
  fade frames**.
- **LEVEL2 `objects.qsc:1111`** — Alpha `3.1415929794311523` (straight down),
  count 200, radius 2.5, Life Time 0.10000000149011612, Fade Time 0.5, Fade Mode
  1, size 0.75, rotation 0.5 / 1.5, intensity 0.02500000037252903, **rate 150.0**
  → 3 delay frames, 600 fade frames, and 4 then 5 particles per tick.
- **LEVEL1 `objects.qsc:903`** — Alpha 1.7802360057830811, Gamma
  −1.6755160093307495 (a real tilt: the direction is a unit vector, not `+Z`),
  count 300, radius 2.0, Life Time 1.5, Fade Time 2.0, size 0.69999998807907104,
  rotation 0.30000001192092896 / 0.60000002384185791, intensity
  0.10000000149011612, rate 30.0.

**AlarmLight: 16 instances, all parsing with 12 parameter tokens, none of them in
any `.qsc`** (the decompiler emits no AlarmLight, so the QVM is the only source).

- **LEVEL9** — `(23470464, −51103984, 174511488)` and
  `(23515142, −50666360, 174495072)`, both Orientation `(0, 0,
  3.1415929794311523)`, condition `AlarmControl_98.isAlarm`, "On model"
  `344_03_1`, "Off model" `344_01_1`, empty destroy model and sounds. Applying
  §5 to this real data: the lamp is authored to show `344_03_1` while on, and a
  faithful runtime installs `344_03_1` when it turns **off**.
- **LEVEL6** — three instances, z-Euler 3.14159298 / 1.57079601 / 4.71238899,
  condition `AlarmControl_98.isAlarm`, no explode sound.
- **LEVEL7** — two instances, condition `AlarmControl_97.isAlarm`, explode sound
  `explo_01_s`.
- **LEVEL8** — seven instances, conditions `AlarmControl_97.isAlarm` (five) and
  `AlarmControl_1097.isAlarm` (two); two of them author the *same* model in both
  slots, so the inversion is invisible there.
- **LEVEL14** — two instances, Orientation
  `(4.71238899230957, 6.283174991607666, 4.71238899230957)`, condition
  `!ConditionalContainer_2222.isRun`, explode sound `explo_01_s`.

---

## 10. The authored declaration is in the data

Both tables are checked against the shipping declaration rather than a
hand-copied list. In the QVM the declaration is the argument block of a
`Task_DeclareParameters` call: `PUSHIIB <Task_DeclareParameters>`, `CALL`,
`BRA <skip>`, then the string pairs interleaved with `BRK`, terminated by `POP`.
The `BRA` skips the whole block, so a naive "next string" reader fails; the tests
decode it positionally.

The retail declaration for Smoke (`LEVEL2/OBJECTS.QVM`) is 42 strings, and for
AlarmLight (`LEVEL9/OBJECTS.QVM`) 16. Both match `SmokeParamTable()` and
`AlarmLightParamTable()` entry for entry, including the retail typo
**"Destroyde model"**, which is reproduced as data.

For Smoke there is a second, independent rendering: LEVEL1 and LEVEL2 also ship a
decompiled `objects.qsc`, whose `Task_DeclareParameters("Smoke", …)` text and
whose 25-value `Task_New(-1, "Smoke", …)` calls are parsed and compared
field-by-field with the QVM's. That test caught two real bugs in the test's own
text reader, which is the point of having it: the decompiler renders a task's
children *inside* its parent's argument list (`LEVEL2/objects.qsc:1042-1046`), and
expressions in the arguments contain their own parentheses and commas inside
string literals, so a flat "find the first `)`" scan both mis-terminates and
skips nested calls.

---

## 11. A finding: the declared ranges are not enforced

`Smoke_Init` registers ranges for 14 of the 21 parameters
(`smoke.c:131-154`), and the retail data violates four of them:

| Field | Declared | Authored | Instances |
|---|---|---|---|
| Number of Particles | 1 … 255 | up to **600** | 13 |
| Alpha (pitch) | 0 … `0x1.921fb6p+1f` | `3.1415929794311523` | 4 |
| Gamma (yaw) | ±`0x1.921fb6p+1f` | `3.1415929794311523` | 2 |
| Minimum/Maximum Velocity | 0 … 14.6484375 | `14.648438453674316` | 9 |

The angle and speed cases are each exactly **one float ulp** above the declared
maximum (bit patterns `0x40490fdc` vs `0x40490fdb`, and `0x416a6001` vs
`0x416a6000`) — a rounding artefact of how the level compiler wrote π and the
speed cap, not a different value. The particle count is not: 255 is a real
ceiling in the engine's `Param_Int32` handling, and the shipping levels exceed it
by a factor of more than two. The ranges are therefore editor metadata that the
level compiler did not enforce, and an editor must not use them to reject retail
data. The tests assert this as counted findings rather than as invariants.

---

## 12. The emitter running: the viewport simulation (the second half of #43)

§1-§4 are the emitter's static half. This section is the live half: the emitter
spawning and stepping particles in the viewport, on the engine's own tick.

### 12.1 The tick

`Smoke_Run` HOOK `0x437DB0` (`smoke.c:298-339`) and `Particle2D_Run` HOOK
`0x4FD660` (`particle.c:264-305`) are both tick handlers. The engine's loop has
no elapsed time at all (`src/win/appmain.c:224-227`), so **one engine frame is
one tick**, and a long editor frame buys no extra steps. `SmokeSimulation::Tick`
(`source/runtime/smoke_sim.cpp`) is called once per rendered frame from
`Renderer_Smoke::Draw`, which is the same rule the weather particles already
follow (`RainEffect_Run` HOOK `0x436990`, `fxobj/rain.c:98-105`; commit
f4204d55 pinned the same invariant for the animation clock and
`scripts/perf-guard.sh` budgets it).

### 12.2 The spawn rate

`acc = t->rate + t->acc; while (acc > 30.0) { emit; acc = t->acc - 30.0; }`
(`smoke.c:316-337`), with `acc` a FLOAT field that `Smoke_Create` starts at 0
(`smoke.c:228`). The steady state is exactly `rate / 30` particles per tick, but
the comparison is strict, so the **first tick of an exact multiple of 30 emits
one fewer** and a rate below 30 emits nothing until the carry crosses 30:

| rate | emissions per tick | first tick |
|---|---|---|
| 30 | 1 | 0 |
| 60 | 2 | 1 |
| 150 | 5 | 4 |
| 1 | 1 every 30 ticks | 0 for the first 30 |

Reproduced, not corrected (`tests/test_smoke_particles.cpp`).

### 12.3 The lifetime

`delay = round("Life Time" × 30)` and `frames = round("Fade Time" × 30 ÷
"Intensity")` (`smoke.c:331-332`), and `Particle2D_Add` stores
`fade = frames ? 1/frames : 1` (`particle.c:563`). `Particle2D_Run` then does:

```c
if (p->delay) { p->delay--; }                  /* particle.c:293-295 */
else { level = (long double)p->level - p->fade;
       p->level = level;
       if (level > 0 && p->scale > 0) keep; else remove; }   /* :296-304 */
```

so the particle is stepped `delay + frames` times, and **not always exactly
`frames` fade ticks**: the fade is a per-tick float subtraction, and
`1 − 30·(1/30)` rounds to exactly 0 while `1 − 60·(1/60)` leaves a positive
residue and survives a 61st tick. The tests pin both (30 → 30, 60 → 61), which
is why the default "Fade Time" of 1 s gives a 45-tick particle and a steady-state
pool of 44 for a rate of 30.

### 12.4 The velocity and the cone

`Particle2D_Emit` HOOK `0x4FE210` (`particle.c:569-634`) builds the direction
from an orthonormal frame `(c, w, u)` around the emitter axis (`u`), then tilts
it:

```c
tilt = Random_Float(gParticle2DRandom) * spread;          /* :610 */
az   = kPi - Random_Float(gParticle2DRandom) * kTwoPi;    /* :611 */
ax   = spread * 0.5f - tilt;                              /* :612 */
Matrix_SetEuler(&m, ax, 0, az);                           /* :613 */
d[i] = m.m[2]*c[i] + m.m[5]*w[i] + m.m[8]*u[i];           /* :614-615 */
speed = (speedMax - speedMin) * Random_Float + speedMin;  /* :616 */
vel[i] = normalize(d)[i] * speed;                         /* :617-631 */
```

Column 2 of `RotZ(az)·RotX(ax)` is `(sin az·sin ax, −cos az·sin ax, cos ax)`
(`matrix.c:99-164`), so the deviation of the emitted direction from the axis is
exactly `|ax|`, and `ax` runs over `(−spread/2, spread/2]`. **The cone's
half-angle is `spread/2`, not `spread`**, even though the authored parameter is
named "Maximum Random Angle" — with the authored default of `0x1.921fb6p-1`
(π/4, `smoke.c:226`) the cone opens to π/8. The placement gizmo in
`renderer_objects_visual.cpp` used to draw `spread`; it now draws `spread/2`.

The speed is uniform on `[min × 136, max × 136]` (`kSpeed`, `smoke.c:98`), and
the reversal of the two authored velocity names (§1) does not change the
distribution.

### 12.5 The two random streams

`Smoke_Create` calls `Random_Init(gSmokeRandom)` (`smoke.c:246`) and
`Particle2D_Open` calls `Random_Init` + `Random_Seed(gParticle2DRandom,
0x12332153)` (`particle.c:99-100`). The emitter's three draws per particle
(angle, radius, rotation fraction) come from the first stream and
`Particle2D_Emit`'s three (tilt, azimuth, speed) from the second, in that order.
A first particle of a fresh simulation is therefore fully determined, and the
suite pins it against an independent re-derivation of the oracle's arithmetic:
with the default axis (+Z), "Radius" 2 m, "Maximum Random Angle" π/4 and
velocities 4.5/6.0 the first particle is born at `(1489.50269, −2263.80298)` and
leaves with velocity `(10.762025, 190.378006, 734.919495)`.

Two bugs were caught by that pin while this was written, and are worth recording
because both are invisible to a range-only check: C++ leaves argument evaluation
order unspecified, so passing the three `Random_Float()` draws as arguments
permuted them; and a simulation that does not apply `Random_Seed(0x12332153)`
starts the particle stream from the *emitter's* state, which shifts every
velocity.

### 12.6 The draw

`Particle2D_Draw` HOOK `0x4FD020` (`particle.c:108-208`) projects each particle
(`TransContext_Project` `0x497F20`) and draws a screen-space quad whose
half-extent is `size × scaleX ÷ depth` (`TransContext_ScaleSize` `0x498040`,
`transctx.c:282-288`), rotated by the accumulated `angle` when the particle
spins, with the colour and alpha selected by the flags:

```c
if (t->flags & 0x80000) alpha = p->level;          /* :195-196 */
else { alpha = 1.0f; r = colour.x * p->level; ... } /* :197-202 */
```

`Particle2D_New` ORs `0x40000` into the flags (`particle.c:515`), so the branch
is decided by the authored "Fade Mode" alone: mode 0 fades the alpha, modes 1
and 2 fade the colour. A world-space camera-facing quad of half-extent
`size × p.scale` projects to exactly the engine's pixel size, so the viewport
draws the engine's quad as a billboard (`source/renderer/renderer_smoke.cpp`).

**Named divergence: the sprite texels.** The engine binds `smoke1.spr` /
`smoke2.spr` / `fireball.spr` / `blood1.spr` (`smoke.c:189-192`, `:380-393`)
through `QVertex_DrawQuadEx` (`particle.c:205-206`). Those live inside
`COMMON/SPRITES/SPRITES.RES` and are a chunked LOOP/FONT container with palettes
(`Sprite_Load` HOOK `0x4B6720`, `src/res/sprite.c:162-262`), which the editor has
no loader for. The quad is filled with a procedural radial falloff instead: the
geometry, size, colour, alpha and fade are the engine's, the texels are not.

### 12.7 Named divergences (simulation)

* **Emitter before group within a tick.** The group is created by
  `Smoke_Changed` as a child of `Level_GetDynamic()` (`smoke.c:401-404`) and
  `Task_New` appends (`List_AddTail`, `src/task/task.c:146-147`), while the
  emitter itself sits wherever the level nested it — inside the `Dynamic`
  subtree in the shipped data (`LEVEL1/objects.qsc:546` opens `Dynamic`, the
  first `Smoke` is at `:903`). The port therefore emits first and integrates
  after, so a particle moves on its birth tick. This is an argument from the tree
  and the shipped file, not a runtime trace.
* **Two per-simulation random streams.** The engine shares one `gSmokeRandom`
  across every Smoke task and one `gParticle2DRandom` across every group. One
  simulation object with its own pair reproduces a single emitter exactly and
  keeps two emitters' previews independent; with several emitters the engine's
  shared stream would interleave their draws.
* **"Generate factor" is not evaluated.** The authored VarString expression
  (`smoke.c:314-315`) needs a script context the preview does not have, so the
  authored "Initial generate factor value" is used for every tick.
  `SmokeSimulation::EmittersWithExpression()` reports how many emitters the
  editor is approximating.
* **The emitter does not follow a moving ancestor.** `Smoke_Move` HOOK
  `0x437AD0` and `Smoke_UpdateLocal` HOOK `0x438240` need a `RigidDynCubeObj`
  parent with a live matrix; the emitter stays at its authored Position.
  `Particle2D_Translate` (`0x4FE610`) is applied, because it only needs the
  emitter's own delta ("Move Particles").

### 12.8 Verified, and not

* **Verified by execution (host + Windows).** 24 new host-portable tests
  (`tests/test_smoke_particles.cpp`) pin the spawn rate per tick (30/60/150/1),
  the pool cap, the lifetime including the 30-vs-61 float case, the first
  particle against an independent derivation, the speed range, the cone
  half-angle (never past `spread/2`, and filled to it), the zero-spread axis,
  the emission disc, the axis formula, the cone frame's orthonormality in both
  `|u.x| > 0.75` branches, straight-line motion under zero gravity, grow, spin,
  the one-tick-per-call rule, the rebuild rule, "Move Particles", determinism and
  the seeded stream. All 24 pass in the host lane and in the Windows i686
  `igi_tests` binary.
* **Not verified — no rendered result.** The GUI needs OpenGL 4.1 and this host
  provides 2.1, so **no smoke particle has ever been seen on screen**. What is
  proven is that the simulation runs on the engine's tick and that the draw path
  compiles, is wired into `Renderer::Draw` once per frame, and uses the engine's
  own quad size, colour and alpha rules. The appearance, the blend result and the
  billboard orientation are unverified.
* **Not verified — the group's task-tree ordering** (§12.7) and the animated
  sprite frame counter (`Particle2D.frame`), which the editor never advances
  because it loads no `.spr` frame map.
* **Not verified — `Particle2D_Run`'s collision callback.** `Smoke_Changed`
  passes a NULL callback (`smoke.c:404`), so the engine's per-15-tick
  `Coll_Lines` trace (`particle.c:348-388`) never runs for smoke; the port omits
  it for the same reason.

---

## 13. Verified, and not

**Verified by execution** (host lane, `IGI_GAME_PATH=/Users/haseeb-mir/Downloads/IGI`):

```
$ IGI_HOST_NO_CACHE=1 IGI_HOST_TESTS=tests/test_smoke_rules.cpp bash scripts/run-host-tests.sh
   PASS  test_smoke_rules                     39 passed    0 failed  (39 tests, exit 0)
   tests run             : 39
   tests passed          : 39
   tests failed          : 0
   tests skipped         : 0 (in 0 suite(s))
OK: 39 tests passed in 1 suites
[exit code: 0]

$ IGI_HOST_NO_CACHE=1 IGI_HOST_TESTS=tests/test_alarm_light_rules.cpp bash scripts/run-host-tests.sh
   PASS  test_alarm_light_rules               26 passed    0 failed  (26 tests, exit 0)
   tests run             : 26
   tests passed          : 26
   tests failed          : 0
   tests skipped         : 0 (in 0 suite(s))
OK: 26 tests passed in 1 suites
[exit code: 0]
```

The 33 + 21 non-corpus tests assert hard-coded expected values derived
independently of the port (a separate 32-bit RNG reimplementation and a separate
implementation of the emission loop, cross-checked in Python); the 6 + 5 corpus
tests read the retail files and ran rather than skipping (0 skips).

- **Proved:** the parse of all 74 retail Smoke and 16 retail AlarmLight instances
  against the authored declarations; the exact authored values above; the
  accumulator's first tick and steady state; the RNG sequence; `fistpll`
  rounding and the integer-indefinite case; the emission geometry, speed
  arguments, spin range, group size/colour/grow; the direction formula and the
  three blend flags and four sprite slots; `Smoke_Move`/`Smoke_UpdateLocal`
  against the engine's summation order; the AlarmLight state machine including the
  one-tick sound lag, the destroyed early return and the `char` truncation; the
  model-slot inversion on real LEVEL9 data; the reflector turn gate, the 30-tick
  period, the red triangle wave at 0/1/7/14/15/29/30/45/60 ticks, the NaN
  propagation, the authored-orientation facing test, and `Matrix_SetEuler`'s
  orthonormality for the LEVEL9 orientation.
- **Not verified — no rendered result.** The editor's GUI needs OpenGL 4.1 and
  this host provides 2.1, so nothing here was observed on screen. No renderer,
  level or common code was modified, and the port is not wired into a draw path.
  Whether the ported rules produce the retail *appearance* is unverified.
- **Not verified — bit-exactness of the transcendental functions.** The engine
  uses x87 `fsin`/`fcos`; the port uses libm `sinl`/`cosl` (§3). The particle
  positions may differ in the last bits. This is the only known numerical
  divergence in the emission path.
- **Not verified — the `default` blend arm.** The original passes stack garbage
  for a Fade Mode outside 0…2; the port returns 0 and documents it. What retail
  actually passed is unknowable from the decompilation.
- **Not verified — `radius`'s engine default.** `Smoke_Create` never assigns it
  and the table's default template is uninitialised stack memory (§1.1). The
  port's `0.0f` is a choice.
- **Not verified — the reflector's full transform.** The port covers the turn
  rate, the period, the flare colour and the facing test, but not
  `AlarmLightReflector_Run`'s full `RotX(a)·local·placeMatrix` chain
  (`alarm.c:166-190`), which needs the object hierarchy the editor does not model
  in this module.
- **Not checked in this task:** the Windows `igi_tests` binary and the MCP tool
  tests, which the host lane does not build.

---

## 14. Files

| Path | What |
|---|---|
| `source/runtime/smoke.h`, `smoke.cpp` | the Smoke port (the static half, plus Particle2D_Emit's direction and Particle2D_Run's step) |
| `source/runtime/smoke_sim.h`, `smoke_sim.cpp` | the emitter running: Smoke_Run + the Particle2D pool (the live half, §12) |
| `source/renderer/renderer_smoke.h`, `renderer_smoke.cpp` | the particles in the viewport (§12.6) |
| `source/runtime/alarm_light.h`, `alarm_light.cpp` | the AlarmLight port |
| `tests/test_smoke_rules.cpp` | 33 unit + 6 corpus tests |
| `tests/test_smoke_particles.cpp` | 24 unit tests for the live half (§12.8) |
| `tests/test_alarm_light_rules.cpp` | 21 unit + 5 corpus tests |
| `CMakeLists.txt` | purely additive: three test files and three runtime sources |
| `docs/igi-re-parity/TRIAGE_REMAINING_ISSUES.md` | the #43 row this closes |
