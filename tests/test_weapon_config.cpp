// tests/test_weapon_config.cpp
//
// Issue #49 part 1 — the weapon / item configurator's data layer.
//
// WHAT IS PROVED HERE.  Three things, in increasing order of strength:
//
//   1. the parameter table in source/runtime/weapon_config.cpp is
//      `DefineWeaponType`'s own argument order (HOOK 0x413F90,
//      src/weapon/weapontype.c:287-360), one row per `Script_GetArg*` call;
//   2. the parser/emitter round-trips a weapon definition, and the two player
//      slot tables (`DefineHumanPlayerWeaponCycle` HOOK 0x4101C0,
//      `DefineHumanPlayerAmmoLimit` HOOK 0x4102D0) round-trip their arguments;
//   3. THE CORPUS TEST — the arguments are read out of the SHIPPING
//      `WEAPONS/<NAME>/WEAPON.QVM` files and every value the editor's runtime
//      catalog claims is compared against what the engine would actually load.
//      This is what makes the configurator's numbers evidence rather than a
//      transcription; it needs IGI_GAME_PATH and self-skips without one.
//
// No test here weakens or replaces an existing one.

#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "../source/level/task_schema.h"
#include "../source/runtime/pickup_rules.h"
#include "../source/runtime/weapon_config.h"
#include "../source/weapon_system.h"

namespace {

using igi::weapons::CallArg;
using igi::weapons::CallArgs;
using igi::weapons::WeaponTypeConfig;

// --- helpers ---------------------------------------------------------------

CallArg Int(int v) {
    CallArg a;
    a.kind = CallArg::Kind::Int;
    a.int_value = v;
    return a;
}

CallArg Real(float v) {
    CallArg a;
    a.kind = CallArg::Kind::Real;
    a.real_value = v;
    return a;
}

CallArg Str(const std::string& s) {
    CallArg a;
    a.kind = CallArg::Kind::String;
    a.text = s;
    return a;
}

CallArg Ident(const std::string& s) {
    CallArg a;
    a.kind = CallArg::Kind::Identifier;
    a.text = s;
    a.value_known = igi::weapons::KnownConstantValue(s, nullptr);
    return a;
}

// The retail install root, or an empty string.
std::string GameRoot() {
    const char* env = std::getenv("IGI_GAME_PATH");
    if (env != nullptr && *env != '\0') return env;
    return std::string();
}

bool FileExists(const std::string& p);

}  // namespace

// ===========================================================================
// 1. The parameter table
// ===========================================================================

TEST(WeaponConfig, TheParamTableIsTheEnginesOwnArgumentOrder) {
    // `DefineWeaponType` makes one Script_GetArg* call per parameter, in this
    // order (src/weapon/weapontype.c:289-341).  The table must be exactly that
    // list: 48 fixed parameters, arg indices 0..47, one token each.
    ASSERT_EQ(igi::weapons::kWeaponParamCount, 48);
    for (int i = 0; i < igi::weapons::kWeaponParamCount; ++i) {
        const igi::weapons::WeaponParamDesc& p = igi::weapons::kWeaponParamTable[i];
        EXPECT_EQ(p.arg_index, i) << "row " << i << " (" << p.name << ")";
        EXPECT_EQ(p.token_count, 1) << "row " << i << " (" << p.name << ")";
    }
    // `next = 48; w->taskType = Script_GetArgInt(args, 47);` (weapontype.c:344-345)
    EXPECT_EQ(igi::weapons::kWeaponTailArgIndex, 48);
}

TEST(WeaponConfig, TheThreeParametersIssue49NamesAreWhereTheEngineReadsThem) {
    // fire rate / damage / ammo capacity, at the argument index the engine uses.
    auto at = [](int arg) -> const igi::weapons::WeaponParamDesc* {
        for (int i = 0; i < igi::weapons::kWeaponParamCount; ++i) {
            if (igi::weapons::kWeaponParamTable[i].arg_index == arg)
                return &igi::weapons::kWeaponParamTable[i];
        }
        return nullptr;
    };
    const auto* damage = at(10);
    const auto* rate = at(15);
    const auto* clip = at(16);
    ASSERT_NE(damage, nullptr);
    ASSERT_NE(rate, nullptr);
    ASSERT_NE(clip, nullptr);

    // arg 10 -> WeaponType + 0x654 (weapontypes.h:44-51), read as damage by
    // comprun/knifestab.c:108 and ammotype/gunshot.c:204.
    EXPECT_EQ(damage->type_offset, 0x654);
    // arg 15 -> WeaponType + 0x644 (`unk640[1]`, `rate`), shots per minute.
    EXPECT_EQ(rate->type_offset, 0x644);
    // arg 16 -> WeaponType + 0x648 (`unk640[2]`, `clipSize`).
    EXPECT_EQ(clip->type_offset, 0x648);

    // Every row that lands in a member names the engine function that reads it,
    // or says nothing reads it.  The three above must name a reader.
    EXPECT_STRNE(damage->engine_use, "");
    EXPECT_STRNE(rate->engine_use, "");
    EXPECT_STRNE(clip->engine_use, "");
}

TEST(WeaponConfig, EveryParamRowOffsetMatchesTheWeaponTypeLayout) {
    // The offsets are the `WeaponType` members (src/weapon/weapontypes.h:17-76),
    // whose three _Static_asserts pin 0x664, 0x6C0 and 0x6E8.
    std::map<int, int> arg_to_offset;
    for (int i = 0; i < igi::weapons::kWeaponParamCount; ++i) {
        const auto& p = igi::weapons::kWeaponParamTable[i];
        if (p.type_offset >= 0) arg_to_offset[p.arg_index] = p.type_offset;
    }
    EXPECT_EQ(arg_to_offset[5], 0x610);    // category
    EXPECT_EQ(arg_to_offset[6], 0x614);    // sightDisplay
    EXPECT_EQ(arg_to_offset[7], 0x618);    // ammoDisplay
    EXPECT_EQ(arg_to_offset[9], 0x63C);    // caliber
    EXPECT_EQ(arg_to_offset[10], 0x654);   // damage
    EXPECT_EQ(arg_to_offset[11], 0x658);   // pierce
    EXPECT_EQ(arg_to_offset[13], 0x660);   // throwSpeed
    EXPECT_EQ(arg_to_offset[14], 0x640);   // pellets
    EXPECT_EQ(arg_to_offset[15], 0x644);   // rate
    EXPECT_EQ(arg_to_offset[16], 0x648);   // clipSize
    EXPECT_EQ(arg_to_offset[17], 0x64C);   // burst
    EXPECT_EQ(arg_to_offset[18], 0x664);   // spread[0]
    EXPECT_EQ(arg_to_offset[19], 0x668);   // spread[1]
    EXPECT_EQ(arg_to_offset[20], 0x66C);   // recoilPitch
    EXPECT_EQ(arg_to_offset[21], 0x670);   // recoilYaw
    EXPECT_EQ(arg_to_offset[22], 0x674);   // recoilPitchRand
    EXPECT_EQ(arg_to_offset[23], 0x678);   // recoilYawRand
    EXPECT_EQ(arg_to_offset[25], 0x650);   // range
    EXPECT_EQ(arg_to_offset[29], 0x61C);   // key (the model name)
    EXPECT_EQ(arg_to_offset[30], 0x62C);   // casing
    EXPECT_EQ(arg_to_offset[44], 0x6B0);   // sound
    EXPECT_EQ(arg_to_offset[45], 0x6C0);   // sound2
    EXPECT_EQ(arg_to_offset[46], 0x6D0);   // detectRange
    EXPECT_EQ(arg_to_offset[47], 0x6D4);   // taskType

    // The anim2 block is authored in an order the members do NOT follow:
    // weapontype.c:332-338 reads anim2[3] and anim2[4] before anim2[2].  The
    // table records the ARGUMENT order, so 39->0x6A0 and 41->0x69C.
    EXPECT_EQ(arg_to_offset[39], 0x6A0);
    EXPECT_EQ(arg_to_offset[40], 0x6A4);
    EXPECT_EQ(arg_to_offset[41], 0x69C);
    EXPECT_EQ(arg_to_offset[42], 0x6A8);
    EXPECT_EQ(arg_to_offset[43], 0x6AC);
}

// ===========================================================================
// 2. Parsing and emitting
// ===========================================================================

TEST(WeaponConfig, ParseNeedsTheEnginesFullArgumentList) {
    std::vector<CallArg> short_list(47, Int(0));
    WeaponTypeConfig cfg = igi::weapons::ParseWeaponTypeArgs(short_list);
    EXPECT_FALSE(cfg.ok);
    EXPECT_FALSE(cfg.error.empty());

    std::vector<CallArg> full(48, Int(0));
    cfg = igi::weapons::ParseWeaponTypeArgs(full);
    EXPECT_TRUE(cfg.ok) << cfg.error;
}

TEST(WeaponConfig, ParseRefusesAComputedArgumentInsteadOfGuessing) {
    std::vector<CallArg> args(48, Int(0));
    args[16].kind = CallArg::Kind::NotLiteral;
    WeaponTypeConfig cfg = igi::weapons::ParseWeaponTypeArgs(args);
    EXPECT_FALSE(cfg.ok);
    EXPECT_NE(cfg.error.find("not a literal"), std::string::npos);
}

TEST(WeaponConfig, TheAccessorsReadTheArgumentTheEngineReads) {
    std::vector<CallArg> args;
    args.reserve(48);
    for (int i = 0; i < 48; ++i) args.push_back(Int(i));
    args[0] = Int(5);
    args[1] = Str("WEAPON_ID_AK47");
    args[5] = Ident("WEAPONTYPE_GUN");
    args[10] = Real(0.3f);
    args[15] = Int(650);
    args[16] = Int(30);
    args[25] = Int(2000);
    args[29] = Str("100_01_1");
    args[44] = Str("ak47_loop");
    args[46] = Ident("HUMANAI_DETECTIONEVENT_GUNSHOT_RANGE");
    args[47] = Ident("TASKTYPE_GUNSHOT");

    WeaponTypeConfig cfg = igi::weapons::ParseWeaponTypeArgs(args);
    ASSERT_TRUE(cfg.ok) << cfg.error;

    EXPECT_EQ(cfg.Id(), 5);
    EXPECT_EQ(cfg.Name(), "WEAPON_ID_AK47");
    EXPECT_EQ(cfg.Category(), igi::weapons::kWeaponTypeGun);
    // 0.3f widened to double is 0.30000001192092896, which is what the engine
    // loads; compare with a tolerance rather than against the decimal literal.
    EXPECT_NEAR(cfg.Damage(), 0.3, 1e-6);
    EXPECT_DOUBLE_EQ(cfg.FireRate(), 650.0);
    EXPECT_EQ(cfg.ClipSize(), 30);
    EXPECT_DOUBLE_EQ(cfg.Range(), 2000.0);
    EXPECT_EQ(cfg.Model(), "100_01_1");
    EXPECT_EQ(cfg.Sound(), "ak47_loop");
    EXPECT_EQ(cfg.FirstTaskType(), args[47].int_value);

    // `HUMANAI_DETECTIONEVENT_GUNSHOT_RANGE` is registered by another script, so
    // the editor cannot prove its value; the flag says so rather than reporting
    // a zero as if it were the engine's number.
    EXPECT_FALSE(args[46].value_known);
    // `WEAPONTYPE_GUN` IS registered by WeaponType_Init (weapontype.c:246), so
    // that one is exact.
    EXPECT_TRUE(args[5].value_known);
}

TEST(WeaponConfig, KnownConstantsAreTheEnginesOwnRegistrationList) {
    int v = -1;
    ASSERT_TRUE(igi::weapons::KnownConstantValue("WEAPONTYPE_GUN", &v));
    EXPECT_EQ(v, 0);
    ASSERT_TRUE(igi::weapons::KnownConstantValue("WEAPONTYPE_PROXIMITYMINE", &v));
    EXPECT_EQ(v, 7);
    ASSERT_TRUE(igi::weapons::KnownConstantValue("SIGHTDISPLAYTYPE_CROSSHAIR", &v));
    EXPECT_EQ(v, 1);
    ASSERT_TRUE(igi::weapons::KnownConstantValue("AMMODISPLAYTYPE_GRENADE", &v));
    EXPECT_EQ(v, 5);
    // A task type constant is registered by the task system, not here.
    EXPECT_FALSE(igi::weapons::KnownConstantValue("TASKTYPE_GUNSHOT", &v));
}

TEST(WeaponConfig, EmitRoundTripsEveryLiteralKind) {
    std::vector<CallArg> args;
    args.push_back(Int(5));
    args.push_back(Str("WEAPON_ID_AK47"));
    args.push_back(Ident("WEAPONTYPE_GUN"));
    args.push_back(Real(0.3f));
    CallArg neg = Real(3.5f);
    neg.negated = true;
    args.push_back(neg);
    CallArg neg_int = Int(1);
    neg_int.negated = true;
    args.push_back(neg_int);

    bool all = false;
    const std::vector<std::string> tokens =
        igi::weapons::EmitWeaponTypeArgs(args, &all);
    ASSERT_TRUE(all);
    ASSERT_EQ(tokens.size(), args.size());
    EXPECT_EQ(tokens[0], "5");
    EXPECT_EQ(tokens[1], "\"WEAPON_ID_AK47\"");
    EXPECT_EQ(tokens[2], "WEAPONTYPE_GUN");
    EXPECT_EQ(tokens[3], "0.30000001192092896");  // %.17g of the float 0.3f
    EXPECT_EQ(tokens[4], "-3.5");
    EXPECT_EQ(tokens[5], "-1");
}

TEST(WeaponConfig, EmitKeepsAnUnparsedArgumentRatherThanShiftingTheRest) {
    // Dropping an argument would move every later one, which is worse than
    // writing a placeholder the caller is told about.
    std::vector<CallArg> args;
    args.push_back(Int(1));
    args.push_back(CallArg{});  // NotLiteral
    args.push_back(Int(3));
    bool all = true;
    const std::vector<std::string> tokens =
        igi::weapons::EmitWeaponTypeArgs(args, &all);
    EXPECT_FALSE(all);
    ASSERT_EQ(tokens.size(), 3u);
    EXPECT_EQ(tokens[2], "3");
}

TEST(WeaponConfig, TheCycleAndTheAmmoLimitsRoundTrip) {
    std::vector<CallArg> cycle_args;
    cycle_args.push_back(Ident("WEAPON_ID_KNIFE"));
    cycle_args.push_back(Int(1));
    cycle_args.push_back(Int(21));

    // `WEAPON_ID_KNIFE` is registered by DefineWeaponType, not by
    // WeaponType_Init, so the editor needs the roster to resolve it — exactly
    // what a UI that has read WEAPONS/*/WEAPON.QVM can supply.
    const std::map<std::string, int> weapon_ids = {{"WEAPON_ID_KNIFE", 20}};
    const igi::weapons::ConstantResolver resolve =
        [&weapon_ids](const std::string& name, int* out) {
            const auto it = weapon_ids.find(name);
            if (it == weapon_ids.end()) return false;
            *out = it->second;
            return true;
        };

    // Without the roster it must REFUSE, not write a zero over a real id.
    const igi::weapons::WeaponCycle unresolved =
        igi::weapons::ParseWeaponCycleArgs(cycle_args);
    EXPECT_FALSE(unresolved.ok);
    EXPECT_TRUE(unresolved.slots.empty());

    const igi::weapons::WeaponCycle cycle =
        igi::weapons::ParseWeaponCycleArgs(cycle_args, resolve);
    ASSERT_TRUE(cycle.ok) << cycle.error;
    ASSERT_EQ(cycle.slots.size(), 3u);
    EXPECT_EQ(cycle.slots[0], 20);
    EXPECT_EQ(cycle.slots[2], 21);
    const std::vector<std::string> emitted =
        igi::weapons::EmitWeaponCycleArgs(cycle.slots);
    ASSERT_EQ(emitted.size(), 3u);
    EXPECT_EQ(emitted[1], "1");

    std::vector<CallArg> limit_args;
    limit_args.push_back(Int(127));
    limit_args.push_back(Int(30));
    limit_args.push_back(Int(919));
    limit_args.push_back(Int(68));
    const igi::weapons::AmmoLimits limits =
        igi::weapons::ParseAmmoLimitArgs(limit_args);
    ASSERT_TRUE(limits.ok) << limits.error;
    ASSERT_EQ(limits.slots.size(), 2u);
    EXPECT_EQ(limits.slots[0].ammo_id, 127);
    EXPECT_EQ(limits.slots[0].limit, 30);
    EXPECT_EQ(limits.slots[1].ammo_id, 919);
    EXPECT_EQ(limits.slots[1].limit, 68);
    const std::vector<std::string> limit_tokens =
        igi::weapons::EmitAmmoLimitArgs(limits.slots);
    ASSERT_EQ(limit_tokens.size(), 4u);
    EXPECT_EQ(limit_tokens[0], "127");
    EXPECT_EQ(limit_tokens[3], "68");
}

TEST(WeaponConfig, TheAmmoLimitParserRefusesAnOddArgumentCount) {
    // `for (i = 0; i < *args; gAmmoLimitCount++) { l->id = arg(i++);
    //  l->limit = arg(i++); }` (humanplayer.c:1185-1189): an odd count makes the
    // engine read one argument past the end.  The editor refuses instead.
    std::vector<CallArg> odd;
    odd.push_back(Int(1));
    odd.push_back(Int(2));
    odd.push_back(Int(3));
    const igi::weapons::AmmoLimits limits = igi::weapons::ParseAmmoLimitArgs(odd);
    EXPECT_FALSE(limits.ok);
    EXPECT_FALSE(limits.error.empty());
    EXPECT_TRUE(limits.slots.empty());
}

TEST(WeaponConfig, TheInventoryDropTypesAreTheEnginesThree) {
    EXPECT_TRUE(igi::weapons::IsInventoryDropTaskType("GunPickup"));
    EXPECT_TRUE(igi::weapons::IsInventoryDropTaskType("AmmoPickup"));
    EXPECT_TRUE(igi::weapons::IsInventoryDropTaskType("AddAmmo"));
    // GenericPickup delivers no message at all
    // (GenericPickup_Touch 0x46C630, src/comprun/pickup.c:361-366), so it
    // cannot carry an inventory item.
    EXPECT_FALSE(igi::weapons::IsInventoryDropTaskType("GenericPickup"));
    EXPECT_FALSE(igi::weapons::IsInventoryDropTaskType("HumanPlayer"));
}

// ===========================================================================
// 3. The inspector rows (issue #49's level-file half)
// ===========================================================================

TEST(WeaponConfigSchema, AddAmmoHasAnInspectorRowDerivedFromTheEngineTable) {
    // Before #49 GetSchema("AddAmmo") was null, so the property panel rendered
    // nothing for the task the retail data authors as the player's starting
    // ammunition (MISSIONS/location0/LEVEL2/objects.qsc:36 and :1080).
    const TaskSchemaNS::TaskSchema* sc = TaskSchemaNS::GetBuiltinSchema("AddAmmo");
    ASSERT_NE(sc, nullptr);
    ASSERT_EQ(sc->size(), 2u);
    EXPECT_EQ((*sc)[0].name, "ID");
    EXPECT_EQ((*sc)[0].typeName, "EnumString32");
    EXPECT_EQ((*sc)[0].argOffset, 3);  // argTokens: [0]=id, [1]=type, [2]=name
    EXPECT_EQ((*sc)[1].name, "Ammo");
    EXPECT_EQ((*sc)[1].typeName, "Int16");
    EXPECT_EQ((*sc)[1].argOffset, 4);
}

TEST(WeaponConfigSchema, ThePickupRowsAgreeWithThePortedParamTables) {
    // The three rows are derived from igi::PickupLayouts(), so the schema and
    // the engine table cannot disagree; this asserts the derivation actually
    // ran and produced the engine's names, types and indices.
    for (int li = 0; li < igi::PickupLayoutCount(); ++li) {
        const igi::PickupLayout& layout = igi::PickupLayouts()[li];
        const TaskSchemaNS::TaskSchema* sc =
            TaskSchemaNS::GetBuiltinSchema(layout.task_type);
        ASSERT_NE(sc, nullptr) << layout.task_type;
        ASSERT_EQ(static_cast<int>(sc->size()), layout.param_count)
            << layout.task_type;

        int offset = 3;
        for (int i = 0; i < layout.param_count; ++i) {
            const igi::PickupParam& p = layout.params[i];
            EXPECT_EQ((*sc)[static_cast<size_t>(i)].name, p.name)
                << layout.task_type << " row " << i;
            EXPECT_EQ((*sc)[static_cast<size_t>(i)].argOffset, offset)
                << layout.task_type << " row " << i;
            offset += igi::PickupParamArgCount(p.type);
        }

        // And the enum/count indices the layout advertises really are inside
        // the row set, so a UI can point at them.
        if (layout.enum_arg_index >= 0)
            EXPECT_LT(layout.enum_arg_index, offset) << layout.task_type;
        if (layout.count_arg_index >= 0)
            EXPECT_LT(layout.count_arg_index, offset) << layout.task_type;
    }
}

TEST(WeaponConfigSchema, GenericPickupKeepsExactlyItsThreeRows) {
    // GenericPickup used to be built by the generic "Position/Orientation/Model"
    // loop AND is now built by the pickup-family loop, so it is the one type
    // that could have been double-counted.  Its three rows are the engine's
    // (GenericPickup_Init HOOK 0x46C0C0, src/comprun/pickup.c:268-271).
    const TaskSchemaNS::TaskSchema* sc =
        TaskSchemaNS::GetBuiltinSchema("GenericPickup");
    ASSERT_NE(sc, nullptr);
    ASSERT_EQ(sc->size(), 3u);
    EXPECT_EQ((*sc)[0].argOffset, 3);
    EXPECT_EQ((*sc)[1].argOffset, 6);
    EXPECT_EQ((*sc)[2].name, "Model");
    EXPECT_EQ((*sc)[2].typeName, "String16");
    EXPECT_EQ((*sc)[2].argOffset, 9);
}

TEST(WeaponConfigSchema, AddAmmoIsStillNotAPlacedPickup) {
    // The predicate is unchanged: AddAmmo has no Position and no touch handler.
    // Having an inspector row must not make the viewport draw a touch volume at
    // the origin for it.
    EXPECT_FALSE(igi::IsPlacedPickupType("AddAmmo"));
    EXPECT_TRUE(igi::IsPlacedPickupType("GunPickup"));
    EXPECT_TRUE(igi::IsPlacedPickupType("AmmoPickup"));
    EXPECT_TRUE(igi::IsPlacedPickupType("GenericPickup"));
}

// ===========================================================================
// 4. Corpus: the shipping weapon files
// ===========================================================================

namespace {

// The 19 weapons the editor's runtime catalog carries, by id -> directory name.
const std::map<int, const char*>& CatalogDirectories() {
    static const std::map<int, const char*> kDirs = {
        {1, "GLOCK"},         {3, "DESERTEAGLE"}, {4, "M16A2"},
        {5, "AK47"},          {6, "UZI"},         {7, "MP5SD"},
        {8, "SPAS12"},        {9, "JACKHAMMER"}, {10, "MINIMI"},
        {11, "DRAGUNOV"},     {12, "RPG18"},      {13, "UZIX2"},
        {14, "GRENADE"},      {15, "FLASHBANG"},  {16, "PROXIMITYMINE"},
        {18, "BINOCULARS"},   {19, "MEDIPACK"},   {20, "KNIFE"},
        {21, "COLTANACONDA"},
    };
    return kDirs;
}

}  // namespace

TEST(WeaponConfigCorpus, TheRetailAk47ParsesIntoTheEnginesOwnNumbers) {
    const std::string root = GameRoot();
    if (root.empty())
        GTEST_SKIP() << "set IGI_GAME_PATH to a retail install to run this test";
    const std::string path = root + "/WEAPONS/AK47/WEAPON.QVM";
    if (!FileExists(path))
        GTEST_SKIP() << path << " not found";

    const CallArgs call = igi::weapons::ReadCallArgsFromQvmFile(path, "DefineWeaponType");
    ASSERT_TRUE(call.found) << call.error;
    EXPECT_EQ(call.callee, "DefineWeaponType");

    const WeaponTypeConfig cfg = igi::weapons::ParseWeaponTypeArgs(call.args);
    ASSERT_TRUE(cfg.ok) << cfg.error;

    // The AK47's shipped values.  These are the numbers the engine loads.
    EXPECT_EQ(cfg.Id(), 5);
    EXPECT_EQ(cfg.Name(), "WEAPON_ID_AK47");
    EXPECT_EQ(cfg.Category(), igi::weapons::kWeaponTypeGun);
    EXPECT_EQ(cfg.Calibre(), 762);
    EXPECT_NEAR(cfg.Damage(), 0.3, 1e-6);      // arg 10 -> +0x654
    EXPECT_DOUBLE_EQ(cfg.Pierce(), 2.0);       // arg 11 -> +0x658
    EXPECT_DOUBLE_EQ(cfg.ThrowSpeed(), 710.0); // arg 13 -> +0x660
    EXPECT_EQ(cfg.Pellets(), 1);               // arg 14
    EXPECT_DOUBLE_EQ(cfg.FireRate(), 650.0);   // arg 15 -> +0x644
    EXPECT_EQ(cfg.ClipSize(), 30);             // arg 16 -> +0x648
    EXPECT_EQ(cfg.Burst(), 30);                // arg 17 -> +0x64C
    EXPECT_DOUBLE_EQ(cfg.SpreadFirst(), -3.5); // arg 18, authored `MINUS 3.5`
    EXPECT_DOUBLE_EQ(cfg.SpreadRest(), 3.5);   // arg 19
    EXPECT_DOUBLE_EQ(cfg.Range(), 2000.0);     // arg 25 -> +0x650
    EXPECT_EQ(cfg.Model(), "100_01_1");        // arg 29 -> +0x61C
    EXPECT_EQ(cfg.CasingModel(), "113_01_1");  // arg 30 -> +0x62C
    EXPECT_EQ(cfg.Sound(), "ak47_loop");       // arg 44
    EXPECT_EQ(cfg.Sound2(), "ak47_loop_e");    // arg 45

    // The tail is variable and the editor does not guess at it: the AK47's
    // `taskType` is TASKTYPE_GUNSHOT and the file carries one more argument.
    ASSERT_EQ(call.args.size(), 49u);
    EXPECT_EQ(cfg.Tail().size(), 1u);
    EXPECT_EQ(cfg.Tail()[0].text, "TASKTYPE_GUN");
}

TEST(WeaponConfigCorpus, TheRetailRosterMatchesTheParsedWeaponTypes) {
    // The strong claim: every value the editor's runtime catalog uses is the
    // value in the shipping file.  `WeaponSystem::GetVanillaWeaponCatalog()` is
    // a hand-written table (source/weapon_system.cpp:145-216); this is what
    // checks it.
    const std::string root = GameRoot();
    if (root.empty())
        GTEST_SKIP() << "set IGI_GAME_PATH to a retail install to run this test";

    const igi::WeaponSystem system;
    const auto& catalog = igi::WeaponSystem::GetVanillaWeaponCatalog();
    ASSERT_FALSE(catalog.empty());

    int checked = 0;
    for (const igi::WeaponDefinition& w : catalog) {
        const auto it = CatalogDirectories().find(static_cast<int>(w.id));
        ASSERT_NE(it, CatalogDirectories().end()) << "unmapped weapon id " << w.id;
        const std::string path =
            root + "/WEAPONS/" + it->second + "/WEAPON.QVM";
        if (!FileExists(path))
            GTEST_SKIP() << path << " not found";

        const CallArgs call =
            igi::weapons::ReadCallArgsFromQvmFile(path, "DefineWeaponType");
        ASSERT_TRUE(call.found) << path << ": " << call.error;
        const WeaponTypeConfig cfg = igi::weapons::ParseWeaponTypeArgs(call.args);
        ASSERT_TRUE(cfg.ok) << path << ": " << cfg.error;

        EXPECT_EQ(cfg.Id(), static_cast<int>(w.id)) << it->second;
        EXPECT_EQ(cfg.Name(), w.script_id) << it->second;
        EXPECT_EQ(cfg.Model(), w.model_id) << it->second;
        EXPECT_EQ(cfg.Calibre(), w.calibre_id) << it->second;
        // rounds_per_minute <- arg 15
        EXPECT_DOUBLE_EQ(cfg.FireRate(), static_cast<double>(w.rounds_per_minute))
            << it->second << " fire rate (arg 15)";
        // clip_capacity <- arg 16.
        //
        // ONE DELIBERATE DIVERGENCE: WEAPONS/KNIFE/WEAPON.QVM ships -1, and the
        // runtime catalog carries 1 because `WeaponDefinition::clip_capacity` is
        // the magazine `Reload()` fills (`rounds_needed = clip_capacity -
        // current_clip_ammo_`, source/weapon_system.cpp) and a -1 there would
        // make the knife load 0xFFFFFFFF rounds.  The shipped value is asserted
        // separately below so the divergence is pinned rather than hidden.
        if (w.id == 20) {
            EXPECT_EQ(cfg.ClipSize(), -1) << "the retail knife's clip size";
        } else {
            EXPECT_EQ(cfg.ClipSize(), static_cast<int>(w.clip_capacity))
                << it->second << " clip size (arg 16)";
        }
        // maximum_rounds_per_burst <- arg 17
        EXPECT_EQ(cfg.Burst(), static_cast<int>(w.maximum_rounds_per_burst))
            << it->second << " burst (arg 17)";
        // bullets_per_round <- arg 14
        EXPECT_EQ(cfg.Pellets(), static_cast<int>(w.bullets_per_round))
            << it->second << " pellets (arg 14)";
        // muzzle_velocity <- arg 13 (throwSpeed)
        EXPECT_DOUBLE_EQ(cfg.ThrowSpeed(), static_cast<double>(w.muzzle_velocity))
            << it->second << " throw speed (arg 13)";
        // effective_range_meters <- arg 25 (range)
        EXPECT_DOUBLE_EQ(cfg.Range(), static_cast<double>(w.effective_range_meters))
            << it->second << " range (arg 25)";
        // engine_damage <- arg 10 (the retail damage the engine applies)
        EXPECT_NEAR(cfg.Damage(), static_cast<double>(w.engine_damage), 1e-6)
            << it->second << " retail damage (arg 10)";
        // spread[0] / spread[1] <- args 18 / 19
        EXPECT_DOUBLE_EQ(cfg.SpreadFirst(), static_cast<double>(w.minimum_spread_degrees))
            << it->second << " spread[0] (arg 18)";
        EXPECT_DOUBLE_EQ(cfg.SpreadRest(), static_cast<double>(w.maximum_spread_degrees))
            << it->second << " spread[1] (arg 19)";
        // sound / sound2 <- args 44 / 45
        EXPECT_EQ(cfg.Sound(), w.fire_sound) << it->second << " sound (arg 44)";
        EXPECT_EQ(cfg.Sound2(), w.fire_loop_end_sound)
            << it->second << " sound2 (arg 45)";
        ++checked;
    }
    EXPECT_EQ(checked, static_cast<int>(catalog.size()));
}

TEST(WeaponConfigCorpus, TheRetailPlayerSlotsMatchTheEditorsTranscription) {
    const std::string root = GameRoot();
    if (root.empty())
        GTEST_SKIP() << "set IGI_GAME_PATH to a retail install to run this test";
    const std::string path = root + "/humanplayer/humanplayer.qvm";
    if (!FileExists(path))
        GTEST_SKIP() << path << " not found";

    // DefineHumanPlayerWeaponCycle HOOK 0x4101C0
    // (src/humanplayer/humanplayer.c:1149-1160): every argument is a slot.
    const CallArgs cycle_call =
        igi::weapons::ReadCallArgsFromQvmFile(path, "DefineHumanPlayerWeaponCycle");
    ASSERT_TRUE(cycle_call.found) << cycle_call.error;

    // The slots are authored as `WEAPON_ID_*` names, which DefineWeaponType
    // registers against the id it was given (src/weapon/weapontype.c:306).  Read
    // the roster so the resolver has them, the way a configurator UI would.
    std::map<std::string, int> weapon_ids;
    for (const igi::WeaponDefinition& w : igi::WeaponSystem::GetVanillaWeaponCatalog()) {
        const auto dir = CatalogDirectories().find(static_cast<int>(w.id));
        if (dir == CatalogDirectories().end()) continue;
        const std::string wpath = root + "/WEAPONS/" + dir->second + "/WEAPON.QVM";
        if (!FileExists(wpath)) continue;
        const CallArgs wcall = igi::weapons::ReadCallArgsFromQvmFile(wpath, "DefineWeaponType");
        if (!wcall.found) continue;
        const WeaponTypeConfig wcfg = igi::weapons::ParseWeaponTypeArgs(wcall.args);
        if (wcfg.ok && !wcfg.Name().empty()) weapon_ids[wcfg.Name()] = wcfg.Id();
    }
    ASSERT_FALSE(weapon_ids.empty());
    const igi::weapons::ConstantResolver resolve =
        [&weapon_ids](const std::string& name, int* out) {
            const auto it = weapon_ids.find(name);
            if (it == weapon_ids.end()) return false;
            *out = it->second;
            return true;
        };

    const igi::weapons::WeaponCycle cycle =
        igi::weapons::ParseWeaponCycleArgs(cycle_call.args, resolve);

    // DefineHumanPlayerAmmoLimit HOOK 0x4102D0 (humanplayer.c:1178-1193):
    // (id, limit) pairs.
    const CallArgs ammo_call =
        igi::weapons::ReadCallArgsFromQvmFile(path, "DefineHumanPlayerAmmoLimit");
    ASSERT_TRUE(ammo_call.found) << ammo_call.error;
    // `AMMO_ID_*` is registered by DefineAmmoType (src/ammotype/ammotype.c:95),
    // so read the ammo table too.
    std::map<std::string, int> ammo_ids;
    for (const char* dir : {"12", "127", "357", "44", "556", "762", "919", "1000"}) {
        const std::string apath = root + "/AMMO/" + dir + "/AMMO.QVM";
        if (!FileExists(apath)) continue;
        const CallArgs acall = igi::weapons::ReadCallArgsFromQvmFile(apath, "DefineAmmoType");
        if (!acall.found || acall.args.size() < 2) continue;
        ammo_ids[acall.args[1].AsString()] = acall.args[0].AsInt();
    }
    const igi::weapons::ConstantResolver resolve_ammo =
        [&ammo_ids](const std::string& name, int* out) {
            const auto it = ammo_ids.find(name);
            if (it == ammo_ids.end()) return false;
            *out = it->second;
            return true;
        };
    const igi::weapons::AmmoLimits limits =
        igi::weapons::ParseAmmoLimitArgs(ammo_call.args, resolve_ammo);
    ASSERT_TRUE(limits.ok) << limits.error;
    EXPECT_FALSE(limits.slots.empty());
    for (const igi::weapons::AmmoSlot& s : limits.slots)
        EXPECT_GE(s.limit, 0) << "ammo id " << s.ammo_id;
}

TEST(WeaponConfigCorpus, TheEditorNeverWritesBackAWeaponFileItCouldNotRead) {
    // The round-trip guarantee: reading a shipping file and emitting it again
    // yields the same number of arguments, so nothing is silently dropped.
    const std::string root = GameRoot();
    if (root.empty())
        GTEST_SKIP() << "set IGI_GAME_PATH to a retail install to run this test";
    const std::string path = root + "/WEAPONS/AK47/WEAPON.QVM";
    if (!FileExists(path))
        GTEST_SKIP() << path << " not found";

    const CallArgs call = igi::weapons::ReadCallArgsFromQvmFile(path, "DefineWeaponType");
    ASSERT_TRUE(call.found) << call.error;
    bool all_literal = false;
    const std::vector<std::string> tokens =
        igi::weapons::EmitWeaponTypeArgs(call.args, &all_literal);
    EXPECT_TRUE(all_literal) << "a shipping weapon definition must be all literals";
    EXPECT_EQ(tokens.size(), call.args.size());
}

// ---------------------------------------------------------------------------
// A tiny existence check kept out of the anonymous namespace's forward decls.
// ---------------------------------------------------------------------------
namespace {
bool FileExists(const std::string& p) {
    if (p.empty()) return false;
    FILE* f = std::fopen(p.c_str(), "rb");
    if (f == nullptr) return false;
    std::fclose(f);
    return true;
}
}  // namespace
