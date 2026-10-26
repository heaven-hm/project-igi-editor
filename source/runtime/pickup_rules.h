// pickup_rules.h - what a placed weapon/ammo pickup IS, the way IGI-RE treats it.
//
// Issue #75 ("weapon/ammo pickup parity") cites 0x46C630 and 0x46D230. Both
// resolve in IGI-RE's symbols/names.tsv:
//
//   0046c630  GenericPickup_Touch  manual  entry
//   0046d230  GunPickup_Touch      manual  entry
//
// and both live in src/comprun/pickup.c, which also reconstructs AmmoPickup and
// the AddAmmo child task. This header ports only the rules a LEVEL EDITOR needs
// in order to represent and preview a pickup the way the engine will treat it:
//
//   * the declared parameter layout of each pickup task (name + type + argument
//     count), because that is what the inspector and the .qsc writer must agree
//     on with the engine's ParamTable;
//   * the touch test: a horizontal cylinder of 2867.2 units (0.7 m) minimum
//     radius and 4096 units (1.0 m) half-height, whose radius is
//     VirModel_GetRadius(model) * max_scale unless that is larger;
//   * what the pickup GRANTS: nothing (GenericPickup), the weapon's clipSize
//     (GunPickup), or the authored Int16 (AmmoPickup);
//   * the one-shot lifecycle: `pickedUp` is set and never cleared, so a pickup
//     is consumed exactly once; GenericPickup is removed from the world while
//     GunPickup/AmmoPickup delete their task;
//   * the `<type>_<id>.isPickedUp` script flag the pickup publishes.
//
// Everything below is cited to IGI-RE as FUNCTION + ADDRESS + FILE:LINE. Where
// the reconstructed engine is ambiguous or leaves a value undefined, the
// divergence is named in a comment and in docs/igi-re-parity/PICKUPS.md rather
// than silently invented.
//
// Deliberately NOT ported here (see PICKUPS.md "Not modelled"):
//   * the GunPickup_Run gravity/bounce integrator (pickup.c:523-558) - it is a
//     per-frame physics simulation, not an editor-representable rule. An
//     AUTHORED GunPickup whose ID resolves never moves: GunPickup_Changed sets
//     `t->ground = t->pos` (pickup.c:578), so on the first frame
//     `!(t->pos.z > t->ground.z)` at pickup.c:537 is TRUE and the settle branch
//     runs; `fall` is still 0, so `!(f >= kRest)` at pickup.c:542 is also TRUE
//     and the body does `t->fall = 0.0; goto moved;` (pickup.c:543-544),
//     skipping the `pos.z -= fall` at pickup.c:547. A pickup whose ID does NOT
//     resolve keeps ground = (0,0,0) from GunPickup_Create (pickup.c:511-513)
//     and does sink;
//   * the rendering of the pickup (the editor already resolves the enum to a
//     model id in source/level/level_objects.cpp).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace igi {

// ---------------------------------------------------------------------------
// Engine constants (src/comprun/pickup.c:159-170)
// ---------------------------------------------------------------------------

// pickup.c:159  `static const double kMinTouch = 2867.2;`
// The smallest horizontal pickup radius, in game units: 0.7 m (2867.2 / 4096).
constexpr double kPickupMinTouchUnits = 2867.2;

// pickup.c:160  `static const double kTouchHeight = 4096.0;`
// The pickup cylinder's half-height: a toucher more than 1 m above or below the
// pickup cannot reach it. The engine's test is `!(dz >= kTouchHeight)`, so
// exactly 4096 units away is already too far.
constexpr double kPickupTouchHeightUnits = 4096.0;

// src/math/mathvars.h:6  `extern const float k4096; /* 4096 */`
// 4096 game units per metre, the scale the editor already uses elsewhere.
constexpr double kPickupUnitsPerMetre = 4096.0;

// VirModel_GetRadius 0x4D0950 (src/lod/mesh3d.c:381-387):
//     if (vm && vm->lods[0]) return lod_header(vm->lods[0])->radius;
//     return kOne;                                        /* mesh3d.c:88, = 1.0f */
// A pickup whose model never resolved (GunPickup_Changed's `if (m)
// RigidDynCubeObj_SetModel(...)` guard, pickup.c:580-581, and the same guard in
// AmmoPickup_Changed pickup.c:716-717) keeps `model == NULL` and therefore has
// radius 1.0, which the clamp below raises to 2867.2. This is the radius a
// caller must use when the model's MeshHeader.radius is unavailable, NOT a
// substitute for it.
constexpr float kPickupNoModelRadius = 1.0f;

// pickup.c:517  `t->weapon = 12;` in GunPickup_Create.
// A GunPickup's weapon id before its ID parameter is evaluated. 12 is
// WEAPON_ID_RPG18 in the retail weapon table (WEAPONS/RPG18/WEAPON.QVM,
// DefineWeaponType arg 0; src/weapon/weapontype.c:287-360). The value is only
// observable if the authored ID never resolves.
constexpr int kGunPickupDefaultWeaponId = 12;

// The count parameter is an Int16 field (pickup.c:121 `int16_t count;`,
// _Static_assert(offsetof(AmmoPickup, count) == 0x130, pickup.c:143).
// ParamInt16_Parse 0x4F0110 (src/physobj/paramops.c:627-633) is a plain
// narrowing assignment with NO clamp:
//     *v = Script_GetArgInt(c->args, c->index);   // paramops.c:630
constexpr int kPickupCountBits = 16;

// ---------------------------------------------------------------------------
// Declared parameter layout
// ---------------------------------------------------------------------------

// The IGI-RE parameter types the pickup ParamTables use. Each maps to a number
// of .qsc argument tokens; that count is what fixes every later argument index.
enum class PickupParamType {
    ObjectPos,        // Param_ObjectPos 0x4F1010 -> 3 args (x, y, z)
    Real32x9,         // Param_Real32x9  0x4F0F70 -> 3 args (the Euler triple)
    String16,         // Param_String16  0x4F0FD0 -> 1 arg
    EnumString32,     // Param_EnumString32 0x4F1E20 -> 1 arg
    Int16,            // Param_Int16     0x4F0FB0 -> 1 arg
};

// Argument-token count each type consumes, from its ArgCount slot in the
// parameter type table (src/physobj/paramops.c:153-158: Param_One / Param_Three).
int PickupParamArgCount(PickupParamType type);

// A single declared parameter, in declaration order.
struct PickupParam {
    const char* name;
    PickupParamType type;
};

// Which ParamTable an enum parameter is populated from, so the editor can offer
// the right value list.
enum class PickupEnumDomain {
    None,
    Weapon,  // WeaponType_GetIdEnum 0x413DE0 -> the WEAPON_ID_* constants
    Ammo,    // AmmoType_GetEnum     0x47CF10 -> the AMMO_ID_* constants
};

enum class PickupKind {
    GenericPickup,
    GunPickup,
    AmmoPickup,
    AddAmmo,
};

// The complete editor-visible description of one pickup task type.
struct PickupLayout {
    PickupKind kind;
    const char* task_type;      // the TaskType_New name
    const PickupParam* params;  // declaration order, exactly as ParamTable_Add
    int param_count;

    // Index into the Task_New argument tokens [0]=id, [1]=type, [2]=name,
    // [3]=first parameter. -1 when the type has no such argument.
    int enum_arg_index;   // the WEAPON_ID_*/AMMO_ID_* enum, when there is one
    int model_arg_index;  // a literal model name (GenericPickup only)
    int count_arg_index;  // the authored Int16, AmmoPickup/AddAmmo only

    PickupEnumDomain enum_domain;
    bool has_authored_count;

    // Total Task_New argument tokens, including the leading id/type/name:
    // 3 + the sum of every parameter's argument count.
    int ArgCount() const;
};

// The four pickup-family layouts, or nullptr for any other task type.
const PickupLayout* FindPickupLayout(const std::string& task_type);
// The four layouts in declaration order (GenericPickup, GunPickup, AmmoPickup,
// AddAmmo) - useful for a registry sweep in tests and in the schema builder.
const PickupLayout* PickupLayouts();
int PickupLayoutCount();

// ---------------------------------------------------------------------------
// The touch test
// ---------------------------------------------------------------------------

// `radius()` pickup.c:172-177:
//     float s = max_scale(o);
//     return VirModel_GetRadius(o->model) * s;
//
// VirModel_GetRadius 0x4D0950 (src/lod/mesh3d.c:381-387) returns the LOD0
// header radius, or kOne when there is no model at all - so a pickup whose
// model has not resolved yet still has radius 1.0 * scale, and is therefore
// clamped up to kPickupMinTouchUnits by the rule below.
//
// max_scale (src/editobj/editobj.h:41-46, src/props/props.h:60-65) is the
// largest of the three scale factors, and RigidDynCubeObj_Init 0x4C5710
// (src/magicobj/magicobj.c:340-342) initialises all three to 1.0.
//
// The multiply is a FLOAT multiply: pickup.c:1 carries
// `#pragma GCC optimize("excess-precision=standard")`, so the product is
// rounded to float before it is widened to the long double return type.
//
// The clamp is the engine's `if (!(r >= kMinTouch)) range = kMinTouch;`
// (pickup.c:187-188). Note the negated form: it is also taken when r is NaN,
// so a NaN radius falls back to 2867.2 rather than producing a NaN range.
float PickupTouchRange(float model_radius, float max_scale);

// `touches()` pickup.c:180-200:
//     long double r = radius(o);
//     double range = r;
//     if (!(r >= kMinTouch)) range = kMinTouch;
//     d.x = o->pos.x - by->pos.x;
//     d.y = o->pos.y - by->pos.y;
//     { long double z = o->pos.z - by->pos.z; if (z < 0) z = -z; dz = z; }
//     d.z = 0.0;
//     return !(Vec3d_Length(&d) >= range) && !(dz >= kTouchHeight);
//
// `pickup_pos` is the RigidDynCubeObj's position (obj+0x20), which DynCube_Move
// keeps in step with the authored position; `toucher_pos` is Toucher::pos
// (src/comprun/comprun.h:17-21). Both are in game units.
//
// `range` is the horizontal reach. It is clamped to kPickupMinTouchUnits here as
// well as in PickupTouchRange, so callers may pass either the raw
// VirModel_GetRadius*scale product or an already-clamped value; the clamp is
// idempotent.
//
// Both comparisons are negated `>=`, so a toucher EXACTLY at `range` or exactly
// 4096 units above/below is NOT touching. Vec3d_Length 0x4B3130
// (src/math/vec3.c:39-44) is an x87 sqrt of the sum of squares, so the
// horizontal test is on the true 2D distance, not a box.
//
// Precision: the engine stores the three differences into a `Vec3d` and a
// `double dz` (pickup.c:189-197), so each one is rounded to double before
// Vec3d_Length widens it back to long double. This port rounds the same way.
bool PickupTouches(double range, const double pickup_pos[3], const double toucher_pos[3]);

// Convenience: the two steps together, for the common case where the caller has
// the model radius and scale in hand.
bool PickupTouchesFromModel(float model_radius, float max_scale, const double pickup_pos[3],
                            const double toucher_pos[3]);

// ---------------------------------------------------------------------------
// What a pickup grants
// ---------------------------------------------------------------------------

// What a touched pickup hands to the toucher: PickupMsg pickup.c:135-138
// (`struct { int id; int count; }`), delivered through the gun/ammo pickup task
// event to the toucher's type handler.
struct PickupGrant {
    bool delivered = false;  // the engine's `if (fn)` guard
    int id = 0;
    int count = 0;
};

// GenericPickup_Touch 0x46C630 (pickup.c:361-366):
//     if (touches(&t->obj, by)) t->pickedUp = 1;
// A GenericPickup delivers NO message at all - it is a pure marker that vanishes
// and publishes isPickedUp. The editor must not offer it a weapon/ammo field.
PickupGrant GenericPickupGrant();

// GunPickup_Touch 0x46D230 (pickup.c:599-616):
//     if (!touches(&t->obj, by)) return;         // pickup.c:605-606
//     msg.id = t->weapon; msg.count = t->ammo;   // pickup.c:607-608
//     fn = gTaskHandlers[gGunPickupEvent][by->task->type];
//     if (fn) fn(by->task, &msg);                // pickup.c:610-611
//
// `t->weapon` comes from `Script_EvalExpr(t->weaponName)` (pickup.c:573) and
// `t->ammo` from `*(int *)(w + 0x648)` (pickup.c:583) in GunPickup_Changed
// 0x46D050 (pickup.c:567-585). WeaponType (src/weapon/weapontypes.h:18-73) puts
// `key` at 0x61C and the `clipSize` union member at 0x648, so:
//
//   **a GunPickup grants the weapon's CLIP SIZE, and there is no authored ammo
//   count for GunPickup anywhere in the engine or in retail data.**
//
// `weapon_found` is `WeaponType_Find(t->weapon) != NULL`. When it is false,
// GunPickup_Changed returns at pickup.c:576-577 before assigning ammo, and
// GunPickup_Create already set ammo = 0 (pickup.c:519) - so the touch still
// delivers {weapon id, 0}. This is the engine's behaviour for an enum the
// weapon table does not know.
PickupGrant GunPickupGrant(int weapon_id, int weapon_clip_size, bool weapon_found);

// The value GunPickup_Changed stores into `t->ammo` from a resolved weapon:
// `t->ammo = *(int *)(w + 0x648);` (pickup.c:583), i.e. WeaponType::clipSize.
// Retail clip sizes come from WEAPONS/<name>/WEAPON.QVM, DefineWeaponType
// 0x413F90 (src/weapon/weapontype.c:287-360), argument 16 of 49.
int GunPickupGrantedAmmo(int weapon_clip_size);

// AmmoPickup_Touch 0x46D990 (pickup.c:734-749):
//     if (!touches(&t->obj, by)) return;   // pickup.c:739-740
//     msg.id = t->ammo; msg.count = t->count;
// `t->ammo` is `Script_EvalExpr(t->ammoName)` (AmmoPickup_Changed 0x46D7D0,
// pickup.c:704-720) and `t->count` is the authored Int16 parameter
// (AmmoPickup_Init 0x46D420, pickup.c:659-662). Unlike the gun case the touch
// does NOT consult the ammo table, so an unknown AMMO_ID_* still delivers its
// authored count; the toucher's AmmoList_Add simply finds no matching entry.
PickupGrant AmmoPickupGrant(int ammo_id, int authored_count);

// The human side, for completeness - these are the handlers the pickup event
// is dispatched to, registered on the human type:
//
//   WeaponGunPickup  0x45FFC0 (src/humanbase/human.c:698-728)
//     ignores the message when the human is dead; adds `msg[1]` to the ammo of
//     an already-carried gun with the same id, otherwise creates the gun with
//     `Gun_New(h, 0, msg[0], msg[1], 1)`; plays "weaponpickup_1".
//   WeaponAmmoPickup 0x45FF80 (src/humanbase/human.c:687-694)
//     `AmmoList_Add(h->ammo, msg[0], msg[1])` + "weaponpickup_1"; ignores death.
//
// AddAmmo_Run 0x46DC40 (pickup.c:790-798) is the third path into the same
// grant: it checks the parent is a Human (`TaskType_IsA(parent->type,
// Human_GetType())`, Human_GetType 0x460BE0) AND that the ammo id resolves
// (`AmmoType_Find`), then `AmmoList_Add(parent + 0x340, ammo, count)` and
// deletes itself (pickup.c:794-796). A pickup preview therefore has three
// distinct grant sources.
bool AddAmmoApplies(bool parent_is_human, bool ammo_found);

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

enum class PickupDisposition {
    // GenericPickup_Run 0x46C3C0 (pickup.c:306-314): on the next run tick it
    // plays the "pickup" sound, removes itself from the sector and the dynamic
    // cube world, and sets inWorld = 0. The TASK survives: GenericPickup_Show
    // 0x46C750 (pickup.c:368-376) can put it back if it was never picked up.
    RemoveFromWorld,
    // GunPickup_Run 0x46CEA0 (pickup.c:555-556) and AmmoPickup_Run 0x46D740
    // (pickup.c:682-683) both `Task_Delete(t)`. Pickup_Destroy 0x46D760
    // (pickup.c:688-695) then unlinks the task, the sector object and the
    // dynamic cube. Note it does NOT unregister the isPickedUp symbol, so a
    // gun/ammo pickup leaves its flag behind after being collected.
    DeleteTask,
};

PickupDisposition PickupDispositionFor(PickupKind kind);

// Every pickup is one-shot: `pickedUp` is set by the touch handler
// (pickup.c:364, 612, 746) and nothing in pickup.c ever clears it.
// GenericPickup_Show's `!t->pickedUp` guard (pickup.c:370) is the only reader
// outside the run handlers. There is no respawn timer anywhere in the file.
bool PickupIsOneShot(PickupKind kind);

// ---------------------------------------------------------------------------
// Script flag
// ---------------------------------------------------------------------------

// register_flag() pickup.c:203-213, called from GenericPickup_Register 0x46C570
// (pickup.c:349-353), GunPickup_Register 0x46D170 (pickup.c:587-591) and
// AmmoPickup_Register 0x46D8D0 (pickup.c:722-726):
//     Task_GetName(name, t);                        // pickup.c:210
//     crt_sprintf(buf, "%s.isPickedUp", name);      // pickup.c:211
//     Script_RegisterBool8(buf, flag);              // pickup.c:212
// The registered value is the task's own `pickedUp` byte, so the script symbol
// and the engine flag are the same storage. unregister_flag() pickup.c:215-225
// removes the same symbol.
//
// Task_GetName 0x4F1270 (src/param/task.c:123-135) uses the level name event
// handler when the task's type has one, and otherwise the fallback
//     crt_sprintf(out, "%s_%d", TaskType_GetName(task->type), id);  // task.c:130
// No task type in IGI-RE ever registers a handler on Level_GetNameEvent
// (`gLevelNameEvent`, src/param/paramvars.h:7; accessor Level_GetNameEvent
// src/param/task.c:89). The only writers of that slot are the alloc in
// src/physobj/paramops.c:220 and the free/reset in src/level/level.c:287,292,
// so the fallback ALWAYS applies - even for the 86 of the 189 retail placed
// pickups that do carry an authored name (LEVEL6 "Shotgun shells", LEVEL10
// "Law Pickup", LEVEL11 "SniperPickup", LEVEL14 "9MM", ...). The flag is
// `<TypeName>_<task id>.isPickedUp`.
std::string PickupScriptFlagName(const std::string& task_type, int task_id);

// ---------------------------------------------------------------------------
// Validating an authored pickup
// ---------------------------------------------------------------------------

// What the editor's parsed argument tokens say about a pickup. `arg_tokens` is
// LevelObject::argTokens, i.e. [0]=task id, [1]=type, [2]=name, [3]=first
// parameter - the layout the .qsc lexer produces and the .qsc writer consumes.
struct PickupArgCheck {
    bool is_pickup = false;      // the type is one of the four
    bool token_count_ok = false; // exactly PickupLayout::ArgCount() tokens
    bool enum_present = false;   // the enum/model token is a non-empty string
    bool enum_known = false;     // it matches the type's expected prefix
    bool count_ok = false;       // the count token parses as an integer
    int count = 0;               // the parsed (int16-truncated) count
    int expected_tokens = 0;
};

// Checks the argument vector against the engine's declared layout. The enum
// prefix check is `WEAPON_ID_` for GunPickup and `AMMO_ID_` for AmmoPickup,
// matching the constants ParamEnum_AddConstants registers
// (src/weapon/weapontype.c:269 `"WEAPON_ID_.*"`, src/ammotype/ammotype.c:268
// `"AMMO_ID_.*"`) and every retail level's Task_DeclareParameters.
PickupArgCheck CheckPickupArgs(const std::string& task_type,
                               const std::vector<std::string>& arg_tokens);

// The engine stores the AmmoPickup/AddAmmo count through ParamInt16_Parse
// 0x4F0110 (src/physobj/paramops.c:627-633), which is
// `*v = Script_GetArgInt(c->args, c->index);` (paramops.c:630) with no range
// check. The conversion to int16_t therefore TRUNCATES; this exposes it so the
// editor can warn about a count it cannot represent.
int TruncatePickupCount(int value);

}  // namespace igi
