#pragma once
// ============================================================================
// Weapon / item configurator — the pure, host-testable data layer (issue #49
// part 1).
//
// WHAT THIS IS.  Issue #49 asks for a UI that edits "active weapon parameters
// (fire rate, damage, ammo capacity), ammunition slot assignment, and dropping
// custom inventory items directly into the level".  This header is the part of
// that request the engine actually defines: the parameter table of
// `DefineWeaponType`, the argument vector it is called with, and the two player
// slot tables (`DefineHumanPlayerWeaponCycle`, `DefineHumanPlayerAmmoLimit`).
// The editor's inspector rows, the round-trip writer and the corpus checks are
// all derived from these tables, so they cannot drift from the engine.
//
// ORACLE.  IGI-RE is the only authority used here; every row carries its
// `file:line`.  The one function that defines a weapon type is
//
//   DefineWeaponType   HOOK 0x413F90   src/weapon/weapontype.c:287-360
//
// which fills one `WeaponType` (src/weapon/weapontypes.h:17-76) from a
// positional script argument list.  It is called once per file by
// `weapon.qsc`, which `WeaponType_Open` (HOOK 0x413A90, weapontype.c:34-51)
// runs for every subdirectory of `LOCAL:weapons`; the retail data ships those
// compiled as `WEAPONS/<NAME>/WEAPON.QVM`.
//
// THERE IS NO AUTHORED PARAMETER NAME.  Unlike the level task types, a weapon
// type is not declared with `Task_DeclareParameters`, so the shipped data
// carries no parameter names at all — only positions.  Every label below is
// therefore either (a) the field name the IGI-RE decompilation recovered for
// the `WeaponType` member the argument lands in, or (b) explicitly marked
// `Unk<offset>` when nothing reads that member.  Nothing here is invented.
// ============================================================================

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace igi {
namespace weapons {

// ---------------------------------------------------------------------------
// The argument vector of one script call, read from a compiled QVM.
// ---------------------------------------------------------------------------

// One literal argument, as the engine's own argument evaluator would see it.
// `Qvm_OpCall` HOOK 0x4BD880 (src/qvm/qvm.c:641-689) hands the callee an array
// of code addresses (`args.argPc = (int *)(p + 5)`, :654) and `Qvm_GetArg`
// HOOK 0x4BCA80 (src/qvm/qvm.c:149-198) runs the block at one of them
// (`vm->pc = args->argPc[index]`, :161) until its BRK, then pops the value.
//
// This struct is the editor's reading of that block, restricted to the blocks
// that are a single literal: a weapon definition is all literals, and a block
// that is not is reported as `Kind::NotLiteral` rather than guessed at.
struct CallArg {
    enum class Kind {
        NotLiteral,  // the block computes something; the editor refuses it
        Int,         // PUSHB / PUSHW / PUSHI / PUSHS?  (see qvm_parser.cpp)
        Real,        // PUSHF
        String,      // a string-pool index (Qvm_GetStringArg, src/qvm/qvm.c:350)
        Identifier,  // a name-pool index: a registered constant or function name
    };

    Kind kind = Kind::NotLiteral;

    // Int value, or the identifier/string pool index.
    int32_t int_value = 0;
    // Real value (PUSHF).
    float real_value = 0.0f;
    // Resolved text for String / Identifier.
    std::string text;
    // The engine's unary minus (`MINUS`, opcode 44) in front of the literal.
    // `DefineWeaponType` reads signed values this way: the AK47's first spread
    // is authored as `MINUS 3.5`.
    bool negated = false;

    // False when AsInt()/AsReal() CANNOT return what the engine would see.
    //
    // A `PUSHII*` argument pushes a SYMBOL, not a number: `Qvm_OpPushIndexed`
    // HOOK 0x4BEA30 (src/qsc/qvmop.c:378-459) resolves the pool index through
    // `vm->symVars` / `Script_FindSymbol` and calls `Qvm_PushSymbol` (:447), so
    // `Script_GetArgInt` returns whatever the symbol currently holds.  For the
    // enum constants IGI-RE names (`WEAPONTYPE_*`, `SIGHTDISPLAYTYPE_*`,
    // `AMMODISPLAYTYPE_*`, registered at src/weapon/weapontype.c:246-261) that
    // value is known; for a constant some other script registers
    // (`HUMANAI_DETECTIONEVENT_*`, `TASKTYPE_*`) it is not, and this flag is
    // false with AsInt()/AsReal() returning 0.
    bool value_known = true;

    // The value as the engine's `Script_GetArgInt` (HOOK 0x4B8A20,
    // src/script/script.c:318-324 -> Qvm_GetIntArg, src/qvm/qvm.c:139-146)
    // would return it for an argument the callee reads as an int.  A string or
    // a real argument coerces to 0, which is what the engine does too.
    int AsInt() const;
    // The value as `Script_GetArgReal` (HOOK 0x4B8A50, src/script/script.c:
    // 327-333 -> Qvm_GetRealArg, src/qvm/qvm.c:341-348) would return it.
    double AsReal() const;
    // The value as `Script_GetArgString` (HOOK 0x4B8A80, src/script/script.c:
    // 335-341 -> Qvm_GetStringArg, src/qvm/qvm.c:350-356) would return it.
    std::string AsString() const;
};

// The integer value a registered script constant holds, for the constants
// IGI-RE names.  Returns false for a name the editor cannot prove a value for
// (one registered by a script it has not run), which is what leaves
// `CallArg::value_known` false.
bool KnownConstantValue(const std::string& name, int* out);

// One `CALL` in a QVM, with the literal arguments of every argument block.
struct CallArgs {
    bool found = false;
    std::string callee;          // the name the callee was pushed as
    std::vector<CallArg> args;   // one entry per CALL operand, in order
    std::string error;           // why `found` is false, when it is
};

// Reads `path` and returns the arguments of the FIRST call to `callee`.
//
// The blocks are walked with the same per-instruction decode the editor's QVM
// reader performs (`QVM_Parse`, source/level/qvm_parser.cpp), so the argument
// addresses come from the engine's own CALL operand layout.  A block that is
// not a single literal push (optionally preceded by `MINUS`) yields
// `CallArg::Kind::NotLiteral` in place; it is never approximated.
CallArgs ReadCallArgsFromQvmFile(const std::string& path, const std::string& callee);

// ---------------------------------------------------------------------------
// The DefineWeaponType parameter table
// ---------------------------------------------------------------------------

// One row of `DefineWeaponType`'s argument list.
struct WeaponParamDesc {
    // The label the configurator shows.  See the file header: the shipped data
    // has no names, so this is the IGI-RE `WeaponType` member name where the
    // decompilation recovered one and "Unk<offset>" where it did not.
    const char* name;
    // Index into the Task_New-shaped argument vector: `DefineWeaponType` reads
    // it with `Script_GetArg* (args, arg_index)`.
    int arg_index;
    // Arguments this parameter consumes in the compiled script.  Every row is
    // 1: the whole list is scalars and single string slots.
    int token_count;
    // Byte offset of the `WeaponType` member it lands in, or -1 for the two
    // rows that are not a plain member store (the id, which is also registered
    // as a script constant, and the task types).
    int type_offset;
    // The engine function that READS the member, as `file:line`.  Empty when
    // nothing in the decompilation reads it — which is itself the finding.
    const char* engine_use;
};

// Every fixed parameter, in `DefineWeaponType` order:
//   arg 0..47  the fixed list, of which arg 47 is the first task type
//   arg 48..   the tail (see WeaponTypeConfig::tail)
//
// The list is the whole of `DefineWeaponType`'s body
// (src/weapon/weapontype.c:289-341), one row per `Script_GetArg*` call, in the
// order those calls are made.  `param_count` is 48.
extern const WeaponParamDesc kWeaponParamTable[];
extern const int kWeaponParamCount;

// The first argument index that is NOT part of the fixed list.
// `next = 48; w->taskType = Script_GetArgInt(args, 47);` (weapontype.c:344-345).
constexpr int kWeaponTailArgIndex = 48;

// ---------------------------------------------------------------------------
// The parsed weapon type
// ---------------------------------------------------------------------------

// The engine's `WeaponCategory` constants (src/weapon/weapontype.c:246-253).
enum WeaponCategoryId {
    kWeaponTypeGun          = 0,
    kWeaponTypePistol       = 1,
    kWeaponTypeShotgun      = 2,
    kWeaponTypeKnife        = 3,
    kWeaponTypeGrenade      = 4,
    kWeaponTypeBinocular    = 5,
    kWeaponTypeMedipack     = 6,
    kWeaponTypeProximityMine = 7,
};

// A `DefineWeaponType` call, resolved.
//
// Only the members the configurator exposes are named; the rest of the call is
// kept in `args` so a round-trip re-emits the file unchanged.  Every accessor
// documents the engine rule that gives it meaning.
struct WeaponTypeConfig {
    // The whole argument vector, exactly as read.  `ok` is false when the call
    // was not found or an argument was not a literal.
    std::vector<CallArg> args;
    bool ok = false;
    std::string error;

    // --- the three parameters issue #49 names -------------------------------

    // DAMAGE — arg 10 -> WeaponType + 0x654 (unk654[0]).
    //   DefineWeaponType 0x413F90  src/weapon/weapontype.c:317-318
    //   read at  src/comprun/knifestab.c:108   `msg.damage = *(float *)(f->type + 0x654)`
    //   and     src/ammotype/gunshot.c:204     `memcpy(&b->unk38, type + 0x654, 4)`
    // The bullet's copy is what reaches the victim: `Bullet_Update` copies
    // `b->unk38` into `BulletHitMsg.unk38` (src/ammotype/bullets.c:139-142) and
    // dispatches it (bullets.c:165-166) to the human's `Game_GetEvent9` handler,
    // whose parameter is a `HumanHit` (src/loadscreen/loadscreentypes.h:13-18)
    // laid over that message — so `unk38` IS `HumanHit.damage`, which
    // `HumanPlayer_HitHandler` HOOK 0x412440 (src/weapon/humanplayer.c:688-704)
    // applies at :704.  There is NO damage field in the ammo table:
    // `DefineAmmoType` HOOK 0x47CAD0 (src/ammotype/ammotype.c:88-115) has none.
    double Damage() const;

    // FIRE RATE — arg 15 -> WeaponType + 0x644 (`unk640[1]`, `rate`), shots per
    // minute.  DefineWeaponType weapontype.c:319-320; the cadence is
    // `g->cooldown = round_int((kMinute / (long double)wt->rate) * kFpsD)`
    // (src/guncasing/gun.c:670, with kMinute 60.0 and kFpsD 30.0 at
    // gun.c:693-694), i.e. frames between shots at 30 fps.
    double FireRate() const;

    // AMMO CAPACITY — arg 16 -> WeaponType + 0x648 (`unk640[2]`, `clipSize`).
    //   the magazine the reload fills: `g->rounds = w->clipSize`
    //   (src/ammotype/gun.c:177; `need = w->clipSize - g->rounds` at :184)
    //   what a GunPickup grants: `t->ammo = *(int *)(w + 0x648)`
    //   (src/comprun/pickup.c:583)
    //   what an AI is given: `give.ammo = w[0x648 / 4] > 0 ? w[0x648 / 4] : 0`
    //   (src/game/game.c:630)
    int ClipSize() const;

    // --- the rest of the parameters the engine reads ------------------------

    int    Id() const;            // arg 0   the WEAPON_ID_* value
    std::string Name() const;     // arg 1   the script constant's name
    int    Category() const;      // arg 5   WEAPONTYPE_* (weapontype.c:246-253)
    int    Calibre() const;       // arg 9   AmmoType_FindByCaliber (guncasing/gun.c:494)
    double Pierce() const;        // arg 11  gunshot.c:206, vs the material hardness
    double ThrowSpeed() const;    // arg 13  missile.c:629
    int    Pellets() const;       // arg 14  guncasing/gun.c:608
    int    Burst() const;         // arg 17  aitask/aitask.c:259
    double SpreadFirst() const;   // arg 18  guncasing/gun.c:604 (first 3 shots)
    double SpreadRest() const;    // arg 19  guncasing/gun.c:604 (shot 4 and later)
    double Range() const;         // arg 25  gunshot.c:205 — the bullet's travel
    std::string Model() const;    // arg 29  VirModel_Find(w + 0x61C), pickup.c:579
    std::string CasingModel() const; // arg 30  GunCasing_New, guncasing/gun.c:658
    std::string Sound() const;    // arg 44
    std::string Sound2() const;   // arg 45
    double DetectRange() const;   // arg 46  the gunshot noise radius, ammotype/gun.c:146

    // arg 47: the weapon's FIRST task type.  `w->taskType`
    // (src/weapon/weapontype.c:345).
    int FirstTaskType() const;

    // The arguments after the 48 fixed ones, verbatim.
    //
    // `next = 48; w->taskType = Script_GetArgInt(args, 47);` then
    // `init_task_data(..., args, &next)` (weapontype.c:344-353) lets the first
    // task type consume its own arguments before `taskType2` is read at
    // `next`.  HOW MANY it consumes is a property of that task type's size
    // handler, which is registered in C (`TaskType_SetHandler(gWeaponSizeEvent,
    // ...)`) and is NOT recoverable from the script — so the editor does not
    // guess where `taskType2` starts.  The retail files range from one tail
    // argument (AK47, argc 49) to six (PROXIMITYMINE, argc 53), which is the
    // evidence that the position is genuinely variable.
    std::vector<CallArg> Tail() const;
};

// Resolves `args` the way `DefineWeaponType` does.  Fails (ok == false) when
// fewer than 48 arguments are present.
WeaponTypeConfig ParseWeaponTypeArgs(const std::vector<CallArg>& args);

// Re-emits the argument vector as QSC-shaped tokens, in order, so a weapon
// file read by the editor can be written back byte-for-byte equal in its
// `DefineWeaponType` call.  A `NotLiteral` argument is emitted as `0` and sets
// `out_all_literal = false`; it is never silently dropped, because dropping an
// argument would shift every later one.
std::vector<std::string> EmitWeaponTypeArgs(const std::vector<CallArg>& args,
                                            bool* out_all_literal = nullptr);

// ---------------------------------------------------------------------------
// The player's weapon and ammunition slots
// ---------------------------------------------------------------------------

// The player's weapon cycle, in slot order.
//
//   DefineHumanPlayerWeaponCycle  HOOK 0x4101C0  src/humanplayer/humanplayer.c:1149-1160
//     `gWeaponCycleCount = 0; for (i = 0; i < *args; i++)
//          gWeaponCycle[gWeaponCycleCount++] = Script_GetArgInt(args, i);`
//
// `*args` is the CALL's argument count, so EVERY argument is one slot: there is
// no fixed arity and no padding.  `WeaponSystem::SelectWeaponSlot` walks the
// same order (source/weapon_system.cpp:216-221 is the retail transcription).
//
// The retail file is `humanplayer/humanplayer.qvm`; its cycle is 18 slots,
// `WEAPON_ID_KNIFE` first.
struct WeaponCycle {
    std::vector<int> slots;
    bool ok = false;
    std::string error;
};

// A caller-supplied resolver for the constants a SCRIPT registered, which the
// editor cannot know from `KnownConstantValue` alone.
//
// The two that matter are the weapon ids and the ammo ids, and both are
// recoverable exactly:
//
//   * `DefineWeaponType` does `Script_RegisterConstant(name, w->id)`
//     (HOOK 0x413F90, src/weapon/weapontype.c:306), so argument 1 of every
//     weapon's call IS the name of its id constant — `WeaponIdConstants` below
//     builds the map from a parsed roster;
//   * `DefineAmmoType` does the same for `a->id` (HOOK 0x47CAD0,
//     src/ammotype/ammotype.c:95), and the ammo table is read the same way.
//
// Returning false leaves the argument unresolved, and the parser then REFUSES
// the list rather than writing a zero over a real id.
using ConstantResolver = std::function<bool(const std::string& name, int* out)>;

// `WEAPON_ID_*` -> id, from a parsed `WEAPONS/*/WEAPON.QVM` roster.  The map is
// exactly what the engine's script symbol table holds once the weapons have
// loaded, because `DefineWeaponType` registers argument 1 against argument 0.
std::map<std::string, int> WeaponIdConstants(
    const std::vector<WeaponTypeConfig>& roster);

WeaponCycle ParseWeaponCycleArgs(const std::vector<CallArg>& args,
                                 const ConstantResolver& extra = {});
std::vector<std::string> EmitWeaponCycleArgs(const std::vector<int>& slots);

// One `(ammo id, limit)` pair.
struct AmmoSlot {
    int ammo_id = 0;
    int limit = 0;
};

// The player's per-ammunition limits.
//
//   DefineHumanPlayerAmmoLimit  HOOK 0x4102D0  src/humanplayer/humanplayer.c:1178-1193
//     `gAmmoLimitCount = 0;
//      for (i = 0; i < *args; gAmmoLimitCount++) {
//          l = &gAmmoLimits[gAmmoLimitCount];
//          l->id = Script_GetArgInt(args, i++);
//          l->limit = Script_GetArgInt(args, i++);
//      }`
//
// The arguments are pairs, so an ODD count leaves the last id without a limit;
// the engine then reads one argument past the end.  The editor refuses that
// input instead of reproducing a read past the argument array.
//
// The limits are applied by `AmmoList_SetLimit(h->ammo, id, limit)` for every
// entry (src/humanplayer/humanplayer.c:1219-1220).
struct AmmoLimits {
    std::vector<AmmoSlot> slots;
    bool ok = false;
    std::string error;
};

AmmoLimits ParseAmmoLimitArgs(const std::vector<CallArg>& args,
                               const ConstantResolver& extra = {});
std::vector<std::string> EmitAmmoLimitArgs(const std::vector<AmmoSlot>& slots);

// ---------------------------------------------------------------------------
// Dropping an inventory item into the level
// ---------------------------------------------------------------------------

// The three task types that put a weapon or ammunition into a level, and the
// engine function that reads each one's parameters.  These ARE level-file data
// — unlike a weapon type, they are authored with `Task_DeclareParameters` and
// written into `objects.qsc` — so they are what the configurator's "drop" half
// round-trips.
//
//   GunPickup   GunPickup_Init   HOOK 0x46CB70  src/comprun/pickup.c:460-481
//   AmmoPickup  AmmoPickup_Init  HOOK 0x46D420  src/comprun/pickup.c:642-664
//   AddAmmo     AddAmmo_Init     HOOK 0x46DB40  src/comprun/pickup.c:766-780
//
// `AddAmmo` is the one that feeds the PLAYER: it is created as a CHILD of a
// human task and `AddAmmo_Run` HOOK 0x46DC40 (pickup.c:790-798) does
// `if (TaskType_IsA(((Task *)h)->type, Human_GetType()) && AmmoType_Find(t->ammo))
//      AmmoList_Add(h + 0x340, t->ammo, t->count);
//  Task_Delete(t);`
// so it is the authored form of "give the player N rounds of ammo X at level
// start".  The retail data authors it: `MISSIONS/location0/LEVEL2/objects.qsc`
// declares it at :36 (`Task_DeclareParameters("AddAmmo", "ID", "EnumString32",
// "Ammo", "Int16")`) and places one at :1080
// (`Task_New(-1, "AddAmmo", "", "AMMO_ID_919", 49)`), as a child of the
// `HumanPlayer` task opened at :1074.
//
// This function reports whether a task type is one of the three, so a UI can
// gate its "drop item" action on the engine's own table rather than a
// hand-written list.  It is the same answer as
// `igi::FindPickupLayout` (source/runtime/pickup_rules.h:156) but expressed
// against this header's vocabulary.
bool IsInventoryDropTaskType(const std::string& task_type);

}  // namespace weapons
}  // namespace igi
