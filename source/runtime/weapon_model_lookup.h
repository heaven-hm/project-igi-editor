#pragma once
/******************************************************************************
 * @file    weapon_model_lookup.h
 * @brief   WEAPON_ID_* -> the model id of that WEAPON TYPE, read from the
 *          retail `WEAPONS/<NAME>/WEAPON.QVM` roster.
 *
 * WHY THIS EXISTS. A placed `StationaryGun` mounts its authored `weapon` string
 * (default "WEAPON_ID_M2HB", src/statgun/statgun.c:114,146). The engine resolves
 * it in two steps, neither of which is a guess:
 *
 *   Script_EvalExpr(o->weapon)  -> the numeric id   (statgun.c:331, ORIG 0x4B8770)
 *   WeaponType_Find(id)         -> the WeaponType   (statgun.c:333, HOOK 0x413BD0)
 *   Gun_New(o, 0, id, ...)      -> a Gun task       (statgun.c:334, HOOK 0x477C50)
 *
 * and `Gun_ParamChangedHandler` then loads the gun's model with
 * `VirModel_Find(wt->key)` (HOOK 0x4789C0, src/guncasing/gun.c:493-506). `key`
 * is `DefineWeaponType` argument 29 (`WeaponType` + 0x61C;
 * src/weapon/weapontype.c:335-337, read at src/comprun/pickup.c:579). The
 * editor's `igi::weapons::WeaponTypeConfig::Model()` is that same argument 29.
 *
 * `WeaponType_Open` (HOOK 0x413A90, src/weapon/weapontype.c:34-51) loads every
 * subdirectory of `LOCAL:weapons`, and each file registers its own id constant
 * (`Script_RegisterConstant(name, w->id)`, weapontype.c:306), which is why the
 * shipped constant is argument 1 of the same call. So scanning
 * `WEAPONS/<NAME>/WEAPON.QVM` and taking (argument 1 -> argument 29) reproduces
 * exactly the table `WeaponType_Find` searches -- including the directory name
 * not matching the constant (`WEAPONS/SENTRYGUN/WEAPON.QVM` defines
 * `WEAPON_ID_SENTRY`).
 *
 * This is pure data reading (QVM + filesystem); it renders nothing and knows no
 * engine task.
 *
 * Oracle: /Users/haseeb-mir/Documents/Code/IGI-RE (read-only).
 *****************************************************************************/

#include <map>
#include <string>

namespace igi {
namespace weapons {

// Every `WEAPON_ID_*` a shipped `WEAPONS/<NAME>/WEAPON.QVM` defines, mapped to
// its weapon type's model id (argument 29). Empty when the directory or the
// files are absent; entries whose name is not a WEAPON_ID_* constant, or whose
// model argument is empty/not a literal, are skipped (never guessed).
std::map<std::string, std::string> LoadWeaponModelRoster(const std::string& game_root);

// One lookup into that roster. The roster is built once per `game_root` and
// cached, because a level can place many StationaryGuns. Returns an empty
// string when the constant is not in the roster -- callers must then draw no
// weapon rather than invent a model.
std::string FindWeaponModelForEnum(const std::string& game_root,
                                   const std::string& enum_name);

}  // namespace weapons
}  // namespace igi
