// Weapon / item configurator — the pure data layer (issue #49 part 1).
//
// Every rule below is transcribed from IGI-RE and cited at the point of use.
// See source/runtime/weapon_config.h for the oracle summary and
// docs/igi-re-parity/WEAPON_CONFIGURATOR.md for the full trace.

#include "weapon_config.h"

#include <algorithm>
#include <cstdio>
#include <map>
#include <unordered_map>

#include "../level/qvm_parser.h"

namespace igi {
namespace weapons {

// ===========================================================================
// Registered constants the editor can prove a value for.
//
// `WeaponType_Init` HOOK 0x413DF0 (src/weapon/weapontype.c:236-271) registers
// these with `Script_RegisterConstant` at :246-261.  A `PUSHII*` argument for
// one of them resolves to exactly this value, so the editor may present it.
// ===========================================================================
bool KnownConstantValue(const std::string& name, int* out) {
    static const std::unordered_map<std::string, int> kKnown = {
        // weapontype.c:246-253
        {"WEAPONTYPE_GUN", 0},
        {"WEAPONTYPE_PISTOL", 1},
        {"WEAPONTYPE_SHOTGUN", 2},
        {"WEAPONTYPE_KNIFE", 3},
        {"WEAPONTYPE_GRENADE", 4},
        {"WEAPONTYPE_BINOCULAR", 5},
        {"WEAPONTYPE_MEDIPACK", 6},
        {"WEAPONTYPE_PROXIMITYMINE", 7},
        // weapontype.c:254-255
        {"SIGHTDISPLAYTYPE_NONE", 0},
        {"SIGHTDISPLAYTYPE_CROSSHAIR", 1},
        // weapontype.c:256-261
        {"AMMODISPLAYTYPE_NONE", 0},
        {"AMMODISPLAYTYPE_NORMAL", 1},
        {"AMMODISPLAYTYPE_CLIP", 2},
        {"AMMODISPLAYTYPE_BARREL", 3},
        {"AMMODISPLAYTYPE_SHELL", 4},
        {"AMMODISPLAYTYPE_GRENADE", 5},
    };
    const auto it = kKnown.find(name);
    if (it == kKnown.end()) return false;
    if (out) *out = it->second;
    return true;
}

// ---------------------------------------------------------------------------
// CallArg
// ---------------------------------------------------------------------------

int CallArg::AsInt() const {
    switch (kind) {
        case Kind::Int:
            return negated ? -int_value : int_value;
        case Kind::Real:
            return negated ? -static_cast<int>(real_value)
                           : static_cast<int>(real_value);
        case Kind::Identifier: {
            int v = 0;
            if (!KnownConstantValue(text, &v)) return 0;
            return negated ? -v : v;
        }
        // The engine's `Qvm_PopNumber` (src/qvm/qvm.c:200-230) yields 0 for a
        // value that is not a number, so a string argument read as an int is 0.
        case Kind::String:
        case Kind::NotLiteral:
            return 0;
    }
    return 0;
}

double CallArg::AsReal() const {
    switch (kind) {
        case Kind::Int:
            return negated ? -static_cast<double>(int_value)
                           : static_cast<double>(int_value);
        case Kind::Real:
            return negated ? -static_cast<double>(real_value)
                           : static_cast<double>(real_value);
        case Kind::Identifier: {
            int v = 0;
            if (!KnownConstantValue(text, &v)) return 0.0;
            return negated ? -static_cast<double>(v) : static_cast<double>(v);
        }
        case Kind::String:
        case Kind::NotLiteral:
            return 0.0;
    }
    return 0.0;
}

std::string CallArg::AsString() const {
    // `Qvm_GetStringArg` HOOK 0x4BD040 (src/qvm/qvm.c:350-356) formats whatever
    // the block pushed; only a string value yields text.
    if (kind == Kind::String) return text;
    return std::string();
}

// ---------------------------------------------------------------------------
// Reading a call's arguments out of a compiled QVM
// ---------------------------------------------------------------------------

namespace {

// The name a name-pool push resolves to, or an empty string when the
// instruction is not one.
std::string IdentifierOf(const QVMInstruction& in, const QVMFile& qvm) {
    switch (in.type) {
        case QVMOpType::PUSHII:
        case QVMOpType::PUSHIIB:
        case QVMOpType::PUSHIIW:
            return qvm.name_pool.Get(in.operand);
        // The inline forms carry the text in the code section
        // (`Qvm_OpPushS` HOOK 0x4BE9D0, src/qsc/qvmop.c:354-372).
        case QVMOpType::PUSHI:
            return in.inline_text;
        default:
            return std::string();
    }
}

// One literal argument block, or NotLiteral.
//
// The engine runs the block and pops the value (`Qvm_GetArg`, src/qvm/qvm.c:
// 149-198).  The blocks a weapon definition uses are a single push, optionally
// followed by `MINUS` — the AK47's first spread is authored negative — and end
// in BRK.  Anything else is reported as NotLiteral rather than approximated.
CallArg ReadLiteralBlock(const QVMFile& qvm, uint32_t address) {
    CallArg out;
    const auto it = std::find_if(qvm.instructions.begin(), qvm.instructions.end(),
                                 [address](const QVMInstruction& i) {
                                     return i.address == address;
                                 });
    if (it == qvm.instructions.end()) {
        out.kind = CallArg::Kind::NotLiteral;
        return out;
    }

    bool saw_minus = false;
    for (auto cur = it; cur != qvm.instructions.end(); ++cur) {
        const QVMInstruction& in = *cur;
        if (in.type == QVMOpType::BRK) break;
        // MINUS is opcode 44 (source/level/qvm_parser.h:107-113); the engine's
        // handler is `Qvm_OpMinus`.
        if (in.type == QVMOpType::MINUS) {
            // Toggled rather than latched, so a block that negates twice comes
            // out positive, as the engine's own two MINUS applications would.
            saw_minus = !saw_minus;
            continue;
        }
        if (out.kind != CallArg::Kind::NotLiteral) {
            // A second push means the block computes something.
            out.kind = CallArg::Kind::NotLiteral;
            return out;
        }
        switch (in.type) {
            case QVMOpType::PUSHB:
            case QVMOpType::PUSHW:
                out.kind = CallArg::Kind::Int;
                out.int_value = in.signed_operand;
                break;
            case QVMOpType::PUSHF:
                out.kind = CallArg::Kind::Real;
                out.real_value = in.operand_float;
                break;
            case QVMOpType::PUSH0:
                out.kind = CallArg::Kind::Int;
                out.int_value = 0;
                break;
            case QVMOpType::PUSH1:
                out.kind = CallArg::Kind::Int;
                out.int_value = 1;
                break;
            case QVMOpType::PUSHS:
            case QVMOpType::PUSHSI:
            case QVMOpType::PUSHSIB:
            case QVMOpType::PUSHSIW:
                out.kind = CallArg::Kind::String;
                out.text = (in.type == QVMOpType::PUSHS)
                               ? in.inline_text
                               : qvm.string_pool.Get(in.operand);
                break;
            case QVMOpType::PUSHI:
            case QVMOpType::PUSHII:
            case QVMOpType::PUSHIIB:
            case QVMOpType::PUSHIIW: {
                out.kind = CallArg::Kind::Identifier;
                out.text = IdentifierOf(in, qvm);
                out.int_value = in.signed_operand;
                int resolved = 0;
                out.value_known = KnownConstantValue(out.text, &resolved);
                break;
            }
            default:
                out.kind = CallArg::Kind::NotLiteral;
                return out;
        }
    }
    out.negated = saw_minus;
    return out;
}

}  // namespace

CallArgs ReadCallArgsFromQvmFile(const std::string& path, const std::string& callee) {
    CallArgs out;

    QVMFile qvm = QVM_Parse(path);
    if (!qvm.valid) {
        out.error = qvm.error.empty() ? "QVM did not parse" : qvm.error;
        return out;
    }

    for (size_t i = 0; i < qvm.instructions.size(); ++i) {
        const QVMInstruction& in = qvm.instructions[i];
        if (in.type != QVMOpType::CALL) continue;

        // The callee is the statement the engine popped before the call
        // (`Qvm_OpCall` HOOK 0x4BD880, src/qvm/qvm.c:641-689, pops the symbol
        // first at :646).  The retail shape is a single name-pool push
        // immediately before the CALL; anything else is refused by name.
        if (i == 0) {
            out.error = "CALL at offset " + std::to_string(in.address) +
                        " has no preceding callee statement";
            return out;
        }
        const std::string name = IdentifierOf(qvm.instructions[i - 1], qvm);
        if (name != callee) continue;

        out.found = true;
        out.callee = name;
        out.args.reserve(in.call_targets.size());
        for (int32_t target : in.call_targets) {
            out.args.push_back(
                ReadLiteralBlock(qvm, static_cast<uint32_t>(target)));
        }
        return out;
    }

    out.error = "no call to '" + callee + "' in " + path;
    return out;
}

// ===========================================================================
// The DefineWeaponType parameter table
// ===========================================================================
//
// One row per `Script_GetArg*` call in `DefineWeaponType`
// (src/weapon/weapontype.c:287-360), in call order.  `engine_use` names the
// function that READS the member; an empty string means the decompilation shows
// no reader, which is recorded rather than papered over.
const WeaponParamDesc kWeaponParamTable[] = {
    //  name                        arg  tok  offset  engine_use
    {"ID",                          0,  1,  -1,     "weapontype.c:294-307 (id + Script_RegisterConstant)"},
    {"Name",                        1,  1,  0x010,  "weapontype.c:304-307 (w->name; registered as a constant)"},
    {"HUD Name",                    2,  1,  0x110,  ""},
    {"Description",                 3,  1,  0x310,  ""},
    {"Category Name",               4,  1,  0x210,  ""},
    {"Category",                    5,  1,  0x610,  "aitask/aitask.c:252; loadscreen/aicombat.c:735-736,763; soldier/soldier.c:962,968"},
    {"Sight Display",               6,  1,  0x614,  "ammotype/gun.c:434"},
    {"Ammo Display",                7,  1,  0x618,  "ammotype/gun.c:355,370,398; guncasing/gun.c:467"},
    {"Unk004",                      8,  1,  0x004,  ""},
    {"Calibre",                     9,  1,  0x63C,  "guncasing/gun.c:494 AmmoType_FindByCaliber; ammotype/ammotype.c:163"},
    {"Damage",                     10,  1,  0x654,  "comprun/knifestab.c:108; ammotype/gunshot.c:204 -> HumanHit.damage"},
    {"Pierce",                     11,  1,  0x658,  "ammotype/gunshot.c:206 (vs the material hardness)"},
    {"Unk65C",                     12,  1,  0x65C,  ""},
    {"Throw Speed",                13,  1,  0x660,  "missile/missile.c:629"},
    {"Pellets",                    14,  1,  0x640,  "guncasing/gun.c:608"},
    {"Rate",                       15,  1,  0x644,  "guncasing/gun.c:385,391,402,411,670,677,678 (shots per minute)"},
    {"Clip Size",                  16,  1,  0x648,  "ammotype/gun.c:177,184; comprun/pickup.c:583; game/game.c:630; statgun/spas12.c:55"},
    {"Burst",                      17,  1,  0x64C,  "aitask/aitask.c:259; loadscreen/aicombat.c:739-742"},
    {"Spread (first 3)",           18,  1,  0x664,  "guncasing/gun.c:604 (g->burst > 3 ? spread[1] : spread[0])"},
    {"Spread (rest)",              19,  1,  0x668,  "guncasing/gun.c:604"},
    {"Recoil Pitch",               20,  1,  0x66C,  "ammotype/gun.c:163"},
    {"Recoil Yaw",                 21,  1,  0x670,  "ammotype/gun.c:162"},
    {"Recoil Pitch Random",        22,  1,  0x674,  "ammotype/gun.c:163"},
    {"Recoil Yaw Random",          23,  1,  0x678,  "ammotype/gun.c:162"},
    {"Group Name",                 24,  1,  0x410,  ""},
    {"Range",                      25,  1,  0x650,  "ammotype/gunshot.c:205 (b->speed = range * k4096)"},
    {"Users Name",                 26,  1,  0x510,  ""},
    {"Unk008",                     27,  1,  0x008,  ""},
    {"Unk00C",                     28,  1,  0x00C,  ""},
    {"Model",                      29,  1,  0x61C,  "comprun/pickup.c:579 VirModel_Find(w + 0x61C)"},
    {"Casing Model",               30,  1,  0x62C,  "guncasing/gun.c:636,658 GunCasing_New"},
    {"Anim 0",                     31,  1,  0x67C,  ""},
    {"Anim 1",                     32,  1,  0x680,  ""},
    {"Anim 2",                     33,  1,  0x684,  ""},
    {"Anim 3",                     34,  1,  0x688,  ""},
    {"Anim 4",                     35,  1,  0x68C,  ""},
    {"Anim 5",                     36,  1,  0x690,  ""},
    {"Anim2 0",                    37,  1,  0x694,  ""},
    {"Anim2 1",                    38,  1,  0x698,  ""},
    // The authored order is NOT the member order here: `DefineWeaponType` reads
    // anim2[3] before anim2[2] (weapontype.c:332-338).  The row order below is
    // the ARGUMENT order, which is what a reader of the file needs; the offset
    // column is the member each one lands in.
    {"Anim2 3",                    39,  1,  0x6A0,  ""},
    {"Anim2 4",                    40,  1,  0x6A4,  ""},
    {"Anim2 2",                    41,  1,  0x69C,  ""},
    {"Anim2 5",                    42,  1,  0x6A8,  ""},
    {"Anim2 6",                    43,  1,  0x6AC,  ""},
    {"Sound",                      44,  1,  0x6B0,  ""},
    {"Sound 2",                    45,  1,  0x6C0,  ""},
    {"Detect Range",               46,  1,  0x6D0,  "ammotype/gun.c:146 (the gunshot noise radius)"},
    {"First Task Type",            47,  1,  0x6D4,  "weapontype.c:345-348 init_task_data(gWeaponSizeEvent)"},
};
const int kWeaponParamCount =
    static_cast<int>(sizeof(kWeaponParamTable) / sizeof(kWeaponParamTable[0]));

// ===========================================================================
// WeaponTypeConfig
// ===========================================================================

namespace {

double ArgReal(const std::vector<CallArg>& args, int index) {
    if (index < 0 || index >= static_cast<int>(args.size())) return 0.0;
    return args[static_cast<size_t>(index)].AsReal();
}

int ArgInt(const std::vector<CallArg>& args, int index) {
    if (index < 0 || index >= static_cast<int>(args.size())) return 0;
    return args[static_cast<size_t>(index)].AsInt();
}

std::string ArgString(const std::vector<CallArg>& args, int index) {
    if (index < 0 || index >= static_cast<int>(args.size())) return std::string();
    return args[static_cast<size_t>(index)].AsString();
}

}  // namespace

double WeaponTypeConfig::Damage() const     { return ArgReal(args, 10); }
double WeaponTypeConfig::FireRate() const   { return ArgReal(args, 15); }
int    WeaponTypeConfig::ClipSize() const   { return ArgInt(args, 16); }

int         WeaponTypeConfig::Id() const            { return ArgInt(args, 0); }
std::string WeaponTypeConfig::Name() const          { return ArgString(args, 1); }
int         WeaponTypeConfig::Category() const      { return ArgInt(args, 5); }
int         WeaponTypeConfig::Calibre() const       { return ArgInt(args, 9); }
double      WeaponTypeConfig::Pierce() const        { return ArgReal(args, 11); }
double      WeaponTypeConfig::ThrowSpeed() const    { return ArgReal(args, 13); }
int         WeaponTypeConfig::Pellets() const       { return ArgInt(args, 14); }
int         WeaponTypeConfig::Burst() const         { return ArgInt(args, 17); }
double      WeaponTypeConfig::SpreadFirst() const   { return ArgReal(args, 18); }
double      WeaponTypeConfig::SpreadRest() const    { return ArgReal(args, 19); }
double      WeaponTypeConfig::Range() const         { return ArgReal(args, 25); }
std::string WeaponTypeConfig::Model() const         { return ArgString(args, 29); }
std::string WeaponTypeConfig::CasingModel() const   { return ArgString(args, 30); }
std::string WeaponTypeConfig::Sound() const         { return ArgString(args, 44); }
std::string WeaponTypeConfig::Sound2() const        { return ArgString(args, 45); }
double      WeaponTypeConfig::DetectRange() const   { return ArgReal(args, 46); }
int         WeaponTypeConfig::FirstTaskType() const { return ArgInt(args, 47); }

std::vector<CallArg> WeaponTypeConfig::Tail() const {
    if (static_cast<int>(args.size()) <= kWeaponTailArgIndex) return {};
    return std::vector<CallArg>(args.begin() + kWeaponTailArgIndex, args.end());
}

WeaponTypeConfig ParseWeaponTypeArgs(const std::vector<CallArg>& args) {
    WeaponTypeConfig out;
    out.args = args;
    if (static_cast<int>(args.size()) < kWeaponParamCount) {
        out.error = "DefineWeaponType needs at least " +
                    std::to_string(kWeaponParamCount) + " arguments, got " +
                    std::to_string(args.size());
        return out;
    }
    for (const CallArg& a : args) {
        if (a.kind == CallArg::Kind::NotLiteral) {
            out.error =
                "an argument block is not a literal; the editor does not guess "
                "at a computed weapon parameter";
            return out;
        }
    }
    out.ok = true;
    return out;
}

std::vector<std::string> EmitWeaponTypeArgs(const std::vector<CallArg>& args,
                                            bool* out_all_literal) {
    std::vector<std::string> out;
    out.reserve(args.size());
    bool all_literal = true;
    char buf[64];
    for (const CallArg& a : args) {
        switch (a.kind) {
            case CallArg::Kind::Int:
                std::snprintf(buf, sizeof(buf), "%d",
                              a.negated ? -a.int_value : a.int_value);
                out.emplace_back(buf);
                break;
            case CallArg::Kind::Real:
                // %.17g round-trips a float through strtod exactly, which is
                // what the QVM's PUSHF payload is.
                std::snprintf(buf, sizeof(buf), "%.17g",
                              a.negated ? -static_cast<double>(a.real_value)
                                        : static_cast<double>(a.real_value));
                out.emplace_back(buf);
                break;
            case CallArg::Kind::String:
                out.push_back("\"" + a.text + "\"");
                break;
            case CallArg::Kind::Identifier:
                out.push_back(a.text);
                break;
            case CallArg::Kind::NotLiteral:
                // Never dropped: dropping one would shift every later argument.
                all_literal = false;
                out.emplace_back("0");
                break;
        }
    }
    if (out_all_literal) *out_all_literal = all_literal;
    return out;
}

// ===========================================================================
// The player's slots
// ===========================================================================

namespace {

// The value of one argument as the engine's symbol table would hold it: the
// literal itself, a constant IGI-RE names, or whatever the caller can resolve.
// Returns false when the argument's value cannot be established, which the
// callers below treat as a refusal rather than a zero.
bool ResolveInt(const CallArg& a, const ConstantResolver& extra, int* out) {
    if (a.kind == CallArg::Kind::NotLiteral) return false;
    if (a.kind == CallArg::Kind::Identifier) {
        int v = 0;
        if (KnownConstantValue(a.text, &v)) {
            *out = a.negated ? -v : v;
            return true;
        }
        if (extra && extra(a.text, &v)) {
            *out = a.negated ? -v : v;
            return true;
        }
        return false;
    }
    *out = a.AsInt();
    return true;
}

}  // namespace

WeaponCycle ParseWeaponCycleArgs(const std::vector<CallArg>& args,
                                 const ConstantResolver& extra) {
    WeaponCycle out;
    // `for (i = 0; i < *args; i++)` (humanplayer.c:1155-1156): every argument
    // is a slot, so there is nothing to validate but the argument values.
    for (const CallArg& a : args) {
        int value = 0;
        if (!ResolveInt(a, extra, &value)) {
            out.error = (a.kind == CallArg::Kind::NotLiteral)
                            ? std::string("a weapon-cycle argument is not a literal")
                            : "weapon-cycle argument '" + a.text +
                                  "' is a constant this editor cannot resolve to an id";
            return out;
        }
        out.slots.push_back(value);
    }
    out.ok = true;
    return out;
}

std::vector<std::string> EmitWeaponCycleArgs(const std::vector<int>& slots) {
    std::vector<std::string> out;
    out.reserve(slots.size());
    char buf[32];
    for (int id : slots) {
        std::snprintf(buf, sizeof(buf), "%d", id);
        out.emplace_back(buf);
    }
    return out;
}

AmmoLimits ParseAmmoLimitArgs(const std::vector<CallArg>& args,
                               const ConstantResolver& extra) {
    AmmoLimits out;
    // `for (i = 0; i < *args; gAmmoLimitCount++) { l->id = arg(i++);
    //  l->limit = arg(i++); }` (humanplayer.c:1185-1189).
    if (args.size() % 2 != 0) {
        out.error = "DefineHumanPlayerAmmoLimit takes (id, limit) pairs; got an "
                    "odd count of " + std::to_string(args.size()) +
                    " arguments, which makes the engine read past the last one";
        return out;
    }
    for (size_t i = 0; i + 1 < args.size(); i += 2) {
        const CallArg& id = args[i];
        const CallArg& limit = args[i + 1];
        AmmoSlot slot;
        if (!ResolveInt(id, extra, &slot.ammo_id)) {
            out.error = (id.kind == CallArg::Kind::NotLiteral)
                            ? std::string("an ammo-limit argument is not a literal")
                            : "ammo-limit id '" + id.text +
                                  "' is a constant this editor cannot resolve to an id";
            return out;
        }
        if (!ResolveInt(limit, extra, &slot.limit)) {
            out.error = "an ammo-limit argument is not a literal";
            return out;
        }
        out.slots.push_back(slot);
    }
    out.ok = true;
    return out;
}

std::map<std::string, int> WeaponIdConstants(
    const std::vector<WeaponTypeConfig>& roster) {
    // `Script_RegisterConstant(name, w->id)` — DefineWeaponType HOOK 0x413F90,
    // src/weapon/weapontype.c:306, with `name` read from argument 1 at :304.
    std::map<std::string, int> out;
    for (const WeaponTypeConfig& w : roster) {
        if (!w.ok) continue;
        const std::string name = w.Name();
        if (!name.empty()) out[name] = w.Id();
    }
    return out;
}

std::vector<std::string> EmitAmmoLimitArgs(const std::vector<AmmoSlot>& slots) {
    std::vector<std::string> out;
    out.reserve(slots.size() * 2);
    char buf[32];
    for (const AmmoSlot& s : slots) {
        std::snprintf(buf, sizeof(buf), "%d", s.ammo_id);
        out.emplace_back(buf);
        std::snprintf(buf, sizeof(buf), "%d", s.limit);
        out.emplace_back(buf);
    }
    return out;
}

// ===========================================================================
// Inventory drops
// ===========================================================================

bool IsInventoryDropTaskType(const std::string& task_type) {
    // The three level-file task types of the pickup family.  `GenericPickup` is
    // deliberately absent: it has no weapon and no ammo parameter
    // (GenericPickup_Init HOOK 0x46C1xx, src/comprun/pickup.c:268-271) and
    // `GenericPickup_Touch` HOOK 0x46C630 (pickup.c:361-366) delivers no
    // message at all, so it cannot carry an inventory item.
    return task_type == "GunPickup" || task_type == "AmmoPickup" ||
           task_type == "AddAmmo";
}

}  // namespace weapons
}  // namespace igi
