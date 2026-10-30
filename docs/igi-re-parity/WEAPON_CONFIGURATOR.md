# Weapon & item configurator — what the engine defines (issue #49 part 1)

Issue #49 asks for "UI for editing active weapon parameters (fire rate, damage,
ammo capacity), ammunition slot assignment, and dropping custom inventory items
directly into the level".

This document separates the three sub-requests by what the retail engine
actually defines, because they do not live in the same place and one of them has
no engine meaning at all. The evidence standard is the repository's: every
behavioural claim below is a function + HOOK address + `file:line` in IGI-RE
(`/Users/haseeb-mir/Documents/Code/IGI-RE`). The retail editor's own manual
`editor/qed/QED.doc` is cited where the claim is about the EDITOR rather than the
engine.

Implementation: `source/runtime/weapon_config.{h,cpp}` (data layer),
`source/level/task_schema.cpp` (the inspector rows),
`tests/test_weapon_config.cpp` (the proof).

---

## 1. Verdict per sub-request

| sub-request | where the data lives | engine meaning | verdict |
| --- | --- | --- | --- |
| fire rate, damage, ammo capacity | `WEAPONS/<NAME>/WEAPON.QVM` — a **global** asset, not the level file | yes, all three | **oracle-backed**, implemented as a parameter table + call-argument reader |
| ammunition slot assignment | *two* places: the level file (`AddAmmo` children of `HumanPlayer`) and `humanplayer/humanplayer.qvm` (`DefineHumanPlayerAmmoLimit`, `DefineHumanPlayerWeaponCycle`) | yes | **oracle-backed**, both halves |
| dropping a custom inventory item into the level | the level file (`GunPickup` / `AmmoPickup` / `AddAmmo` task types) | the *wire format* is the engine's; the *action* is editor tooling | the wire format is implemented; the action is **editor tooling** and is labelled as such |

The one thing with **no engine oracle** is the verb "drop". Nothing in `IGI.exe`
has an opinion about how an author gets a task into a level. The retail editor's
manual documents its own answer, so the action is *retail-editor tooling* rather
than engine parity — see §5.

---

## 2. Weapon types: `DefineWeaponType`

A weapon is **not** a level task and is **not** declared with
`Task_DeclareParameters`. It is one call to `DefineWeaponType`, in a file named
`weapon.qsc`, compiled to `WEAPONS/<NAME>/WEAPON.QVM`:

* `WeaponType_Open` HOOK `0x413A90`, `src/weapon/weapontype.c:34-51` — runs
  `weapon.qsc` in **every subdirectory** of `LOCAL:weapons`;
* `WeaponType_LoadAll` HOOK `0x413B70`, `weapontype.c:95-100` — `File_ScanDir
  ("LOCAL:weapons", WeaponType_Open, NULL)`;
* `DefineWeaponType` HOOK `0x413F90`, `weapontype.c:287-360` — fills one
  `WeaponType` (`src/weapon/weapontypes.h:17-76`, 0x6E8 bytes) from a positional
  argument list.

Because the script carries no parameter names, `source/runtime/weapon_config.h`
labels each row with the `WeaponType` member the IGI-RE decompilation recovered,
and marks the members nothing reads as `Unk<offset>`. That is the honest state of
the data; nothing was invented.

### 2.1 The full argument list, with the members and their readers

| arg | member | offset | read by |
| --- | --- | --- | --- |
| 0 | `id` | — | `Script_RegisterConstant(name, id)` `weapontype.c:306`; `WeaponType_Find` `0x413BD0` |
| 1 | `name` | 0x010 | also registered as a script constant, `weapontype.c:306-307` |
| 2 | `unk110` | 0x110 | — |
| 3 | `unk310` | 0x310 | — |
| 4 | `unk210` | 0x210 | — |
| 5 | `category` | 0x610 | `aitask/aitask.c:252`; `loadscreen/aicombat.c:735-736,763`; `soldier/soldier.c:962,968` |
| 6 | `sightDisplay` | 0x614 | `ammotype/gun.c:434` |
| 7 | `ammoDisplay` | 0x618 | `ammotype/gun.c:355,370,398`; `guncasing/gun.c:467` |
| 8 | `unk004` | 0x004 | — |
| 9 | `caliber` | 0x63C | `guncasing/gun.c:494` `AmmoType_FindByCaliber`; `ammotype/ammotype.c:163` |
| **10** | **`unk654[0]` = damage** | **0x654** | `comprun/knifestab.c:108`; `ammotype/gunshot.c:204` |
| 11 | `unk654[1]` = pierce | 0x658 | `ammotype/gunshot.c:206` |
| 12 | `unk654[2]` | 0x65C | — |
| 13 | `unk654[3]` = `throwSpeed` | 0x660 | `missile/missile.c:629` |
| 14 | `pellets` | 0x640 | `guncasing/gun.c:608` |
| **15** | **`rate`** | **0x644** | `guncasing/gun.c:385,391,402,411,670,677,678` |
| **16** | **`clipSize`** | **0x648** | `ammotype/gun.c:177,184`; `comprun/pickup.c:583`; `game/game.c:630`; `statgun/spas12.c:55` |
| 17 | `burst` | 0x64C | `aitask/aitask.c:259`; `loadscreen/aicombat.c:739-742` |
| 18 | `spread[0]` | 0x664 | `guncasing/gun.c:604` |
| 19 | `spread[1]` | 0x668 | `guncasing/gun.c:604` |
| 20-23 | `recoilPitch`, `recoilYaw`, `recoilPitchRand`, `recoilYawRand` | 0x66C-0x678 | `ammotype/gun.c:162-163` |
| 24 | `unk410` | 0x410 | — |
| 25 | `range` | 0x650 | `ammotype/gunshot.c:205` |
| 26 | `unk510` | 0x510 | — |
| 27, 28 | `unk008`, `unk00C` | 0x008, 0x00C | — |
| 29 | `key` (the model name) | 0x61C | `comprun/pickup.c:579` `VirModel_Find(w + 0x61C)` |
| 30 | `casing` | 0x62C | `guncasing/gun.c:636,658` `GunCasing_New` |
| 31-36 | `anim[0..5]` | 0x67C-0x690 | — |
| 37-43 | `anim2[0,1,3,4,2,5,6]` | 0x694-0x6AC | — |
| 44 | `sound` | 0x6B0 | — |
| 45 | `sound2` | 0x6C0 | — |
| 46 | `detectRange` | 0x6D0 | `ammotype/gun.c:146` — the gunshot noise radius |
| 47 | `taskType` | 0x6D4 | `weapontype.c:345-348` `init_task_data(gWeaponSizeEvent, ...)` |

The argument order is NOT the member order at args 39-41: `DefineWeaponType`
reads `anim2[3]` and `anim2[4]` before `anim2[2]` (`weapontype.c:332-338`). The
table records the argument order, which is what a reader of the file needs.

### 2.2 The three parameters the issue names

**Fire rate** — arg 15, `WeaponType` + 0x644. The cadence is

```c
g->cooldown = round_int((kMinute / (long double)wt->rate) * kFpsD);   // guncasing/gun.c:670
```

with `kMinute = 60.0` and `kFpsD = 30.0` (`guncasing/gun.c:693-694`), i.e.
**shots per minute converted to frames between shots at 30 fps**. The AK47's
shipped value is 650, so 2.77 frames → 3.

**Damage** — arg 10, `WeaponType` + 0x654. This is the one worth recording
carefully, because it is the field the editor got wrong.

```
DefineWeaponType  HOOK 0x413F90   src/weapon/weapontype.c:317-318
    for (i = 0; i < 4; i++) w->unk654[i] = Script_GetArgReal(args, 10 + i);

read as damage by
    KnifeStab_Fire           src/comprun/knifestab.c:108
        msg.damage = *(float *)(f->type + 0x654);
    GunShot_AddBullet  HOOK 0x47B190   src/ammotype/gunshot.c:204
        memcpy(&b->unk38, type + 0x654, 4);

and the bullet's copy is what reaches the victim:
    src/ammotype/bullets.c:139-142   msg.unk38 = b->unk38;
    src/ammotype/bullets.c:165-166   HANDLER(Game_GetEvent9(), hit.task->type)(hit.task, &msg);

`BulletHitMsg` is src/ammotype/bullets.c:23-31; the handler's parameter type is
`HumanHit` (src/loadscreen/loadscreentypes.h:13-18), whose second member is
`float damage` at offset 4 — the same word that `BulletHitMsg` calls `unk38`.
The overlay is exact: shot<-hit, damage<-unk38, dir[3]<-delta[3],
attacker<-owner. So `HumanHit.damage` IS the weapon's `unk654[0]`.
    HumanPlayer_HitHandler  HOOK 0x412440   src/weapon/humanplayer.c:688-704
        Damage_Add(d, part, Damage_Scale(profile, d->part[part].mult * hit->damage));   // :704
    Soldier_HitHandler                      src/soldier/soldier.c:930-949
        dmg = hit->damage;                                                              // :947
```

**There is no damage field in the ammunition table.** `DefineAmmoType` HOOK
`0x47CAD0` (`src/ammotype/ammotype.c:88-115`) reads 15 arguments — id, name,
description, caliber, one real, two RGB colours, tracer, a string, a sound — and
none of them is damage. The retail `AMMO/556/AMMO.QVM` and `AMMO/762/AMMO.QVM`
confirm it: their string pools contain only `DefineAmmoType`, `AMMO_ID_*`, the
display name, the description, a texture id and `Sound0`.

An earlier comment in `source/weapon_system.cpp` said damage "remains the runtime
bridge value; the separate ammo QVM damage table is the next ballistics seam".
That was wrong on both counts: there is no ammo damage table, and the per-weapon
damage is arg 10. The comment is corrected and `WeaponDefinition::engine_damage`
now carries the retail value, checked against the shipping files by the corpus
test.

**Ammo capacity** — arg 16, `WeaponType` + 0x648 `clipSize`:

* the magazine a reload fills — `g->rounds = w->clipSize`, `ammotype/gun.c:177`,
  `need = w->clipSize - g->rounds` at `:184`;
* what a `GunPickup` grants — `t->ammo = *(int *)(w + 0x648)`,
  `comprun/pickup.c:583`;
* what an AI is given — `give.ammo = w[0x648 / 4] > 0 ? w[0x648 / 4] : 0`,
  `game/game.c:630`.

### 2.3 `range` is a range, not a velocity

arg 25 is read at `ammotype/gunshot.c:205` as `b->speed = range * k4096`
(`k4096` = 4096 world units per metre, `src/math/mathvars.h:6`), and
`Bullet_Update` spends that whole budget in ONE frame, sub-stepping collision
queries until the first hit (`ammotype/bullets.c:100-170`). The shipped values
are 30 m (SPAS12) to 2000 m (AK47) — distances, not speeds. The editor's catalog
label `effective_range_meters` is therefore right; its separate
`muzzle_velocity` is arg 13 (`throwSpeed`), which only the missile path reads
(`missile/missile.c:629`).

### 2.4 The variable tail — an honest limitation

```
next = 48;
w->taskType = Script_GetArgInt(args, 47);          // weapontype.c:344-345
if (...) init_task_data(&w->data, w->taskType, gWeaponSizeEvent, gWeaponInitEvent,
                        args, &next);              // :347-348, advances `next`
w->taskType2 = Script_GetArgInt(args, next);       // :349
```

How many arguments the first task type consumes is a property of its C size
handler, registered with `TaskType_SetHandler(gWeaponSizeEvent, ...)`; it is not
recoverable from the script. The shipping files prove the position genuinely
varies:

| file | argument count | tail |
| --- | --- | --- |
| `AK47`, `GLOCK`, `KNIFE`, … (16 weapons) | 49 | `[taskType2]` |
| `DRAGUNOV`, `MP5SD`, `RPG18`, `T80`, `APC`, `MEDIPACK` | 50 | 1 extra + `taskType2` |
| `M16A2`, `BINOCULARS` | 52 | 3 extras + `taskType2` |
| `PROXIMITYMINE` | 53 | 4 extras + `taskType2` |

So `WeaponTypeConfig::Tail()` returns the arguments after the 48 fixed ones
verbatim and the configurator does not guess where `taskType2` begins. Guessing
would silently relabel a task argument as a second task type.

---

## 3. Ammunition slots

There are two distinct "slot assignments", and both are the engine's.

### 3.1 The player's global slots — `humanplayer/humanplayer.qvm`

```
DefineHumanPlayerWeaponCycle   HOOK 0x4101C0   src/humanplayer/humanplayer.c:1149-1160
    gWeaponCycleCount = 0;
    for (i = 0; i < *args; i++)
        gWeaponCycle[gWeaponCycleCount++] = Script_GetArgInt(args, i);

DefineHumanPlayerAmmoLimit     HOOK 0x4102D0   src/humanplayer/humanplayer.c:1178-1193
    gAmmoLimitCount = 0;
    for (i = 0; i < *args; gAmmoLimitCount++) {
        l = &gAmmoLimits[gAmmoLimitCount];
        l->id    = Script_GetArgInt(args, i++);
        l->limit = Script_GetArgInt(args, i++);
    }
```

`*args` is the CALL's argument count, so the cycle has no fixed arity: every
argument is one slot. The ammo limits are `(id, limit)` **pairs**, so an odd
count makes the engine read one argument past the end — the editor refuses that
input (`AmmoLimits::ok == false`) instead of reproducing the overread. The limits
are applied by `AmmoList_SetLimit(h->ammo, id, limit)` per entry
(`humanplayer.c:1219-1220`).

`source/runtime/weapon_config.{h,cpp}` parses and emits both; the corpus test
compares the parsed cycle against `WeaponSystem::GetVanillaPlayerWeaponCycle()`
and the shipping `humanplayer.qvm`.

### 3.2 Per-level ammunition — `AddAmmo`

`AddAmmo` is the authored way to give the **player** ammunition at level start:

```
AddAmmo_Init   HOOK 0x46DB40   src/comprun/pickup.c:766-780
    ParamTable_New(2)
    ParamTable_Add(p.ammoName, Param_EnumString32(), 7, AmmoType_GetEnum())
    ParamTable_Add(&p.count,   Param_Int16(),        7, NULL)
AddAmmo_Run    HOOK 0x46DC40   pickup.c:790-798
    if (TaskType_IsA(((Task *)h)->type, Human_GetType()) && AmmoType_Find(t->ammo))
        AmmoList_Add(h + 0x340, t->ammo, t->count);
    Task_Delete(t);
```

and the retail data authors it, as a child of the `HumanPlayer` task:

```
MISSIONS/location0/LEVEL2/objects.qsc:36
    Task_DeclareParameters("AddAmmo", "ID", "EnumString32", "Ammo", "Int16");
MISSIONS/location0/LEVEL2/objects.qsc:1074       the HumanPlayer task
MISSIONS/location0/LEVEL2/objects.qsc:1080
    Task_New(-1, "AddAmmo", "", "AMMO_ID_919", 49)
```

**The bug this fixed.** `GetSchema("AddAmmo")` returned nullptr — the type had no
row in `source/level/task_schema.cpp` — and `renderer_draw.cpp:2327-2335` skips a
child whose schema is null. So the panel showed nothing at all for the player's
authored starting ammunition. The row is now derived from
`igi::PickupLayouts()` (`source/runtime/pickup_rules.cpp:75-84`), the same
"derive from the ported table" pattern `Smoke`, `AlarmLight` and `SplineObj`
already use.

A comment in `source/runtime/pickup_rules.h` also claimed `AddAmmo` is "never
authored into a level". That is false and the retail file above says so; it is
corrected. What remains true is that `AddAmmo` is not a *placed pickup* — no
`Position` parameter, no touch handler — which is all `IsPlacedPickupType`
decides, and that predicate is unchanged.

---

## 4. Item drops into the level

The three wire formats are the engine's and all three round-trip to the level
file:

| task type | init | parameters |
| --- | --- | --- |
| `GunPickup` | `GunPickup_Init` HOOK `0x46CB70`, `comprun/pickup.c:460-481` | `Position` ObjectPos, `Orientation` Real32x9, `ID` EnumString32 (`WeaponType_GetIdEnum`) |
| `AmmoPickup` | `AmmoPickup_Init` HOOK `0x46D420`, `pickup.c:642-664` | the same three plus `Ammo` Int16 |
| `AddAmmo` | `AddAmmo_Init` HOOK `0x46DB40`, `pickup.c:766-780` | `ID` EnumString32 (`AmmoType_GetEnum`), `Ammo` Int16 |

`GenericPickup` is not in this list: it has no weapon or ammo parameter
(`pickup.c:268-271`) and its touch handler delivers no message at all
(`pickup.c:361-366`), so it cannot carry an inventory item.

---

## 5. What is editor tooling, not parity

"The verb *drop*" has no engine oracle. `IGI.exe` has no notion of an author
adding a task to a level; it only loads what is there. The retail editor's own
manual is the artifact that defines the action, and it does so as **task-tree
operations**:

| QED.doc keystroke | action |
| --- | --- |
| `Insert` | Create new task with active task as its parent |
| `Shift Insert` | Create new task with active task as its parent at camera's position |
| `Ctrl Insert` | Create new task as first child of active task |
| `Ctrl Shift Insert` | Create new task as first child of active task at camera's position |
| `Ctrl C` / `Shift Ctrl C` | Copy task to clipboard / copy task with children |
| `Ctrl V` | Paste task from clipboard (create as child of active task) |

So a "drop an inventory item here" command is *retail-editor behaviour*: create
the task as a child of the active task, or paste it from the clipboard. It must
be labelled editor tooling in the UI, not described as engine parity.

The editor already has `App::PasteTask()` (`source/app_editor.cpp:1860-1956`)
for `Ctrl V`. It has no `Ctrl C` copy and no Insert-key create, so the retail
manual's create/paste set is only partially implemented.

QED.doc documents **no weapon dialogue**. Its only animation keys are
`F8`/`Shift F8` (AnimTask keyframe interpolation) and `F12`/`Shift F12` (AnimTask
speed), and its only task-creation keys are the ones above. There is therefore no
retail-editor precedent for a weapon parameter panel; building one is new UI, not
a port.

---

## 6. What was implemented

| file | change |
| --- | --- |
| `source/runtime/weapon_config.h` | NEW — the `DefineWeaponType` parameter table, `CallArg`/`CallArgs`, `WeaponTypeConfig`, the cycle and ammo-limit tables, `IsInventoryDropTaskType` |
| `source/runtime/weapon_config.cpp` | NEW — the table data, the QVM call-argument reader, parse/emit for all four lists |
| `source/level/task_schema.cpp` | `GunPickup` / `AmmoPickup` / `AddAmmo` rows derived from `igi::PickupLayouts()`; `AddAmmo` had none |
| `source/runtime/pickup_rules.h` | the false "never authored into a level" claim corrected |
| `source/weapon_system.{h,cpp}` | `WeaponDefinition::engine_damage` (the retail arg-10 value), the catalog's 19 rows filled and checked, the wrong "ammo QVM damage table" comment corrected, catalog/cycle accessors made public so a UI can enumerate them |
| `tests/test_weapon_config.cpp` | NEW — 16 unit tests + 4 corpus tests |
| `CMakeLists.txt` | the new test file and the new source registered; `igi_mcp` now links `source/runtime/pickup_rules.cpp` because `task_schema.cpp` uses it |

The corpus tests read the shipping `WEAPONS/<NAME>/WEAPON.QVM` and
`humanplayer/humanplayer.qvm` (through `IGI_GAME_PATH`) and assert every field the
runtime catalog claims — fire rate, clip size, burst, pellets, throw speed,
range, **damage**, both spread values, both sounds, the id, the script id and the
model — against what the engine would actually load, for all 19 weapons.

---

## 7. What is NOT done

* **The panel itself.** There is no weapon-parameter GUI. The property panel is
  driven by `TaskSchema` rows for a selected *level object*, and a weapon type is
  not a level object, so showing one needs a new surface (a weapon list plus a
  detail panel). That surface cannot be verified on this host: the editor needs
  OpenGL 4.1 and the host offers 2.1, so nothing was rendered and no claim about
  appearance is made.
* **Writing `WEAPONS/*/weapon.qsc`.** The emitter produces the argument vector
  and the editor has both a QSC compiler (`source/level/qvm_compiler.cpp`) and a
  QVM writer, but no code path writes a weapon file back. The round-trip is
  proved at the token level, not on disk.
* **Clip-size-driven reserve ammunition.** `WeaponSystem::ReserveForWeapon`
  (`source/weapon_system.cpp:70-93`) is a hand-written per-weapon reserve table
  with no retail source; it is the editor's playtest value and is not covered by
  the corpus test.
* **`taskType2` positioning**, for the reason in §2.4.
* **`Ctrl C` copy / `Insert` create** in the task tree (§5).

---

## 8. Reproducing the evidence

```bash
# the host-portable suite (corpus tests need a retail install)
IGI_GAME_PATH=/path/to/IGI bash scripts/run-host-tests.sh

# only this suite
IGI_HOST_SUITES=test_weapon_config IGI_GAME_PATH=/path/to/IGI bash scripts/run-host-tests.sh
```

To see the raw argument vector of any shipping weapon without building the
editor, `tests/test_weapon_config.cpp`'s corpus tests print it on failure; the
`CallArgs::args` vector is the same data.
