# Animation playback and skinning (#73)

Status: implemented in `source/runtime/anim_rules.{h,cpp}`; tested in
`tests/test_anim_rules.cpp` (72 tests, all green on the host lane with the retail
corpus present — §14). Every constant, offset, argument count and line number
below is cited to IGI-RE (`/Users/haseeb-mir/Documents/Code/IGI-RE`), the faithful
C decompilation of retail `IGI.exe`. No web source, no other port and no
open-igi code is used as authority. **Nothing here is a rendered or in-game
result**: the editor's GUI cannot run on the development host (it needs OpenGL
4.1, the host offers 2.1), so no pose, no skinned frame and no played animation
was ever drawn. The evidence is the unit and corpus tests in §14.

---

## 0. "Skinning" is not an IGI-RE symbol

The issue title uses an open-igi class name. **IGI-RE has no symbol named
`skinning`.** A scan of `symbols/names.tsv` (7068 lines) finds no such entry, and
no `Skinning*`, `Skin_*` or `*Skinning*` function. The two things the word could
mean in retail both exist under different names, and both are covered here:

| What the issue means | What IGI-RE actually has | Ported? |
|---|---|---|
| Choosing and playing an authored animation on a placed object or human | `GenericTBA_GetAnim` 0x4485D0, `Human_PlayAnim` 0x489EA0, `Anim_Play` 0x4D61D0, `Soldier_AnimEnter` 0x45C750 | Yes — §2–§9 |
| Deforming a mesh by its bone matrices | `BoneMesh_Skin` 0x49B700 (`src/dxinit/d3drender.c:226-313`) | Yes, the maths — §10 |

`BoneMesh_Skin` is the nearest retail analogue of "skinning", and it is not a
class: it is a single free function that walks the mesh's vertex array and writes
transformed positions, normals and UVs into the output. Its layout inputs are
`SkinVertex` (`d3drender.c:37-44`, sizeof 0x28) and `NormalVertex`
(`d3drender.c:46-50`, sizeof 0x20).

The editor already has a skinned-vertex path of its own
(`source/animation.cpp`, `SkinSkinnedVertices`, exercised by
`tests/test_animation.cpp:135-157` `AccumulatesExtraInfluencesIntoTheirBaseVertex`).
That path is **not** reimplemented here; §10 ports the engine's arithmetic so the
two can be compared, and §11 explains why both exist.

**Scope.** This port covers what a level/object editor needs: the animation table
lookup, the frame/tick model, the per-frame playhead rule, looping, the authored
parameters that select an animation, and the vertex transform. It does **not**
port the AI action system (`AI_PLAY_ANIMATION`, `gAIChannelMap`,
`HumanAI_SetChannel`), the combat animation-selection switch inside
`HumanPlayer_SetWeaponAnim` 0x4137E0, or the network path.
`AIAction_PlayAnimation` 0x44D6E0 (`src/aiaction/aiaction.c:516-524`) is cited
only for its data flow: it takes an animation id and a priority from script and
hands them to the human's animation layer.

---

## 1. Two different animation models, and which one this is

The editor and the engine disagree about what an animation *is*, and conflating
them is the main way to get this wrong.

| | Engine (retail) | Editor |
|---|---|---|
| Storage | `ANIMS/*.IFF` — an IFF/BOBJ container | BEF text produced by `igi1conv` |
| Identity | an **integer id** into a per-skeleton table | a named `AnimationClip` |
| Time | float **ticks**, 4800 per second | seconds/frames |
| Lookup | `gAnimSets[skel].byId[id]` | name → clip map |
| Instance | `AnimInst` inside a `Bone` | `AnimPlayback` |

So the engine's animation set is *per skeleton index*, selected by the authored
"Bone Heirachy" value, and an animation is *an integer*. There is no name lookup
anywhere in the runtime path. The authored values a level carries are therefore
ids, and an id is only meaningful together with the skeleton it belongs to —
which is exactly the trap in §3 and §12.

---

## 2. The animation table

`BoneObj_Load` 0x4D67A0 is `src/boneobj/boneobj.c:1543-1636`. It walks one BOAN
form per animation, in file order, and writes two arrays per skeleton
(`boneobj.c:1583-1597`):

```c
if (!set->count) return;
set->descs = Mem_Alloc(total * 0x14, 4);
set->byId  = Mem_Alloc(total * 4, 4);
memset(set->byId, 0, total * 4);
for (k = anims; k > 0; k--) {
    ...
    id = ah[2];
    memcpy(&d->length, &ah[0], 4);
    d->flags = ah[1];
    set->byId[id] = d;
}
```

Two words come out of BALH: `anims` (the count of BOAN forms actually present)
and `total` (the **capacity**). `set->count` is the capacity, and only `anims` of
those slots are ever written. In the shipped data the two differ a lot — skeleton
1 has 212 slots and 95 clips — so an id can be **inside the capacity and still
NULL**. `BuildAnimSetTable` (`anim_rules.h:228`, `anim_rules.cpp:68-114`)
reproduces that split and reports it as `kAnimSlotEmpty`.

The per-clip fields are `AnimDesc { float length; uint32_t flags; PosTrack *pos;
RotTrack *rot; EventTrack *events; }` (`src/boneobj/boneobjtypes.h:44-50`). The
only flag bit the playback path reads is `ANIM_LOOPS 0x80000000u`
(`boneobj.h:65`) — and note that this is a **desc** flag, unrelated to the
**instance** flags in §5.

Position keys are `PosKey { float pos[3]; float time; float out[3]; float in[3]; }`
— 40 bytes (`boneobjtypes.h:32-36`) — and `BoneObj_Load` allocates
`keys * sizeof(PosKey)` **only when `keys > 1`**; a single-key track gets
`keys * 16`, i.e. position and time with no tangents (`boneobj.c:1606`). The port
mirrors that: `PosKey` is the full 40-byte record, and the corpus decoder in the
test applies the same 16-vs-40 stride rule.

`BoneObj_NewAnimSource` 0x4D47D0 (`boneobj.c:775-786`) is the lookup:

```c
AnimDesc *desc = gAnimSets[skel].byId[id];
```

No null check, no bounds check. `BoneObj_InitAnimInst` 0x4D4F60 then dereferences
`desc->pos` (`boneobj.c:1014`, `:1018`). A missing id is therefore an access
violation, not a fallback. `InitAnimInst` (`anim_rules.cpp:198-244`) returns
`false` instead and the header says why.

---

## 3. Lookup, and the `Anim_Exists` hole

`Anim_Exists` 0x4D74F0 (`src/haze/anim.c:244-255`) is the only guard the game
puts in front of a lookup:

```c
if (!obj || !obj->bones || bone < 0 || bone >= gSkeletons[obj->skel].count)
    return 0;
if (!set->count || id >= set->count)
    return 0;
return set->byId[id] != NULL;
```

Three things follow, all reproduced:

1. **The bone index is checked first**, against the skeleton's bone count. A
   skeleton with no hierarchy loaded has `count == 0`, so every bone index fails.
   `BoneObj_Create` 0x4D2D60 reacts to that with `Error_Show("Failed to create
   boneobj of type %d, no hierarchy loaded", type)` followed by an infinite loop
   (`src/lod/boneobj.c:111-116`) — it does not return.
2. **`id >= set->count` compares against the capacity**, not the clip count. This
   is the check that rejects LEVEL2's authored animation 245 on a soldier (§12).
3. **There is no `id < 0` test.** A negative id passes both halves of the guard
   and reaches `byId[-1]`, reading the four bytes in front of the allocation. The
   port reports `kAnimIdNegative` and reads nothing. This is not hypothetical:
   **`-1` is the engine's own "no animation authored" sentinel** — `GenericTBA_Create`
   writes `t->anim = -1` (`src/glass/tba.c:106`) and `Soldier_CreateHandler` writes
   `s->s.idleAnim = -1` (`src/soldier/soldier.c:746`) — so the value is one
   authoring mistake away from the hole. Every shipped record has a non-negative
   id (§12), so the hole is latent, not exercised.

`ResolveAnim` (`anim_rules.cpp:116-163`) returns one of six verdicts
(`anim_rules.h:248-266`) so a caller can tell "not authored" from "not loaded"
from "past the capacity" — a distinction the engine itself cannot make.

---

## 4. Ticks and frames

`kTicksPerSecond = 4800.0f` (`src/boneobj/anim.c:9`, "Times are in ticks, 4800 a
second"). The frame rate is a global set once:

- `BoneObj_Init` 0x4D2C30 sets `gTicksPerFrame = 0` (`src/lod/boneobj.c:71`);
- `BoneObj_SetFrameRate` 0x4D2D30 computes `gTicksPerFrame = 4800 / fps`
  (`lod/boneobj.c:86-90`);
- `game.c:449` calls it with `30.0f`.

So at retail `gTicksPerFrame` is exactly **160.0f**, and it is **0.0f** until
`game.c:449` runs. That zero is load-bearing and is reproduced in §7.

Two independent frame constants exist for authored durations, both 30:
`FPS 30.0f` in `src/humanstate/humanstate.c:91` and `kFps = 30.0f` in
`src/soldier/soldier.c:145`. `AIAction`'s `kFpsLocal = 30.0` is a `double`
(`src/aiaction/aiaction.c:159`) and is out of scope.

`kSecondsPerTick` (`src/haze/anim.c:12`) is the float `0x1.b4e81cp-13f` =
0.00020833333837799728, which is the nearest float to 1/4800 but **not** equal to
it. `Anim_GetLengthSeconds` 0x4D74D0 multiplies by it
(`haze/anim.c:238-242`), and `Anim_GetLengthTicks` 0x4D7040 goes the other way
through `fistpll` (`haze/anim.c:110-115`) — a **round**, not a truncation. Both
are ported (`anim_rules.cpp:165-182`) and both are asserted in §14.

---

## 5. The playhead: one addition, one comparison, one subtraction

`BoneObj_AnimEventsCB` 0x4D4B60 (`src/boneobj/boneobj.c:873-941`) is the whole
playback rule, once per frame:

```c
inst->flags &= ~BONE_EVENTS_DONE;
bone->flags &= ~BONE_EVENTS_DONE;
old = inst->time;
now = inst->speed + inst->time;
inst->time = now;
...
if ((!(inst->time < d->length) && !(inst->flags & ANIM_REVERSE)) ||
    (!(inst->time > 0.0f)   &&  (inst->flags & ANIM_REVERSE))) {
    inst->flags |= BONE_EVENTS_DONE;
    if (inst->flags & ANIM_LOOP) {
        if (reverse) { inst->time = d->length + inst->time; ... }
        else         { inst->time = inst->time - d->length; ... }
        /* root-motion fixup */
    } else {
        if (reverse) { inst->time = 0.0f; ... }
        else         { inst->time = d->length; }
    }
}
inst->flags &= ~ANIM_FIRST;
```

Every quirk here is deliberate and is reproduced:

- **The end test is `>=`.** `!(time < length)` is true when `time == length`, so
  a tick landing exactly on the clip length counts as past the end. The reverse
  test `!(time > 0.0f)` treats `time == 0` the same way.
- **NaN and `+inf` are both "past the end".** `!(NaN < length)` is true, and so
  is `!(inf < length)`. Neither comparison can be satisfied by a non-finite
  playhead, so both take the end branch instead of running forever.
- **The wrap is a single subtraction/addition, not a `while` loop.** A speed
  larger than the clip length leaves the playhead past the end after one wrap.
  `AnimAdvanceTest.LoopWrapIsNotAWhileLoop` pins this.
- **A one-shot clamps and keeps running.** It sets `time = length` (forward) or
  `0` (reverse) and raises `BONE_EVENTS_DONE`; it does not stop the instance, and
  the next frame adds `speed` again and clamps again.
- **`BONE_EVENTS_DONE` is cleared at the top of every tick** and is the only
  signal the caller gets. `HumanPlayer_AnimDone` 0x489F50
  (`src/loadscreen/human.c:200-203`) is exactly `(flags & 0x100) != 0`, which is
  `HumanAnimDone` (`anim_rules.cpp:585-596`).
- **`ANIM_FIRST` is cleared on every path out** (`boneobj.c:939`).
- **The root-motion fixup runs only on the wrap tick, only for bone 0, and only
  while `ANIM_FIRST` is clear** (`boneobj.c:909-929`). It re-samples the position
  track at the far end (time 0 for reverse, the length for forward), then places
  the root at `keys[0].pos - delta` (forward) or `keys[last].pos - delta`
  (reverse). In the engine's own sequence the instance's position already holds
  the far-end sample by then, so the delta is zero and the root snaps to the key.
  `AdvanceAnimInst` (`anim_rules.cpp:360-459`) takes the track as an argument
  precisely so this is testable, and returns the fixup in `AnimAdvance`
  (`anim_rules.h:385-396`).

Instance flags come from the **caller's** `loop` argument and are a different
word from the desc flags:

| Constant | Value | Meaning |
|---|---|---|
| `ANIM_LOOP` | 1 | loop rather than clamp |
| `ANIM_REVERSE` | 2 | run the playhead down from the length |
| `ANIM_FIRST` | 0x8000 | suppress the wrap root fixup |

`ANIM_REVERSE` is also maintained by `BoneObj_UpdateAnimDirection` 0x4D6550
(`anim.c:163-169`), which sets it iff `inst->speed < 0.0f` and **clears** it
otherwise — so a speed of exactly 0 clears the reverse bit rather than keeping
it. `Anim_SetSpeed` 0x4D6580 (`anim.c:172-179`) is
`inst->speed = gTicksPerFrame * speed` followed by that update;
`Anim_SetTime` 0x4D65B0 (`anim.c:181-185`) is a raw store with no clamp and no
wrap. All three are ported (`anim_rules.cpp:497-518`).

`BoneObj_InitAnimInst` 0x4D4F60 (`boneobj.c:988-1021`) starts the instance:

- forward: `pos = keys[0].pos`, `time = 0`, `speed = +gTicksPerFrame`;
- reverse: `pos = keys[last].pos`, `time = desc->length`, `speed = -gTicksPerFrame`.

Both `time` and `speed` are written with `memcpy`, i.e. bit copies, which matters
only for a signalling NaN. `InitAnimInst` (`anim_rules.cpp:198-244`) reproduces
the two branches.

`Anim_GetPhase` 0x4D6520 (`anim.c:155-161`) is `Anim_GetTime(...) /
Anim_GetLength(...)`, with the time narrowed to a float on the way out of the
getter and widened back for the divide — ported as `AnimPhase`
(`anim_rules.cpp:184-196`).

---

## 6. The position track

`BoneObj_AnimPositionCB` 0x4D4840 (`src/boneobj/boneobj.c:788-868`) samples the
track and stores `inst->delta = p - inst->pos`, then `inst->pos = p`. Four cases:

| Case | Behaviour |
|---|---|
| `n == 1` | `pos = keys[0].pos`, `delta = 0` |
| `time` exactly a key time | `pos = keys[i].pos` |
| `time` past the last key | `pos = keys[n-1].pos` |
| otherwise | between `keys[i-1]` and `keys[i]` |

The between-keys case has **two forms**, and which one runs is a global:
`gBoneSmoothAnims` defaults to 1 (`src/lod/boneobj.c:65`), so the **Hermite**
branch (`boneobj.c:819-841`) is the default and the lerp (`boneobj.c:842-855`) is
the fallback. They do not agree except at the endpoints and at the exact midpoint
of antisymmetric tangents.

`SamplePosTrack` (`anim_rules.cpp:246-358`) reproduces both, including three
details that are easy to "clean up" and wrong to:

- **The three components do not share a term order.** `boneobj.c:839-841` reads
  `p[0] = ((k->pos[0]*h2 + i0*h4) + pk->pos[0]*h1) + o0*h3`,
  `p[1] = ((k->pos[1]*h2 + pk->pos[1]*h1) + i1*h4) + o1*h3`,
  `p[2] = ((k->pos[2]*h2 + pk->pos[2]*h1) + i2*h4) + o2*h3`. Floating-point
  addition is not associative, so the three are written out separately rather
  than through a loop.
- **`h4` is a `float` subtraction** of two floats, not a long double one
  (`boneobj.c:826`), while `h`, `h1`, `h2`, `h3` stay long double.
- **The lerp mixes precision**: `d0` and `e0`/`e2` stay long double while `d1` is
  stored back into a float, so the x and y components are rounded differently
  (`boneobj.c:843-854`).

`gBoneSmoothAnims` is initialised in `BoneObj_Init` 0x4D2C30 (`lod/boneobj.c:65`),
i.e. once at startup, and `Anim_PrepareSlerps` 0x4D6D40 (`src/haze/anim.c`) is the
rotation-side counterpart (`boneobj.c:1632`).

**The `n == 0` hole.** With an empty track the search loop never runs, so `kt`
keeps the value it was initialised with. The decompilation models that as a union
over the `AnimInst *` (`boneobj.c:800-804`), which is the compiler's way of saying
the source left it uninitialised. The `tm == kt` test then fails, `i == n` takes
`keys[n - 1].pos` = `keys[-1]`, and `tr->keys` was never assigned at all
(`if (keys > 0)` guards the allocation, `boneobj.c:1604-1609`). `SamplePosTrack`
returns `false` instead. Shipped data never reaches it: all 376 clips in the six
files have at least one position key (§12).

---

## 7. Blend and fade

`Anim_Play` 0x4D61D0 (`src/boneobj/anim.c:38-58`) converts the caller's blend
**seconds** into **frames** and decides whether to fade or to switch immediately:

```c
float t = blend * kTicksPerSecond / gTicksPerFrame;
...
if (old || b->parent) {
    if (!(t < 1.0f)) { BoneObj_FadeIn(b, src, t, 0); b->flags |= BONE_ANIMATED; return; }
    if (old) BoneObj_DeleteBlendNode(old);
}
b->flags = (b->flags & 0xFFFAFEFF) | BONE_ANIMATED;
```

`AnimBlendFrames` (`anim_rules.cpp:461-469`) is that expression. With
`gTicksPerFrame == 0` — the state before `game.c:449` — the divide is by zero:
`+inf` for a non-zero blend, **NaN** for a zero blend. Since `!(NaN < 1.0f)` is
true, a zero blend with an uninitialised frame rate still takes the fade path.
`AnimPlayModeFor` (`anim_rules.cpp:471-480`) returns `kAnimPlayFadeNaN` for that
case so the difference is visible rather than collapsed.

`Anim_FadeOut` 0x4D62B0 (`anim.c:72-95`) is different in one place: it computes
`long double tt` and tests `if (tt < 1.0f)` **on the long double**, so a value
that rounds up to `1.0f` as a float but is below 1 as a long double takes the
"delete the blend node now" path. `AnimFadeOutFrames` (`anim_rules.cpp:482-495`)
keeps that distinction.

The `BONE_ANIMATED` / `BONE_FADING` bits and the `0xFFFAFEFF` mask that clears
`BONE_ANIMATED`/`BONE_ORIENTED` are exposed as constants
(`anim_rules.h:133-142`) but no logic is built on them here: the blend-node
lifetime is `BoneObj_FadeIn` / `BoneObj_DeleteBlendNode`, which are out of scope.

---

## 8. The authored form: which animation a placed object plays

### 8.1 GenericTBA — "Activate Anim"

`GenericTBA_Open` 0x447DB0 (`src/glass/tba.c:181-218`) extends the ExplodeObject
table by five parameters (`tba.c:90`), registering `anim` as an **Int16** at
`tba.c:93` and `use_time` as a **Real32** at `tba.c:94`. `GenericTBA_Create`
0x447FF0 sets the defaults `t->use_time = 4.0f; t->anim = -1;`
(`tba.c:105-106`).

`GenericTBA_GetAnim` 0x4485D0 (`tba.c:280-284`) and `GenericTBA_GetUseTime`
0x4485C0 (`tba.c:274-278`) are bare field reads. (IGI-RE declares
`GenericTBA_GetAnim` with two extra arguments at `humanstate.c:35`; those are a
caller-cleans-stack artifact of the call site, not parameters — the body reads
only `t->anim`.)

The authored declaration is 18 parameters
(`MISSIONS/location0/LEVEL2/objects.qsc:53-54`), and it produces a **22-token**
argument list, because `ParamReal32x9_Parse` 0x4EFBA0
(`src/physobj/paramops.c:423-435`) consumes **three** tokens — three Euler angles
fed to `Matrix_SetEuler` — not nine, despite its name. `ObjectPos` is
`Real64x3`, also three tokens (`paramops.c:178-188`). So two parameters eat six
tokens and the other sixteen eat one each.

| Token | Authored parameter | Declared type | Engine field |
|---|---|---|---|
| 0 / 1 / 2 | task id / type / name | — | not a parameter |
| 3–5 | Position | `ObjectPos` | `place` |
| 6–8 | **Orientation** | `Real32x9` (**3** tokens) | `placeMatrix` |
| 9 | Model | `String16` | `modelName` |
| 10 | Destroyed model | `String16` | `deadModel` |
| 11 | Damage scale | `Real32` | `maxHealth` |
| 12 | Explosion radius | `Real32` | `blastRadius` |
| 13 | Explosion falloff radius | `Real32` | `unk1FC` |
| 14 | Explosion damage scale | `Real32` | `unk204` |
| 15 | Explosion delay | `Real32` | `delay` |
| 16 | Explosion fragments | `Int32` | `unk20C` |
| 17 | Explosion fireballs | `Int32` | `unk210` |
| 18 | Explosion expression | `VarString` | `trigger` |
| 19 | Explosion sound | `String16` | `sound` |
| 20 | Explodable | `bool8` | `unk238` (`tba.c:91`) |
| 21 | On Expression | `VarString` | `script` (`tba.c:92`) |
| **22** | **Activate Anim** | `Int16` | **`anim`** (`tba.c:93`) |
| **23** | **Activate Time (s)** | `Real32` | **`use_time`** (`tba.c:94`) |
| 24 | Click to select sprite | `DropDownCombo` | `icon_name` (`tba.c:95`) |

`ParseGenericTbaAnim` (`anim_rules.cpp:520-531`) reads tokens 22 and 23 and falls
back to the create-time defaults. Note the shape of the trap: the authored type
name says `Real32x9` and the argument really is three words, so any decoder that
trusts the type name is off by six tokens and reads the sound name as the
animation id.

`AnimUseTimeFrames` (`anim_rules.cpp:557-570`) is `FRAMES(seconds)` from
`humanstate.c:520` with `FPS 30.0f` (`humanstate.c:91`), i.e.
`fistpll(seconds * 30.0f)` — round to nearest even, the x87 helper at
`humanstate.c:122-128`. `HumanPlayer_TypeComputerEnter` 0x40C670 uses it for
`h->actionFrames` (`humanstate.c:789`, `:1010`, `:1117`).

The caller is `HumanPlayer_Use35Enter` 0x40CF70 (`humanstate.c:996`):
`Human_PlayAnim(h, GenericTBA_GetAnim(obj, 1, 0), 1, 0.0f)` — **loop = 1**, and a
zero blend. `Human_PlayAnim` 0x489EA0 (`src/loadscreen/human.c:176-180`) is
`Anim_Play(h->anim, 0, anim, loop, blend)`, so that `loop` argument lands in
`Anim_Play`'s **flags** word. `loop = 1` is `ANIM_LOOP`.

### 8.2 HumanSoldier — "Bone Heirachy" and "Stand Animation"

`Soldier_Open` 0x45CAB0 (`src/soldier/soldier.c:717-734`) registers the authored
"Bone Heirachy" onto `s.model` and "Stand Animation" onto `s.idleAnim`.
`Soldier_CreateHandler` 0x45CC50 (`soldier.c:736-753`) defaults `idleAnim = -1`
and **`model = 1`** (`soldier.c:746`, `:751`) — so a soldier with no authored
hierarchy uses **skeleton 1**, not 0. `SoldierModelOrDefault`
(`anim_rules.cpp:546-555`) reproduces that.

`Soldier_AnimEnter` 0x45C750 (`soldier.c:617-631`) is the interesting path:

```c
if (s->scriptAnim == -1)        Human_PlayAnimSlot(h, 2, 0.0f);
else if (Anim_Exists(h->anim, 0, s->scriptAnim))
                                Human_PlayAnim(h, s->scriptAnim, 0, 0.0f);
else { Warning_Show("Animation #%d not found", s->scriptAnim);
                                Human_PlayAnimSlot(h, 2, 0.0f); }
s->scriptAnim = 0;
```

Three things to reproduce rather than fix: the fallback is a **slot**, not an id;
the failure is a `Warning_Show`, i.e. it degrades to the idle slot and carries on;
and `scriptAnim` is reset to **0**, not to `-1`, so the `== -1` test is a
one-shot.

The "Stand Animation" itself is played with **flags = 0** by the pistol and RPG
soldier enters — `Anim_Play(h->h.anim, 0, h->idleAnim, 0, 0)`
(`src/pistol/soldier.c:119-131`), `src/rpg/rpg.c:159-171`. So an authored
"Stand Animation" is a **one-shot that clamps at its length**, not a loop. That is
almost certainly not what an author expects, and it is what the engine does.

### 8.3 The player — hardcoded

`HumanPlayer_CreateHandler` **0x410370** (`src/humanplayer/humanplayer.c:1200-1252`)
is the only place the player's skeleton is chosen, and it is hardcoded:
`BoneDynCubeObj_Init(Game_GetWorld(), h, 0, 0)` (`:1212`, skeleton 0) followed by
`Human_PlayAnim(h, 2, 0, 0)` (`:1214`, clip 2, flags 0). By contrast `src/rpg/rpg.c:675`
and `src/patrol/female.c:745` use the authored `h->model`, and
`src/viewcam/humanview.c:238` hardcodes `5`.

---

## 9. The player's slot table

`Human_PlayAnimSlot` 0x489E70 (`src/loadscreen/human.c:168-174`) indirection:
`AnimSlot *s = &h->animSlots[slot]; Human_PlayAnim(h, s->anim, s->loop, blend);`
— again `s->loop` straight into the flags word.

`h->animSlots` is assigned the 49-entry table at `humanplayer.c:1222`
(`h->animSlots = (struct AnimSlot *)gAnimSlots`), and `gAnimSlots` is the
`char store_538870[392]` literal at `humanplayer.c:1563` — 392 / 8 = 49 entries of
`{int anim; int loop;}`. The literal lists 389 bytes, so the final three are the
C initialiser's implicit zeros and slot 48 is `{2, 1}`. `kAnimSlots`
(`anim_rules.cpp:598-608`) is that table.

**Exactly one entry has `loop == 2`**: slot 38, `{170, 2}`. Since `loop` is a
flags word, that is `ANIM_REVERSE` — the animation runs backwards — and not "loop
twice". Thirteen entries have `loop == 0`, i.e. one-shot clamps. The state names
in `kBodyStateNames` come from `gBodyStates` (`humanplayer.c:1554-1556`, 49
entries), the table `StateMachine_Init` installs at `humanplayer.c:1225`.

Two caveats, both stated in §14: these byte literals are IGI-RE `STORE`
reconstructions rather than a read-back from the shipped binary, and id 0 appears
in nine slots (`(0, 0)`), which is not a clip in any shipped file — playing one
of those slots would take the `byId[0] == NULL` path of §2.

---

## 10. The bone/skin side

`BoneMesh_Skin` 0x49B700 (`src/dxinit/d3drender.c:226-313`) takes a mesh and a
`BoneObj`. `count = *(int *)(mesh + 0x10)` (`:228`); above 4000 vertices it prints
`"Bone model too large. Model %s has %d vertices."` and spins forever (`:234-238`).
Vertices start at `mesh + 0x18` (`:253`) and bones are reached as
`&(*(Bone **)(bones + 8))[v->bone]` (`:258`).

There are three code paths and they do **not** compute the same expression:

| Path | Lines | Rotation | Weight | Extras |
|---|---|---|---|---|
| non-rigid base | `:257-274` | `p0*m0 + p2*m2 + p1*m1` … | `o->x = X * v->w` | — |
| non-rigid extra | `:275-290` | `m2*p2 + m1*p1 + m0*p0` … | `t->x = w*X + t->x` | accumulated into the base vertex |
| rigid | `:291-309` | `p0*m0 + p2*m2 + p1*m1` … | none | ignored |

Specifically:

- The base path computes `long double X = x + b->pos.x` where `x` is a **float**
  expression, so the sum is rounded as a float and then widened, and
  `o->x = X * v->w` is a long double multiply stored back to a float — two
  roundings. `SkinTransformVertex` (`anim_rules.cpp:696-763`) keeps both.
- The extra-influence path uses a **different term order** for the same matrix
  product and accumulates with `t->x = w*X + t->x`, so it adds into an
  already-written base vertex and leaves that vertex's normal and UV alone.
- The rigid path has **no weight at all** and skips the extra vertices entirely.

The normal product `m1*n1 + m2*n2 + n0*m0` / `n2*m5 + n1*m4 + n0*m3` /
`m6*n0 + m8*n2 + m7*n1` is identical in the non-rigid and rigid paths
(`:271-273`, `:303-305`), so it is one helper. UVs are copied unchanged
everywhere.

The layout constants are exposed and asserted: `SkinVertex` 0x28 with `p` at
0x00, `n` at 0x0C, `uv` at 0x18, `w` at 0x20, `target` at 0x24, `bone` at 0x26
(`d3drender.c:37-44`); `NormalVertex` 0x20 (`:46-50`); `Bone` 0x84 with `pos` at
0x18 and `matrix` at 0x24 (`boneobjtypes.h:60-74`). `SkinAccumulatePosition`
(`anim_rules.cpp:765-771`) is the extra-influence rule as a single call.

All three files that own this code (`dxinit/d3drender.c`, `boneobj/anim.c`,
`boneobj/boneobj.c`, `lod/boneobj.c`, `haze/anim.c`, `soldier/soldier.c`,
`pistol/soldier.c`, `glass/tba.c`, `loadscreen/human.c`) open with
`#pragma GCC optimize("excess-precision=standard")` — visible at
`d3drender.c:1` and `anim.c:2`. That is why the intermediate types in
`anim_rules.cpp` are spelled out rather than left to the compiler.

**Two cited files do not set it**: `src/humanstate/humanstate.c` and
`src/level/level.c`. That is the one place in this port where the original build
may have differed from what is written here — see §14.

---

## 11. The module

`source/runtime/anim_rules.h` (705 lines) carries the citation map in its header
comment and declares:

| Area | Declarations |
|---|---|
| Table | `AnimDesc`, `AnimRecord`, `AnimSetTable`, `BuildAnimSetTable` |
| Lookup | `AnimLookup`, `AnimResolution`, `ResolveAnim` |
| Timing | `AnimLengthSeconds`, `AnimLengthTicks`, `AnimPhase` |
| Playhead | `PosKey`, `PosTrack`, `AnimInst`, `InitAnimInst`, `AnimAdvance`, `AdvanceAnimInst`, `SamplePosTrack` |
| Blend | `AnimBlendFrames`, `AnimPlayMode`, `AnimPlayModeFor`, `AnimFadeOutFrames`, `AnimSetSpeed`, `AnimSetTime` |
| Authored | `GenericTbaAnim`, `ParseGenericTbaAnim`, `SoldierAnim`, `ParseSoldierAnim`, `SoldierModelOrDefault`, `AnimUseTimeFrames`, `GenericTbaGetAnim`, `GenericTbaGetUseTime`, `HumanAnimDone` |
| Slots | `AnimSlot`, `kAnimSlots`, `kBodyStateNames`, `kAnimSlotCount` |
| Skin | `SkinVertex`, `NormalVertex`, `SkinTransformInput`, `BoneTransform`, `SkinTransformOutput`, `SkinTransformVertex`, `SkinAccumulatePosition` |

`source/runtime/anim_rules.cpp` (773 lines) implements all of it. Every branch
carries its `file.c:NN` citation. The token helpers (`TokenFloat`, `TokenInt`,
`RoundInt`) are file-local statics duplicated per module, matching
`source/runtime/mission_fog.cpp` and `source/runtime/cutscene_rules.cpp:10-55`.

**What this module deliberately does not do.** It does not read files. The IFF
decoder lives in the test (§12), and the editor's own readers
(`QVM_Parse`, the BEF/`AnimationClip` path in `source/animation.cpp`) are left
alone. It also does not touch the renderer: `SkinTransformVertex` is a pure
function of one vertex and one bone, and the caller decides which of the three
paths a mesh takes. And it does not reimplement `SkinSkinnedVertices`.

**Named divergences** (each also marked in the header):

1. A record whose id is outside the table is **dropped and reported**
   (`BuildAnimSetTable`'s `out_of_range`); retail writes `byId[id]` with no check.
2. `ResolveAnim` **refuses a negative id**; retail reads `byId[-1]`.
3. `InitAnimInst` **returns false** for a missing id; retail dereferences NULL.
4. `SamplePosTrack` **returns false** for an empty track; retail reads `keys[-1]`.
5. `kAnimSlotCount` is 49 and every slot is represented; retail's literal has 389
   explicit bytes and relies on zero-fill for the last three.

None of these changes an outcome for the shipped data — §12 shows the data never
takes any of the five paths — but each one is a case where the port reports
instead of reading out of bounds.

---

## 12. The retail corpus

### 12.1 The shipped animation files

`common/ANIMS/` holds six files; there is no `004.IFF`. The test decodes each one
with its own IFF walker and asserts the table the port builds from the raw
records:

| File | Skeleton | Bones | Clips (`anims`) | Capacity (`total`) | Ids |
|---|---|---|---|---|---|
| 000.IFF | 0 | 33 | 122 | 247 | 2 … 246, **123 holes** inside that span |
| 001.IFF | 1 | 33 | 95 | 212 | 2 … 95 **plus a single clip at 211**; 115 holes |
| 002.IFF | 2 | 33 | 18 | 20 | 2 … 19, contiguous |
| 003.IFF | 3 | 33 | 47 | 50 | 2 … 49, one hole at 8 |
| 005.IFF | 5 | 56 | 78 | 81 | 2 … 80, one hole at 42 |
| 006.IFF | 6 | 33 | 16 | 18 | 2 … 17, contiguous |

376 clips in total. Note how sparse skeleton 0 is: 122 clips spread over 245 id
slots, so the player's skeleton has 123 ids that are in range and empty. Ids are
**not** dense or ordered, which is why the table — not a count — is the only safe
way to ask what exists.

Every clip has at least one position key, every first key time is `0.0f`, every id
is non-negative and inside its capacity, and no file has a duplicate id. So
**none** of the five divergence paths above is reachable with shipped data: no
negative id, no out-of-range record, no empty position track, and no lookup that
misses a *loaded* slot in the places the game actually asks.

### 12.2 The LEVEL2 authored animation, and why skeleton choice matters

`MISSIONS/location0/LEVEL2/objects.qsc:763`, `:766` and `:769` are three
GenericTBA objects, each ending `..., FALSE, "<expr>", 245, 4.0,
"STATUSSCREEN:a_c4.spr")`. So the authored pair is **`Activate Anim = 245`** and
**`Activate Time (s) = 4.0`**, which is `120` frames of use window.

On skeleton 0 — the player's, and the skeleton `HumanPlayer_Use35Enter` plays on —
id 245 is a real clip: **length 14400 ticks, exactly 3.0 seconds at 4800 a
second, flagged `ANIM_LOOPS`**. At 160 ticks per frame that is exactly **90
frames**, and the tick that lands exactly on 14400 is the one that wraps. The
corpus test plays it forward with `loop = 1` and counts 90 ticks to the wrap, then
plays the same clip with `flags = 0` and shows it clamping at 14400 and raising
`BONE_EVENTS_DONE`.

On skeleton 1 — a `HumanSoldier` with the shipped `Bone Heirachy = 1` — the
capacity is 212, so **245 is past the end**: `Anim_Exists` returns 0,
`Soldier_AnimEnter` prints `"Animation #%d not found"`, and the soldier falls back
to slot 2. Same number, same level, opposite outcome, decided entirely by the
skeleton. That is the single most useful thing this port can tell an author.

The authored use window (4.0 s) is longer than the clip (3.0 s), so the loop wraps
before the interaction ends — an authored mismatch the engine silently absorbs.

### 12.3 Every shipped soldier and every level

`LEVEL2/objects.qsc:47` declares HumanSoldier's last two parameters as
`"Bone Heirachy","Int32","Stand Animation","Int32"`; the shipped instances author
`Bone Heirachy = 1` with `Stand Animation = -1`, which is the engine's own
"no animation authored" sentinel and therefore takes the slot fallback.

The test also reads every `OBJECTS.QVM` under `MISSIONS/location0/LEVEL*` with the
editor's own `QVM_Parse`, extracts every authored GenericTBA call, and checks that
each non-negative `Activate Anim` resolves on **skeleton 0**. It also cross-checks
LEVEL2's `OBJECTS.QVM` against its decompiled `objects.qsc` call for call, which is
what proves the QVM token reader is aligned with the 22-value argument list rather
than off by a token.

### 12.4 A finding: the shipped FORM sizes are wrong

Reproduced rather than worked around, and now asserted by the corpus test.

Every `ANIMS/*.IFF` is one `FORM ... BOBJ`, and **its declared size covers only its
first child**. 000.IFF declares 588, which implies the file ends at offset 596, but
the real child chain runs to 1512568. The nested `FORM ... BOAL` has the same
defect — it excludes its final child. Every other FORM (`BOBH`, each `BOAN`) and
every leaf chunk is exactly self-consistent, and the child chain ends exactly on
the file size in all six files.

This is harmless in retail because **`BoneObj_Load` never reads a FORM size at
all**. It locates chunks with sequential id scans — `Iff_FindChunk(it, ID('BOSH'))`,
`Iff_FindChunk(it, ID('BALH'))`, `Iff_FindChunk(it, ID('BOAH'))`,
`Iff_FindNextChunk(prev, ID('BORH'))` (`boneobj.c:1543-1630`) — so a wrong FORM size
is invisible to it. The test's decoder has to walk the top level and BOAL to EOF
for the same reason, and it asserts the defect explicitly
(`EXPECT_NE(file.declared_end, file.chain_end)`) so the walk is not accidentally
correct by luck.

This is a defect in the shipped asset, not in the port, and it is left unfixed:
the port reads the same bytes the engine reads.

---

## 13. Verified, and not

### Verified

| Claim | Evidence |
|---|---|
| The port reproduces all five §11 divergences and the six lookup verdicts | `tests/test_anim_rules.cpp`, `AnimLookupTest.*`, `AnimTableTest.*` |
| The end test is `>=`, the wrap is one subtraction, a one-shot clamps and keeps running, NaN counts as past the end | `AnimAdvanceTest.*` |
| The root fixup runs only on the wrap tick, only for bone 0, only while `ANIM_FIRST` is clear, and snaps to the first (or last) key | `AnimAdvanceTest.RootFixupSnapsTheRootBackToTheFirstKeyOnAWrap`, `.ReverseRootFixupSnapsToTheLastKey`, `.RootFixupIsSkippedWhileAnimFirstIsSet` |
| `AnimLengthTicks` rounds to nearest even rather than truncating | `AnimLengthTest.TicksRoundRatherThanTruncate` |
| `gTicksPerFrame == 0` makes the blend `inf`, or NaN for a zero blend, and NaN still takes the fade path | `AnimBlendTest.ZeroTicksPerFrameDividesByZero`, `.NaNBlendTakesTheFadePath` |
| The Hermite and lerp forms disagree, with the arithmetic pinned | `AnimPosTrackTest.SmoothAndLinearDisagreeAwayFromTheEndpoints` |
| The extra-influence path accumulates and leaves the base normal/UV alone; the rigid path ignores the weight | `AnimSkinTest.*` |
| The six shipped files decode to the counts in §12.1, the id layout is sparse as §12.1 says (including 001's lone clip at 211), and every non-zero slot id resolves on skeleton 0 | `AnimRulesCorpus.*` |
| LEVEL2 authors 245 / 4.0, and 245 is 14400 ticks and loops in exactly 90 frames on skeleton 0 | `AnimRulesCorpus.Level2AuthorsActivateAnim245AndUseTime4`, `.TheLevel2GenericTbaClipLoopsInExactlyNinetyFrames` |
| Skeleton 1 rejects id 245 as past its 212-slot capacity | `AnimRulesCorpus.SkeletonOneRejectsTheLevel2GenericTbaAnimation` |
| LEVEL2's shipped declaration puts "Activate Anim" at token 22 and "Activate Time (s)" at 23, derived from the 18 declared parameters rather than a copied list | `AnimRulesCorpus.DeclaredParametersPutActivateAnimAtToken22` |
| The QVM and QSC readers agree on LEVEL2's GenericTBA calls | `AnimRulesCorpus.QvmGenericTbaCallsAgreeWithTheDecompiledQsc` |
| Every level's authored `Activate Anim` resolves on skeleton 0 | `AnimRulesCorpus.EveryLevelsGenericTbaAuthorsAResolvableAnimation` |
| The shipped FORM sizes are short, and the child chains still tile to EOF | `AnimRulesCorpus.AnimFilesDecodeWithTheExpectedSkeletonShape` |
| One engine frame is `gTicksPerFrame * speed` ticks, and the step's end test, single-subtraction wrap, reverse clamp and reverse wrap are the engine's | `AnimFrameStep73.*` (pure), `AnimPlaybackStepTest.OneStepIsExactlyOneEngineFrameEvenWhilePaused`, `.AStepScalesWithTheSpeedLadder`, `.AStepOntoTheEndOfAOneShotStopsThePlayback`, `.AStepWrapsALoopingClipByOneDuration`, `.SteppingBackWalksTheClockDownAndClampsAtZero` |
| The pure step arithmetic agrees with the ported engine clock on every rung of the speed ladder, in both loop modes and both directions | `AnimPlaybackStepTest.PureStepMatchesTheEngineClock` |
| A frame step poses the frame it stepped to, and works while the playback is paused | `AnimPlaybackStepTest.TheStepShowsTheFrameItSteppedTo`, `.OneStepIsExactlyOneEngineFrameEvenWhilePaused` |
| The F10 panel's buttons, hit-test and text come from one pure layout, and its speed label is the same formatter the `F12` keys report | `AnimFrameStep73.EveryButtonIsInsideThePanelAndNoneOverlap`, `.HitTestRoundTripsEveryButtonCentre`, `.ThePanelFitsTheSmallestViewportAndItsBandsDoNotOverlap`, `.RowTextUsesTheSameSpeedLabelAsTheKeys`, `.TitleReportsTheTransportStateAndTheEngineFrameSize` |

**Not verified, and why:**

- **No rendered or in-game result.** The GUI cannot run on this host (OpenGL 4.1
  required, 2.1 available). No pose, no skinned frame, no played animation was
  drawn. Every skinning assertion is arithmetic on one vertex, not a picture.
- **The IFF container framing is not cited to IGI-RE.** `Iff_Parse` exists there
  only as an `ORIG` stub, so the chunk walk in `tests/test_anim_rules.cpp` is the
  **test's own decoder**, labelled as such in its header comment. It is validated
  by internal consistency (the chain tiles to EOF, BALH's `anims` equals the number
  of BOAN forms, ids are unique and in range) and by reproducing the values
  `BoneObj_Load` consumes — not by a citation.
- **`FRAMES(seconds)` may have been evaluated in extended precision.** 
  `src/humanstate/humanstate.c` is one of the two cited files that does **not**
  set `excess-precision=standard` (the other is `src/level/level.c`). In the
  original build the `seconds * 30.0f` product could have been carried in x87
  extended precision into the `long double` parameter of `x87_fistp`. The port
  computes the standard float product. The two differ only when
  `use_time * 30.0f` lands within one ulp of a `.5` boundary; no shipped
  GenericTBA authors a value that does.
- **`gAnimSlots` and `gBodyStates` are `STORE` reconstructions.** IGI-RE writes
  them as byte literals (`humanplayer.c:1554-1563`) rather than reading them back
  from `IGI.exe`, so the 49 entries are as faithful as that literal. The
  animation ids in them were re-read as `int32` rather than signed bytes — the
  literal prints `0xA8` as `-88`, but the field is `0x000000A8` = **+168** — and
  with that reading every non-zero slot resolves on skeleton 0, which is the
  consistency check that the reading is right.
- **The `keys[-1]` and `n == 0` position-track paths are not exercised.** They are
  unreachable with the shipped data (all 376 clips have a key at time 0), so the
  port's refusal is asserted directly rather than observed against the engine.
- **`BoneMesh_Skin`'s runtime behaviour is not traced.** The arithmetic is
  transcribed from `d3drender.c:226-313` and asserted per branch, but no frame was
  rendered and no trace was taken, so this is "matches the decompiled source", not
  "matches the running engine".
- **`BoneObj_AllocAnimInst` 0x4D4F20, `Anim_GetInst` 0x4D6460, `Anim_GetId`
  0x4D64C0 and `Anim_GetLength` 0x4D64E0 are cited in the header but not ported** —
  the first is the 0x34-byte instance pool and the other three are accessors; the
  editor owns its own `AnimInst`, so none of the four has a caller here.

### Found but left unfixed, and why

| Finding | Why it is left alone |
|---|---|
| The shipped ANIMS FORM sizes are short (§12.4) | It is a defect in the retail asset; the engine's id scans ignore it, and the port reads the same bytes |
| `Anim_Exists` has no `id < 0` guard | Reproduced as a reported verdict, not corrected — a caller that wants retail's behaviour must be able to see it |
| `Soldier_AnimEnter` resets `scriptAnim` to 0, not -1 | Reproduced; it is a behaviour, not a bug to patch |
| "Stand Animation" plays as a one-shot | Reproduced; the port exposes `kAnimLoopSoldierStand = 0` so the caller can see it |
| Slot 38 uses `loop = 2` meaning reverse | Reproduced; renaming it would hide what the data says |
| Nine slots are `{0, 0}`, and id 0 is not a clip | Reported via `kAnimSlots`; the port does not invent a clip |
| The AI action system and the combat weapon-animation switch | Out of scope for a level editor (§0) |

---

## 14. Commands and results

Both commands were run from
`/Users/haseeb-mir/Documents/Code/project-igi-editor-igi-re`.

**1. The host test lane** (with the retail corpus present at
`/Users/haseeb-mir/Downloads/IGI`, so nothing skipped):

```bash
IGI_HOST_NO_CACHE=1 IGI_HOST_OUT_DIR=build-mingw/ht-anim \
IGI_HOST_LOG_DIR=build-mingw/log-anim \
IGI_HOST_TESTS=tests/test_anim_rules.cpp bash scripts/run-host-tests.sh
```

```
   PASS  test_anim_rules                      72 passed    0 failed  (72 tests, exit 0)
   suites attempted      : 1
   suites run            : 1
   suites failed         : 0
   tests run             : 72
   tests passed          : 72
   tests failed          : 0
   tests skipped         : 0 (in 0 suite(s))
   game corpus           : /Users/haseeb-mir/Downloads/IGI
   suites not host-portable (compile): 0
   suites not host-portable (link)   : 0
   suites not host-portable (runtime unresolved symbol): 0
OK: 72 tests passed in 1 suites
==> run-host-tests.sh exit=0  finished 2026-10-26T09:04:31Z
[exit code: 0]
```

**2. The standalone syntax check of the module**, with the project's strict
warnings:

```bash
clang++ -std=c++20 -Wall -Wextra -Wpedantic -fsyntax-only \
  -I source -I third_party/glm-1.0.1 -I third_party/GL/include \
  source/runtime/anim_rules.cpp
```

```
[exit code: 0]
```

No output: clean under `-Wall -Wextra -Wpedantic`.

**This is the host lane, not the full gate.** Per the repository rules, the host
lane does not build `tests/test_runtime_subsystems.cpp` (Windows-only) or the MCP
tool tests; the Windows `igi_tests` suite is the gate and is run by the Lead. The
two commands above prove "compiles" and "these 72 tests pass"; they do **not**
prove behavioural equivalence with retail, which would need a runtime trace.

---

## 15. Files

| File | Lines | Change |
|---|---|---|
| `source/runtime/anim_rules.h` | 705 | New. Citations, constants, declarations. |
| `source/runtime/anim_rules.cpp` | 773 | New. Implementation, every branch cited. |
| `tests/test_anim_rules.cpp` | 1823 | New. 56 unit tests + 16 corpus tests. |
| `CMakeLists.txt` | +2 | Two purely additive lines: `tests/test_anim_rules.cpp` in the `igi_tests` `add_executable` block, `source/runtime/anim_rules.cpp` in `target_sources(igi_tests PRIVATE)`. |

Read but not modified, deliberately: `source/animation.cpp`,
`tests/test_animation.cpp` (the editor's own BEF/`AnimationClip` model and its
`SkinSkinnedVertices`), and `source/renderer/mef_native.{h,cpp}` (which already
carries `baseVertexCount` / `extraInfluenceCount`, the same base-vs-extra split as
§10).

---

## 16. The editor's transport: play/pause, speed, and a frame step (#73)

The engine has no transport UI and no pause: a live instance advances exactly
once per frame (§5). The editor's preview therefore owns three editor decisions
and nothing else, and they all act on one `igi::AnimPlayback` per object:

| Control | Where | What it does | Engine side |
|---|---|---|---|
| play / pause | F10 panel button, `Ctrl T` | `AnimPlayback::playing`, the editor's gate over the instance | none — retail has no pause; the clock rule it gates is `inst->time = inst->speed + inst->time` (`BoneObj_AnimEventsCB` 0x4D4B60, `src/boneobj/boneobj.c:884-885`) |
| speed ladder `{0.125 … 8}` | F10 panel buttons, `F12` / `Shift F12` | `AnimPlayback::speed` | `Anim_SetSpeed` 0x4D6580: `inst->speed = gTicksPerFrame * speed` (`src/boneobj/anim.c:172-179`), so the multiplier is the ENGINE's frame size |
| frame step `-1` / `+1` | F10 panel buttons, `,` / `.` | `AnimPlayback::StepFrames` | one engine frame of the clock, through `BoneObj_GetEvents` 0x4D3210 → `BoneObj_AnimEventsCB` 0x4D4B60, then the draw-time pose (`Human_Wake` 0x45F240, `src/humanbase/human.c:289-297`) |

**The step size is the engine's frame, not a constant.** At 1× a step is
`gTicksPerFrame` = 160 ticks (30 fps, `BoneObj_SetFrameRate` 0x4D2D30,
`src/lod/boneobj.c:86-90`, installed at `src/game/game.c:449`); at 2× it is 320.
That is the only "one frame" the engine knows, and it is what makes the step and
the speed control the same clock rather than two.

**Stepping backwards is the engine's own reverse path.** `direction < 0` sets a
negative instance speed (`Anim_SetSpeed` + `BoneObj_UpdateAnimDirection` 0x4D6550,
which sets `ANIM_REVERSE` iff `inst->speed < 0`, `src/boneobj/anim.c:163-179`) and
runs the same clock callback, so the reverse clamp at 0 and the reverse wrap
`length + time` are `BoneObj_AnimEventsCB`'s (boneobj.c:892-937), not a
re-implementation. The forward speed is restored afterwards with the same
multiplier.

**Stepping while paused is deliberate.** Retail's clock is not gated on anything
the editor can pause, so a paused preview that steps one frame performs exactly
the addition the next retail frame would; the editor's `playing` gate only stops
the *automatic* advance.

**The pure arithmetic is pinned against the engine, not trusted.**
`igi::AnimStepTargetTick` (`source/runtime/anim_panel.h`) is the panel's model of
a step — a transcription of the end test, the single-subtraction wrap and the
reverse branch above, including the quirk that the wrap is one subtraction and not
a `while` loop. `tests/test_anim_playback.cpp`
(`AnimPlaybackStepTest.PureStepMatchesTheEngineClock`) drives a real
`AnimPlayback` one engine frame at a time for every rung of the ladder, in both
loop modes and both directions, and requires the ported engine's clock to land
exactly where the pure rule says — including the clamp cases, where both must
agree that nothing moves. `AnimFrameStep73.*`
(`tests/test_editor_quick_actions.cpp`) pins the arithmetic's boundaries, the
panel's layout (buttons inside the panel, no overlap, hit-test round-trip,
baselines inside their bands) and the row text.

**The panel and the keys are one state.** `App::ToggleSelectedAnimPlayback`,
`ResetSelectedAnim`, `AdjustSelectedAnimSpeed` and `StepSelectedAnim`
(`source/app.cpp`) are called by both the F10 panel's buttons
(`App::AnimPanelClick`, hit-tested with the very layout the renderer draws) and
the key handlers in `source/app_input.cpp` / `source/app_input_keyboard.cpp`. The
speed label in the panel is `igi::AnimTimerSpeedLabel`, the same formatter the
`F12` status message uses.

**Not verified.** No frame was rendered: the GUI needs OpenGL 4.1 and this host
offers 2.1, so the panel's appearance, its button positions on screen and the
mouse routing are asserted as geometry, text and compiled code — never as a
picture. The step's *timing* against a running retail client was not traced
either; what is proven is the tick arithmetic and that it matches the ported
engine's own clock.
