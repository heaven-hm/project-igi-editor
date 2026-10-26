// pickup_rules.cpp - see pickup_rules.h for the full IGI-RE citations.
//
// This file is pure logic: no engine types, no filesystem, no globals. Every
// branch below mirrors a branch of src/comprun/pickup.c.

#include "runtime/pickup_rules.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace igi {
namespace {

// --- the ParamTables, in ParamTable_Add order ------------------------------
//
// GenericPickup_Init 0x46C0C0 (pickup.c:250-273; ParamTable_Add at 269-271):
//     ParamTable_New(3)
//     ParamTable_Add(&p.pos,     Param_ObjectPos(), 7, NULL)
//     ParamTable_Add(&p.obj.matrix, Param_Real32x9(), 7, NULL)
//     ParamTable_Add(p.model,    Param_String16(),  7, NULL)
constexpr PickupParam kGenericPickupParams[] = {
    {"Position", PickupParamType::ObjectPos},
    {"Orientation", PickupParamType::Real32x9},
    {"Model", PickupParamType::String16},
};

// GunPickup_Init 0x46CB70 (pickup.c:460-481; ParamTable_Add at 477-479):
//     ParamTable_New(3)
//     ParamTable_Add(&p.pos,        Param_ObjectPos(),  7, NULL)
//     ParamTable_Add(&p.obj.matrix, Param_Real32x9(),   7, NULL)
//     ParamTable_Add(p.weaponName,  Param_EnumString32(), 7, WeaponType_GetIdEnum())
constexpr PickupParam kGunPickupParams[] = {
    {"Position", PickupParamType::ObjectPos},
    {"Orientation", PickupParamType::Real32x9},
    {"ID", PickupParamType::EnumString32},
};

// AmmoPickup_Init 0x46D420 (pickup.c:642-664; ParamTable_Add at 660-663):
//     ParamTable_New(4)
//     ParamTable_Add(&p.pos,       Param_ObjectPos(),   7, NULL)
//     ParamTable_Add(&p.obj.matrix, Param_Real32x9(),   7, NULL)
//     ParamTable_Add(p.ammoName,   Param_EnumString32(), 7, AmmoType_GetEnum())
//     ParamTable_Add(&p.count,     Param_Int16(),       7, NULL)
constexpr PickupParam kAmmoPickupParams[] = {
    {"Position", PickupParamType::ObjectPos},
    {"Orientation", PickupParamType::Real32x9},
    {"ID", PickupParamType::EnumString32},
    {"Ammo", PickupParamType::Int16},
};

// AddAmmo_Init 0x46DB40 (pickup.c:766-780; ParamTable_Add at 776-777):
//     ParamTable_New(2)
//     ParamTable_Add(p.ammoName, Param_EnumString32(), 7, AmmoType_GetEnum())
//     ParamTable_Add(&p.count,   Param_Int16(),        7, NULL)
// AddAmmo has no Position and no Orientation: it is a child of the human it
// feeds, not a placed object.
constexpr PickupParam kAddAmmoParams[] = {
    {"ID", PickupParamType::EnumString32},
    {"Ammo", PickupParamType::Int16},
};

// The enum argument is the third parameter of GunPickup/AmmoPickup, i.e. the
// tenth Task_New token: 3 (id/type/name) + 3 (ObjectPos) + 3 (Real32x9).
constexpr int kEnumArgIndex = 9;
// GenericPickup's third parameter is a literal model name in the same slot.
constexpr int kModelArgIndex = 9;
// AmmoPickup's Int16 count follows the enum.
constexpr int kAmmoCountArgIndex = 10;
// AddAmmo's enum is its first parameter.
constexpr int kAddAmmoEnumArgIndex = 3;
constexpr int kAddAmmoCountArgIndex = 4;

constexpr PickupLayout kLayouts[] = {
    {PickupKind::GenericPickup, "GenericPickup", kGenericPickupParams, 3,
     -1, kModelArgIndex, -1, PickupEnumDomain::None, false},
    {PickupKind::GunPickup, "GunPickup", kGunPickupParams, 3,
     kEnumArgIndex, -1, -1, PickupEnumDomain::Weapon, false},
    {PickupKind::AmmoPickup, "AmmoPickup", kAmmoPickupParams, 4,
     kEnumArgIndex, -1, kAmmoCountArgIndex, PickupEnumDomain::Ammo, true},
    {PickupKind::AddAmmo, "AddAmmo", kAddAmmoParams, 2,
     kAddAmmoEnumArgIndex, -1, kAddAmmoCountArgIndex, PickupEnumDomain::Ammo, true},
};

// Argument counts from the parameter type table's ArgCount slot
// (src/physobj/paramops.c:153-158). Param_ObjectPos and Param_Real32x9 consume
// three .qsc arguments each: ParamReal32x9_Parse 0x4EFBA0 reads the Euler triple
// and ParamObjectPos_Parse reads x, y, z.
int ArgCountFor(PickupParamType type) {
    switch (type) {
        case PickupParamType::ObjectPos: return 3;
        case PickupParamType::Real32x9: return 3;
        case PickupParamType::String16: return 1;
        case PickupParamType::EnumString32: return 1;
        case PickupParamType::Int16: return 1;
    }
    return 0;
}

// The engine's `if (!(r >= kMinTouch)) range = kMinTouch;` (pickup.c:187-188).
// Written with the negation intact so the NaN case matches the engine: NaN
// fails `>=`, so the minimum is used.
double ApplyMinTouch(double r) {
    if (!(r >= kPickupMinTouchUnits)) return kPickupMinTouchUnits;
    return r;
}

}  // namespace

int PickupParamArgCount(PickupParamType type) { return ArgCountFor(type); }

int PickupLayout::ArgCount() const {
    int n = 3;  // task id, task type, instance name
    for (int i = 0; i < param_count; ++i) n += ArgCountFor(params[i].type);
    return n;
}

const PickupLayout* PickupLayouts() { return kLayouts; }

int PickupLayoutCount() { return static_cast<int>(sizeof(kLayouts) / sizeof(kLayouts[0])); }

const PickupLayout* FindPickupLayout(const std::string& task_type) {
    for (const PickupLayout& layout : kLayouts) {
        if (task_type == layout.task_type) return &layout;
    }
    return nullptr;
}

// --- touch -----------------------------------------------------------------

float PickupTouchRange(float model_radius, float max_scale) {
    // radius() pickup.c:172-177. The product is a float multiply: pickup.c:1
    // sets `#pragma GCC optimize("excess-precision=standard")`, so the result is
    // rounded to float before the long double return widens it.
    const float r = model_radius * max_scale;
    return static_cast<float>(ApplyMinTouch(static_cast<double>(r)));
}

bool PickupTouches(double range, const double pickup_pos[3], const double toucher_pos[3]) {
    // touches() pickup.c:180-200. The engine's differences are stored into a
    // `Vec3d d` and a `double dz`, so under the file's
    // `excess-precision=standard` pragma (pickup.c:1) each subtraction is
    // ROUNDED TO DOUBLE first and only widened back to long double inside
    // Vec3d_Length (vec3.c:39-44). Doing the subtraction in long double here
    // would differ in the low bits, so it is done in double and widened after.
    const double effective_range = ApplyMinTouch(range);

    const double dx = pickup_pos[0] - toucher_pos[0];  // pickup.c:189
    const double dy = pickup_pos[1] - toucher_pos[1];  // pickup.c:190
    double dz = pickup_pos[2] - toucher_pos[2];        // pickup.c:192-197
    if (dz < 0.0) dz = -dz;

    const long double x = dx, y = dy;
    // d.z = 0.0 before the length (pickup.c:198), so the horizontal test is the
    // true 2D distance; Vec3d_Length 0x4B3130 sums the three squares in long
    // double (vec3.c:39-44).
    const long double length = std::sqrt(x * x + y * y + 0.0L);

    // Both comparisons are negated `>=`: exactly at range, or exactly 4096 units
    // away vertically, is NOT a touch.
    return !(length >= effective_range) && !(dz >= kPickupTouchHeightUnits);
}

bool PickupTouchesFromModel(float model_radius, float max_scale, const double pickup_pos[3],
                            const double toucher_pos[3]) {
    return PickupTouches(PickupTouchRange(model_radius, max_scale), pickup_pos, toucher_pos);
}

// --- grants ----------------------------------------------------------------

PickupGrant GenericPickupGrant() {
    // GenericPickup_Touch 0x46C630 (pickup.c:361-366) has no message and no
    // event dispatch: it only sets pickedUp.
    return PickupGrant{false, 0, 0};
}

PickupGrant GunPickupGrant(int weapon_id, int weapon_clip_size, bool weapon_found) {
    // GunPickup_Touch 0x46D230 (pickup.c:605-614) always fills the message;
    // whether the toucher has a handler is the caller's `fn` test, which the
    // editor cannot see, so `delivered` stays false here and is left to the
    // caller. The id/count are the engine's values regardless.
    (void)weapon_found;
    return PickupGrant{false, weapon_id, weapon_found ? weapon_clip_size : 0};
}

int GunPickupGrantedAmmo(int weapon_clip_size) {
    // `t->ammo = *(int *)(w + 0x648);` pickup.c:583. WeaponType::clipSize lives
    // in the union at 0x648 (src/weapon/weapontypes.h:35-43).
    return weapon_clip_size;
}

PickupGrant AmmoPickupGrant(int ammo_id, int authored_count) {
    // AmmoPickup_Touch 0x46D990 (pickup.c:741-742). No ammo-table lookup gates
    // the message, so an unknown id still delivers.
    return PickupGrant{false, ammo_id, TruncatePickupCount(authored_count)};
}

bool AddAmmoApplies(bool parent_is_human, bool ammo_found) {
    // AddAmmo_Run 0x46DC40 (pickup.c:790-798):
    //   if (TaskType_IsA(((Task *)h)->type, Human_GetType()) && AmmoType_Find(t->ammo))
    //                                                        // pickup.c:794
    //       AmmoList_Add(h + 0x340, t->ammo, t->count);
    return parent_is_human && ammo_found;
}

// --- lifecycle -------------------------------------------------------------

PickupDisposition PickupDispositionFor(PickupKind kind) {
    switch (kind) {
        case PickupKind::GenericPickup:
            // GenericPickup_Run 0x46C3C0 (pickup.c:306-314): remove from the
            // sector + dynamic cube, keep the task.
            return PickupDisposition::RemoveFromWorld;
        case PickupKind::GunPickup:
            // GunPickup_Run 0x46CEA0 (pickup.c:555-556).
            return PickupDisposition::DeleteTask;
        case PickupKind::AmmoPickup:
            // AmmoPickup_Run 0x46D740 (pickup.c:682-683).
            return PickupDisposition::DeleteTask;
        case PickupKind::AddAmmo:
            // AddAmmo_Run 0x46DC40 (pickup.c:796) deletes itself after granting.
            return PickupDisposition::DeleteTask;
    }
    return PickupDisposition::DeleteTask;
}

bool PickupIsOneShot(PickupKind kind) {
    // pickedUp is set in every touch handler (pickup.c:364, 612, 746) and never
    // cleared. GenericPickup_Show (pickup.c:370) is the only other reader.
    (void)kind;
    return true;
}

// --- script flag -----------------------------------------------------------

std::string PickupScriptFlagName(const std::string& task_type, int task_id) {
    // register_flag pickup.c:203-213: Task_GetName(name, t) at 210, then
    // crt_sprintf(buf, "%s.isPickedUp", name). Task_GetName 0x4F1270
    // (src/param/task.c:123-135) falls back at task.c:130 to "%s_%d" with the TaskType name and
    // the task id, because no task type registers a Level_GetNameEvent handler.
    char id_buf[32];
    std::snprintf(id_buf, sizeof(id_buf), "%d", task_id);
    return task_type + "_" + id_buf + ".isPickedUp";
}

// --- authored-argument validation -----------------------------------------

PickupArgCheck CheckPickupArgs(const std::string& task_type,
                               const std::vector<std::string>& arg_tokens) {
    PickupArgCheck out;
    const PickupLayout* layout = FindPickupLayout(task_type);
    if (layout == nullptr) return out;

    out.is_pickup = true;
    out.expected_tokens = layout->ArgCount();
    out.token_count_ok = static_cast<int>(arg_tokens.size()) == out.expected_tokens;

    if (layout->enum_arg_index >= 0 &&
        static_cast<int>(arg_tokens.size()) > layout->enum_arg_index) {
        const std::string& value = arg_tokens[layout->enum_arg_index];
        out.enum_present = !value.empty();
        if (out.enum_present) {
            // The enum constants the engine registers: ParamEnum_AddConstants
            // "WEAPON_ID_.*" for WeaponType_GetIdEnum and "AMMO_ID_.*" for
            // AmmoType_GetEnum (src/ammotype/ammotype.c:268; the weapon prefix is
            // registered at src/weapon/weapontype.c:269).
            const char* prefix = layout->enum_domain == PickupEnumDomain::Weapon ? "WEAPON_ID_"
                                                                                : "AMMO_ID_";
            out.enum_known = value.rfind(prefix, 0) == 0;
        }
    } else if (layout->model_arg_index >= 0 &&
               static_cast<int>(arg_tokens.size()) > layout->model_arg_index) {
        // GenericPickup's third parameter is a literal model name; the engine
        // passes it straight to VirModel_Find (pickup.c:334).
        out.enum_present = !arg_tokens[layout->model_arg_index].empty();
        out.enum_known = out.enum_present;
    }

    if (layout->count_arg_index >= 0 &&
        static_cast<int>(arg_tokens.size()) > layout->count_arg_index) {
        const std::string& text = arg_tokens[layout->count_arg_index];
        if (!text.empty()) {
            char* end = nullptr;
            const long value = std::strtol(text.c_str(), &end, 10);
            if (end != nullptr && *end == '\0') {
                out.count_ok = true;
                out.count = TruncatePickupCount(static_cast<int>(value));
            }
        }
    } else if (layout->count_arg_index < 0) {
        // No count parameter: nothing to parse, and nothing is missing.
        out.count_ok = true;
    }

    return out;
}

int TruncatePickupCount(int value) {
    // ParamInt16_Parse 0x4F0110 (src/physobj/paramops.c:627-633) is
    // `*v = Script_GetArgInt(...)` into an int16_t: no clamp, just truncation.
    return static_cast<int16_t>(value);
}

}  // namespace igi
