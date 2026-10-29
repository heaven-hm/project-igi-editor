# Authored sound emitters: `ConditionalSound` and `AmbientArea` (#70)

Status: implemented in `source/runtime/sound_emitter.{h,cpp}`, tested in
`tests/test_sound_rules.cpp`. Every constant, offset and line number below is
cited to IGI-RE (`/Users/haseeb-mir/Documents/Code/IGI-RE`), which is the only
evidence source used. Nothing here is taken from open-igi or from web research.

Two things are deliberately **not** in scope, and the reader should know it up
front: this module does not play audio (there is no audio pipeline in the editor,
and the GUI cannot run on this host — it needs GL 4.1, the host has 2.1), and it
does not model the DirectSound layer. It ports the **authored data** and the
**decision logic**: which emitter fires, when, how loud, and where. Everything
that depends on a real voice being allocated is exposed as an input the caller
supplies, and is marked unverified at the end.

---

## 1. What the issue asks, and what the engine actually has

The brief asks how a level authors a sound emitter, what the radius/falloff model
is, whether sounds loop or fire once, what the condition model is, and how
`AmbientArea` differs from `ConditionalSound`. The short answers, each defended
below:

| Question | Answer |
|---|---|
| Task types | `ConditionalSound` (HOOK 0x424E80) and `AmbientArea` (HOOK 0x44C130) |
| Parameters | 6 each, but different types and different authored-argument counts (8 vs 12) |
| Radius | **The emitter carries none.** The radius is the *sound definition's* `maxDist`, in metres, scaled by 4096 at use |
| Falloff | Linear in distance between `minDist` and `maxDist`; hard zero past `maxDist`; untouched inside `minDist` |
| Looping | A property of the **definition** (`loop`), not of the emitter. A `ConditionalSound` can additionally be a one-shot stream |
| Trigger | A script expression string, evaluated to a number and coerced through a signed `char`, used as a **rising edge** |
| `AmbientArea` vs `ConditionalSound` | `AmbientArea` is a **rotated box** with an eight-slot sound list, a random delay and a 0.05/frame volume integrator; `ConditionalSound` is a **point** with one named definition and a script condition |

The most common misconception the data kills is that the emitter has a radius.
Neither ParamTable registers one (condsound.c:87-92, ambientarea.c:74-79), and
the audible distance comes from the `DefineSound` record instead
(sounddef.c:65-71). A `ConditionalSound` may sit in the middle of a hangar and be
audible 1000 m away if its definition says so.

---

## 2. Where the authored data lives

Both emitters are ordinary task instances, so they live in the level's
`OBJECTS.QVM`, with a human-readable `objects.qsc` shipped alongside for three of
the fourteen levels (level1, LEVEL2, LEVEL4). The editor parses both:

* `source/level/qvm_parser.{h,cpp}` — `QVM_Parse` yields `QVMInstruction`s with a
  `QVMOpType`, an integer `operand` and a `float operand_float`, plus the
  `strings[]` and `identifiers[]` tables.
* `tests/test_sound_rules.cpp` — walks the bytecode for `Task_New` calls and
  cross-checks the three text levels against it.

### 2.1 The bytecode shape of an instance

An instance is a `Task_New` call:

```
PUSHS*  "ConditionalSound"      ; the type, pushed first
PUSHS*  ""                      ; the instance name
BRK
PUSHIIB "Task_New"              ; the function identifier
CALL    11                      ; 3 + 8 authored values
BRA     <next>
PUSHS*  "AlarmControl_98.isAlarm"
...
```

Two details matter for any extractor:

1. **The instance name is often the empty string.** For `ConditionalSound` it
   nearly always is: the readable scripts write `Task_New(-1, "ConditionalSound",
   "", "AlarmControl_98.isAlarm", ...)` (LEVEL1/objects.qsc:562). The first
   *parameter* is the condition, not the name. Reading the type and then treating
   the next string as a name misreads every instance.
2. **`Task_DeclareParameters` pushes the same type string.** Its argument list
   then repeats the declared `(name, type)` pairs verbatim, so it is told apart by
   looking for those pairs — for `ConditionalSound` the first four tokens are
   `Condition, VarString, SoundDef, String32` (LEVEL1/objects.qsc:78). The simpler
   "three strings in a row" test fails here, because a `ConditionalSound`
   instance's first two arguments are both strings.

`CALL 11` is the check that the authored argument count is 8: the operand is
`id + type + name + 8`. `AmbientArea` instances carry `CALL 15` (12 authored
values), and their declarations `CALL 13` (`1 + 2*6`).

---

## 3. The parameter schema

This is the authoritative schema, and it is *not* what the field names suggest.
The order is the `ParamTable_Add` order, which is the order the values appear in
the compiled call, and the authored names are the ones in the `.qsc`.

### 3.1 `ConditionalSound`

`ConditionalSound_Init`, HOOK 0x424E80, `src/vehicle/condsound.c:73-93`:

| # | Struct field | Authored name | Type | Args | Authored token |
|---|---|---|---|---|---|
| 0 | `cond` | `Condition` | `VarString` | 1 | 3 |
| 1 | `name` | `SoundDef` | `String32` | 1 | 4 |
| 2 | `params` | `Position` | `ObjectPos` | 3 | 5-7 |
| 3 | `stream` | `Simple` | `bool8` | 1 | 8 |
| 4 | `once` | `OneShot` | `bool8` | 1 | 9 |
| 5 | `loop` | `Relative to microphone` | `bool8` | 1 | 10 |

`sizeof(ConditionalSound) == 0x128`, pinned by the engine's own
`_Static_assert` at condsound.c:59.

The last row is a **naming divergence with behavioural consequences**; see §4.4.

### 3.2 `AmbientArea`

`AmbientArea_Init`, HOOK 0x44C130, `src/aiaction/ambientarea.c:62-81`:

| # | Struct field | Authored name | Type | Args | Authored token |
|---|---|---|---|---|---|
| 0 | `pos` | `Position` | `ObjectPos` | 3 | 3-5 |
| 1 | `matrix` | `Orientation` | `Real32x9` | **3** | 6-8 |
| 2 | `size` | `Size` | `Real64x3` | 3 | 9-11 |
| 3 | `minDelay` | `Min Delay` | `Real64` | 1 | 12 |
| 4 | `randDelay` | `Random wait` | `Real64` | 1 | 13 |
| 5 | `sounds` | `SoundDef` | `String256` | 1 | 14 |

`sizeof(AmbientArea) == 0x210`, pinned by `_Static_assert` at
ambientarea.c:45.

Note `Size` is a **half extent in metres**, not a full size: the containment test
multiplies it by ±4096 (ambientarea.c:142-143).

### 3.3 Why `Real32x9` takes three authored values

The type name is a lie. `kParamTypes` (`src/physobj/paramops.c:133-160`) maps
`Real32x9` and `Real64x3` to `Param_Three`, and `ObjectPos` is an alias of
`Real64x3` (paramops.c:183). `Param_Three` sets `*n = 3`
(paramops.c:398-402, HOOK 0x4EFB00); everything else is `Param_One`
(paramops.c:355-359, HOOK 0x4EFA10).

`Real32x9` is bound to a 3×3 matrix field, and the three authored values are
**Euler angles**, not matrix rows: `ParamReal32x9_Parse`
(paramops.c:423-435, HOOK 0x4EFBA0) reads three floats and calls
`Matrix_SetEuler`. That function (`src/math/matrix.c:99-110`, HOOK 0x4B38E0) is
`Rz * Ry * Rx`, built with `SetRotZ`, then `* SetRotY`, then `* SetRotX`
(matrix.c:112-164), row-major `m[i*3+j]`.

So the editor's `MatrixFromEuler` reproduces the engine's rotation order, and
`tests/test_sound_rules.cpp` pins the identity, a Z quarter-turn and an X
quarter-turn. The engine's `sin`/`cos` are x87 `fsin`/`fcos`
(`src/math/x87.h:19-29`); the host uses `std::sin`/`std::cos` on `long double`,
so last-bit differences are possible and the tests use a tolerance for the
cosine terms.

### 3.4 `round_int` is round-half-to-EVEN

Several rules call `round_int` (missile.h:111-117, ui.h:123-129), which is x87
`fistpll` under the current rounding mode — round to nearest, ties to even. It is
**not** a truncation, and the difference is observable:

* `round_int(0.5) == 0`, `round_int(1.5) == 2`, `round_int(2.5) == 2`
* `round_int(0.6) == 1`, `round_int(0.4) == 0`

The editor's `RoundHalfEven` matches, and the corpus tests pin the ties.

---

## 4. `ConditionalSound`

### 4.1 The run loop is a rising-edge gate

`ConditionalSound_Run`, HOOK 0x4250D0, `src/vehicle/condsound.c:121-136`:

```c
if (VarString_HasExpr(&cs->cond)) {
    cs->isLastRun = cs->isRun;
    cs->isRun = (char)round_int(VarString_EvalReal(&cs->cond, cs));
}
ConditionalSound_UpdatePlaying(cs);
if (cs->once && cs->played)
    return;
if (!cs->isRun)
    ConditionalSound_Stop(cs);
else if (!cs->isLastRun)
    ConditionalSound_Play(cs);
```

Three consequences the editor models exactly:

1. **Without an expression, `isRun` is never written.** `Create` leaves it 0
   (condsound.c:111), so a `ConditionalSound` with an empty `Condition` is
   stopped every tick and never plays, no matter what the caller passes. The
   editor's `TickConditionalSound` takes `has_condition` separately from
   `condition_value` precisely for this.
2. **The coercion is through a signed `char`.** `(char)round_int(...)` wraps:
   `255 → -1` (true), `256 → 0` (false), `257 → 1` (true). `ConditionHolds`
   reproduces this and the tests pin 255/256/257.
3. **`once` is checked after `UpdatePlaying` but before the stop/play decision.**
   A one-shot that has played is never stopped again, so a `OneShot` sound keeps
   playing through a false condition. This is an authoring trap, not a bug in the
   editor.

### 4.2 Play and Stop

`ConditionalSound_Play`, HOOK 0x425200, `src/vehicle/condsound.c:150-177`:
stops first, then

* if `stream` (authored `Simple`): hashes the definition name into the four-word
  `SoundHandle`, `SoundParams_Init`, `SoundHandle_Play`, and sets
  `played = isPlaying = 1`. **`cs->sound` is left NULL.**
* otherwise: `Sound_Play(cs, cs->name)`, bail if NULL, `TaskRef_Add`, then
  `if (cs->loop) Sound_SetGlobal(sound, 1); else Sound_SetPos(sound, cs->params);`

`ConditionalSound_Stop`, HOOK 0x4251C0, condsound.c:138-148, returns immediately
when `!cs->sound` — so **`Stop` is a no-op for a `Simple` emitter**, and
`isPlaying` is only cleared on the path that deletes a sound task.

`ConditionalSound_UpdatePlaying`, HOOK 0x4252E0, condsound.c:179-189, refreshes
`isPlaying` each frame and, for a stream, **re-requests it** — because a sound
without flag 1 lasts one frame unless it is played again
(`src/sound/soundsys.c:217-246`, and the comment above `SoundSys_Update` says so
verbatim).

Put together, a `Simple` `ConditionalSound` has a genuinely surprising lifetime:
`UpdatePlaying` runs *before* the condition test and `Stop` cannot touch it, so
**once a `Simple` emitter starts it outlives its own condition** and keeps being
re-requested until the streamed voice itself ends. The editor models that, and
the test says so in as many words. Whether the voice ends is a property of the
DirectSound sample, so it is an **input** to the module (`stream_alive`), not a
prediction.

### 4.3 `Reset`

`ConditionalSound_Reset`, HOOK 0x425560, condsound.c:257-263: `Stop`, then
`isRun = isLastRun = 0`. So a level reset makes the next true condition a rising
edge again. The editor's `ResetConditionalSound` matches.

### 4.4 The `Relative to microphone` divergence

The struct field is named `loop`, but it is authored as **"Relative to
microphone"** (condsound.c:92, LEVEL1/objects.qsc:78) and it is consumed as
`Sound_SetGlobal(sound, 1)` (condsound.c:171). `Sound_SetGlobal`
(`src/sound/soundsys.c:612-617`) is documented in the decompilation as
*"on: heard at any distance"* and its only reader is the
`(v->flags & 2) && !(v->flags & 4)` guard in `Voice_SetParams` — that is, bit
`0x4` **suppresses the distance fade entirely**.

So the flag means *non-positional*, not *looping*. Looping is a property of the
definition (`s->loop` → `SoundDefSound_Play` sets flag `0x10`,
sounddef.c:79). The editor names the field `relative_to_microphone` and
`docs`/tests both record that the engine's own name is misleading. Every retail
instance that sets it also authors `Position = 0,0,0`, which is consistent: the
global path never reads `params` (condsound.c:170-173).

`SoundDef_SetGlobal`'s body is declared (sounddef.c:14) and registered
(sounddef.c:53) but is **not present in the decompilation**, so the exact flag
write is inferred from the consumer, not read. That is the one rule here whose
mechanism is not fully cited, and it does not affect any editor output.

### 4.5 `nPlayTick`

`cs->nPlayTick` is initialised to `-1` (condsound.c:111) and exported to script
as `<name>.nPlayTick` (condsound.c:232-233, 252), but nothing in the decompilation
ever writes it. The editor does not model it.

---

## 5. `AmbientArea`

### 5.1 Constants

ambientarea.c:51-54:

```c
static const double kHalfSize = 4096.0;
static const double kNegHalfSize = -4096.0;
static const double kFadeStep = 0.05;
static const double kFps = 30.0;
```

### 5.2 The random delay

`random_delay`, ambientarea.c:120-123:

```c
return round_int((Random_Float(Game_GetRandom()) * a->randDelay + a->minDelay) * kFps);
```

`Random_Float` is `(Random_Next() & 0xFFFF) / 65536.0`, i.e. `[0, 1)`
(`src/math/random.c:22-26`). So the delay is in **frames**, from `minDelay`
seconds up to but not including `minDelay + randDelay` seconds, at 30 fps.
`AmbientDelayFrames` reproduces it.

**The delay is re-rolled on every silent frame.** `Run` takes the silent branch
(ambientarea.c:156-158) whenever the volume is exactly 0 or the sound task died,
and that branch assigns `a->delay = random_delay(a)` — then the tail of the same
call decrements it (ambientarea.c:176-177). The observable effect is that a
listener outside every box keeps drawing randoms forever, and the delay can only
count down while the listener is *inside* with a non-zero volume and no live
sound. The tests pin both the re-roll (899 after a 900 roll) and the countdown.

### 5.3 Containment is a strict, rotated, half-extent box

`Run`, HOOK 0x44C370, ambientarea.c:125-179. The listener is
`Game_Get() + 0x88` (the "ear"). The world delta is rotated by the **transpose**
of the authored matrix:

```c
lx = (m[6] * dz + m[3] * dy) + m[0] * dx;   /* double      */
ly = (m[7] * dz + m[4] * dy) + m[1] * dx;   /* double      */
lz = (m[8] * dz + m[5] * dy) + m[2] * dx;   /* long double */
```

then ambientarea.c:142-143:

```c
if (a->size.x * kNegHalfSize < lx && a->size.x * kHalfSize > lx &&
    a->size.y * kNegHalfSize < ly && a->size.y * kHalfSize > ly &&
    lz > a->size.z * kNegHalfSize && lz < a->size.z * kHalfSize)
```

All six comparisons are **strict**, so a listener exactly on a face is outside.
The box is therefore `2*size` metres per axis, centred on `Position`, oriented by
`Orientation`. `AmbientAreaContainsListener` is the faithful end-to-end path and
is what `TickAmbientArea` calls; the decomposed `AmbientAreaLocalPoint` +
`AmbientAreaContains` pair is exposed for callers that want the local
coordinates, and it narrows `lz` to `double` where the engine keeps `long double`
— a documented, one-instruction divergence that no authored size can reach.

### 5.4 The fade integrator

ambientarea.c:131-154. Each frame the volume moves by `±kFadeStep`:

```c
v = a->volume + kFadeStep;   /* inside, or the condition is true  */
v = a->volume - kFadeStep;   /* outside, or the condition is false */
a->volume = v;
if (!(v >= 0.0)) a->volume = 0;
else if (v > 1.0) a->volume = 1.0;
```

Note the clamps test `v`, the long double, not `a->volume`, and `!(v >= 0.0)`
also catches NaN. `Create` sets `volume = 0` (ambientarea.c:97), so an area fades
**in** when the listener arrives and **out** when it leaves; at 30 fps a full
0 to 1 ramp takes 20 frames (≈ 0.667 s).

### 5.5 The sound list

`AmbientArea_Changed`, HOOK 0x44C740, ambientarea.c:198-224, splits the
`SoundDef` string in place:

```c
if (c == ' ' || c == ';' || c == ',' || c == 0) {
    if (inWord) {
        crt_strncpy(a->names[a->count], start, p - start);
        a->names[a->count][p - start] = 0;
        inWord = 0;
        a->count++;
    }
} else { ... }
```

Three hazards, all real, all reproduced by `SplitAmbientSoundList` and reported
by `ValidateAmbientArea`:

* **`count` is unbounded.** The array is `names[8]` of `char[0x20]`
  (ambientarea.c:38), so a ninth name writes past the end of the struct.
* **The copy is not length-clamped.** `crt_strncpy(dst, src, p - start)` then
  `dst[p-start] = 0` writes a NUL one past the copied run, so a word of 32 bytes
  or more overruns a 0x20-byte slot.
* **The source is `sounds[0x20]`** — the split reads the same buffer it is
  parsing, and `a->count` grows past 8 with no check.

The retail corpus stays inside all of this: the longest list is 29 bytes, the
longest word 19, and the largest name count 5 across all 162 areas, so none of
these fire in shipping data. They fire on edited data, which is exactly why the
validator names them.

The pick is `round_int(Random_Float * count)` (ambientarea.c:164) — `round`, not
`floor`, so index `count` is reachable and the last name is under-selected. When
that happens `Sound_Play` is handed an empty name and returns NULL
(`src/sound/soundsys.c:552`, `!*name`), so the frame is simply silent.
`AmbientSoundIndexForRandom` reproduces the rounding and the tests pin the
out-of-range index.

### 5.6 Where the sound is placed

ambientarea.c:156-174:

```c
if (a->volume == 0.0 || (a->sound && !*a->sound)) {
    a->delay = random_delay(a);
    stop_sound(a);
} else if (a->sound) {
    if (inside) Sound_SetPos(*a->sound, ear);
    Sound_SetVolume(*a->sound, -1.0f, a->volume);
} else if (inside) {
    i = round_int(Random_Float(Game_GetRandom()) * a->count);
    if (!a->delay) {
        s = Sound_Play(a, a->names[i]);
        if (s) {
            Sound_SetPos(s, ear);
            a->sound = TaskRef_Add(s);
            Sound_SetVolume(*a->sound, -1.0f, a->volume);
        }
    }
}
if (a->delay) a->delay--;
```

The sound is positioned **on the listener** (`ear`), not on the emitter. The
consequence is important and is the reason a 1-metre definition like `dog_01` is
usable at all: the definition's `minDist`/`maxDist` never attenuate an
`AmbientArea` sound, because the distance is always zero. The **box** decides
whether it is heard; the **integrator** decides how loud.

### 5.7 The dead `cond` field

`Run`'s first branch (ambientarea.c:131-132) fades the volume out whenever
`VarString_HasExpr(&a->cond)` is true and the expression evaluates false. But
`cond` is **not in the ParamTable** (ambientarea.c:74-79), so no authored value
can ever reach it, and it is not among the 12 authored arguments of any of the
162 retail instances. `VarString_Bind` is called on it in `Changed`
(ambientarea.c:223) but `HasExpr` is false for an unbound string. The branch is
therefore **unreachable in retail**. The editor reproduces it because it is what
the code does, and documents it as dead; the test drives it with a forced
condition so the code path is still covered.

---

## 6. The audible radius belongs to the definition

### 6.1 `DefineSound`

`SoundDefSound_Write`, HOOK 0x4E8910, `src/define/sounddef.c:206-218`, emits the
13 arguments in order:

| # | Field | Meaning |
|---|---|---|
| 0 | `name` | the key `Sound_Play` looks up |
| 1 | `sample` | the WAV/sample name |
| 2-4 | `offset.x/y/z` | emitter offset |
| 5 | `volume` | base volume 0..1 |
| 6 | `minDist` | metres; inside this the volume is untouched |
| 7 | `maxDist` | metres; past this the sound is culled outright |
| 8-10 | `volumeIndex`, `pitchIndex`, `sampleIndex` | `-1` when unset |
| 11 | `unk124` | — |
| 12 | `loop` | `TRUE`/`FALSE` |

`ParseSoundDefinition` reads all 13 and the corpus test parses all 722 retail
records.

### 6.2 Two layers, and the first one wins

`game.c:441-442` loads the definitions in this order:

```c
SoundSys_LoadDefs(g, "LOCAL:common/sounds/");
SoundSys_LoadDefs(g, "MISSION:sounds/");
```

and `SoundDefSound_Find`, HOOK 0x4E8AA0, `src/define/sounddef.c:220-224`, only
accepts a match while `!a->found`:

```c
if (!a->found && strcmp(a->name, s->name) == 0)
    a->found = &s->task;
```

So the **first** definition of a name wins, which means COMMON shadows a mission
duplicate. This is observable in retail data: `machinery_01` is defined twice in
COMMON/SOUNDS/SOUNDS.QVM, as the 338th and the 360th `DefineSound` record in
file order (`volume 0.349999994`, `maxDist 25`, and `volume 0.5`, `maxDist 65`),
and it is the **25-metre** one that plays.
`SoundDefinitionRegistry` is first-wins for the same reason.

### 6.3 The falloff, and the cull

`Voice_SetParams`, HOOK 0x496E60, `src/transctx/voice.c:568-618`, is the only
place the emitter-independent falloff happens:

```c
l.minDist = p->minDist * kDistScale;   /* kDistScale = 4096.0, voice.c:79 */
l.maxDist = p->maxDist * kDistScale;
if ((v->flags & 2) && !(v->flags & 4)) {
    dist = Vec3d_Length(p->pos - gListenerPos);
    if (dist > l.maxDist) {
        l.volume = 0;
    } else if (dist >= l.minDist) {
        k = (1 - (dist - l.minDist) / (l.maxDist - l.minDist)) * l.volume;
        l.volume = k;
        if (!(k >= 0)) l.volume = 0;
        else if (l.volume > 1) l.volume = 1;
    }
    ...
}
memcpy(p, &l, 0x20);
```

Points the editor pins:

* Below `minDist` the volume is **left alone** — full, not faded in.
* Between `minDist` and `maxDist` the fade is **linear** in distance.
* Past `maxDist` the volume is a hard **0**, not a negative extrapolation.
* `minDist == maxDist` at exactly that distance divides by zero; `!(k >= 0)`
  turns the NaN into 0, so the sound is silenced rather than passed through.
* The scaled distances are written **back into the caller's params**
  (`memcpy(p, &l, 0x20)`), so the second application sees scaled values.
* Pan (voice.c:601-611) is `r / dist * (dist / (maxDist + minDist))`, where `r`
  is the listener's x axis dotted with the delta — the two distance factors
  cancel, so `pan = r / (maxDist + minDist)`, clamped to ±1, and exactly 0 at
  zero distance. `SoundPan` reproduces that and the tests pin the cancellation.

`SoundParams_InitBasic`, HOOK 0x4974E0, `voice.c:715-724`, is the default a sound
starts from: `minDist 0`, `volume 1.0`, `pitch 1.0`, `pan 0`, `maxDist 1.0`.

Separately, `SoundDefSound_Play`, HOOK 0x4E8520, `src/define/sounddef.c:61-81`,
hard-culls a positional definition before any of that:

```c
if (s->h.flags & 2) {
    Vec3d *lp = SoundSysMicrophone_GetPos(a->listener);
    double r = rd(s->range * kDistScale);
    if (!(r * r >= Vec3d_DistanceSq(lp, &s->pos)))
        return;
}
```

Compared as **squares**, with `r` narrowed through `volatile double`
(`rd`, define.h:92-97). The boundary is **inclusive**: at exactly `r` the sound
plays. The same function sets the loop flag:
`s->h.flags = (s->h.flags & ~0x10) | (s->loop ? 0x10 : 0)`.

---

## 7. The retail inventory

Measured from the shipped `OBJECTS.QVM` of all fourteen missions under
`MISSIONS/location0` (`IGI_GAME_PATH`), with the editor's own `QVM_Parse`:

| Level | `ConditionalSound` | `AmbientArea` |
|---|---|---|
| level1 | 21 | 21 |
| LEVEL2 | 25 | 6 |
| LEVEL3 | 33 | 18 |
| LEVEL4 | 18 | 7 |
| LEVEL5 | 18 | 8 |
| LEVEL6 | 19 | 17 |
| LEVEL7 | 31 | 9 |
| LEVEL8 | 26 | 9 |
| LEVEL9 | 16 | 13 |
| LEVEL10 | 53 | 15 |
| LEVEL11 | 19 | 8 |
| LEVEL12 | 46 | 5 |
| LEVEL13 | 16 | 11 |
| LEVEL14 | 43 | 15 |
| **total** | **384** | **162** |

For level1, LEVEL2 and LEVEL4 the same counts come out of the shipped
`objects.qsc`, and every authored argument matches value-for-value. That
cross-check is what makes the bytecode walk trustworthy rather than merely
self-consistent.

Corpus invariants the tests assert:

* 0 `ConditionalSound` instances author an empty `SoundDef`.
* Every `AmbientArea` has positive half extents on all three axes (range 1..10000
  metres) and non-negative delays.
* At most 5 names per area, longest word 19 bytes, longest list 29 bytes — inside
  every one of the engine's unchecked limits.

### 7.1 Dangling sound references (real retail defects)

Of the 644 authored references to a sound definition — 384 from
`ConditionalSound` and 260 names across the 162 `AmbientArea` lists — **630
resolve and 14 do not**. Every one of the 14 is a defect in the retail data, and `Sound_Play`
returns NULL for each (soundsys.c:552), so the emitter is silent:

| Level | Emitter | Reference | What COMMON actually defines |
|---|---|---|---|
| LEVEL3 | `AmbientArea` `dogs` | `dogs_01`, `dogs_02`, `dogs_03` | `dog_01`, `dog_02`, `dog_03` |
| LEVEL6 | `ConditionalSound` | `detected_hear` | nothing similar |
| LEVEL8 | `ConditionalSound` | `gondola_fx_01`, `gondola_fx_06` | nothing similar |
| LEVEL10 | `ConditionalSound` | `anya_test_01`, `m10o_heliblfd` | nothing similar |
| LEVEL10 | `AmbientArea` `hammer & car` | `hammer_1`, `car_1` | `hammer_01`, `car_01` |
| LEVEL11 | `AmbientArea` `wind fx on roof 1` | `wind_fx_1`, `wind_fx_2` | `wind_fx_01`, `wind_fx_02` |
| LEVEL12 | `ConditionalSound` ×2 | `helicopter_cut` | nothing similar |

The pattern is a dropped zero (`hammer_1` for `hammer_01`), a plural
(`dogs_01` for `dog_01`) or a definition that was never written. The editor's
validator reports them as `UnresolvedSoundDef`, and the test asserts the exact
list — so a future change to the resolver cannot silently "fix" the data by
fuzzy-matching, and a future corpus cannot silently lose a reference.

### 7.2 Degenerate definitions

Seven of the 722 `DefineSound` records are unusable, and the test pins them:

* `COMMON/SOUNDS/SOUNDS.QVM` — `alarmsystem_destroyed` has an **empty sample**.
* `COMMON/SOUNDS/SOUNDS.QVM` — three records have an **empty name** with real
  samples (`RADARWIND_1`, `RADARWIND_1`, `RADARENGINE_1`), `volume 1`,
  `minDist 0`, `maxDist 10`/`20`, `loop TRUE`. LEVEL8 ships the same three
  verbatim.

An empty-name record is unreachable twice over: `Sound_Play` rejects an empty
lookup name, and `SoundDefSound_Find`'s `strcmp` would only match another empty
name. These look like generated positional emitters that lost their name.

---

## 8. The module

`source/runtime/sound_emitter.h` / `.cpp`, in `namespace igi`:

| Area | API |
|---|---|
| Schema | `SoundParamSpec`, `ConditionalSoundParamSpecs`, `AmbientAreaParamSpecs`, `SoundParamTypeName`, `SoundParamScriptArgs`, `ConditionalSoundScriptArgs`, `AmbientAreaScriptArgs` |
| Parsing | `ParseConditionalSound`, `ParseAmbientArea` (both take the full token vector with `id, type, name` at 0-2) |
| Definitions | `SoundDefinition`, `ParseSoundDefinition`, `SoundDefinitionRegistry` (first-wins) |
| Math | `MatrixFromEuler`, `RoundHalfEven`, `ConditionHolds`, `SoundWithinAudibleRadius`, `SoundAttenuatedVolume`, `SoundDistanceFade`, `SoundPan` |
| Geometry | `AmbientAreaLocalPoint`, `AmbientAreaContains`, `AmbientAreaContainsListener`, `SplitAmbientSoundList`, `AmbientSoundIndexForRandom`, `AmbientDelayFrames` |
| State | `ConditionalSoundState`/`TickConditionalSound`/`ResetConditionalSound`, `AmbientAreaState`/`TickAmbientArea` |
| Validation | `SoundIssue`, `SoundIssueKind`, `SoundIssueKindName`, `ValidateConditionalSound`, `ValidateAmbientArea` |

Design notes:

* **The tick functions return a decision, not an action.** `ConditionalSoundTick`
  carries `start_sound`, `stop_sound`, `request_stream`; `AmbientAreaTick` carries
  `inside`, `volume`, `start_sound`, `stop_sound`, `update_playing`,
  `sound_index`. The host performs the audio work. This is what makes the logic
  testable without a device.
* **Facts the engine learns from DirectSound are inputs.** `stream_alive`
  (`SoundHandle_IsPlaying`) and `sound_alive` (`*cs->sound != NULL`) are supplied
  by the caller rather than predicted.
* **`ValidateConditionalSound` / `ValidateAmbientArea` name the engine's own
  unchecked limits** (`NameTooLong` at the `Param_String32` boundary,
  `TooManySounds` past `names[8]`, `SoundWordTooLong` past 0x20 bytes,
  `SoundListTooLong` past `sounds[0x20]` — and again past `sounds[0x100]`, where
  `Param_String256` also truncates — `NoSounds`, `NonPositiveSize`,
  `NegativeDelay`, `EmptyCondition`, `EmptySoundDef`, `UnresolvedSoundDef`) so
  the editor can warn before a level is saved into undefined behaviour.

### 8.1 Tests

`tests/test_sound_rules.cpp`, 34 tests in two suites, all host-portable:

* `SoundRulesTest` (28) — schema, `round_int` ties, `Rz*Ry*Rx`, the list split,
  parsing, the linear fade and its edge cases (`minDist == maxDist`, the
  inclusive cull), the pan cancellation, strict box containment and its
  inverse-rotation path, the pick's out-of-range index, the delay in frames, the
  fade integrator's 20-frame ramp, the delay re-roll, the condition branch, the
  rising edge, the one-shot gate, the stream lifetime, the global-flag naming,
  definition parsing, first-wins and every validator issue kind.
* `SoundRulesCorpus` (6) — the 14-level inventory, the `.qsc`↔`.QVM` cross-check
  for level1/LEVEL2/LEVEL4, level1's authored geometry, the level1/2/4
  conditions, the real audible radii from the definitions, and every one of the
  722 definitions with the seven anomalies pinned. All six `GTEST_SKIP` when
  `IGI_GAME_PATH` is absent or unpopulated.

---

## 9. Verified vs unverified

**Verified (compiled and tested on the host):**

* The parameter schema, argument counts and struct sizes, against the engine's
  own `ParamTable_Add` order and `_Static_assert`s.
* The parsing of all 384 + 162 retail instances, cross-checked against the three
  shipped `.qsc` files argument-by-argument.
* The falloff, cull, pan, containment, fade and delay arithmetic, against
  hand-computed values and real authored numbers.
* `RoundHalfEven`, `ConditionHasExpr`-style condition coercion, and the
  first-wins definition lookup.

**Unverified:**

* **No audio is produced or inspected.** There is no playback path here, so
  "the emitter fires" is verified and "the emitter is audible" is not.
* `SoundDef_SetGlobal`'s flag write (declared sounddef.c:14, registered :53, body
  absent) — inferred from its only consumer, `voice.c:586`.
* `SoundHandle_IsPlaying` for a `Simple` emitter is an **input**; the editor
  cannot know whether the streamed voice is still running. The *consequence*
  (a `Simple` emitter outliving its condition) is derived from cited code, but the
  lifetime itself is not measured.
* The `lz` axis in `AmbientAreaLocalPoint` is narrowed to `double` where the
  engine keeps `long double` (ambientarea.c:140). `AmbientAreaContainsListener`,
  which the tick uses, keeps it.
* x87 `fsin`/`fcos` are approximated by `std::sin`/`std::cos` on `long double`,
  so the rotation matrix can differ in the last bits. Tests use a tolerance for
  the cosine terms.
* `nPlayTick` is exported to script but never written by any decompiled code, and
  is not modelled.
* The seven degenerate definitions and the fourteen dangling references are
  **reported**, not repaired. Fixing retail data is an authoring decision.

---

## 10. Files

| File | Role |
|---|---|
| `source/runtime/sound_emitter.h` | the schema, the parsed forms, the rules, the state machines |
| `source/runtime/sound_emitter.cpp` | their implementation |
| `tests/test_sound_rules.cpp` | 34 host tests, including the retail corpus |
| `CMakeLists.txt` | two additive lines: the test file and `source/runtime/sound_emitter.cpp` |

Nothing outside those files was changed. No renderer, level, or common code was
touched, and no audio dependency was added.

---

# Part B — the retail container and sample format

Part A models the *authored* emitters. This part is the other half the issue asks
for: the bytes the emitters ultimately name, and the format they are stored in.
It was added after the section above was written, and it changes one claim in it:
there IS an audio pipeline in the editor now — not a device, but the engine's own
container and sample decoding, and a conversion to a file a plain host API can
play. `source/runtime/igi_sound_bank.{h,cpp}` and `tests/test_igi_sound_bank.cpp`.

## B1. There is no SoundBank and no OpenAL

The issue's title says "retail bank playback (open-igi SoundSystem/OpenAL port)".
Read against the engine, two of those three nouns do not exist:

* **No OpenAL.** `Sound_Init` (HOOK 0x495280, `src/d3d/sound.c:89-93`) is
  `CD_Open(); Mixer_Init(); Sound_InitDirectSound(); Stream_Init();` — DirectSound
  plus the winmm mixer plus MCI for CD audio. The decompiled tree has no OpenAL
  symbol.
* **No SoundBank.** The word does not appear. Definitions are a *script*:
  `SoundSys_LoadDefs` (HOOK 0x4E6550, `src/sound/soundsys.c:306-319`) runs
  `<dir>sounds.qsc`, whose `DefineSound` calls become the task tree Part A §6.1
  parses. Samples are separate files.
* **No ILSF file type either.** `ILSF` is a *member header*: the first word of the
  bytes inside a pack (see B2). It never appears as a file extension, and a grep
  for `ILSF` across IGI-RE finds nothing because the engine compares the numeric
  word `0x46534C49` (`src/d3d/sound.c:548`) rather than the text.

## B2. The pack: `ILFF` form `IRES`

`Sound_LoadResource` (HOOK 0x4E6750, `soundsys.c:418-429`) loads a `.res`, walks
its members with `Resource_ForEach`, and hands each member's bytes to
`Sound_Load`. `Resource_Add` (HOOK 0x4B5F80, `src/res/resource.c:281-318`) is the
reader, and it recognises two spellings:

* an IFF-like form whose id is `ILFF` and whose form type is `IRES`
  (`resource.c:287`), read by `Resource_AddChunks` (HOOK 0x4B60F0,
  `resource.c:328-375`) as `NAME`/`BODY` pairs;
* a `"RESOURCE"` record list (`resource.c:293-313`), count at `+8`, records from
  `+0x20`.

The chunk layer is `src/res/chunk.c`: `Chunk_Next` is `c + c->next` (HOOK
0x4B7470, `:9-15`), `Chunk_FirstChild` is `(align + 0x13) & ~(align - 1)` (HOOK
0x4B7490, `:17-31`) and `Chunk_Data` is
`parent + ((align + (c - parent) + 0xF) & ~(align - 1))` (HOOK 0x4B74D0,
`:34-49`). All three are reproduced in `igi_sound_bank.cpp` and cross-checked
against the shipped bytes.

Both shipped sound packs are the `ILFF`/`IRES` spelling:

| Archive | Size | Members | `BODY` | `PATH` |
|---|---|---|---|---|
| `COMMON/SOUNDS/SOUNDS.RES` | 15,081,765 | 334 | 333 | 1 |
| `menusystem/SOUND/SOUNDS.RES` | 1,766,902 | 14 | 13 | 1 |

The `PATH` member is `resource.c:359-361` — a `;`-separated name list that
`Resource_ForEach` (HOOK 0x4B5D80, `resource.c:185-217`) walks. It is named after
the directory (`LOCAL:common/sounds`) and is **not a sample**: `Sound_Load` is
never called with it. It is what points the loader at the 333 `BODY` members, and
`SplitPathList` reproduces it.

## B3. The member: a 20-byte header, then the payload

`Sound_Load` (HOOK 0x495F70, `d3d/sound.c:535-621`) is the whole format
definition. Field offsets come from `Sound` (`transctxtypes.h:46-60`) and the
`memcpy(&s->magic, res, 0x14)` at `:563`:

| Offset | Type | Field | Cited |
|---|---|---|---|
| 0x00 | int | magic `ILSF` | `d3d/sound.c:548` rejects any other first word |
| 0x04 | short | `format` | `transctxtypes.h:51` |
| 0x06 | short | `bits` | `transctxtypes.h:52` |
| 0x08 | short | `channels` | `transctxtypes.h:53` |
| 0x0A | short | **`unk1A` — purpose NOT established** | `transctxtypes.h:54` |
| 0x0C | int | `rate` (Hz) | `transctxtypes.h:55` |
| 0x10 | int | `samples` — **frames**, not interleaved values | `d3d/sound.c:579`, `:602` |
| 0x14 | | payload | `voice.c:372` and `:453` both `File_Seek(v->file, 0x14, 0)` |

`samples` counts frames because `d3d/sound.c:579` allocates
`channels * bits * samples / 8` for the decoded buffer and `:602` sizes the
DirectSound buffer `samples * blockAlign`; a 44.1 kHz stereo file with
`samples = 587776` is 587,776 frames, i.e. 13.3 s.

**`unk1A` is left uninterpreted.** The engine copies it and never reads it: every
read of the struct is `format`, `bits`, `channels`, `rate` or `samples`. It is 0
in every loose file and `0x1002` in every pack member — which looks like a
per-container property rather than a per-sample one, but that is an observation
about the data, not a rule read out of the engine, so the field is reported raw.

## B4. The four formats, and which payload sizes are established

`d3d/sound.c:564-588` switches on `format`:

| Code | Meaning | Where decoded | Payload size |
|---|---|---|---|
| 0 | raw PCM, **streamed** | `voice.c:443` `Voice_ReadPcm` | `samples*channels*bits/8` |
| 1 | raw PCM, whole buffer | `d3d/sound.c:576` | `samples*channels*bits/8` |
| 2 | 4-bit IMA ADPCM, streamed | `voice.c:440-441` `Voice_ReadAdpcm` | `ceil(samples*channels/2)` |
| 3 | ADPCM, decoded at load | `d3d/sound.c:578-587` | **not established** |

Two of these are worth stating carefully.

**Format 0 is not 4-bit.** `Voice_Create` treats 0 and 2 identically when it sets
up the stream buffer (`voice.c:349-387`) and `Voice_IsPlaying` does too
(`voice.c:279`), but `Voice_Fill` branches on the format and sends 0 down
`Voice_ReadPcm` (`voice.c:440-443`). Its payload is therefore raw PCM, exactly
like format 1. The header comment in `transctxtypes.h:51` — "0 / 2 streamed
(2 = ADPCM)" — is right and the `igi_sound_bank.h` enum names it
`StreamedPcm`. All 6 shipped format-0 files have
`payload == samples * channels * bits / 8`, which confirms it: a 4-bit reading
would predict a quarter of the bytes actually present.

**Format 2 is `ceil(samples * channels / 2)`.** `voice.c:480` computes
`bytes = ch * frames / 2` and `voice.c:482-487` adds one more byte when the frame
count or the read position lands mid-byte, i.e. ceiling division. Verified
against all 669 shipped format-2 files: 333 are mono with an odd frame count
(`ceil` = `(samples+1)/2`), 281 are mono with an even count (`samples/2`), and 55
are stereo (`samples`). No file has padding beyond that.

**Format 3's payload length is NOT established, and is not guessed.** The loader
never derives a byte count from the header for it: `d3d/sound.c:586` hands
`res + 5` straight to `Adpcm_Decode`. The 8 shipped format-3 members are
consistent with `ceil(samples*channels/2)` plus up to 3 bytes of pad, but that is
a measurement of one archive, not a rule read out of the engine.
`PayloadSizeEstablished` returns false, `TranscodeToRiffWav` refuses with a named
reason, and the resolver returns no path rather than caching something it cannot
vouch for. This is the one place the module deliberately does less than it could.

## B5. The decoder is IMA, but not the textbook predictor

`Adpcm_Decode` (HOOK 0x496280, `voice.c:98-148`) is ported verbatim, including
`Adpcm_Init` (HOOK 0x496420, `:150-158`). The tables at 0x543920 and 0x543930
(`voice.c:739-740`) are the canonical IMA index table
`{-1,-1,-1,-1,2,4,6,8,...}` and the 89 canonical step values 7..32767.

Three properties are load-bearing and each is pinned by a test:

1. **The high nibble comes first** (`voice.c:118`).
2. **`odd` is toggled on every sample, not every byte** (`voice.c:120`), and a
   frame ending mid-byte leaves the low nibble for the next frame
   (`voice.c:110-116`, `:492-495`). One channel consumes two samples per byte;
   two channels consume one byte per frame, one nibble each.
3. **`step[ch]` is refreshed at the END of each sample** (`voice.c:139`), so a
   sample's difference uses the step the *previous* sample's index produced.
   Index update and step refresh are one sample apart.

The difference formula is the engine's and is **not** the standard IMA one
(`voice.c:126-133`):

```c
s = step[ch];
diff = s >> 3;
if (nib & 4) diff += s;        /* the FULL step, where IMA uses s >> 2 */
if (nib & 2) diff += s >> 1;
if (nib & 1) diff += s >> 2;
```

At the initial step 7 the sixteen nibbles therefore give magnitudes
`0,1,3,4,7,8,10,11,0,1,3,4,7,8,10,11`. A nibble of 4 moves by `1.125 * step`
where textbook IMA moves by `0.25 * step`. `SoundBankAdpcmTest` pins the whole
table so the port cannot be "corrected" to the standard formula — the retail
samples decode through the engine's arithmetic, not the textbook one.

## B6. What the editor does with it now

`source/runtime/igi_sound_bank.h` / `.cpp`, in `namespace igi::soundbank`:

| Area | API |
|---|---|
| Header | `SampleHeader`, `ParseSampleHeader`, `kHeaderSize`, `SampleFormat` |
| Sizes | `PayloadSizeForHeader`, `PayloadSizeEstablished`, `DecodedPcmSize` |
| Codec | `DecodeImaAdpcm` |
| Pack | `ResourceKind`, `PackEntry`, `PackScanResult`, `ScanPack`, `ScanPackFile`, `SplitPathList`, `ReadEntryPayload` |
| Output | `TranscodeToRiffWav` |

`AudioAssetResolver` (`source/runtime/audio_asset_resolver.cpp`) now converts a
packed `ILSF` member to RIFF/WAVE before caching it. Before this change it wrote
the pack's raw bytes to `<cache>/audio/<archive>/<name>.wav`, so **every packed
sound resolved to a file neither `PlaySoundA` nor MCI's `waveaudio` device can
play** — the member's first four bytes were `ILSF`, not `RIFF`. That is a real
defect the new corpus test would have caught, and the fix is what makes the
issue's "bank playback" reachable at all. The cache directory name carries a `v2`
marker so a cache written by the old layout is never reused.

The conversion is format work and is oracle-backed. **The output device is not.**
Nothing here proves a sound is audible: this host has no DirectSound and cannot
run the GUI (it needs GL 4.1; the host offers 2.1).

## B7. Tests and the corpus

`tests/test_igi_sound_bank.cpp`, 30 tests in 6 suites, all host-portable:

* `SoundBankHeaderTest` (4) — the magic, a short buffer, impossible field
  combinations, and every field at the offset the engine uses.
* `SoundBankPayloadTest` (3) — the format-1 size, the format-0/2 ceiling rule
  (even/odd mono and stereo), and format 3 reported as unestablished.
* `SoundBankAdpcmTest` (7) — high-nibble-first, the sixteen-nibble difference
  table, the step-table walk, the index clamp, one byte per stereo frame, frames
  versus samples, and a truncated stream decoding to silence.
* `SoundBankPackTest` (4) — the `NAME`/`BODY` walk, a rejected non-archive, a
  truncated archive, and the `"RESOURCE"` spelling.
* `SoundBankTranscodeTest` (4) — the RIFF header fields against the PCM shape the
  engine asks DirectSound for, ADPCM decoded before writing, and the two refusal
  paths.
* `SoundBankCorpusTest` (8) — the shipped archives. It pins 334 members /
  333 `BODY` / 1 `PATH`; that all 333 samples are format 1, 16-bit, 331 mono and
  2 stereo; that **every** payload equals `samples*channels*bits/8` with no
  padding; that all 333 transcode to a well-formed RIFF/WAVE totalling
  **15,039,330** bytes of PCM; that the `PATH` list equals the `BODY` names in
  order; the first member's exact shape (`wire_drag_1.wav`, 26,016 frames,
  22,050 Hz, 52,052-byte `BODY`); and that the menu pack is 5 PCM + 8 format-3
  with the latter's layout reported unestablished.

`tests/test_audio_asset_resolver.cpp` gained three tests: a packed `ILSF` member
becomes RIFF/WAVE, a packed format-2 member is ADPCM-decoded on the way out, and
a format-3 member yields no path. Its existing "extracts from packed archive"
test now passes a real RIFF member through unchanged rather than four arbitrary
bytes.

## B8. Verified vs unverified (Part B)

**Verified (compiled and run on the host against the shipped data):**

* The pack walk, against all 347 members of the two shipped archives, including
  the `PATH` member.
* The header layout, against every field of all 333 + 13 samples.
* The payload arithmetic for formats 0, 1 and 2, against all 675 loose `ILSF`
  files and all 346 pack members.
* The ADPCM decoder, against hand-computed vectors and a literal transcription of
  `voice.c:98-148`.
* That the resolver now hands back a RIFF/WAVE for a packed member, and no path
  when it cannot.

**Unverified:**

* **No audio is produced or heard.** The transcoded file is a well-formed
  RIFF/WAVE with the engine's PCM shape; that a device plays it is not tested,
  and this host cannot test it.
* Format 3's payload layout, `unk1A`'s purpose, and the `CSTR` member spelling —
  all reported as unestablished rather than guessed.
* The `"RESOURCE"` pack spelling is implemented and unit-tested against a
  synthetic archive; **no shipped sound archive uses it**, so it is unexercised
  against retail data.
* Nothing here touches the authored-emitter logic of Part A, and no caller yet
  drives `TickAmbientArea` or `TickConditionalSound` into `AudioSystem`: the
  emitter decision layer and the sample layer are both present and tested, but
  the wire between them is not run by any GUI code path on this host.

