# Helicopter rotor + flight preview (#60)

Status: implemented in `source/runtime/heli_rules.{h,cpp}` and tested in
`tests/test_heli_rules.cpp`. Every constant, offset, argument index and line
number below is cited to IGI-RE (`/Users/haseeb-mir/Documents/Code/IGI-RE`), the
faithful C decompilation of retail `IGI.exe`. No web source and no other port is
used as authority. Nothing here is a rendered result: the editor's GUI cannot run
on the development host (it needs OpenGL 4.1; the host offers 2.1), so the
evidence is the host-lane unit and corpus tests quoted in §12.

**Verdict, in one line.** Both halves are recoverable as pure logic and are
ported: the **`Heli`** task's 13 authored parameters, its per-tick control
contract (including the `0xBF800000` throttle sentinel and the tick-0 teleport
back to the authored start), its speed ramp, its smoothed three-axis turn forces,
its nose-pitch composition and its four landing-wheel probes; and the **`Rotor`**
magic object's `DefinePhysicsObjType` definition, its per-tick spin accumulator,
its lift and tilt forces and its per-blade draw transform. The load-bearing
findings are that the authored **"Original Thrust"** is the *start speed* and
**"Speed"** is the *start velocity*, that the nose pitch is a **body-frame
post-multiply** (`nm = m·r`, not `r·m`), that the wheel parameters are **stored
swapped**, that the rotor's `dir` field is a **blade-copy count, not a sign**, and
that `HeliConfig`'s first authored list is parsed and then **never read**.

---

## 1. The two task types and where their arguments start

`Heli_Init` is `src/heli/heli.c:166-202`, `HOOK(0x4310F0, Heli_Init)` at `:203`.
It builds the parameter table, registers `TASKTYPE_HELI` and installs eighteen
event handlers (`heli.c:166-182`). The relevant ones:

| Event | Handler | Address |
|---|---|---|
| `Vehicle_GetControlEvent` | `Heli_Control` | `0x431B70` |
| `Vehicle_GetInputInfoEvent` | `Heli_GetControlInfo` | `0x432BE0` |
| `Vehicle_GetKeysEvent` | `Heli_ReadKeys` | `0x432C10` |
| `PhysicsObjType_GetParseEvent` | `Heli_ParseArgs` → `Heli_ParseConfig` | `0x432F60` / `0x433030` |
| `World_GetEvent1` | `Heli_RemoveFromWorld` | `0x432F90` |

`Heli_ParseArgs` (`src/plane/heli.c:34-39`, `HOOK(0x432F60, ...)`) hands
`Heli_ParseConfig` the physics definition's argument cursor. Where that cursor
starts is decided by `DefinePhysicsObjType` (`src/physobj/physobjtype.c:66-121`,
`HOOK(0x4EDD20, ...)`): after the shared header it sets `arg = 10` (`:91`), the
`jointCount` loop consumes six tokens per joint (`:94-101`), and it then passes
`arg + 2` as the task parser's start index (`:113`). The task argument list
therefore begins at index

```
first_arg = 10 + 6 * jointCount + 2 = 12 + 6 * jointCount
```

`RotorDefArgIndex()` in the port computes exactly this, and the corpus test pins
it at 12 for the `Heli` definition (`jointCount = 0`) and 24 for the three `Rotor`
definitions (`jointCount = 2`) in the shipped `BELL/HELI.QVM`.

---

## 2. The Heli parameter table

`ParamTable_New(13)` at `heli.c:183`; the thirteen `ParamTable_Add` calls run
`:186-200`; the two ranges are `gHeliRange[0] = 0; gHeliRange[1] = 1000;` at
`:184-185` and `Param_Range02()` at `:188`.

| # | Engine field (`heli.c:186-200`) | Authored name | Type | Tokens | Range |
|---|---|---|---|---|---|
| 0 | `v.startPos` | Position | `ObjectPos` | 3 | — |
| 1 | `v.startMatrix` | Orientation | `Real32x9` | 3 | — |
| 2 | **`v.start_speed`** | **Original Thrust** | `Real32` | 1 | 0 … 2 |
| 3 | **`v.startVel`** | **Speed** | `Real32x3` | 3 | — |
| 4 | **`v.name`** | **Model** | `String16` | 1 | — |
| 5 | `v.collide` | Collision detection | `bool8` | 1 | — |
| 6 | **`v.airborne`** | **Force Z** | `bool8` | 1 | — |
| 7 | `rotor_expr` | Open door | `VarString` | 1 | — |
| 8 | `light_expr` | Can fire | `VarString` | 1 | — |
| 9 | `engine_expr` | Play sound | `VarString` | 1 | — |
| 10 | `light_params[0]` | ViewCone Alpha (degrees) | `Degrees` | 1 | — |
| 11 | `light_params[1]` | ViewCone Gamma (degrees) | `Degrees` | 1 | — |
| 12 | `light_params[2]` | ViewCone Length (meter) | `Real32` | 1 | 0 … 1000 |

**19 argument tokens**, so an authored instance carries 22 `argTokens` with the
editor's `[0]=task id, [1]=type, [2]=name` prefix
(`source/level/level_objects.h:116`). `ObjectPos` is an alias of `Real64x3` and
consumes three doubles (`paramops.c:185`), `Real32x9` and `Real32x3` consume three
each (`paramops.c:145-146`, `:149-150`).

**Three findings are load-bearing.**

1. **The two booleans are crossed against their authored names.** Slot 6 is
   authored *"Force Z"* but binds `v.airborne` (`heli.c:194`), and slot 5 is
   *"Collision detection"* binding `v.collide` (`heli.c:193`). Every retail
   instance authors `Force Z = FALSE` (§10), so the port's `airborne` is `false`
   for all 29 of them.
2. **"Original Thrust" is not thrust.** It is `v.start_speed`, the throttle the
   flight preview starts at (`heli.c:190`, read back in `Heli_Control` at
   `:379-380`). The retail values are 0.7 … 1.5, inside `Param_Range02`.
3. **"Speed" is not the throttle.** It is `v.startVel`, a body-frame velocity
   triplet that `Heli_Control` rotates into world space at tick 0
   (`heli.c:390-393`).

`Param_Degrees` is a derived type whose parent is `Real32`
(`paramops.c:184`), so the two ViewCone angles consume one token each and the
value is stored **verbatim in degrees** — no conversion happens on parse. The
port keeps `viewcone_alpha`/`viewcone_gamma` in degrees for that reason.

`ParamReal32x9_Parse` (`paramops.c:423-435`, `HOOK(0x4EFBA0, ...)`) reads its
three tokens as **Euler angles** `x, y, z` and calls `Matrix_SetEuler(m, x, y, z)`,
which is `Rz(z)·Ry(y)·Rx(x)` with `count` forced to 0
(`src/math/matrix.c:99-110`). The port's `HeliSetEuler` reproduces that, including
the `count = 0`.

### 2.1 Defaults

`Heli_Reset` (`heli.c:214-237`, `HOOK(0x431560, ...)` at `:238`) sets
`v.start_speed = 1.0f` (`:226`) and `light_params[0..2] = 180.0f, 360.0f, 60.0f`
(`:234-236`). Those are exactly the values 26 of the 29 retail instances author,
which is why the declaration's defaults matter: they are the engine's, not the
level's.

---

## 3. Heli_Control — the sentinel and the tick-0 reset

`Heli_Control` is `heli.c:358-405`, `HOOK(0x431B70, Heli_Control)` at `:406`. It
is the flight-preview entry point and it is where the `0x431B70` triage row
points.

**The throttle sentinel is a raw bit test, not a float comparison:**

```c
if (*(uint32_t *)&arg->axes[3] != 0xBF800000)
    h->v.target_speed = arg->axes[3];
```

(`heli.c:360-361`). `0xBF800000` is exactly `-1.0f`, so `-1.0f` means "no
throttle key this tick" while `-0.99999994f` and a NaN **are** requests. The port
exposes this as `HeliControlThrustRequested`, which `memcpy`s the bits.

`axes[0..2]` are copied straight into `v.steer[0..2]` (`heli.c:362-364`).

**Tick 0 teleports the helicopter back to its authored start** (`heli.c:365-399`):

| Step | Line | Effect |
|---|---|---|
| 1 | `:371-375` | writes the caller's array back: `axes[3] = -1.0f`, `axes[4] = -2.0f`, `axes[0..2] = 0` |
| 2 | `:376` | `v.matrix = v.startMatrix` |
| 3 | `:379-380` | `v.speed = v.target_speed = v.start_speed` |
| 4 | `:381-388` | `v.turn[0..2] = 0`, `v.steer[0..2] = 0`, then `Vehicle_DeleteParts`/`Vehicle_NewParts` |
| 5 | `:390-393` | `v.body.vel = matrix * startVel`, row-major: `vel.x = m[2]*v.z + m[1]*v.y + v.x*m[0]` |
| 6 | `:394` | `Body_Update(&h->v.body)` |

Step 1 is what makes the reset visible to the caller: the tick-0 write-back is
how the engine tells the preview to stop feeding a throttle. `Vehicle_ResetControls`
(`src/vehicle/vehicle.c:189-197`, `HOOK(0x4F4600, ...)`) runs between steps 1 and 3
at `heli.c:378` and zeroes `turn`, `speed`, `steer`, `target_speed` and `brake`,
but steps 3-4 overwrite all of those anyway, so only `brake` is its lasting
effect.

`DynCube_Move(Game_GetWorld(), h, &h->v.startPos, r)` (`heli.c:370`) is the one
step the port cannot perform — it needs the world — so `HeliControlReset` takes
`start_pos` and reports it to the caller instead of applying it.

Only ticks **above** zero advance the simulation: `if (arg->tick > 0) {
Vehicle_RunParts(h); Heli_Move(h); }` (`heli.c:401-404`). Tick 0 therefore both
resets and skips the move, and a negative tick does nothing at all.

---

## 4. Heli_Move

`Heli_Move` is `heli.c:412-519`, `HOOK(0x431E30, Heli_Move)` at `:520`. The port
splits it into `HeliMoveStep` plus one function per block so each rule is testable
on its own.

### 4.1 The speed ramp

```c
float rate = !(h->v.speed >= kAccelSpeed) ? cfg->accel_slow : cfg->accel;   /* :416 */
long double s = h->v.speed;                                                 /* :418 */
if (!(s >= target)) { s = s + rate; if (s > target) s = target; }           /* :422-425 */
else if (s > target) { s = s - rate; if (!(s >= target)) s = target; }      /* :426-429 */
if (!(s >= kMilli) && !fne(h->v.target_speed, kZero)) s = kZero;            /* :431-432 */
h->v.speed = s;                                                             /* :433 */
```

Three things matter. `kAccelSpeed` is `0.8f` and the test is `!(speed >= 0.8)`, so
a NaN speed takes `accel_slow`. `s` stays a `long double` and is rounded **once**
on the store into `v.speed` — but the *same wide value* is then used for the roll
force at `:451`, so the port exposes `HeliApproachSpeedWide` rather than re-reading
the narrowed field. And the sub-milli collapse needs **both** `s < 0.001` and a
target of exactly zero (or NaN), because `fne` is `a < b || a > b`
(`src/heli/heli.h:119-122`): a helicopter hovering under throttle keeps its
sub-milli ramp value instead of snapping to a standstill.

### 4.2 The smoothed turn and the three forces

```c
t0 = (kOne - cfg->axis[0].smooth) * h->v.steer[0] + cfg->axis[0].smooth * h->v.turn[0];
h->v.turn[0] = t0;   /* and the same for axes 1 and 2, heli.c:434-439 */
```

The gate is `(fne(t0,0) || fne(t1,0) || fne(t2,0)) && (!h->v.touching ||
h->v.body.speed > kTwo)` (`heli.c:440-441`). `kTwo` is `2.0f`, and the comparison
is strictly greater, so a grounded helicopter at exactly 2.0 does **not** get turn
forces.

Inside the gate (`heli.c:443-455`), with `a0 = t0*axis[0].rate`,
`a1 = t1*axis[1].rate`:

| Force | Value | Application point | Line |
|---|---|---|---|
| `f1` | `{0, a0*mass*kYawForce, 0}` | `{0, 0, 819.2}` | `:445`, `:449`, `:453` |
| `f2` | `{a1*mass, 0, 0}` | `{0, 0, 409.6}` | `:446`, `:450`, `:454` |
| `f3` | `{s*(t2*axis[2].rate)*mass*kRollForce, 0, 0}` | `{0, 12288, 0}` | `:447`, `:451`, `:455` |

`stale = f2[0]` is saved at `:452` and consumed by §4.3.

### 4.3 The nose pitch — and a reused local

The block runs only while airborne and fast: `if (!h->v.touching && h->v.body.speed > kTwo)`
(`heli.c:457`).

```c
if (fne(m[0], kZero) || fne(m[3], kZero)) {                       /* :463 */
    long double inv = kOneD / x87_sqrt(m[0] * m[0] + m[3] * m[3]);/* :464 */
    float cx = m[0] * inv;                                        /* :465 */
    long double cy = m[3] * inv;                                  /* :466 */
    pitch = kZero * m[8] + cy * m[5] + cx * m[2];                 /* :468 */
    if (m[8] > kZero) pitch = pitch * kPitchDown;                 /* :470 */
    else              pitch = pitch * kSevenTenths;               /* :472 */
} else {
    pitch = stale;   /* the original reuses a stack slot here */  /* :475 */
}
Matrix_SetRotX(&r, pitch * cfg->axis[2].rate * kPitchScale);      /* :477 */
for (i = 0; i < 3; i++) {                                         /* :478-483 */
    nm.m[i]     = r.m[i]     * m[0] + r.m[6+i] * m[2] + r.m[3+i] * m[1];
    nm.m[3 + i] = r.m[i]     * m[3] + r.m[6+i] * m[5] + r.m[3+i] * m[4];
    nm.m[6 + i] = r.m[6 + i] * m[8] + r.m[i]   * m[6] + r.m[3+i] * m[7];
}
nm.count = h->v.matrix.count + r.count + 1;                       /* :483 */
h->v.matrix = nm;                                                 /* :484 */
```

Three findings:

1. **The composition is `nm = m·r`, not `r·m`.** Row `i` of `nm` is
   `Σ_k m[i][k]·r[k][j]`, which is why the engine's `r.m[6 + i] * m[2]` term is
   `m[i][2] * r[2][j]`. Because `r` is a pure X rotation and `m` is the heading,
   this is a **body-frame** pitch: the nose dips relative to the airframe, not the
   world. Writing it as `r·m` would pitch about the world X axis and is a
   different rotation for any non-trivial heading. `r.count` is 0 because
   `Matrix_SetRotX` sets it (`matrix.c:113-128`), so `nm.count = m.count + 1`.
2. **`stale` is not uninitialised.** The declaration is
   `float t0, t1, t2, stale = 0;` (`heli.c:419`), so the degenerate branch reads 0
   unless the turn-force block ran this tick, in which case it reads `f2[0]`
   (`:452`). The engine's own comment calls it a reused stack slot; the port
   reproduces the reuse as `HeliMoveState::stale`.
3. **The degeneracy test is `fne`, not `==`.** `fne` is false when either side is
   NaN, so a NaN `m[0]` takes the `else` branch and pitches by `stale`.

The per-row term order differs between rows (`k = 0, 2, 1` for rows 0 and 1;
`k = 2, 0, 1` for row 2) and is reproduced term for term. Note that this order is
**not** numerically observable here, and neither is the `last` parameter of
`mat_mul` (§8.4): every product and the whole sum are accumulated in `long double`
and rounded once, so the permutations agree. The port keeps them for source
fidelity, and the test pins the *term selection* rather than a rounding artefact.

### 4.4 The rest gate, the aero forces and the collide call

```c
if (!PhysicsObj_IsResting(h)) {                                   /* :486 */
    Heli_UpdateWheels(h);                                         /* :490 */
    if (c->part_count) Body_AddAeroForces(c->parts, c->part_count,
                          0x1.56e264p-12f, &h->v.body, &h->v.matrix, &v);  /* :498 */
    PhysicsObj_Collide(h, 0x1.64ce9ep+5f, 1.0f, 0x1.d70a3ep-1f,
                       Game_GetWorld(), 12, msg);                 /* :500 */
}
```

`PhysicsObj_IsResting` (`src/graphtask/physobj.c:514-527`, `HOOK(0x4FC7B0, ...)`)
is a five-way AND: `touching > 0`, `!moved`, `forceCount == 0`,
`!(speed >= restSpeed)` and `!(|angVel|² >= restSpin)`. The rest thresholds come
from the physics definition at indices 10 and 11 — for `BELL/HELI.QVM` that is
`68.0` and `0.001`. Note that the turn forces of §4.2 have already been added when
this runs, so any tick that added forces is a tick that cannot be resting.

`Heli_UpdateWheels` and `PhysicsObj_Collide` need the retail world and are
therefore **not** ported (§5 covers the wheel arithmetic, which is). The port
exposes `resting_gate_open` so the caller knows whether the engine would have run
them.

### 4.5 The sound parameters

```c
h->sound_params[1] = h->v.speed;                                  /* :511 */
int fast = 1;
if (!(h->v.speed > kHundredth)) fast = 0;                         /* :512-513 */
h->sound_params[2] = fast;                                        /* :514 */
```

`kHundredth` is `0.01f` and the test is `!(speed > 0.01)`, so exactly `0.01` is
*slow* and a NaN is slow too. `SoundParams_New(2)` (`src/sound/soundsys.c:526-541`,
`HOOK(0x4E6AB0, ...)`) stores the count in slot 0 and zeroes the rest, which is why
the port's `sound_params[0]` is `2.0f`.

---

## 5. The landing wheels, and a swapped pair of parameters

`Heli_UpdateWheels` is `heli.c:523-618`, `HOOK(0x432550, ...)` at `:619`.

**The authored pair is stored swapped.** `HeliWheel_ParseArgs`
(`src/vehicle/parts.c:284-292`, `HOOK(0x427240, ...)`):

```c
p[1] = arg(i) * kAngleUnits;
p[0] = arg(i + 1) * kAngleUnits;
```

with `kAngleUnits = 0x1.0p+12f` = 4096 (`parts.c:61`). The port's
`ParseHeliWheelParams` names the results by what they are used as downstream
(`top` and `len`), so `top` gets authored argument 0 and `len` gets argument 1.
This is reproduced, not corrected, and pinned in the corpus test.

**The probe** (`heli.c:541-562`) starts at the wheel's own position raised by
`kTwoMetersD` (`8192.0`, two metres) and runs down `len + kTwoMetersD`, recording
the offset vector at `w + 0xF0`. The locals `len`, `top`, `depth[]` and `max_lift`
are **doubles** (`heli.c:526-527`), `max_lift` starting at `-DBL_MAX`. The loop
stops after four wheels (`heli.c:546-547`).

**The lift** (`heli.c:567-609`), per hit:

```c
if (!hit->found) continue;                                        /* :573-574 */
long double a = depth[i]; if (!(a >= kZeroD)) a = -a;
if (!(a >= hit->dist)) continue;                                  /* :575-579 */
long double dist = hit->dist; if (!(dist >= kZeroD)) dist = -dist;
double pen = len - (dist - kTwoMetersD);                          /* :583 */
t = top + kTwoMetersD;                                            /* :586 */
if (t > hit->dist) {                                              /* :587 */
    float u = top + kTwoMeters;                                   /* :588 */
    landed = 1;                                                   /* :590 */
    force[2] = ((u - dist) / u + kOne) * mass * kLift;            /* :591 */
    if (t - hit->dist > max_lift) max_lift = t - hit->dist;        /* :592-593 */
} else {
    landed = 0;                                                   /* :595 */
    force[2] = pen / len * mass * kLift;                          /* :596 */
}
force[3..5] = offs[i]; Body_AddForce(...); touching++;            /* :598-602 */
vel.x *= kHeliDamp; vel.y *= kHeliDamp; vel.z *= kDampZ;          /* :603-605 */
angMom *= kDamp98;                                                /* :606-608 */
```

Three findings, all reproduced:

1. **The `pen / len` "compression" branch is reachable only when `top <= len`.**
   The guard above it admits `|depth| >= dist`, i.e. `dist <= len + 8192`, while
   the branch itself requires `top + 8192 <= dist`. For that window to be
   non-empty, `top + 8192 <= len + 8192`, i.e. `top <= len`. The shipped `BELL`
   wheel authors `top = 8192` (authored argument 0) and `len = 2048`, so it can
   **never** reach the compression branch; the test asserts exactly that over the
   whole admissible range.
2. **The division by `len` is unguarded** (`heli.c:596`). A zero-length wheel
   produces a NaN (or an infinity) force. Reproduced rather than clamped.
3. **`landed` is loop-carried and assigned in *both* branches**, while `max_lift`
   only ever grows. `landed` is declared once at `heli.c:534`; `:590` sets it to
   1 and `:595` sets it back to 0. So if an earlier wheel lands and a later one
   compresses, the loop finishes with `landed == 0` and the correction at
   `heli.c:610-617` is skipped even though `max_lift` holds a positive value from
   the earlier wheel. The same set of hits in the other order does lift the hull.
   `HeliWheelLift` is deliberately one iteration, so the aggregation is the
   caller's; `HeliRules.WheelLiftLandingIsDecidedByTheLastProcessedHit` models
   the engine's own loop and pins both orders.

The landing correction (`heli.c:610-617`) lifts the hull by `max_lift` in Z and
moves it, which needs the world and is left to the caller.

---

## 6. The control table and the keyboard

`Heli_GetControlInfo` (`heli.c:671-681`, `HOOK(0x432BE0, ...)`) returns
`&gHeliControls[i]` for `0 <= i < 5`. `gHeliControls` is at `heli.c:754`:

| # | Name | Min | Max | String defined at |
|---|---|---|---|---|
| 0 | `dAlpha` | −360 | 360 | `heli.c:754` |
| 1 | `dBeta` | −360 | 360 | `str_53BEB0` |
| 2 | `dGamma` | −360 | 360 | `str_53BE10`, `src/heli/car.c:533` |
| 3 | `Thrust` | 0 | 1 | `str_53BD14`, `src/missile/train.c:587` |
| 4 | `PilotAnim` | 0 | 16 | `str_53BD08`, `src/missile/train.c:585` |

The three `str_` symbols are `REF_EXTERN` references into the exe's data segment;
the port carries the resolved strings.

`Heli_ReadKeys` (`heli.c:687-732`, `HOOK(0x432C10, ...)` at `:732`) fills the five
axes:

- `out[3] = -1.0f` first (`:700`), then a held key in `2..0xB` overwrites it with
  `throttle[i - 2]` where the ladder is
  `{0.84, 0.88, 0.92, 0.96, 1.0, 1.04, 1.08, 1.12, 1.16, 1.2}` (`:689`, loop
  `:704-706`). The last held key wins.
- `0x29` (the `` ` ``/`0` key) sets `out[3] = 0` **before** the ladder loop
  (`:702-703`), so a held ladder key overrides it.
- `PRESSED(0x31)`/`PRESSED(0x32)` nudge by `kSpeedStep` from `v.target_speed` and
  clamp to `[kAccelSpeed, kMaxThrottle]` = `[0.8, 1.25]` (`:707-720`).
- `out[4] = -2.0f` (`:722`) unless `A` is held with `` ` `` (`-1.0f`) or a digit
  key, giving `i < 0xB ? i - 1 : 0` (`:723-730`).

**The ladder and the nudge clamp disagree**: the ladder's own range is 0.84…1.2
while the nudges clamp to 0.8…1.25. Both are reproduced.

---

## 7. HeliConfig — and a list nobody reads

`Vehicle_ParseConfig` (`src/editobj/vehconfig.c:16-39`, `HOOK(0x5031F0, ...)`)
reads, from `first_arg`:

1. ten floats into `&cfg->unk0`: `start_speed_scale`, then
   `axis[0..2] = {rate, smooth}` (three pairs), then `thrust`, `accel`,
   `accel_slow`;
2. `part_count`;
3. `part_count` × seven tokens, each an `AeroPart_Init(area, pos*4096, normal)`;
4. three trailing floats: `unk10C[0] *= 4096`, `unk10C[1] *= 0.5·degToRad`,
   `unk10C[2] *= 0.5·degToRad`.

`AeroPart_Init` (`src/animtrig/body.c:86-94`, `HOOK(0x4EC4D0, ...)`) normalises
the normal **unless it is exactly the zero vector**, in which case it normalises
the position, and failing that the area. The port normalises only the normal
(the retail data never hits the fallbacks — every `BELL`/`MIL` part authors an
axis-aligned unit normal).

`Heli_ParseConfig` is `src/plane/heli.c:77-89` and then reads two lists:

```c
cfg->count1 = Script_GetArgInt(args, (*next)++);
for (i = 0; i < cfg->count1; i++) cfg->list[i]      = Script_GetArgInt(args, (*next)++);
cfg->count2 = Script_GetArgInt(args, (*next)++);
for (i = 0; i < cfg->count2; i++) cfg->list[3 + i]  = Script_GetArgInt(args, (*next)++);
```

**Finding: `count1` and `list[0..count1-1]` are parsed and then never read
anywhere in the retail engine.** A whole-tree grep for the field finds only the
parse and the struct declaration (`src/heli/helitypes.h:50-51`). Only `count2` and
`list[3..]` are consumed, as explosion point indices for `Model_GetPoint`
(`src/heli/heli.c:265-278`). The port parses both lists so the argument cursor
stays aligned, and the corpus test pins the retail values.

**Finding: the two lists overlap.** The engine's own comment on
`Heli_ParseConfig` (`src/plane/heli.c:76`) says "the second list overlaps the first
when that has more than three entries" — because the second list starts at
`list[3]` while the first starts at `list[0]`. `HeliConfig::list` is
`int list[13]` (`helitypes.h:51`), so a `count1` of 4 or more silently corrupts
whatever the first list held. No retail definition triggers it (§10), so the port
reproduces the aliasing without a guard and documents it.

---

## 8. The Rotor magic object

`Rotor_Create` is `0x42C500` (`src/rotor/rotor.c:88-107`); it zeroes `o->value`
at `:103`. `Rotor_ParseDef` is `0x42D9F0` (`rotor.c:302-316`); `Rotor_Run` is
`0x42D440` (`rotor.c:213-263`); `Rotor_DrawPrim` is `0x42C630`
(`rotor.c:110-166`); `Rotor_Move` is `0x42D680` (`rotor.c:265-290`).

### 8.1 The definition

```c
typedef struct { char lift; char tilt; char pad[2]; float angle; float speed;
                 int dir; char name[0x10]; } RotorDef;   /* 0x20 bytes */
```

`Rotor_ParseDef` reads six tokens at `first_arg` (`rotor.c:309-314`):
`lift = GetArgInt(i)`, `tilt = GetArgInt(i+1)`, `dir = GetArgInt(i+2)`,
`angle = GetArgReal(i+3) * kDegToRad`, `speed = GetArgReal(i+4)`,
`GetArgString(i+5, d->name, 0x10)`. `kDegToRad` is the double
`0x1.1df46aaaaaaabp-6` = 0.017453293005625408, and the conversion is done in
double and narrowed once — the port reproduces that and the test tolerance is a
float's own resolution.

**`dir` is a blade-copy count, not a sign.** `Rotor_DrawPrim` loops
`for (i = 0; i < d->dir; i++)` (`rotor.c:146-164`), drawing `dir` copies of the
blade geometry. The shipped values are 1, 10 and 17 (§10) — a helicopter with a
17-blade tail rotor makes no sense, so `dir` is the authored "how many copies to
place around the axis", with the per-copy angular increment coming from
`speed * car->v.speed`.

### 8.2 Rotor_Run — spin, lift and tilt force

```c
long double lift = mass * car->v.speed * kLift;                   /* :219 */
float f = (float)lift;                                            /* :220 */
if (lift != 0) {                                                  /* :222 */
    up  = (m[1], m[4], m[7]); fwd = (m[2], m[5], m[8]);           /* :226-231 */
    if (up[0] != 0 || up[1] != 0) { up[2] = 0; a = Vec3_Angle(up, fwd); }  /* :232-234 */
    force = {0, (float)(x87_cos(a) * f), f, 0, 0, 0};             /* :236-241 */
}
o->value = (float)((long double)d->dir * d->speed * car->v.speed + o->value);  /* :260 */
```

with `f` the float lift. `Vec3_Angle` (`src/file/vec.c:189-208`, `HOOK(0x4B2B30,
...)`) guards zero vectors, computes `dot/(|a||b|)` into a `volatile double`, and
returns `0` when the cosine reaches 1 and `π` when it does not exceed −1. The
tilt block is gated on `car->v.speed != 0 && (d->tilt || d->lift) && !touching`
(`rotor.c:245`), zeroes `force[0..2]`, sets `force[3] = 0`, `force[4] = kTiltY`
(8192) at `:249`, `force[5] = 0`, and then

```c
if (d->tilt)      force[0] = -(mass * speed * kTilt);   /* :254-255 */
else if (d->lift) force[0] = +(mass * speed * kTilt);   /* :256-257 */
```

so a rotor flagged both `tilt` and `lift` takes the **tilt** sign. The lift block
needs `d->lift` and a non-zero `mass * speed * kLift` (`rotor.c:218`, `:222`) —
the test is on the wide `lift`, not on the narrowed `f`. `Rotor_Run` runs
`RUN_CHILDREN` after both blocks (`rotor.c:261`).

### 8.3 Rotor_DrawPrim — the blade copies and the tilt

```c
if (!copy.model) return;                                          /* :129-130 */
step = (float)((long double)d->speed * car->v.speed);             /* :131 */
maxTilt = d->angle;                                               /* :132 */
rate = m[8]*vel.z + m[5]*vel.y + m[2]*vel.x;   /* long double */   /* :133-134 */
k = (long double)kOne.f - car->v.speed;                           /* :135 */
if (kMinPitch.f > k) k = kMinPitch.f;                             /* :136-137 */
else if (kOne.f < k) k = kOne.f;                                  /* :138-139 */
tilt = (float)(-(k*maxTilt) - rate*kYawTilt.f*maxTilt /
               ((long double)cd->unk0 * kMassTilt.f));            /* :140 */
if (-maxTilt > tilt) tilt = -maxTilt;                             /* :141-142 */
else if (maxTilt < tilt) tilt = maxTilt;                          /* :143-144 */
a = (float)(src->value * kSpin.f);                                /* :145 */
for (i = 0; i < d->dir; i++) {                                    /* :146-164 */
    Matrix_SetRotZ(&rz, tilt);                                    /* :149 */
    Matrix_SetRotX(&copy.matrix, a);                              /* :150 */
    mat_mul(&t, &base, &copy.matrix, 0, 0, 0); copy.matrix = t;   /* :151-152 */
    mat_mul(&t, &copy.matrix, &rz, 0, 0, 0);     copy.matrix = t; /* :153-154 */
    draw(&copy);                                                  /* :157-162 */
    a = a + step;                                                 /* :163 */
}
```

`cd->unk0` is the owner's `VehicleConfig::unk0`, i.e. `start_speed_scale` — the
`BELL` definition authors 200. `base` is the owner's matrix saved **before** the
loop (`:114`), and `Matrix_SetRotX(&copy.matrix, a)` overwrites the drawing copy
each iteration (`:150`), so the composition is `base · RotX(a) · RotZ(tilt)`.

Two quirks are reproduced:

- Because the low clamp tests `-maxTilt > tilt` *before* the high clamp tests
  `maxTilt < tilt`, a **NaN** tilt falls through both and is left alone. The
  engine's `if/else if` chain (not two independent clamps) is what makes that
  possible, so the port keeps the chain.
- Lines `:155-156` carry the engine's own comment that the original
  re-orthonormalises both products once `unk24` passes 1000 "but only into locals
  that are never read". That computation has no effect and is not ported.

`Rotor_Draw` (`rotor.c:168-182`) builds the child frame from the **unspun**
`o->obj.matrix` (`:179-180`), so the children of a rotor do not rotate with the
blades — the spin is confined to the drawing copy `copy`.

### 8.4 Rotor_Move

```c
x = m[2]*offset.z + m[1]*offset.y + m[0]*offset.x;   /* and y, z */ /* :268-270 */
p = xyz + a->pos;                                                  /* :275-277 */
DynCube_Move(...);
mat_mul(&f, a->matrix, &o->local, 1, 0, 0);                        /* :280 */
```

`mat_mul` is the inline in `src/rotor/rotor.h:159-174`: for row `i` it takes
`last = last0/last1/last2`, derives `k0 = (last == 0) ? 1 : 0` and
`k1 = (last == 2) ? 1 : 2`, and sums the three products **in `long double`**,
rounding once, then sets `count = a->count + b->count + 1`. As noted in §4.3, the
`last` parameter selects the terms but, with a single rounding, all three
permutations agree; the port keeps the parameter and the test pins the selection.

### 8.5 The constants

`rotor.c:70-79`: `kLift 0x4232674F` (44.600887298583984), `kTilt 0x40200000`
(2.5), `kTiltY 0x46000000` (8192), `kMinPitch 0xBDCCCCCD` (−0.1),
`kYawTilt 0x3CD80000` (0.0263671875), `kMassTilt 0x3ECCCCCD` (0.4),
`kSpin 0x41AAE714` (21.362831115722656), `kDegToRad 0x1.1df46aaaaaaabp-6`
(double). The port carries them by value with the hex cited.

---

## 9. The module

`source/runtime/heli_rules.h` (533 lines) and `source/runtime/heli_rules.cpp`
(959 lines) are **pure logic**: no renderer, no world, no file I/O. The surface is

| Group | Functions |
|---|---|
| Authored data | `HeliParamTable`, `HeliControlTable`, `HeliThrottleSteps`, `HeliThrottleStep` |
| Parsing | `ParseHeliParams`, `ParseHeliWheelParams`, `ParseRotorDef`, `ParsePhysicsDefHeader`, `ParseHeliVehicleConfig`, `ParseHeliConfig` |
| Matrices | `HeliMatrixMul`, `HeliSetRotX`, `HeliSetRotZ`, `HeliSetEuler`, `HeliFne` |
| Control | `HeliControlThrustRequested`, `HeliControlApplyAxes`, `HeliControlReset` |
| Move | `HeliAccelRate`, `HeliApproachSpeed(Wide)`, `HeliSmoothTurn`, `HeliTurnForces`, `HeliNosePitch`, `HeliComposeNoseMatrix`, `HeliRotorSoundParams`, `HeliIsResting`, `HeliMoveStep` |
| Wheels | `HeliBuildWheelProbe`, `HeliWheelLift`, `HeliWheelDamp` |
| Rotor | `RotorDefArgIndex`, `RotorRunValue`, `RotorSpinAngle`, `RotorBladeStep`, `RotorBladeTilt`, `RotorLiftForce`, `RotorTiltForce`, `RotorOffsetPos`, `RotorLocalMatrix`, `RotorBladeMatrix` |

`HeliMoveStep` is the one function that composes a whole tick; every rule inside
it is also callable alone, which is how the tests isolate the quirk they are
pinning. `Heli_UpdateWheels`, `Body_AddAeroForces` and `PhysicsObj_Collide` are
not ported because they need the retail world; `HeliMoveStep` reports
`resting_gate_open` so a caller knows whether the engine would have run them.

---

## 10. The retail corpus

Six corpus tests read the shipping data with the editor's own `QVM_Parse` and
`QVM_DecompileToString`, and `GTEST_SKIP` if `IGI_GAME_PATH` (default
`/Users/haseeb-mir/Downloads/IGI`) is absent. All six **ran** here; none skipped.

**The authored declaration** appears identically in the `objects.qsc` text and in
the `OBJECTS.QVM` bytecode, and matches `HeliParamTable()` 1:1 in order, name and
type: `LEVEL2/objects.qsc:62-63`, also `level1/objects.qsc:73-74` and
`LEVEL4/objects.qsc:56-57`.

**29 authored `Heli` instances** across 14 missions (`LEVEL9` and `LEVEL14` ship
none):

| Level | Helis | Notable authored values |
|---|---|---|
| `level1` | 2 | #1 `thrust 1.5`; #2 named *"Heli which Jones jumps off onto train"*, the only `collide = FALSE` in the game |
| `LEVEL2` | 1 | `thrust 0.75`, `alpha 180 gamma 360 len 60` |
| `LEVEL3` | 3 | #2 *"Insertion helicopter LANDING"* `thrust 0.94`; #3 named `g`, `thrust 1.15` |
| `LEVEL4` | 2 | #1 `sound "0"`, `thrust 0.7` |
| `LEVEL5` | 2 | — |
| `LEVEL6` | 1 | model `700_01_1`; **the only `Open door` expression in the game**: `AnimTask_852.nTick > 20 * GAME_FREQUENCY` |
| `LEVEL7` | 4 | #1 `len 180`, `Can fire = AnimTask_306.isRun && AnimTask_306.nTick > 10 * GAME_FREQUENCY`; #2 `alpha 270 len 150`, `Can fire = 1` |
| `LEVEL8` | 1 | — |
| `LEVEL10` | 3 | #1 *"Hind"* `len 100`, `Can fire = AnimTask_801.nTick == 30 * GAME_FREQUENCY`; #2 *"Huey"* `thrust 1.4` |
| `LEVEL11` | 3 | #2 named `a`, `thrust 1.5` |
| `LEVEL12` | 4 | #1/#2/#4 *"Ekks helicopter1"* |
| `LEVEL13` | 3 | — |

Cross-cutting: **`Force Z` is `FALSE` in all 29** instances; `alpha`/`gamma` are
`180`/`360` everywhere except `LEVEL7 #2` (`alpha 270`); `len` is `60` except
`LEVEL7 #1` (180), `LEVEL7 #2` (150) and `LEVEL10 #1` (100); the authored
`Original Thrust` spans 0.7…1.5, inside `Param_Range02`; the authored `Model` is
`709_01_1` (22 instances) or `700_01_1` (7), i.e. exactly the model strings of the
two shipped airframes' `DefinePhysicsObjType` headers. Every instance parses to
exactly 22 `argTokens`.

**`physicsobj/HELIS/BELL/HELI.QVM`** — six definitions. The `Heli` one is
`jointCount 0`, so `first_arg = 12` and the config is

| Field | Value |
|---|---|
| `start_speed_scale` | 200 |
| `axis[0..2]` | `{50, 0.4}` each |
| `thrust` / `accel` / `accel_slow` | 3000 / 0.027 / 0.003 |
| `part_count` | 3 |
| parts | `(20, normal 1,0,0)`, `(3, normal 0,1,0)`, `(50, normal 0,0,1)`, all at position 0 |
| `unk10C` | `(0, 0, 0)` |
| `count1` / `list[0..1]` | `2` / `{0, 1}` |
| `count2` | `0` |

The three rotors, at `first_arg = 24`:

| Def | `lift` | `tilt` | `dir` | `angle` | `speed` | name |
|---|---|---|---|---|---|---|
| 1 | 1 | 0 | 1 | 0 | 0.1 | `711_01_1` |
| 2 | 0 | 0 | 17 | 5 | 0.07 | `711_03_1` |
| 3 | 0 | 1 | 10 | 0 | 0.12 | `712_02_1` |

Defs 4 and 5 are `TASKTYPE_GENERICPHYSICSOBJ` and carry no task arguments.

**`physicsobj/HELIS/MIL/HELI.QVM`** is the second airframe and the one that
populates both lists: `unk10C = (60·4096, 180·0.5·degToRad, 360·0.5·degToRad)`,
`count1 = 2`, `list[0..1] = {0, 1}`, `count2 = 6`, `list[3..8] = {9, 10, 11, 12,
13, 14}`. Its `Heli` model string is `700_01_1`, matching the 7 retail instances
above, and its first rotor is `700_03_1`. No shipped definition has `count1 > 3`,
so the `list[3 + i]` overlap of §7 never fires on retail data — which is why the
port reproduces the aliasing without a guard and documents it instead.

The `LEVEL2` instance is asserted field by field against
`LEVEL2/objects.qsc:62-64`, and the three levels that ship a decompiled
`objects.qsc` (`level1`, `LEVEL2`, `LEVEL4`) are cross-checked QVM against text
for every authored value. That comparison is over the 22 authored tokens only:
the decompiler nests a task's **children** inside its parent's argument list
(`level1/objects.qsc:913` gives the first Heli four trailing `Task_New`
arguments), whereas the QVM stores children as separate tasks, so the text form
legitimately carries extra trailing arguments.

---

## 11. A known divergence: the editor's rotor preview rate

`source/renderer/renderer_objects_atta.cpp:1263-1277` spins `TASKTYPE_ROTOR`
attachments for the *drawing* copy using a hardcoded

```cpp
constexpr float kRotorPreviewRadPerSec = 15.0f;   // :1274
```

The retail rate is not a constant: `Rotor_Run` accumulates
`dir * speed * car->v.speed` per tick (`rotor.c:260`) and the drawn angle is
`value * kSpin` (`rotor.c:145`), with a per-blade increment of
`speed * car->v.speed` (`rotor.c:131`). The editor has no vehicle speed, so it
cannot reproduce the rate; the *axis*, *sign* and *composition order* are the
game's. This module now supplies the real rule
(`RotorRunValue`, `RotorSpinAngle`, `RotorBladeStep`, `RotorBladeMatrix`,
`RotorBladeTilt`), so wiring the renderer to it is a follow-up rather than a
missing rule. It is recorded here rather than changed, because
`source/renderer/` is outside this task's scope and the divergence is already
documented in `docs/igi-re-parity/ATTA_CHILDREN_ROTORS.md`.

The editor's object schema (`source/level/task_schema.cpp:57-62`) maps `Heli`
token 8 to a "Heading" field and token 13 to "Model". Token 8 is the third
authored Euler angle, which `Matrix_SetEuler` applies as the Z (yaw) rotation, so
the schema is **correct** for `Heli` — no divergence.

---

## 12. Verified, and not

The commands, run in this worktree on 2026-10-26:

```
$ cd /Users/haseeb-mir/Documents/Code/project-igi-editor-igi-re && \
  clang++ -std=c++20 -Wall -Wextra -Wpedantic -fsyntax-only \
    -I source -I third_party/glm-1.0.1 -I third_party/GL/include \
    source/runtime/heli_rules.cpp
[exit code: 0]
```

(No output: zero warnings, zero errors.)

```
$ cd /Users/haseeb-mir/Documents/Code/project-igi-editor-igi-re && \
  IGI_HOST_NO_CACHE=1 IGI_HOST_OUT_DIR=build-mingw/ht-heli \
  IGI_HOST_LOG_DIR=build-mingw/log-heli \
  IGI_HOST_TESTS=tests/test_heli_rules.cpp bash scripts/run-host-tests.sh
   PASS  test_heli_rules                      58 passed    0 failed  (58 tests, exit 0)
   tests run             : 58
   tests passed          : 58
   tests failed          : 0
   tests skipped         : 0 (in 0 suite(s))
   game corpus           : /Users/haseeb-mir/Downloads/IGI
OK: 58 tests passed in 1 suites
[exit code: 0]
```

The 52 `HeliRules` tests assert hard-coded expected values derived independently
of the port; the 6 `HeliRulesCorpus` tests read the retail files and ran rather
than skipping (0 skips).

- **Proved:** the parse of the authored declaration and of all 29 retail `Heli`
  instances; the `BELL` and `MIL` `HeliConfig` values including the three-part
  `unk10C` scaling and both lists; the three `BELL` rotor definitions; the
  argument index `12 + 6·jointCount`; the `0xBF800000` sentinel against
  `-0.99999994` and NaN; the tick-0 reset including the caller-visible write-back
  and `matrix * startVel`; the ramp's two rates, its overshoot clamp and the
  sub-milli collapse needing a zero target; the first-order turn smoothing; the
  three force values, application points and the `!touching || speed > 2` gate;
  the nose-pitch sign for `m[8] > 0` and `m[8] <= 0`, the degenerate-basis
  fallback to the reused local, and that the composition is `m·r` (the reference
  is written as `m·r`, and `m·r ≠ r·m` for the test's heading, so the test
  discriminates); the rest gate's five conditions; the sound parameters at
  exactly 0.01; the swapped wheel parameters; the probe geometry; both lift
  branches, the `top <= len` reachability condition, the loop-carried `landed`
  in both wheel orderings, and the unguarded division;
  the throttle ladder and the disagreeing nudge clamp; `Matrix_SetEuler` against
  an independent `Rz·Ry·Rx`; `mat_mul`'s term selection for all 27 `last`
  combinations and its aliasing safety; the rotor spin accumulator, blade step,
  tilt clamps and NaN fall-through, lift force geometry and tilt-force signs.
- **Not verified — no rendered result.** The editor's GUI needs OpenGL 4.1 and
  this host provides 2.1, so no rotor, blade, view-cone or flight-preview frame
  was observed. Nothing in `source/renderer/` was modified and the port is not
  wired into a draw path. Whether the ported rules produce the retail
  *appearance* is unverified.
- **Not verified — the physics step itself.** `Heli_UpdateWheels`' `Coll_Lines`
  query, `Body_AddAeroForces`, `Body_Update`, `PhysicsObj_Collide`,
  `DynCube_Move` and `Vehicle_RunParts` all need the retail world. The port
  covers the arithmetic each one is fed with and reports the gate that decides
  whether the engine would have called them; the integrated flight trajectory is
  not reproduced and is not claimed.
- **Not verified — bit-exactness of the transcendental functions.** The engine
  uses x87 `fsin`/`fcos`/`fsqrt`; the port uses libm `sinl`/`cosl`/`sqrtl`. Blade
  angles, the nose pitch and the lift direction may differ in the last bits. This
  is the only known numerical divergence.
- **Not verified — the `stale` local's exact stack slot.** The decompilation
  shows the `else` branch reading the slot that held `f2[0]` and the declaration
  initialising it to 0, which is what the port models. Whether a different
  compiler build could have placed `stale` elsewhere and produced a different
  value is not knowable from the decompilation; the port follows the C.
- **Not verified — `dir`'s upper bound.** The port treats `dir` as a blade-copy
  count because `Rotor_DrawPrim` loops `i < d->dir`; the retail values 1/10/17 are
  consistent with that, but no `Rotor` definition in the shipped data is authored
  with `dir` outside 1…17, so the reading is supported by the code shape and the
  data rather than by an independent trace.
- **Not verified — `HeliConfig::count1`'s purpose.** The field is parsed and never
  read. What the level tools intended it for is not recoverable from the
  decompilation.
- **Not checked in this task:** the Windows `igi_tests` binary and the MCP tool
  tests, which the host lane does not build.

---

## 13. Files

| Path | What |
|---|---|
| `source/runtime/heli_rules.h`, `heli_rules.cpp` | the `Heli` + `Rotor` port |
| `tests/test_heli_rules.cpp` | 52 unit + 6 corpus tests |
| `CMakeLists.txt` | purely additive: one test file and one runtime source |
| `docs/igi-re-parity/ATTA_CHILDREN_ROTORS.md` | the renderer-side rotor record this one complements |
| `docs/igi-re-parity/TRIAGE_REMAINING_ISSUES.md` | the #60 row this closes |
