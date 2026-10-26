# Weapon / ammo pickup parity (#75)

Status: **audit + extracted rule module, not wired.**

* Audit: this document.
* Extracted, tested retail rules: `source/runtime/pickup_rules.{h,cpp}`
  (**no editor code calls it**), tests in `tests/test_pickup_rules.cpp`
  (14 unit tests + 5 retail-corpus tests).
* The editor's live pickup paths (`source/level/task_schema.cpp`,
  `source/level/level_objects.cpp`, `source/app.cpp`,
  `source/weapon_system.cpp`) were read and are cited below, but **not
  modified** — they are owned elsewhere. Two divergences found there are
  reported in §8 rather than fixed here.

Every claim about engine behaviour is cited to IGI-RE
(`/Users/haseeb-mir/Documents/Code/IGI-RE`) as function + address + file:line.
open-igi (C#) is not an authority and was not consulted. No frame was rendered:
the editor needs GL 4.1+ and this host offers GL 2.1, so **nothing visual is
verified here**.

---

## 1. The issue's two addresses

The triage entry for #75 names `0x46C630` and `0x46D230`. Both resolve in
`symbols/names.tsv`:

| Line | Address | Symbol | Source |
|---|---|---|---|
| `names.tsv:2154` | `0x46C630` | `GenericPickup_Touch` | `comprun/pickup.c:361-366` |
| `names.tsv:2167` | `0x46D230` | `GunPickup_Touch` | `comprun/pickup.c:599-616` |

Both live in `src/comprun/pickup.c`, which reconstructs the whole pickup family.
`pickup.c:1` carries `#pragma GCC optimize("excess-precision=standard")`, which
matters: every float expression in the file is rounded to float, while the
`long double` locals really are x87 extended precision.

| Symbol | Address | File:line |
|---|---|---|
| `GenericPickup_Init` | `0x46C0C0` | `comprun/pickup.c:250-273` |
| `GenericPickup_Create` | `0x46C2B0` | `comprun/pickup.c:291-304` |
| `GenericPickup_Run` | `0x46C3C0` | `comprun/pickup.c:306-314` |
| `GenericPickup_Destroy` | `0x46C410` | `comprun/pickup.c:316-323` |
| `GenericPickup_GetParams` | `0x46C480` | `comprun/pickup.c:325-330` |
| `GenericPickup_Changed` | `0x46C490` | `comprun/pickup.c:332-347` |
| `GenericPickup_Register` | `0x46C570` | `comprun/pickup.c:349-353` |
| `GenericPickup_Unregister` | `0x46C5D0` | `comprun/pickup.c:355-359` |
| `GenericPickup_Touch` | `0x46C630` | `comprun/pickup.c:361-366` |
| `GenericPickup_Show` | `0x46C750` | `comprun/pickup.c:368-376` |
| `GenericPickup_Hide` | `0x46C8A0` | `comprun/pickup.c:378-386` |
| `GunPickup_GetEvent` | `0x46C970` | `comprun/pickup.c:397-401` |
| `GunPickup_New` | `0x46C980` | `comprun/pickup.c:404-449` |
| `GunPickup_Init` | `0x46CB70` | `comprun/pickup.c:460-481` |
| `GunPickup_Create` | `0x46CD20` | `comprun/pickup.c:505-521` |
| `GunPickup_Run` | `0x46CEA0` | `comprun/pickup.c:523-558` |
| `GunPickup_GetParams` | `0x46D040` | `comprun/pickup.c:560-565` |
| `GunPickup_Changed` | `0x46D050` | `comprun/pickup.c:567-585` |
| `GunPickup_Register` | `0x46D170` | `comprun/pickup.c:587-591` |
| `GunPickup_Unregister` | `0x46D1D0` | `comprun/pickup.c:593-597` |
| `GunPickup_Touch` | `0x46D230` | `comprun/pickup.c:599-616` |
| `GunPickup_Close` | `0x46D3E0` | `comprun/pickup.c:618-624` |
| `AmmoPickup_GetEvent` | `0x46D410` | `comprun/pickup.c:630-632` |
| `AmmoPickup_Init` | `0x46D420` | `comprun/pickup.c:642-664` |
| `AmmoPickup_Create` | `0x46D5F0` | `comprun/pickup.c:666-678` |
| `AmmoPickup_Run` | `0x46D740` | `comprun/pickup.c:680-685` |
| `Pickup_Destroy` | `0x46D760` | `comprun/pickup.c:688-695` |
| `AmmoPickup_GetParams` | `0x46D7C0` | `comprun/pickup.c:697-702` |
| `AmmoPickup_Changed` | `0x46D7D0` | `comprun/pickup.c:704-720` |
| `AmmoPickup_Register` | `0x46D8D0` | `comprun/pickup.c:722-726` |
| `AmmoPickup_Unregister` | `0x46D930` | `comprun/pickup.c:728-732` |
| `AmmoPickup_Touch` | `0x46D990` | `comprun/pickup.c:734-749` |
| `AmmoPickup_Close` | `0x46DB10` | `comprun/pickup.c:751-757` |
| `AddAmmo_Init` | `0x46DB40` | `comprun/pickup.c:766-780` |
| `AddAmmo_Create` | `0x46DC20` | `comprun/pickup.c:782-788` |
| `AddAmmo_Run` | `0x46DC40` | `comprun/pickup.c:790-798` |
| `AddAmmo_GetParams` | `0x46DCA0` | `comprun/pickup.c:800-805` |
| `AddAmmo_Changed` | `0x46DCB0` | `comprun/pickup.c:807-813` |
| `AddAmmo_Close` | `0x46DCE0` | `comprun/pickup.c:815-820` |

Static helpers, not symbols but the whole of the behaviour:

| Helper | File:line |
|---|---|
| `radius()` | `comprun/pickup.c:172-177` |
| `touches()` | `comprun/pickup.c:180-200` |
| `register_flag()` | `comprun/pickup.c:203-213` |
| `unregister_flag()` | `comprun/pickup.c:215-225` |
| `generic_add()` / `generic_remove()` | `comprun/pickup.c:275-289` |
| `pickup_create()` / `pickup_move()` | `comprun/pickup.c:484-503` |

---

## 2. What a pickup *is*

Four task types, three of them placeable:

| Type | Size | Base type | File:line |
|---|---|---|---|
| `GenericPickup` | `0x120` | `SectorInteriorObject` | `comprun/pickup.c:86-95, 254` |
| `GunPickup` | `0x158` | `SectorInteriorObject` | `comprun/pickup.c:97-110, 464` |
| `AmmoPickup` | `0x138` | `SectorInteriorObject` | `comprun/pickup.c:112-123, 646` |
| `AddAmmo` | `0x48` | `Task_GetRootType()` | `comprun/pickup.c:125-132, 770` |

`AddAmmo` is **not a placed object**: it has no `Position` and no `Orientation`,
and it is a plain child of a `Task`, used to hand ammo to its parent. It is
included here because it is the third path into the same ammo grant.

The declared parameter layout is the contract the editor's inspector and `.qsc`
writer must match. From `ParamTable_New`/`ParamTable_Add`:

| Type | Declared parameters | `Task_New` tokens |
|---|---|---|
| `GenericPickup` | `Position:ObjectPos`, `Orientation:Real32x9`, `Model:String16` | 10 |
| `GunPickup` | `Position:ObjectPos`, `Orientation:Real32x9`, `ID:EnumString32` | 10 |
| `AmmoPickup` | `Position:ObjectPos`, `Orientation:Real32x9`, `ID:EnumString32`, `Ammo:Int16` | 11 |
| `AddAmmo` | `ID:EnumString32`, `Ammo:Int16` | 5 |

* `GenericPickup_Init` — `ParamTable_Add` at `pickup.c:269-271`.
* `GunPickup_Init` — `pickup.c:477-479`; the enum table is
  `WeaponType_GetIdEnum()` `0x413DE0` (`pickup.c:479`).
* `AmmoPickup_Init` — `pickup.c:659-662`; the enum table is
  `AmmoType_GetEnum()` `0x47CF10` (`pickup.c:661`).
* `AddAmmo_Init` — `pickup.c:777-778`.

Argument-token counts come from the parameter type table's `ArgCount` slot
(`src/physobj/paramops.c:153-158`, `Param_One` / `Param_Three`):
`ObjectPos` and `Real32x9` consume three `.qsc` arguments each, the rest one.
`Param_ObjectPos_Parse` `0x4F1070` reads x/y/z; `ParamReal32x9_Parse` `0x4EFBA0`
reads the Euler triple.

The two enum domains are the constants the engine registers:
`ParamEnum_AddConstants(gWeaponIdEnum, "WEAPON_ID_.*")` at
`src/weapon/weapontype.c:269` and
`ParamEnum_AddConstants(gAmmoIdEnum, "AMMO_ID_.*")` at
`src/ammotype/ammotype.c:268`. So a valid `GunPickup` ID starts with
`WEAPON_ID_` and a valid `AmmoPickup`/`AddAmmo` ID starts with `AMMO_ID_`.

---

## 3. What makes a pickup touchable

`touches()` is the whole rule (`comprun/pickup.c:180-200`):

```c
static int touches(RigidDynCubeObj *o, Toucher *by)
{
    long double r = radius(o);
    double range = r;
    double dz;
    Vec3d d;

    if (!(r >= kMinTouch))
        range = kMinTouch;
    d.x = o->pos.x - by->pos.x;
    d.y = o->pos.y - by->pos.y;
    {
        long double z = o->pos.z - by->pos.z;

        if (z < kZero)
            z = -z;
        dz = z;
    }
    d.z = 0.0;
    return !(Vec3d_Length(&d) >= range) && !(dz >= kTouchHeight);
}
```

with

```c
static const double kMinTouch = 2867.2;      /* pickup.c:159 */
static const double kTouchHeight = 4096.0;   /* pickup.c:160 */
```

and `radius()` (`comprun/pickup.c:172-177`):

```c
static long double radius(RigidDynCubeObj *o)
{
    float s = max_scale(o);

    return VirModel_GetRadius(o->model) * s;
}
```

So the touch volume is an **infinite cylinder**, not a box and not a sphere:

1. **Horizontal reach** is `max(VirModel_GetRadius(model) * max_scale, 2867.2)`
   game units. 2867.2 / 4096 = **0.7 m**.
2. **Vertical half-height** is **4096 units = 1.0 m**, independent of the
   horizontal reach and of the model.
3. Both comparisons are written as negated `>=`, so a toucher **exactly at
   `range`**, or **exactly 4096 units above/below**, is **not** touching.
4. `d.z = 0.0` before `Vec3d_Length`, so the horizontal test is the true 2D
   distance (`x87_sqrt(x*x + y*y + z*z)` with `z = 0`,
   `src/math/vec3.c:41-45`), not a per-axis box.

Supporting citations:

* `VirModel_GetRadius` `0x4D0950` (`src/lod/mesh3d.c:381-387`) returns
  `lod_header(vm->lods[0])->radius`, and **`kOne` (1.0f) when `vm` is NULL**. A
  pickup whose model has not resolved therefore has radius `1.0 * scale` and is
  clamped up to 2867.2.
* `max_scale` is the largest of `scale[0..2]`
  (`src/editobj/editobj.h:41-46`, duplicated at `src/props/props.h:60-65`).
* `RigidDynCubeObj_Init` `0x4C5710` sets all three scales to `1.0f`
  (`src/magicobj/magicobj.c:340-342`), so an unscaled model uses its raw radius.
* `Toucher` is `{ Task *task; int unk4; Vec3d pos; }`
  (`src/comprun/comprun.h:17-21`); `by->pos` is the toucher's world position.
* The multiply in `radius()` is a **float** multiply because of the
  `excess-precision=standard` pragma at `pickup.c:1`; the result is widened to
  `long double` afterwards.

### NaN

The clamp is `if (!(r >= kMinTouch)) range = kMinTouch;`. A NaN `r` fails `>=`,
so a NaN radius **falls back to 2867.2** instead of producing a NaN range. The
module reproduces this deliberately and a unit test pins it.

### Where the toucher comes from

`Game_GetEvent1()` is the touch event slot (`pickup.c:264, 475, 659`). The engine
walks touching pairs and dispatches `GenericPickup_Touch` / `GunPickup_Touch` /
`AmmoPickup_Touch`. The **touch handler re-tests** `touches()` before acting
(`pickup.c:605-606`, `739-740`, `363`), so the range test is the gate, not an
advisory.

---

## 4. What a pickup grants

The message type is `PickupMsg { int id; int count; }` (`comprun/pickup.c:135-138`).

### `GenericPickup` — nothing

```c
void GenericPickup_Touch(GenericPickup *t, Toucher *by)
{
    if (touches(&t->obj, by))
        t->pickedUp = 1;
}
```

`comprun/pickup.c:361-366`. No message, no event dispatch, no sound. It is a
pure marker: it vanishes and publishes `isPickedUp`. **The editor must not offer
it a weapon or ammo field.**

### `GunPickup` — the weapon's *clip size*

```c
void GunPickup_Touch(GunPickup *t, Toucher *by)
{
    char buf[260];
    PickupMsg msg;
    TaskFn fn;

    if (!touches(&t->obj, by))
        return;
    msg.id = t->weapon;
    msg.count = t->ammo;
    fn = gTaskHandlers[gGunPickupEvent][by->task->type];
    if (fn)
        fn(by->task, &msg);
    t->pickedUp = 1;
    crt_sprintf(buf, "PICKEDUPWEAPON%s", WeaponType_Find(t->weapon) + 0x110);
    StatusMessageShow(Level_GetDynamic(), buf, gEmptyString, gEmptyString);
}
```

`comprun/pickup.c:599-616`. `t->weapon` and `t->ammo` are set by
`GunPickup_Changed` `0x46D050` (`comprun/pickup.c:567-585`):

```c
    t->weapon = Script_EvalExpr(t->weaponName);   /* pickup.c:573 */
    w = WeaponType_Find(t->weapon);               /* pickup.c:574 */
    if (!w)
        return;                                   /* pickup.c:576-577 */
    t->ground = t->pos;                           /* pickup.c:578 */
    m = VirModel_Find(w + 0x61C);                 /* pickup.c:579 */
    if (m)
        RigidDynCubeObj_SetModel(t, m);
    pickup_move(&t->obj, &t->pos);
    t->ammo = *(int *)(w + 0x648);                /* pickup.c:583 */
```

`WeaponType` (`src/weapon/weapontypes.h:18-50`) puts `key` — "model and physics
type name" — at **`0x61C`**, and the union `{ int unk640[4]; struct { int
pellets; int rate; int clipSize; int burst; }; }` at **`0x640`**, so `clipSize`
is at **`0x648`**.

> **A `GunPickup` grants the weapon's clip size, and there is no authored ammo
> count for `GunPickup` anywhere in the engine or in retail data.** The retail
> `Task_DeclareParameters` in all 12 levels that declare it confirms this: three
> parameters, no `Ammo`.

If `WeaponType_Find` fails, `GunPickup_Changed` returns **before** assigning
`t->ammo`, and `GunPickup_Create` already set `t->ammo = 0`
(`comprun/pickup.c:519`). The touch still delivers `{weapon id, 0}`. That is the
engine's behaviour for an enum the weapon table does not know — and it is also
what happens for the one retail `GunPickup` with an **empty** ID (§7).

`GunPickup_Create` (`comprun/pickup.c:505-521`) also seeds
`t->weapon = 12` (`pickup.c:517`) before any parameter arrives. 12 is
`WEAPON_ID_RPG18` in the retail table, but the value is only observable if the
authored ID never resolves.

**Latent engine bug, noted not ported:** the `StatusMessageShow` line does
`WeaponType_Find(t->weapon) + 0x110` with no NULL check (`pickup.c:614`). For an
unresolvable ID that is `NULL + 0x110`, i.e. a wild read at `0x110`. The editor
must not reproduce it, and the module exposes `weapon_found` so a caller can
avoid the same trap.

### `AmmoPickup` — the authored count

```c
void AmmoPickup_Touch(AmmoPickup *t, Toucher *by)
{
    PickupMsg msg;
    TaskFn fn;

    if (!touches(&t->obj, by))
        return;
    msg.id = t->ammo;
    msg.count = t->count;
    fn = gTaskHandlers[gAmmoPickupEvent][by->task->type];
    if (fn)
        fn(by->task, &msg);
    t->pickedUp = 1;
    StatusMessageShow(Level_GetDynamic(), "PICKEDUPAMMO", "STATUSSCREEN:ammo.spr", gEmptyString);
}
```

`comprun/pickup.c:734-749`. `t->ammo` is `Script_EvalExpr(t->ammoName)`
(`AmmoPickup_Changed` `0x46D7D0`, `comprun/pickup.c:710`) and `t->count` is the
authored `Int16` parameter (`AmmoPickup_Init`, `comprun/pickup.c:662`).

Unlike the gun case, **the touch does not consult `AmmoType_Find`**, so an
unknown `AMMO_ID_*` still delivers its authored count. Only the *model preview*
in `AmmoPickup_Changed` is gated on the ammo table resolving
(`comprun/pickup.c:712-714`).

### The `Int16` count truncates

`AmmoPickup::count` is `int16_t` at `0x130` (`comprun/pickup.c:121`, asserted at
`comprun/pickup.c:143`). `ParamInt16_Parse` `0x4F0110`
(`src/physobj/paramops.c:627-633`) is:

```c
void ParamInt16_Parse(int16_t *v, ParamCursor *c)
{
    ParamCursor_Need(c, 1);
    *v = Script_GetArgInt(c->args, c->index);   /* paramops.c:630 */
    c->index++;
}
```

A plain narrowing assignment with **no range check and no clamp**. An authored
count above 32767 therefore **wraps** rather than saturating: `40000` becomes
`-25536`. The module exposes this as `TruncatePickupCount` so the editor can warn
instead of silently writing a value the engine will reinterpret. No retail level
exercises this: over the 71 `AmmoPickup` and 27 `AddAmmo` instances the observed
ranges are `1..200` and `2..256` (§7).

### `AddAmmo` — a parent-human grant

```c
void AddAmmo_Run(AddAmmo *t)
{
    char *h = (char *)t->task.parent;

    if (TaskType_IsA(((Task *)h)->type, Human_GetType()) && AmmoType_Find(t->ammo))
        AmmoList_Add(h + 0x340, t->ammo, t->count);
    Task_Delete(t);
}
```

`comprun/pickup.c:790-798`. Note the asymmetry with `AmmoPickup_Touch`: here the
ammo id **must** resolve, and the parent **must** be a Human
(`Human_GetType` `0x460BE0`). The human's ammo list is at `Human + 0x340`
(`src/ammotype/ammotype.h:32-33`).

### The receiving end

The gun and ammo events are dispatched to the toucher's type handler
(`gTaskHandlers[event][by->task->type]`). On the human type
(`src/soldier/human.c:274-275`):

```c
TaskType_SetHandler(gHumanType, AmmoPickup_GetEvent(), WeaponAmmoPickup);
TaskType_SetHandler(gHumanType, GunPickup_GetEvent(), WeaponGunPickup);
```

* `WeaponAmmoPickup` `0x45FF80` (`src/humanbase/human.c:688-694`):
  ignores the message when the human is dead; otherwise
  `AmmoList_Add(h->ammo, msg[0], msg[1])` and `Sound_PlayDefined("weaponpickup_1", 0, 0)`.
* `WeaponGunPickup` `0x45FFC0` (`src/humanbase/human.c:698-728`): ignores death;
  walks the human's `Gun` children and, for the one whose id matches `msg[0]`,
  adds `msg[1]` to its ammo via `AmmoList_Add`; otherwise creates it with
  `Gun_New(h, 0, msg[0], msg[1], 1)`; then the same sound.

`GunPickup_GetEvent` is also what the `KEY_P` give-all cheat drives
(`src/game/game.c:626-639`), which is why the event exists at all.

**Ammo count is not "reserve" or "clip" in the abstract**: it is a per-calibre
counter in `AmmoList` (`AmmoList_Add` `0x47CE00`), and the clip is filled from it
by the reload path — out of scope here.

---

## 5. Lifecycle

### One-shot, no respawn

`pickedUp` is set by the touch handler (`pickup.c:364`, `612`, `746`) and
**nothing in `pickup.c` ever clears it**. `GenericPickup_Show`'s
`if (!t->inWorld && !t->pickedUp)` guard (`pickup.c:370`) is the only reader
outside the run handlers. There is no respawn timer anywhere in the file.

### Disposition after a successful touch

| Type | What happens | Citation |
|---|---|---|
| `GenericPickup` | `Sound_PlayDefined("pickup", 0, NULL)`, removed from the sector and the dynamic-cube world, `inWorld = 0`. **The task survives**, so `GenericPickup_Show` `0x46C750` can put it back. | `pickup.c:306-314`, `368-376` |
| `GunPickup` | `if (t->pickedUp) Task_Delete(t);` | `pickup.c:555-556` |
| `AmmoPickup` | `if (t->pickedUp) Task_Delete(t);` | `pickup.c:682-683` |
| `AddAmmo` | `Task_Delete(t)` unconditionally, after granting | `pickup.c:796` |

`Pickup_Destroy` `0x46D760` (`comprun/pickup.c:688-695`) is the shared delete
handler for `GunPickup` and `AmmoPickup`:

```c
QTaskList_Remove(Game_GetTaskList2(), t);
Sector_RemoveObject(t);
DynCube_Remove(Game_GetWorld(), t);
call_dac(&t->task);
```

**It does not call `unregister_flag`.** A collected gun or ammo pickup leaves its
`<type>_<id>.isPickedUp` symbol registered with a stale `pickedUp = 1`. That is
the engine's behaviour; a port should not "fix" it without saying so, because
level scripts may read the flag after the pickup is gone.

### The `GunPickup_Run` fall integrator

Not modelled — it is per-frame physics, and an *authored* `GunPickup` whose ID
resolves never moves:

```c
static const double kGravity = 0x1.64ce9ed57275ep+5;   /* pickup.c:164, = 44.6 */
static const double kBounce  = -0.5;                    /* pickup.c:163 */
static const double kRest    = 409.6;                   /* pickup.c:165 */
```

`GunPickup_Run` (`comprun/pickup.c:523-558`) bounces when
`!(t->pos.z >= t->ground.z)` (`pickup.c:525`), setting `pos.z = ground.z`,
reversing `fall` by `kBounce` and playing `weapondrop_01` or `weapondrop_02`
according to `Random_Float(...) > kHalf` (`pickup.c:530-535`). Otherwise, when
`!(t->pos.z > t->ground.z)` (`pickup.c:537`) and `|fall| < kRest`
(`pickup.c:542`), it takes `t->fall = 0.0; goto moved;` (`pickup.c:543-544`).
The integrator proper is `t->pos.z -= t->fall;` then `t->fall += kGravity;`
(`pickup.c:547-548`) - `kGravity` is positive, so the descent comes from the
subtraction, not the sign of the constant.

Because `GunPickup_Changed` sets `t->ground = t->pos` (`pickup.c:578`) once the
weapon resolves, an authored pickup starts with `pos.z == ground.z`: the bounce
test at `pickup.c:525` is false, the settle test at `pickup.c:537` is true,
`fall` is still 0 so `!(f >= kRest)` at `pickup.c:542` is true, and the body
jumps straight to `moved`. **A resolvable authored `GunPickup` never falls.** A pickup whose ID does *not*
resolve keeps `ground = (0,0,0)` from `GunPickup_Create` (`pickup.c:511-513`) and
does sink at ~44.6 units/frame² — a second reason the empty retail ID in §7 is
worth knowing about.

`GunPickup_New` `0x46C980` (`comprun/pickup.c:404-449`) is the *engine-dropped*
path (a soldier dies and drops its gun). It is the only user of the drop test
constants `kDropAbove = 2048.0` and `kDropBelow = 204800.0`
(`pickup.c:166-167`) and of `Coll_Lines` `0x4CC2A0`, and it is not reachable from
level data. Not ported.

---

## 6. The script flag

`register_flag` (`comprun/pickup.c:203-213`):

```c
if (*registered)
    return;
*registered = 1;
Task_GetName(name, t);
crt_sprintf(buf, "%s.isPickedUp", name);   /* pickup.c:211 */
Script_RegisterBool8(buf, flag);           /* pickup.c:212 */
```

The registered *value* is the task's own `pickedUp` byte (`pickup.c:350, 588,
723` pass `&t->pickedUp`), so the script symbol and the engine flag are the same
storage. `unregister_flag` (`pickup.c:215-225`) removes the same symbol via
`Script_RemoveSymbol` `0x4B80A0`.

`Task_GetName` `0x4F1270` (`src/param/task.c:123-135`):

```c
if (!HANDLER(gLevelNameEvent, task)) {
    int id = Task_GetId(task);

    crt_sprintf(out, "%s_%d", TaskType_GetName(task->type), id);   /* task.c:130 */
    return;
}
```

**No task type in IGI-RE ever registers a handler on `gLevelNameEvent`.** The
only writers of that slot are the alloc at `src/physobj/paramops.c:220`, the free
at `src/level/level.c:287` and the reset at `src/level/level.c:292`; the accessor
is `Level_GetNameEvent` at `src/param/task.c:89`. So the `"%s_%d"` fallback
**always** applies, and the flag is

```
<TypeName>_<task id>.isPickedUp
```

**The authored instance name is not part of the symbol**, even though retail
pickups frequently carry one. Measured over all 14 levels: of the **189** placed
pickups, **86 carry a non-empty authored name** and 103 are `""`. Examples:
`LEVEL6` "Shotgun shells" / "Law 80" / "C4", `LEVEL7` "Grenade pickup" /
"Medipak", `LEVEL10` "Law Pickup", `LEVEL11` "SniperPickup" / "SPASAmmo",
`LEVEL14` "9MM" / "Proximity Mine" / "Medikit ammo". Levels 1, 2, 3, 12 and 13
name none of theirs, which is why the `objects.qsc` cross-check in §7 only ever
sees `""`. The editor's current `"GunPickup_" + taskId + ".isPickedUp"` in
`source/app.cpp:2725-2727` is therefore right in *shape* but built by string
concatenation instead of through the module; see §8.

---

## 7. Retail corpus

All 14 levels of the installed EU release were parsed with the editor's own
`QVM_Parse` and cross-checked. Root: `$IGI_GAME_PATH`, default
`/Users/haseeb-mir/Downloads/IGI`.

### Declarations

**All 43 pickup declarations in all 14 levels match the IGI-RE `ParamTable`
layout exactly**, name and type, with no exceptions:

| Type | Levels declaring it | Count |
|---|---|---|
| `GenericPickup` | 2, 5, 6, 8, 11 | 5 |
| `GunPickup` | all but 4 and 9 | 12 |
| `AmmoPickup` | all but 4 | 13 |
| `AddAmmo` | all but 7 | 13 |

Every declaration is literally one of:

```
Task_DeclareParameters("GenericPickup", "Position","ObjectPos", "Orientation","Real32x9", "Model","String16")
Task_DeclareParameters("GunPickup",     "Position","ObjectPos", "Orientation","Real32x9", "ID","EnumString32")
Task_DeclareParameters("AmmoPickup",    "Position","ObjectPos", "Orientation","Real32x9", "ID","EnumString32", "Ammo","Int16")
Task_DeclareParameters("AddAmmo",       "ID","EnumString32", "Ammo","Int16")
```

This is the strongest single piece of evidence in the audit: the engine's
declared schema and the retail data agree exactly, so the editor's schema can be
pinned to the engine rather than to a guess.

### Instances

The suite prints this table (it is the test's own stdout, `PickupCorpusTest.
EveryPlacedPickupMatchesTheDeclaredLayout`):

```
  level  Generic  GunPickup  AmmoPickup  AddAmmo
      1        0          7           5        1
      2        1         11           8        1
      3        0          3           3        1
      4        0          0           0        3
      5        2          9           3        3
      6        2          5          10        1
      7        0          4           2        0
      8        2         10           1        1
      9        0          0           1        5
     10        0          3           4        3
     11        2         12           7        1
     12        0          2           2        1
     13        0         14           7        4
     14        0         29          18        2
  total        9        109          71       27
```

| Type | Total | Token count | Notes |
|---|---|---|---|
| `GenericPickup` | 9 | 10 in every level | grants nothing |
| `GunPickup` | 109 | 10 in every level | exactly one has an **empty** ID |
| `AmmoPickup` | 71 | 11 in every level | authored counts `1..200` |
| `AddAmmo` | 27 | 5 in every level | authored counts `2..256` |

Every weapon ID matches `WEAPON_ID_*` and every ammo ID matches `AMMO_ID_*`; the
suite asserts the prefix on each one. The weapon histogram over all 109
(`total=109 distinct=15`):

```
WEAPON_ID_GRENADE 40   WEAPON_ID_PROXIMITYMINE 16   WEAPON_ID_MEDIPACK 14
WEAPON_ID_FLASHBANG 10 WEAPON_ID_RPG18 8           WEAPON_ID_UZIX2 5
WEAPON_ID_M16A2 3      WEAPON_ID_MINIMI 3          WEAPON_ID_DRAGUNOV 2
WEAPON_ID_JACKHAMMER 2 WEAPON_ID_UZI 2             WEAPON_ID_COLT 1
WEAPON_ID_DESERTEAGLE 1 WEAPON_ID_SPAS12 1         (empty) 1
```

No retail `GunPickup` uses `WEAPON_ID_BINOCULARS` or `WEAPON_ID_KNIFE` — the two
weapons whose clip size the editor gets wrong (§8.3) — so that divergence is
latent rather than visible in shipped levels.

**No shipped count exceeds `int16` range** (`AmmoPickup` max 200, `AddAmmo` max
256, both asserted by the suite). The truncation described in §4 is therefore a
hazard for a *newly authored* value, not something retail data exercises.

The single empty ID is `LEVEL1` `objects.qsc:601`:

```
Task_New(333, "GunPickup", "", 24372076.0, -56227960.0, 174403984.0, 0, 0, 0, "")
```

`Script_EvalExpr` `0x4B8770` returns `0` when the expression does not parse
(`src/script/script.c:264-265`), and `WeaponType_Find(0)` finds nothing, so this
pickup delivers `{0, 0}` and its `GunPickup_Changed` leaves `ground = (0,0,0)` —
it sinks. The module reports `enum_present == false` for it rather than
pretending it is valid. (What `Expr_Parse` does with an empty reader, and hence
whether the value is `0` by the parse-failure path or by an empty expression, is
**unverified**; either way the weapon table does not contain it.)

### LEVEL1 and LEVEL2 versus the decompiled text

`LEVEL1`, `LEVEL2` and `LEVEL4` are the only levels shipping a decompiled
`objects.qsc` next to the binary. `LEVEL4` places no pickups at all (only its
`AddAmmo` declaration survives), so the binary↔text cross-check runs on `LEVEL1`
and `LEVEL2`: **every pickup token of every placed pickup matches** the
human-readable script, position and orientation floats included.

### Retail clip sizes (the `GunPickup` grant)

The retail weapon table is one `DefineWeaponType` `0x413F90`
(`src/weapon/weapontype.c:287-360`) call per `WEAPONS/<name>/WEAPON.QVM`, with
`clipSize` as argument 16 of 49 (the `unk640` loop at `weapontype.c:319-320`,
`clipSize` at `WeaponType + 0x648`). 19 of the 24 shipped `WEAPON.QVM` folders
are weapon types the editor models; the other five (`APC`, `M2HB`, `MIL`,
`SENTRYGUN`, `T80`) are vehicle/gun-holder entries with a different argument-block
layout and are excluded rather than mis-parsed.

| Weapon | id | retail `clipSize` | editor `clip_capacity` | retail `key` (`+0x61C`) | retail `unk110` |
|---|---|---|---|---|---|
| `WEAPON_ID_GLOCK` | 1 | 17 | 17 | `117_01_1` | Glock 17 |
| `WEAPON_ID_DESERTEAGLE` | 3 | 10 | 10 | `106_01_1` | Desert Eagle |
| `WEAPON_ID_M16A2` | 4 | 20 | 20 | `108_01_1` | M16 A2 |
| `WEAPON_ID_AK47` | 5 | 30 | 30 | `100_01_1` | AK47 |
| `WEAPON_ID_UZI` | 6 | 32 | 32 | `111_01_1` | Uzi |
| `WEAPON_ID_MP5SD` | 7 | 32 | 32 | `103_01_1` | Mp5 SD3 |
| `WEAPON_ID_SPAS12` | 8 | 7 | 7 | `109_01_1` | Spas 12 |
| `WEAPON_ID_JACKHAMMER` | 9 | 12 | 12 | `125_01_1` | Jackhammer |
| `WEAPON_ID_MINIMI` | 10 | 60 | 60 | `107_01_1` | Minimi |
| `WEAPON_ID_DRAGUNOV` | 11 | 10 | 10 | `105_01_1` | Dragunov |
| `WEAPON_ID_RPG18` | 12 | 1 | 1 | `110_01_1` | LAW 80 |
| `WEAPON_ID_UZIX2` | 13 | 64 | 64 | `111_04_1` | Uzi x 2 |
| `WEAPON_ID_GRENADE` | 14 | 1 | 1 | `135_01_1` | Hand Grenade |
| `WEAPON_ID_FLASHBANG` | 15 | 1 | 1 | `137_01_1` | FlashBang |
| `WEAPON_ID_PROXIMITYMINE` | 16 | 1 | 1 | `136_01_1` | Proximitymine |
| `WEAPON_ID_BINOCULARS` | 18 | **10** | **1** | `102_01_1` | Binoculars |
| `WEAPON_ID_MEDIPACK` | 19 | 1 | 1 | `121_01_1` | Medipack |
| `WEAPON_ID_KNIFE` | 20 | **-1** | **1** | `133_01_1` | Knife |
| `WEAPON_ID_COLT` | 21 | 6 | 6 | `101_01_1` | Colt Anaconda |

The editor's `model_id` equals the retail `key` and its `name` equals retail
`unk110` for all 19 — i.e. the editor already stores exactly the two fields
`GunPickup_Changed` uses (`VirModel_Find(w + 0x61C)`) and `GunPickup_Touch`
shows (`WeaponType_Find(...) + 0x110`). Two clip sizes disagree; see §8.

### The clip-size rule is visible in the level data

`LEVEL1` pairs `GunPickup WEAPON_ID_UZI` with `AmmoPickup AMMO_ID_919 = 32`, and
the retail Uzi clip size is 32 — the two agree because the designer matched them.
They are **not** generally equal: `LEVEL1`'s `WEAPON_ID_MINIMI` (clip 60) sits
next to `AMMO_ID_556 = 100`, and `WEAPON_ID_MEDIPACK` (clip 1) next to
`AMMO_ID_MEDIPACK = 50`. That is exactly the difference the module encodes: a
`GunPickup`'s grant is *derived* from the weapon table, an `AmmoPickup`'s grant
is *authored*.

---

## 8. Divergences in the editor (reported, not fixed here)

These files are outside this task's write scope.

### 8.1 `source/level/task_schema.cpp` mislabels the pickup enum parameter

```
source/level/task_schema.cpp:155-162   GenericPickup/GunPickup: Position, Orientation, Model:String16
source/level/task_schema.cpp:204-209   AmmoPickup: Position, Orientation, Model:String16, Ammo:Int16
```

IGI-RE declares `GunPickup`'s third parameter as **`ID` / `EnumString32`
populated from `WeaponType_GetIdEnum`** (`comprun/pickup.c:479`) and
`AmmoPickup`'s third as **`ID` / `EnumString32` from `AmmoType_GetEnum`**
(`comprun/pickup.c:661`). The *argument index* (`@9`) is correct; the parameter
**name and type are wrong**, which is what makes the inspector offer a free-text
model field where the engine expects a `WEAPON_ID_*`/`AMMO_ID_*` enum.

Impact: cosmetic today, because `source/level/level_objects.cpp:104-126` resolves
the token at `@9` through `ResolvePickupModelId` anyway and
`source/level/level_objects_serialize.cpp:248-260, 420-428` preserves
`argTokens[9]` verbatim. But a schema-driven UI cannot warn about
`Task_New(333, "GunPickup", ..., "")`, and `CheckPickupArgs` is exactly the
missing check.

### 8.2 `source/app.cpp` invents an `AmmoPickup` count from the active weapon

```
source/app.cpp:2723-2737   GunPickup: SelectWeaponByScriptId + the flag
source/app.cpp:2739-2759   AmmoPickup: count, then AddReserveAmmo
source/app.cpp:2291-2302   set_boolean builds "<type>_<id>.<field>"
source/app.cpp:2318-2322   those flags are initialised false
```

Three observations, in decreasing order of severity:

1. **The `clip_capacity` fallback has no basis in IGI-RE.** `source/app.cpp:2740`
   seeds `rounds` from `GetActiveWeapon().clip_capacity` and only overwrites it
   from `argTokens[10]` when that token exists and parses
   (`source/app.cpp:2741-2748`, with the comment "Preserve the clip-sized
   fallback for malformed authored counts"). `AmmoPickup`'s count is the authored
   `Int16` parameter (`comprun/pickup.c:662`); it is never the clip size of
   whatever weapon the player happens to be holding, and `AmmoPickup_Touch`
   consults no weapon at all. An `AmmoPickup` with no `Ammo` token is malformed,
   and the engine would have stored whatever `Script_GetArgInt` returned for the
   absent argument - not a weapon-dependent value. `CheckPickupArgs` reports this
   case as `count_ok == false`.
2. **The editor publishes an extra `GenericPickup_<id>.isPickedUp` flag for
   `GunPickup` and `AmmoPickup`** (`source/app.cpp:2728-2730`, `2753-2755`, and
   the init at `2318-2322`). IGI-RE registers only `<TypeName>_<id>.isPickedUp`:
   a `GunPickup` registers `GunPickup_<id>.isPickedUp` and never a
   `GenericPickup_` one (`comprun/pickup.c:588`, `723`). A level script that read
   `GenericPickup_<id>.isPickedUp` for a gun would see `true` in the editor and
   `false` in the engine.
3. The flag *naming* itself is right: `set_boolean` builds
   `task_type + "_" + task_id + "." + field` (`source/app.cpp:2299-2301`), which
   is exactly `Task_GetName`'s `"%s_%d"` fallback plus the `.isPickedUp` suffix
   (section 6). No change needed there - but it should come from
   `PickupScriptFlagName` so the two cannot drift.

The `GunPickup` path is otherwise faithful in shape: it calls
`SelectWeaponByScriptId` (`source/app.cpp:2724`), and
`RestoreOrInitializeAmmoState` (`source/weapon_system.cpp:291-293`) fills the clip
from `clip_capacity` - which is the editor's stand-in for the engine's `clipSize`
grant. That makes 8.3 directly live rather than theoretical.

### 8.3 Two clip sizes disagree with retail

`source/weapon_system.cpp:196-197` sets `WEAPON_ID_BINOCULARS` to
`clip_capacity = 1`; retail is **10**. `source/weapon_system.cpp:202-203` sets
`WEAPON_ID_KNIFE` to `1`; retail is **-1**. Because a `GunPickup` grants
`clipSize` verbatim, a `WEAPON_ID_BINOCULARS` `GunPickup` would grant 10 in the
engine and 1 in the editor. The `KNIFE` case cannot even be represented:
`WeaponDefinition::clip_capacity` is `uint32_t`, so retail's `-1` would become
`4294967295`.

Both are latent; no retail level places a `GunPickup` of either weapon (the
histogram in section 7 has no `BINOCULARS` and no `KNIFE`). A corpus test asserts
both mismatches by name so they cannot change silently.

---

## 9. Not modelled

* The `GunPickup_Run` gravity/bounce integrator and the `GunPickup_New`
  `Coll_Lines` drop test (§5) — per-frame physics and an engine-only path.
* Rendering of the pickup model. The editor resolves the enum to a model id in
  `source/level/level_objects.cpp`; the module only says *which* field the engine
  would use (`WeaponType + 0x61C`, `AmmoType + 0x148`, or the authored `String16`
  for `GenericPickup`).
* `AmmoList_Add` / `Gun_New` and the reload path that consumes the granted ammo.
* The `KEY_P` give-all cheat (`src/game/game.c:626-639`), which reuses
  `GunPickup_GetEvent` but is not a pickup.

---

## 10. Module and tests

* `source/runtime/pickup_rules.h` — constants, `PickupKind`, `PickupParamType`,
  `PickupLayout` tables, `PickupTouchRange`, `PickupTouches`,
  `GunPickupGrant`/`AmmoPickupGrant`/`GenericPickupGrant`, `AddAmmoApplies`,
  `PickupDispositionFor`, `PickupIsOneShot`, `PickupScriptFlagName`,
  `CheckPickupArgs`, `TruncatePickupCount`.
* `source/runtime/pickup_rules.cpp` — the implementation; every branch carries
  the `pickup.c` line it mirrors.
* `tests/test_pickup_rules.cpp` — 14 `PickupRulesTest` unit tests and 5
  `PickupCorpusTest` retail tests.

Run:

```bash
IGI_HOST_NO_CACHE=1 IGI_HOST_TESTS=tests/test_pickup_rules.cpp bash scripts/run-host-tests.sh
```

Result: **19 tests, 19 passed, 0 failed, exit 0**; 0 skipped, so the retail
corpus at `/Users/haseeb-mir/Downloads/IGI` was actually read (the suite
`GTEST_SKIP`s with an `IGI_GAME_PATH` hint when it is absent).

The full lane (`IGI_HOST_NO_CACHE=1 bash scripts/run-host-tests.sh`) exits **1**
in this shared worktree, but **not because of this change**: the five failing
suites are other agents' in-flight work in the same checkout —

| Suite | Failing | Cause |
|---|---|---|
| `test_menu_qvm` | 4 | `source/level/qvm_parser.cpp` is being rewritten (PUSHA/BLK/JSR opcode layout); the suite's hard-coded menu-QVM string/instruction counts are now stale (172 vs 139, 3006 vs 2225) |
| `test_menu_text_res` | 2 | same parser rewrite |
| `test_menu_ui` | 3 | same parser rewrite |
| `test_qvm_roundtrip` | 1 | the same agent's own suite, mid-edit |
| `test_sound_rules` | 5 | a brand-new untracked suite (`source/runtime/sound_emitter.cpp`) from a third agent |

`test_pickup_rules` reports `PASS test_pickup_rules 19 passed 0 failed (19
tests, exit 0)` inside that same run. This change touches only
`source/runtime/pickup_rules.{h,cpp}`, `tests/test_pickup_rules.cpp`,
`docs/igi-re-parity/PICKUPS.md` and two additive lines in `CMakeLists.txt`; it
does not link against or call the QVM parser rewrite or the sound emitter.

No editor code calls the module yet.

---

## 11. What remains unverified

* **Nothing visual.** The editor needs GL 4.1+ and this host offers GL 2.1, so
  no pickup was rendered and no in-game touch was performed. All 19 tests are
  host-lane pure-logic and corpus tests.
* The Windows `igi_tests` suite was not run for this change; the host lane is
  the inner loop, not the gate (`AGENTS.md`).
* The exact value `Script_EvalExpr("")` returns for `LEVEL1`'s empty ID — only
  that the parse-failure path yields `0` (`src/script/script.c:264-265`) and that
  no weapon has id 0.
* The `Toucher::pos` origin actually used at runtime (centre vs feet) was not
  traced; the module takes the position as an argument and does not assume.
* `WeaponGunPickup`'s behaviour for a **negative** ammo count (the retail
  `KNIFE` case) was not traced into `Gun_New` `0x45C0xx`; the module reports the
  engine value and stops there.
* The five vehicle/gun-holder `WEAPON.QVM` folders (`APC`, `M2HB`, `MIL`,
  `SENTRYGUN`, `T80`) were excluded from the clip-size comparison, so their
  argument layout is unverified.
