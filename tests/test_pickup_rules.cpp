// test_pickup_rules.cpp - weapon/ammo pickup parity (#75).
//
// The oracle for every number here is IGI-RE (read-only). Key citations:
//   comprun/pickup.c:159-170          the touch constants (2867.2 / 4096.0)
//   comprun/pickup.c:172-200          radius() and touches()
//   comprun/pickup.c:203-225          register_flag / unregister_flag
//   comprun/pickup.c:250-273          GenericPickup_Init 0x46C0C0
//   comprun/pickup.c:291-314          GenericPickup_Create 0x46C2B0 / _Run 0x46C3C0
//   comprun/pickup.c:361-366          GenericPickup_Touch 0x46C630
//   comprun/pickup.c:460-481          GunPickup_Init 0x46CB70
//   comprun/pickup.c:505-558          GunPickup_Create 0x46CD20 / _Run 0x46CEA0
//   comprun/pickup.c:567-585          GunPickup_Changed 0x46D050
//   comprun/pickup.c:599-616          GunPickup_Touch 0x46D230
//   comprun/pickup.c:642-664          AmmoPickup_Init 0x46D420
//   comprun/pickup.c:680-685          AmmoPickup_Run 0x46D740
//   comprun/pickup.c:688-695          Pickup_Destroy 0x46D760
//   comprun/pickup.c:704-720          AmmoPickup_Changed 0x46D7D0
//   comprun/pickup.c:734-749          AmmoPickup_Touch 0x46D990
//   comprun/pickup.c:766-798          AddAmmo_Init 0x46DB40 / AddAmmo_Run 0x46DC40
//   param/task.c:123-135              Task_GetName 0x4F1270
//   math/vec3.c:41-45                 Vec3d_Length 0x4B3130
//   lod/mesh3d.c:381-387              VirModel_GetRadius 0x4D0950
//   magicobj/magicobj.c:340-342       RigidDynCubeObj_Init 0x4C5710 (scale = 1)
//   weapon/weapontypes.h:18-73        WeaponType: key @0x61C, clipSize @0x648
//   ammotype/ammotype.h:19-30         AmmoType: model name @0x148
//   weapon/weapontype.c:287-360       DefineWeaponType 0x413F90 (arg 16 = clipSize)
//   humanbase/human.c:687-728         WeaponAmmoPickup 0x45FF80 / WeaponGunPickup 0x45FFC0
//   physobj/paramops.c:627-633        ParamInt16_Parse 0x4F0110 (no clamp)
//
// The corpus half reads the retail OBJECTS.QVM with the editor's own QVM_Parse
// and cross-checks LEVEL1/LEVEL2 - the only levels that both ship a decompiled
// objects.qsc and place pickups - against that human-readable text, the way
// tests/test_mission_fog.cpp does.

#include "../source/level/qvm_parser.h"
#include "../source/lod/mef_mesh_header.h"
#include "../source/runtime/pickup_rules.h"
#include "../source/weapon_system.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;

fs::path GameRoot() {
    if (const char* env = std::getenv("IGI_GAME_PATH"); env != nullptr && *env != '\0')
        return fs::path(env);
    return fs::path("/Users/haseeb-mir/Downloads/IGI");
}

fs::path LevelDir(int level) {
    return GameRoot() / "MISSIONS" / "location0" / ("LEVEL" + std::to_string(level));
}

// ---------------------------------------------------------------------------
// OBJECTS.QVM bytecode -> Task_New / Task_DeclareParameters argument tokens
// ---------------------------------------------------------------------------

bool IsStringPush(QVMOpType type) {
    return type == QVMOpType::PUSHS || type == QVMOpType::PUSHSI ||
           type == QVMOpType::PUSHSIB || type == QVMOpType::PUSHSIW;
}

bool IsIntPush(QVMOpType type) {
    return type == QVMOpType::PUSHI || type == QVMOpType::PUSHII ||
           type == QVMOpType::PUSHIIB || type == QVMOpType::PUSHIIW ||
           type == QVMOpType::PUSHB || type == QVMOpType::PUSHW;
}

bool IsIdentifierPush(QVMOpType type) {
    return type == QVMOpType::PUSHII || type == QVMOpType::PUSHIIB ||
           type == QVMOpType::PUSHIIW;
}

std::string StringPushText(const QVMFile& qvm, const QVMInstruction& in) {
    // PUSHS/PUSHI carry the text inline; the indexed forms index qvm.strings.
    if (!in.inline_text.empty()) return in.inline_text;
    return in.operand < qvm.strings.size() ? qvm.strings[in.operand] : std::string();
}

std::string FormatFloat(float value) {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.9g", value);
    return buffer;
}

// True when the instruction pushed a value; the value is appended to `tokens`.
// Returns false when the instruction is not a value push, which ends the
// argument list.
bool AppendPush(const QVMFile& qvm, const QVMInstruction& in, std::vector<std::string>& tokens) {
    if (in.type == QVMOpType::PUSH0) { tokens.push_back("0"); return true; }
    if (in.type == QVMOpType::PUSH1) { tokens.push_back("1"); return true; }
    if (in.type == QVMOpType::PUSHF) { tokens.push_back(FormatFloat(in.operand_float)); return true; }
    if (IsStringPush(in.type)) { tokens.push_back(StringPushText(qvm, in)); return true; }
    if (IsIdentifierPush(in.type)) {
        // The identifier table of a retail OBJECTS.QVM is exactly
        // ["Task_DeclareParameters","Task_New","TRUE","FALSE"], so an identifier
        // push is either a boolean literal (a real argument) or the function
        // being called (which ends the list).
        const std::string text =
            in.operand < qvm.identifiers.size() ? qvm.identifiers[in.operand] : std::string();
        if (text == "Task_New" || text == "Task_DeclareParameters") return false;
        tokens.push_back(text);
        return true;
    }
    if (IsIntPush(in.type)) { tokens.push_back(std::to_string(static_cast<int>(in.operand))); return true; }
    return false;
}

// Walks one call's argument list forward from its type-string push, in the
// editor's argTokens layout: [0]=task id (synthesised as -1, because the id is
// pushed BEFORE the type and is not part of this walk), [1]=type, [2]=name,
// [3]=first parameter.
std::vector<std::string> ReadCallTokens(const QVMFile& qvm, std::size_t type_index,
                                        std::size_t max_tokens) {
    std::vector<std::string> tokens;
    tokens.push_back("-1");
    tokens.push_back(StringPushText(qvm, qvm.instructions[type_index]));

    for (std::size_t i = type_index + 1;
         i < qvm.instructions.size() && tokens.size() < max_tokens; ++i) {
        const QVMInstruction& in = qvm.instructions[i];
        if (in.type == QVMOpType::BRK) continue;
        if (in.type == QVMOpType::MINUS) {
            if (!tokens.empty()) tokens.back() = "-" + tokens.back();
            continue;
        }
        if (!AppendPush(qvm, in, tokens)) break;
    }
    return tokens;
}

// The five parameter type names the pickup ParamTables use. A declaration's
// fourth token is one of these; an instance's fourth token is a coordinate (or,
// for AddAmmo, its first enum string), never a type name.
bool IsParamTypeName(const std::string& text) {
    return text == "ObjectPos" || text == "Real32x9" || text == "String16" ||
           text == "EnumString32" || text == "Int16";
}

// Every `Task_New(..., "<task_type>", ...)` in the file, in file order.
// `Task_DeclareParameters("<task_type>", ...)` is skipped.
std::vector<std::vector<std::string>> ExtractInstances(const QVMFile& qvm,
                                                       const std::string& task_type) {
    std::vector<std::vector<std::string>> out;
    for (std::size_t i = 0; i < qvm.instructions.size(); ++i) {
        const QVMInstruction& in = qvm.instructions[i];
        if (!IsStringPush(in.type)) continue;
        if (StringPushText(qvm, in) != task_type) continue;

        const std::vector<std::string> tokens = ReadCallTokens(qvm, i, 20);
        if (tokens.size() < 4) continue;
        if (IsParamTypeName(tokens[3])) continue;  // a declaration, not an instance
        out.push_back(tokens);
    }
    return out;
}

// The (parameter name, IGI-RE type name) pairs of the first
// `Task_DeclareParameters("<task_type>", ...)` in the file. The task type string
// itself is the call's first argument and is not part of the pairs.
std::vector<std::pair<std::string, std::string>> ExtractDeclaration(const QVMFile& qvm,
                                                                   const std::string& task_type) {
    std::vector<std::pair<std::string, std::string>> out;
    for (std::size_t i = 0; i < qvm.instructions.size(); ++i) {
        const QVMInstruction& in = qvm.instructions[i];
        if (!IsStringPush(in.type)) continue;
        if (StringPushText(qvm, in) != task_type) continue;

        std::vector<std::string> tokens;
        for (std::size_t j = i + 1; j < qvm.instructions.size(); ++j) {
            const QVMInstruction& k = qvm.instructions[j];
            if (k.type == QVMOpType::BRK) continue;
            if (!IsStringPush(k.type)) break;
            tokens.push_back(StringPushText(qvm, k));
        }
        if (tokens.size() < 2) continue;
        for (std::size_t t = 0; t + 1 < tokens.size(); t += 2) {
            out.emplace_back(tokens[t], tokens[t + 1]);
        }
        return out;
    }
    return out;
}

// ---------------------------------------------------------------------------
// objects.qsc text -> the same tokens
// ---------------------------------------------------------------------------

std::string ReadFile(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return {};
    return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

// Splits the argument list of every `Task_New(...)` call in a decompiled .qsc,
// honouring quoted strings and nested parentheses, then keeps the calls whose
// second argument is one of `task_types`.
std::vector<std::vector<std::string>> ParseCallsFromText(const std::string& text,
                                                         const std::vector<std::string>& task_types) {
    std::vector<std::vector<std::string>> out;
    const std::string needle = "Task_New(";
    std::size_t pos = text.find(needle);

    while (pos != std::string::npos) {
        const std::size_t open = pos + needle.size() - 1;
        std::size_t index = open + 1;
        int depth = 1;
        bool in_string = false;
        bool escape = false;
        for (; index < text.size(); ++index) {
            const char c = text[index];
            if (escape) { escape = false; continue; }
            if (in_string && c == '\\') { escape = true; continue; }
            if (c == '"') { in_string = !in_string; continue; }
            if (in_string) continue;
            if (c == '(') ++depth;
            if (c == ')' && --depth == 0) break;
        }
        if (index >= text.size()) break;

        const std::string body = text.substr(open + 1, index - open - 1);
        std::vector<std::string> args;
        std::string current;
        depth = 0;
        in_string = false;
        escape = false;
        for (char c : body) {
            if (escape) { current.push_back(c); escape = false; continue; }
            if (in_string && c == '\\') { current.push_back(c); escape = true; continue; }
            if (c == '"') { in_string = !in_string; current.push_back(c); continue; }
            if (!in_string && c == '(') ++depth;
            if (!in_string && c == ')') --depth;
            if (!in_string && depth == 0 && c == ',') { args.push_back(current); current.clear(); continue; }
            current.push_back(c);
        }
        if (!current.empty()) args.push_back(current);

        for (std::string& arg : args) {
            const std::size_t b = arg.find_first_not_of(" \t\r\n");
            const std::size_t e = arg.find_last_not_of(" \t\r\n");
            arg = (b == std::string::npos) ? std::string() : arg.substr(b, e - b + 1);
            if (arg.size() >= 2 && arg.front() == '"' && arg.back() == '"') {
                arg = arg.substr(1, arg.size() - 2);
            }
        }

        if (args.size() > 1) {
            for (const std::string& type : task_types) {
                if (args[1] == type) { out.push_back(args); break; }
            }
        }
        // The decompiled .qsc is a nested Task_New tree, so resume just past this
        // call's own name rather than past its matching paren.
        pos = text.find(needle, pos + needle.size());
    }
    return out;
}

// True when both tokens denote the same authored value: equal as float when both
// parse as numbers, equal as strings otherwise.
bool SameToken(const std::string& a, const std::string& b) {
    char* end_a = nullptr;
    char* end_b = nullptr;
    const float fa = std::strtof(a.c_str(), &end_a);
    const float fb = std::strtof(b.c_str(), &end_b);
    const bool numeric_a = end_a != a.c_str() && *end_a == '\0';
    const bool numeric_b = end_b != b.c_str() && *end_b == '\0';
    if (numeric_a && numeric_b) return fa == fb;
    return a == b;
}

// The retail weapon table: `DefineWeaponType` 0x413F90 (weapon/weapontype.c:287)
// is called once per WEAPONS/<name>/WEAPON.QVM. Its argument block is emitted
// after a `BRA` that jumps over it, so the walk starts at the first push after
// that branch and stops after `nargs` values. Argument 16 is clipSize:
// `for (i = 0; i < 4; i++) w->unk640[i] = Script_GetArgInt(args, 14 + i);`
// with unk640 = { pellets, rate, clipSize, burst } (weapontypes.h:35-43).
struct RetailWeapon {
    int id = -1;
    std::string constant;   // "WEAPON_ID_UZI" (arg 1)
    std::string hud_name;   // unk110, the HUD text (arg 2)
    int clip_size = 0;      // arg 16, the union member at WeaponType + 0x648
    std::string key;        // arg 29, "model and physics type name" (+0x61C)
};

bool ReadRetailWeapon(const fs::path& path, RetailWeapon& out) {
    const QVMFile qvm = QVM_Parse(path.string());
    if (!qvm.valid) return false;

    std::size_t fn = qvm.instructions.size();
    for (std::size_t i = 0; i < qvm.instructions.size(); ++i) {
        const QVMInstruction& in = qvm.instructions[i];
        if (!IsIdentifierPush(in.type)) continue;
        if (in.operand < qvm.identifiers.size() &&
            qvm.identifiers[in.operand] == "DefineWeaponType") {
            fn = i;
            break;
        }
    }
    if (fn == qvm.instructions.size()) return false;

    std::size_t call = fn + 1;
    while (call < qvm.instructions.size() && qvm.instructions[call].type == QVMOpType::BRK) ++call;
    if (call >= qvm.instructions.size() || qvm.instructions[call].type != QVMOpType::CALL) return false;
    const std::size_t nargs = qvm.instructions[call].operand;

    std::size_t start = call + 1;
    while (start < qvm.instructions.size() && qvm.instructions[start].type == QVMOpType::BRK) ++start;
    if (start < qvm.instructions.size() && qvm.instructions[start].type == QVMOpType::BRA) ++start;

    std::vector<std::string> tokens;
    for (std::size_t i = start; i < qvm.instructions.size() && tokens.size() < nargs; ++i) {
        const QVMInstruction& in = qvm.instructions[i];
        if (in.type == QVMOpType::BRK) continue;
        if (in.type == QVMOpType::MINUS) {
            if (!tokens.empty()) tokens.back() = "-" + tokens.back();
            continue;
        }
        if (!AppendPush(qvm, in, tokens)) break;
    }
    if (tokens.size() != nargs || nargs < 30) return false;

    out.id = std::atoi(tokens[0].c_str());
    out.constant = tokens[1];
    out.hud_name = tokens[2];
    out.clip_size = std::atoi(tokens[16].c_str());
    out.key = tokens[29];
    return true;
}

}  // namespace

// ---------------------------------------------------------------------------
// Declared parameter layout
// ---------------------------------------------------------------------------

TEST(PickupRulesTest, LayoutsMatchTheEngineParamTables) {
    ASSERT_EQ(igi::PickupLayoutCount(), 4);

    // GenericPickup_Init 0x46C0C0 (pickup.c:268-271): three params, the third a
    // String16 model name.
    const igi::PickupLayout* generic = igi::FindPickupLayout("GenericPickup");
    ASSERT_NE(generic, nullptr);
    EXPECT_EQ(generic->param_count, 3);
    EXPECT_STREQ(generic->params[0].name, "Position");
    EXPECT_EQ(generic->params[0].type, igi::PickupParamType::ObjectPos);
    EXPECT_STREQ(generic->params[1].name, "Orientation");
    EXPECT_EQ(generic->params[1].type, igi::PickupParamType::Real32x9);
    EXPECT_STREQ(generic->params[2].name, "Model");
    EXPECT_EQ(generic->params[2].type, igi::PickupParamType::String16);
    EXPECT_EQ(generic->model_arg_index, 9);
    EXPECT_EQ(generic->enum_arg_index, -1);
    EXPECT_EQ(generic->ArgCount(), 10);   // 3 + 3 + 3 + 1

    // GunPickup_Init 0x46CB70 (pickup.c:476-479): the third parameter is an
    // EnumString32 populated from WeaponType_GetIdEnum 0x413DE0 -- NOT a model
    // name, and there is no fourth parameter.
    const igi::PickupLayout* gun = igi::FindPickupLayout("GunPickup");
    ASSERT_NE(gun, nullptr);
    EXPECT_EQ(gun->param_count, 3);
    EXPECT_STREQ(gun->params[2].name, "ID");
    EXPECT_EQ(gun->params[2].type, igi::PickupParamType::EnumString32);
    EXPECT_EQ(gun->enum_domain, igi::PickupEnumDomain::Weapon);
    EXPECT_EQ(gun->enum_arg_index, 9);
    EXPECT_EQ(gun->count_arg_index, -1);
    EXPECT_FALSE(gun->has_authored_count);
    EXPECT_EQ(gun->ArgCount(), 10);

    // AmmoPickup_Init 0x46D420 (pickup.c:659-662): four params, the enum from
    // AmmoType_GetEnum 0x47CF10 then an Int16 count.
    const igi::PickupLayout* ammo = igi::FindPickupLayout("AmmoPickup");
    ASSERT_NE(ammo, nullptr);
    EXPECT_EQ(ammo->param_count, 4);
    EXPECT_STREQ(ammo->params[2].name, "ID");
    EXPECT_EQ(ammo->params[2].type, igi::PickupParamType::EnumString32);
    EXPECT_EQ(ammo->enum_domain, igi::PickupEnumDomain::Ammo);
    EXPECT_STREQ(ammo->params[3].name, "Ammo");
    EXPECT_EQ(ammo->params[3].type, igi::PickupParamType::Int16);
    EXPECT_EQ(ammo->enum_arg_index, 9);
    EXPECT_EQ(ammo->count_arg_index, 10);
    EXPECT_TRUE(ammo->has_authored_count);
    EXPECT_EQ(ammo->ArgCount(), 11);

    // AddAmmo_Init 0x46DB40 (pickup.c:776-778): no position, no orientation --
    // it is a child of the human it feeds.
    const igi::PickupLayout* add = igi::FindPickupLayout("AddAmmo");
    ASSERT_NE(add, nullptr);
    EXPECT_EQ(add->param_count, 2);
    EXPECT_STREQ(add->params[0].name, "ID");
    EXPECT_STREQ(add->params[1].name, "Ammo");
    EXPECT_EQ(add->enum_arg_index, 3);
    EXPECT_EQ(add->count_arg_index, 4);
    EXPECT_EQ(add->ArgCount(), 5);

    EXPECT_EQ(igi::FindPickupLayout("HumanSoldier"), nullptr);
    EXPECT_EQ(igi::FindPickupLayout(""), nullptr);

    // The parameter type -> argument count table (paramops.c:133-160).
    EXPECT_EQ(igi::PickupParamArgCount(igi::PickupParamType::ObjectPos), 3);
    EXPECT_EQ(igi::PickupParamArgCount(igi::PickupParamType::Real32x9), 3);
    EXPECT_EQ(igi::PickupParamArgCount(igi::PickupParamType::String16), 1);
    EXPECT_EQ(igi::PickupParamArgCount(igi::PickupParamType::EnumString32), 1);
    EXPECT_EQ(igi::PickupParamArgCount(igi::PickupParamType::Int16), 1);
}

// ---------------------------------------------------------------------------
// The touch cylinder
// ---------------------------------------------------------------------------

TEST(PickupRulesTest, TouchRangeIsClampedUpToTheMinimum) {
    // pickup.c:187-188: `if (!(r >= kMinTouch)) range = kMinTouch;`
    // A weapon model's bounding radius is a few hundred units, so the clamp is
    // what applies in practice: the reach is 2867.2 units = 0.7 m.
    EXPECT_FLOAT_EQ(igi::PickupTouchRange(100.0f, 1.0f), 2867.2f);
    EXPECT_FLOAT_EQ(igi::PickupTouchRange(0.0f, 1.0f), 2867.2f);
    // VirModel_GetRadius 0x4D0950 returns kOne when there is no model at all
    // (mesh3d.c:385), so an unresolved pickup is 1.0 * scale.
    EXPECT_FLOAT_EQ(igi::PickupTouchRange(1.0f, 1.0f), 2867.2f);

    // Exactly at the minimum is NOT clamped (the comparison is >=).
    EXPECT_FLOAT_EQ(igi::PickupTouchRange(2867.2f, 1.0f), 2867.2f);

    // Above it, the product wins. The multiply is a float multiply because
    // pickup.c:1 sets excess-precision=standard.
    EXPECT_FLOAT_EQ(igi::PickupTouchRange(1000.0f, 4.0f), 4000.0f);
    EXPECT_FLOAT_EQ(igi::PickupTouchRange(2000.0f, 2.0f), 4000.0f);
    // RigidDynCubeObj_Init 0x4C5710 sets all three scales to 1.0
    // (magicobj.c:340-342), so an unscaled model uses its radius directly.
    EXPECT_FLOAT_EQ(igi::PickupTouchRange(5000.0f, 1.0f), 5000.0f);

    // The negated form also catches NaN: a NaN radius falls back to 2867.2
    // rather than producing a NaN range.
    const float nan = std::nanf("");
    EXPECT_FLOAT_EQ(igi::PickupTouchRange(nan, 1.0f), 2867.2f);
}

TEST(PickupRulesTest, TouchIsACylinderNotABox) {
    // pickup.c:189-199 zeroes d.z, so the horizontal test is the true 2D
    // distance via Vec3d_Length 0x4B3130, not a per-axis box.
    const double pickup[3] = {0.0, 0.0, 0.0};

    // 2000,2000 is 2828.43 units away: inside 2867.2, and well outside a
    // 2000-unit box in either axis.
    const double diagonal[3] = {2000.0, 2000.0, 0.0};
    EXPECT_TRUE(igi::PickupTouchesFromModel(100.0f, 1.0f, pickup, diagonal));

    // 2100,2100 is 2969.85 units: outside.
    const double outside[3] = {2100.0, 2100.0, 0.0};
    EXPECT_FALSE(igi::PickupTouchesFromModel(100.0f, 1.0f, pickup, outside));

    // The vertical test is a separate 4096-unit half-height, independent of the
    // horizontal reach: straight up 4000 units still touches.
    const double above[3] = {0.0, 0.0, 4000.0};
    EXPECT_TRUE(igi::PickupTouchesFromModel(100.0f, 1.0f, pickup, above));
    const double below[3] = {0.0, 0.0, -4000.0};
    EXPECT_TRUE(igi::PickupTouchesFromModel(100.0f, 1.0f, pickup, below));

    // At exactly 4096 units the negated `>=` makes it NOT a touch.
    const double at_height[3] = {0.0, 0.0, 4096.0};
    EXPECT_FALSE(igi::PickupTouchesFromModel(100.0f, 1.0f, pickup, at_height));
    const double just_below[3] = {0.0, 0.0, 4095.9};
    EXPECT_TRUE(igi::PickupTouchesFromModel(100.0f, 1.0f, pickup, just_below));
}

TEST(PickupRulesTest, TouchBoundaryIsExclusive) {
    // `return !(Vec3d_Length(&d) >= range) && ...` (pickup.c:199): a toucher at
    // exactly `range` is NOT touching.
    const double pickup[3] = {0.0, 0.0, 0.0};
    const double at_range[3] = {2867.2, 0.0, 0.0};
    const double inside[3] = {2867.0, 0.0, 0.0};
    const double outside[3] = {2867.4, 0.0, 0.0};

    EXPECT_FALSE(igi::PickupTouchesFromModel(0.0f, 1.0f, pickup, at_range));
    EXPECT_TRUE(igi::PickupTouchesFromModel(0.0f, 1.0f, pickup, inside));
    EXPECT_FALSE(igi::PickupTouchesFromModel(0.0f, 1.0f, pickup, outside));

    // A large model radius replaces the minimum, and the same exclusive
    // boundary applies at the larger value.
    const double at_4000[3] = {4000.0, 0.0, 0.0};
    const double in_4000[3] = {3999.0, 0.0, 0.0};
    EXPECT_FALSE(igi::PickupTouchesFromModel(1000.0f, 4.0f, pickup, at_4000));
    EXPECT_TRUE(igi::PickupTouchesFromModel(1000.0f, 4.0f, pickup, in_4000));

    // PickupTouches clamps its own range argument, so passing a raw radius and
    // passing the clamped value agree.
    EXPECT_TRUE(igi::PickupTouches(100.0, pickup, inside));
    EXPECT_FALSE(igi::PickupTouches(100.0, pickup, outside));
    EXPECT_TRUE(igi::PickupTouches(igi::kPickupTouchHeightUnits, pickup, inside));
}

TEST(PickupRulesTest, TouchConstantsAreTheEngineData) {
    // pickup.c:159-160.
    EXPECT_DOUBLE_EQ(igi::kPickupMinTouchUnits, 2867.2);
    EXPECT_DOUBLE_EQ(igi::kPickupTouchHeightUnits, 4096.0);
    // 2867.2 / 4096 = 0.7 m; 4096 / 4096 = 1.0 m.
    EXPECT_NEAR(igi::kPickupMinTouchUnits / igi::kPickupUnitsPerMetre, 0.7, 1e-9);
    EXPECT_DOUBLE_EQ(igi::kPickupTouchHeightUnits / igi::kPickupUnitsPerMetre, 1.0);
}

// ---------------------------------------------------------------------------
// The per-frame acquisition path
// ---------------------------------------------------------------------------

TEST(PickupRulesTest, OnlyThePlacedPickupTypesAreTouchTargets) {
    // The three types that register a touch handler on Game_GetEvent1 and add
    // themselves to task list 2: GenericPickup (pickup.c:264, :293), GunPickup
    // (pickup.c:475, :489) and AmmoPickup (pickup.c:657, :489).
    EXPECT_TRUE(igi::IsPlacedPickupType("GenericPickup"));
    EXPECT_TRUE(igi::IsPlacedPickupType("GunPickup"));
    EXPECT_TRUE(igi::IsPlacedPickupType("AmmoPickup"));

    // AddAmmo is a pickup-family task but never a placed one: it is created by
    // the weapon code as a child of a human (AddAmmo_Create 0x46DC20,
    // pickup.c:782-788) and declares no Position parameter (pickup.c:776-777),
    // so it can neither be authored into a level nor be in task list 2.
    EXPECT_FALSE(igi::IsPlacedPickupType("AddAmmo"));
    // The explicit-use types are a different family entirely (Game_GetEvent2 /
    // task list 0, src/humanbase/human.c:1269-1312).
    EXPECT_FALSE(igi::IsPlacedPickupType("Door"));
    EXPECT_FALSE(igi::IsPlacedPickupType("Terminal"));
    EXPECT_FALSE(igi::IsPlacedPickupType("GenericTBA"));
    EXPECT_FALSE(igi::IsPlacedPickupType(""));
}

TEST(PickupRulesTest, PlayerTouchUsesTheModelRadiusClampedUp) {
    // HumanPlayer_SendPosition 0x410F70 (src/humanplayer/humanplayer.c:1467-1484)
    // hands the player's own position to the pickup; the touch handler re-tests
    // `touches()` (pickup.c:605-606, :739-740), so the gate is the same cylinder
    // PickupTouches pins. A placed pickup's max_scale is 1.0
    // (RigidDynCubeObj_Init 0x4C5710, src/magicobj/magicobj.c:340-342).
    const double pickup[3] = {0.0, 0.0, 0.0};

    // A small weapon model (the retail UZI, 111_01_1, has MeshHeader.radius
    // 1268.1) is clamped up to 2867.2: 2867.2 is still a touch, 2867.4 is not.
    const float uzi_radius = 1268.082763671875f;
    const double just_inside[3] = {2867.0, 0.0, 0.0};
    const double just_outside[3] = {2867.4, 0.0, 0.0};
    EXPECT_TRUE(igi::PickupTouchesPlayer(uzi_radius, pickup, just_inside));
    EXPECT_FALSE(igi::PickupTouchesPlayer(uzi_radius, pickup, just_outside));

    // A large model is NOT clamped: the retail DRAGUNOV (105_01_1) has radius
    // 4059.7, so it is reachable from 4000 units but not from 4100. This is the
    // case the minimum alone gets wrong.
    const float dragunov_radius = 4059.67919921875f;
    const double at_4000[3] = {4000.0, 0.0, 0.0};
    const double at_4100[3] = {4100.0, 0.0, 0.0};
    EXPECT_TRUE(igi::PickupTouchesPlayer(dragunov_radius, pickup, at_4000));
    EXPECT_FALSE(igi::PickupTouchesPlayer(dragunov_radius, pickup, at_4100));
    // ... and the minimum would have refused the 4000-unit case.
    EXPECT_FALSE(igi::PickupTouchesFromModel(uzi_radius, 1.0f, pickup, at_4000));

    // The vertical half-height is the separate 4096-unit rule and does not grow
    // with the model radius.
    const double above[3] = {0.0, 0.0, 4000.0};
    const double above_too_far[3] = {0.0, 0.0, 4096.0};
    EXPECT_TRUE(igi::PickupTouchesPlayer(dragunov_radius, pickup, above));
    EXPECT_FALSE(igi::PickupTouchesPlayer(dragunov_radius, pickup, above_too_far));

    // An unresolved model is VirModel_GetRadius's kOne (mesh3d.c:88) and is
    // therefore clamped to the minimum, exactly like the UZI.
    EXPECT_TRUE(igi::PickupTouchesPlayer(igi::kPickupNoModelRadius, pickup, just_inside));
    EXPECT_FALSE(igi::PickupTouchesPlayer(igi::kPickupNoModelRadius, pickup, just_outside));
}

TEST(PickupRulesTest, MeshHeaderRadiusIsReadFromOffset0x78) {
    // MeshHeader.radius is at 0x78 (src/model/modeltypes.h:21) and is the word
    // VirModel_GetRadius 0x4D0950 returns (src/lod/mesh3d.c:381-387). It is NOT
    // `sphere` (0x3C), which the LOD distance test uses (mesh3d.c:224) -- the
    // retail UZI model has radius 1268.1 and sphere 973.1, so a reader that
    // confused them would be wrong by 30%.
    //
    // Minimal ILFF/HSEM container, built the way Chunk_FirstChild 0x4B7490 /
    // Chunk_Data 0x4B74D0 walk it (src/res/chunk.c:17-49) with align 4, which is
    // what every retail .mef uses: the child chunk header sits at 0x14 and its
    // payload at 0x24, leaving room for the whole 0x90-byte MeshHeader.
    std::vector<uint8_t> mef(0x24 + 0x90, 0);
    auto put_u32 = [](std::vector<uint8_t>& b, size_t off, uint32_t v) {
        std::memcpy(b.data() + off, &v, 4);
    };
    auto put_f32 = [](std::vector<uint8_t>& b, size_t off, float v) {
        std::memcpy(b.data() + off, &v, 4);
    };
    std::memcpy(mef.data(), "ILFF", 4);
    put_u32(mef, 0x04, static_cast<uint32_t>(mef.size()));  // parent size
    put_u32(mef, 0x08, 4);                                  // parent align
    std::memcpy(mef.data() + 0x14, "HSEM", 4);
    put_u32(mef, 0x18, 0x90);                               // child size
    put_u32(mef, 0x1C, 4);                                  // child align
    put_f32(mef, 0x24 + 0x3C, 973.0735473632812f);          // sphere
    put_f32(mef, 0x24 + 0x78, 1268.082763671875f);          // radius

    const igi::lod::MefMeshHeader header = igi::lod::ReadMefMeshHeader(mef);
    ASSERT_TRUE(header.valid);
    EXPECT_FLOAT_EQ(header.radius, 1268.082763671875f);
    EXPECT_FLOAT_EQ(header.sphere, 973.0735473632812f);
    EXPECT_NE(header.radius, header.sphere);

    // A buffer that is not an ILFF container, or carries no MESH chunk, leaves
    // `valid` false -- the caller then uses kPickupNoModelRadius (1.0f), the
    // engine's kOne.
    const std::vector<uint8_t> not_a_mef = {'N', 'O', 'P', 'E'};
    EXPECT_FALSE(igi::lod::ReadMefMeshHeader(not_a_mef).valid);

    std::vector<uint8_t> no_mesh(0x24 + 0x90, 0);
    std::memcpy(no_mesh.data(), "ILFF", 4);
    put_u32(no_mesh, 0x04, static_cast<uint32_t>(no_mesh.size()));
    put_u32(no_mesh, 0x08, 4);
    std::memcpy(no_mesh.data() + 0x14, "ATTA", 4);
    put_u32(no_mesh, 0x18, 0x90);
    put_u32(no_mesh, 0x1C, 4);
    EXPECT_FALSE(igi::lod::ReadMefMeshHeader(no_mesh).valid);
}

// ---------------------------------------------------------------------------
// Grants
// ---------------------------------------------------------------------------

TEST(PickupRulesTest, GenericPickupGrantsNothing) {
    // GenericPickup_Touch 0x46C630 (pickup.c:361-365) sets pickedUp and nothing
    // else -- no PickupMsg, no event dispatch.
    const igi::PickupGrant grant = igi::GenericPickupGrant();
    EXPECT_FALSE(grant.delivered);
    EXPECT_EQ(grant.id, 0);
    EXPECT_EQ(grant.count, 0);
}

TEST(PickupRulesTest, GunPickupGrantsTheWeaponClipSize) {
    // GunPickup_Touch 0x46D230 (pickup.c:607-608): msg.id = t->weapon,
    // msg.count = t->ammo. GunPickup_Changed 0x46D050 (pickup.c:583) sets
    // `t->ammo = *(int *)(w + 0x648)`, and 0x648 is WeaponType::clipSize
    // (weapontypes.h:35-43). There is NO authored ammo parameter on GunPickup.
    const igi::PickupGrant uzi = igi::GunPickupGrant(6, 32, true);
    EXPECT_EQ(uzi.id, 6);
    EXPECT_EQ(uzi.count, 32);
    EXPECT_EQ(igi::GunPickupGrantedAmmo(32), 32);

    // A melee/launcher weapon with clipSize 1 grants a single round.
    EXPECT_EQ(igi::GunPickupGrant(12, 1, true).count, 1);

    // Retail KNIFE has clipSize -1 (WEAPONS/KNIFE/WEAPON.QVM, DefineWeaponType
    // arg 16), and the engine copies it through unchanged.
    EXPECT_EQ(igi::GunPickupGrant(20, -1, true).count, -1);
}

TEST(PickupRulesTest, UnresolvedWeaponGrantsZeroAmmo) {
    // GunPickup_Changed 0x46D050 returns at pickup.c:576-577 when
    // WeaponType_Find fails, BEFORE `t->ammo = clipSize`; GunPickup_Create
    // already set ammo = 0 (pickup.c:519). The touch still delivers the id.
    const igi::PickupGrant grant = igi::GunPickupGrant(9999, 30, false);
    EXPECT_EQ(grant.id, 9999);
    EXPECT_EQ(grant.count, 0);
}

TEST(PickupRulesTest, AmmoPickupGrantsTheAuthoredCount) {
    // AmmoPickup_Touch 0x46D990 (pickup.c:741-742): msg.id = t->ammo,
    // msg.count = t->count, where count is the authored Int16 parameter.
    const igi::PickupGrant grant = igi::AmmoPickupGrant(919, 32);
    EXPECT_EQ(grant.id, 919);
    EXPECT_EQ(grant.count, 32);

    // The count field is int16_t and ParamInt16_Parse 0x4F0110
    // (paramops.c:627-633) is a narrowing assignment with no clamp, so an
    // out-of-range authored value TRUNCATES.
    EXPECT_EQ(igi::AmmoPickupGrant(919, 40000).count, -25536);
    EXPECT_EQ(igi::TruncatePickupCount(32767), 32767);
    EXPECT_EQ(igi::TruncatePickupCount(32768), -32768);
    EXPECT_EQ(igi::TruncatePickupCount(65535), -1);
    EXPECT_EQ(igi::TruncatePickupCount(0), 0);
    EXPECT_EQ(igi::TruncatePickupCount(-1), -1);

    // An unknown AMMO_ID_* still delivers: AmmoPickup_Touch never consults
    // AmmoType_Find, unlike AmmoPickup_Changed.
    EXPECT_EQ(igi::AmmoPickupGrant(4242, 7).count, 7);
}

TEST(PickupRulesTest, AddAmmoNeedsAHumanParentAndAKnownAmmoId) {
    // AddAmmo_Run 0x46DC40 (pickup.c:794-795):
    //   TaskType_IsA(parent->type, Human_GetType()) && AmmoType_Find(t->ammo)
    EXPECT_TRUE(igi::AddAmmoApplies(true, true));
    EXPECT_FALSE(igi::AddAmmoApplies(true, false));
    EXPECT_FALSE(igi::AddAmmoApplies(false, true));
    EXPECT_FALSE(igi::AddAmmoApplies(false, false));
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

TEST(PickupRulesTest, DispositionAndOneShot) {
    // GenericPickup_Run 0x46C3C0 (pickup.c:308-312) removes the object but
    // keeps the task, so GenericPickup_Show 0x46C750 can restore it.
    EXPECT_EQ(igi::PickupDispositionFor(igi::PickupKind::GenericPickup),
              igi::PickupDisposition::RemoveFromWorld);
    // GunPickup_Run 0x46CEA0 (pickup.c:555-556) and AmmoPickup_Run 0x46D740
    // (pickup.c:682-683) Task_Delete.
    EXPECT_EQ(igi::PickupDispositionFor(igi::PickupKind::GunPickup),
              igi::PickupDisposition::DeleteTask);
    EXPECT_EQ(igi::PickupDispositionFor(igi::PickupKind::AmmoPickup),
              igi::PickupDisposition::DeleteTask);
    EXPECT_EQ(igi::PickupDispositionFor(igi::PickupKind::AddAmmo),
              igi::PickupDisposition::DeleteTask);

    // Nothing in pickup.c clears pickedUp, so there is no respawn path.
    EXPECT_TRUE(igi::PickupIsOneShot(igi::PickupKind::GenericPickup));
    EXPECT_TRUE(igi::PickupIsOneShot(igi::PickupKind::GunPickup));
    EXPECT_TRUE(igi::PickupIsOneShot(igi::PickupKind::AmmoPickup));
}

TEST(PickupRulesTest, ScriptFlagUsesTheTypeAndIdNotTheAuthoredName) {
    // register_flag pickup.c:210-212 uses Task_GetName, whose fallback is
    // "%s_%d" of the TaskType name and the id (param/task.c:131-132). No task
    // type registers a Level_GetNameEvent handler, so the fallback always wins
    // -- the authored name (LEVEL6 "Shotgun shells", LEVEL7 "syringe") is NOT
    // part of the symbol.
    EXPECT_EQ(igi::PickupScriptFlagName("GunPickup", 333), "GunPickup_333.isPickedUp");
    EXPECT_EQ(igi::PickupScriptFlagName("AmmoPickup", 7), "AmmoPickup_7.isPickedUp");
    EXPECT_EQ(igi::PickupScriptFlagName("GenericPickup", 0), "GenericPickup_0.isPickedUp");
    EXPECT_EQ(igi::PickupScriptFlagName("GunPickup", -1), "GunPickup_-1.isPickedUp");
}

// ---------------------------------------------------------------------------
// Authoring checks
// ---------------------------------------------------------------------------

TEST(PickupRulesTest, CheckPickupArgsAcceptsTheEngineLayout) {
    // LEVEL1 objects.qsc:646
    //   Task_New(-1, "GunPickup", "", 24553562.0, -55889120.0, 174416912.0,
    //            5.4977869987487793, 1.570796012878418, 0, "WEAPON_ID_UZI")
    const std::vector<std::string> gun = {
        "-1", "GunPickup", "", "24553562.0", "-55889120.0", "174416912.0",
        "5.4977869987487793", "1.570796012878418", "0", "WEAPON_ID_UZI"};
    const igi::PickupArgCheck gun_check = igi::CheckPickupArgs("GunPickup", gun);
    EXPECT_TRUE(gun_check.is_pickup);
    EXPECT_EQ(gun_check.expected_tokens, 10);
    EXPECT_TRUE(gun_check.token_count_ok);
    EXPECT_TRUE(gun_check.enum_present);
    EXPECT_TRUE(gun_check.enum_known);
    EXPECT_TRUE(gun_check.count_ok);  // GunPickup has no count parameter

    // LEVEL1 objects.qsc:645
    //   Task_New(-1, "AmmoPickup", "", 24552868.0, -55889136.0, 174416864.0,
    //            0, 0, 0, "AMMO_ID_919", 32)
    const std::vector<std::string> ammo = {
        "-1", "AmmoPickup", "", "24552868.0", "-55889136.0", "174416864.0",
        "0", "0", "0", "AMMO_ID_919", "32"};
    const igi::PickupArgCheck ammo_check = igi::CheckPickupArgs("AmmoPickup", ammo);
    EXPECT_EQ(ammo_check.expected_tokens, 11);
    EXPECT_TRUE(ammo_check.token_count_ok);
    EXPECT_TRUE(ammo_check.enum_known);
    EXPECT_TRUE(ammo_check.count_ok);
    EXPECT_EQ(ammo_check.count, 32);

    // LEVEL1 objects.qsc:1181
    //   Task_New(-1, "AddAmmo", "", "AMMO_ID_919", 49)
    const std::vector<std::string> add = {"-1", "AddAmmo", "", "AMMO_ID_919", "49"};
    const igi::PickupArgCheck add_check = igi::CheckPickupArgs("AddAmmo", add);
    EXPECT_EQ(add_check.expected_tokens, 5);
    EXPECT_TRUE(add_check.token_count_ok);
    EXPECT_TRUE(add_check.enum_known);
    EXPECT_EQ(add_check.count, 49);

    // A GenericPickup's third parameter is a model name, so it is only required
    // to be present.
    const std::vector<std::string> generic = {
        "-1", "GenericPickup", "", "1", "2", "3", "0", "0", "0", "gen_key"};
    const igi::PickupArgCheck generic_check = igi::CheckPickupArgs("GenericPickup", generic);
    EXPECT_TRUE(generic_check.token_count_ok);
    EXPECT_TRUE(generic_check.enum_present);
    EXPECT_EQ(generic_check.expected_tokens, 10);

    // Not a pickup at all.
    EXPECT_FALSE(igi::CheckPickupArgs("Door", {"-1", "Door"}).is_pickup);
}

TEST(PickupRulesTest, CheckPickupArgsRejectsTheWrongShape) {
    // LEVEL1 objects.qsc:601 authors an EMPTY weapon id:
    //   Task_New(333, "GunPickup", "", 24372076.0, -56227960.0, 174403984.0,
    //            0, 0, 0, "")
    // The engine accepts it (Script_EvalExpr yields 0 for an unparseable
    // expression, script.c:264-265) but the pickup resolves to no weapon.
    const std::vector<std::string> empty_id = {
        "333", "GunPickup", "", "24372076.0", "-56227960.0", "174403984.0",
        "0", "0", "0", ""};
    const igi::PickupArgCheck empty_check = igi::CheckPickupArgs("GunPickup", empty_id);
    EXPECT_TRUE(empty_check.token_count_ok);
    EXPECT_FALSE(empty_check.enum_present);
    EXPECT_FALSE(empty_check.enum_known);

    // A model id where the weapon enum belongs: the editor's old schema called
    // this field "Model", so this is the mistake the check is there to catch.
    std::vector<std::string> wrong_prefix = empty_id;
    wrong_prefix[9] = "111_01_1";
    EXPECT_FALSE(igi::CheckPickupArgs("GunPickup", wrong_prefix).enum_known);
    wrong_prefix[9] = "AMMO_ID_919";
    EXPECT_FALSE(igi::CheckPickupArgs("GunPickup", wrong_prefix).enum_known);

    // Truncated and over-long argument lists.
    std::vector<std::string> short_args = empty_id;
    short_args.pop_back();
    EXPECT_FALSE(igi::CheckPickupArgs("GunPickup", short_args).token_count_ok);
    std::vector<std::string> long_args = empty_id;
    long_args.push_back("0");
    EXPECT_FALSE(igi::CheckPickupArgs("GunPickup", long_args).token_count_ok);

    // A non-numeric count.
    const std::vector<std::string> bad_count = {
        "-1", "AmmoPickup", "", "1", "2", "3", "0", "0", "0", "AMMO_ID_919", "lots"};
    const igi::PickupArgCheck bad_check = igi::CheckPickupArgs("AmmoPickup", bad_count);
    EXPECT_TRUE(bad_check.token_count_ok);
    EXPECT_TRUE(bad_check.enum_known);
    EXPECT_FALSE(bad_check.count_ok);

    // An out-of-range count is accepted by the engine but truncates.
    const std::vector<std::string> big_count = {
        "-1", "AmmoPickup", "", "1", "2", "3", "0", "0", "0", "AMMO_ID_919", "40000"};
    const igi::PickupArgCheck big_check = igi::CheckPickupArgs("AmmoPickup", big_count);
    EXPECT_TRUE(big_check.count_ok);
    EXPECT_EQ(big_check.count, -25536);
}

// ---------------------------------------------------------------------------
// Retail corpus
// ---------------------------------------------------------------------------

namespace {

// IGI-RE's type name for each parameter type, so a declaration read from the
// bytecode can be compared against the module's table without a second mapping.
const char* ParamTypeName(igi::PickupParamType type) {
    switch (type) {
        case igi::PickupParamType::ObjectPos: return "ObjectPos";
        case igi::PickupParamType::Real32x9: return "Real32x9";
        case igi::PickupParamType::String16: return "String16";
        case igi::PickupParamType::EnumString32: return "EnumString32";
        case igi::PickupParamType::Int16: return "Int16";
    }
    return "?";
}

const char* const kPickupTaskTypes[] = {"GenericPickup", "GunPickup", "AmmoPickup", "AddAmmo"};

}  // namespace

TEST(PickupCorpusTest, EveryLevelDeclaresTheEngineParameterLayout) {
    if (!fs::is_regular_file(LevelDir(1) / "OBJECTS.QVM")) {
        GTEST_SKIP() << "retail IGI not found at " << GameRoot().string()
                     << " (set IGI_GAME_PATH to point at it)";
    }

    int declarations_seen = 0;
    std::printf("\n  level  task           declared parameters\n");
    for (int level = 1; level <= 14; ++level) {
        const fs::path path = LevelDir(level) / "OBJECTS.QVM";
        ASSERT_TRUE(fs::is_regular_file(path)) << path.string();
        const QVMFile qvm = QVM_Parse(path.string());
        ASSERT_TRUE(qvm.valid) << path.string() << ": " << qvm.error;

        for (const char* type : kPickupTaskTypes) {
            const std::vector<std::pair<std::string, std::string>> declared =
                ExtractDeclaration(qvm, type);
            if (declared.empty()) continue;
            ++declarations_seen;

            const igi::PickupLayout* layout = igi::FindPickupLayout(type);
            ASSERT_NE(layout, nullptr) << type;

            std::string rendered;
            for (const std::pair<std::string, std::string>& param : declared) {
                if (!rendered.empty()) rendered += ", ";
                rendered += param.first + ":" + param.second;
            }
            std::printf("  %5d  %-13s  %s\n", level, type, rendered.c_str());

            ASSERT_EQ(static_cast<int>(declared.size()), layout->param_count)
                << "level " << level << " " << type;
            for (int i = 0; i < layout->param_count; ++i) {
                EXPECT_EQ(declared[i].first, layout->params[i].name)
                    << "level " << level << " " << type << " parameter " << i;
                EXPECT_EQ(declared[i].second, ParamTypeName(layout->params[i].type))
                    << "level " << level << " " << type << " parameter " << i;
            }
        }
    }

    // Pinned retail totals for the installed EU release. AmmoPickup and AddAmmo
    // are declared by 13 levels each (LEVEL4 has no AmmoPickup, LEVEL7 no
    // AddAmmo); GunPickup by 12 (not LEVEL4 or LEVEL9); GenericPickup by 5
    // (LEVEL2, LEVEL5, LEVEL6, LEVEL8, LEVEL11).
    EXPECT_EQ(declarations_seen, 43) << "retail pickup declarations changed";
}

TEST(PickupCorpusTest, EveryPlacedPickupMatchesTheDeclaredLayout) {
    if (!fs::is_regular_file(LevelDir(1) / "OBJECTS.QVM")) {
        GTEST_SKIP() << "retail IGI not found at " << GameRoot().string();
    }

    int gun_total = 0;
    int ammo_total = 0;
    int add_total = 0;
    int generic_total = 0;
    int empty_gun_ids = 0;
    // The extreme authored Int16 counts in the release. No retail count exceeds
    // int16 range, so ParamInt16_Parse's truncation (paramops.c:630) is never
    // observable in shipped data - it is a hazard for a NEW authored value.
    int ammo_min = 0, ammo_max = 0, add_min = 0, add_max = 0;

    std::printf("\n  level  Generic  GunPickup  AmmoPickup  AddAmmo  ammo counts\n");
    for (int level = 1; level <= 14; ++level) {
        const fs::path path = LevelDir(level) / "OBJECTS.QVM";
        const QVMFile qvm = QVM_Parse(path.string());
        ASSERT_TRUE(qvm.valid) << path.string() << ": " << qvm.error;

        const std::vector<std::vector<std::string>> generics =
            ExtractInstances(qvm, "GenericPickup");
        const std::vector<std::vector<std::string>> guns = ExtractInstances(qvm, "GunPickup");
        const std::vector<std::vector<std::string>> ammos = ExtractInstances(qvm, "AmmoPickup");
        const std::vector<std::vector<std::string>> adds = ExtractInstances(qvm, "AddAmmo");

        std::string counts;
        for (const std::vector<std::string>& ammo : ammos) {
            const igi::PickupArgCheck check = igi::CheckPickupArgs("AmmoPickup", ammo);
            ASSERT_TRUE(check.token_count_ok) << "level " << level << " ammo token count";
            ASSERT_TRUE(check.enum_present) << "level " << level << " ammo enum empty";
            EXPECT_TRUE(check.enum_known)
                << "level " << level << " ammo id " << ammo[9];
            EXPECT_TRUE(check.count_ok) << "level " << level << " count " << ammo[10];
            // Every retail AmmoPickup count is a positive Int16. (ParamInt16_Parse
            // 0x4F0110 truncates rather than clamps, so a value above 32767 would
            // show up here as negative.)
            EXPECT_GT(check.count, 0) << "level " << level << " " << ammo[9];
            EXPECT_LE(check.count, 32767) << "level " << level << " " << ammo[9];
            if (ammo_min == 0 || check.count < ammo_min) ammo_min = check.count;
            if (check.count > ammo_max) ammo_max = check.count;
            if (!counts.empty()) counts += " ";
            counts += ammo[9] + "=" + std::to_string(check.count);
        }

        for (const std::vector<std::string>& generic : generics) {
            const igi::PickupArgCheck check = igi::CheckPickupArgs("GenericPickup", generic);
            ASSERT_TRUE(check.token_count_ok) << "level " << level << " generic token count";
        }

        for (const std::vector<std::string>& gun : guns) {
            const igi::PickupArgCheck check = igi::CheckPickupArgs("GunPickup", gun);
            ASSERT_TRUE(check.token_count_ok) << "level " << level << " gun token count";
            if (!check.enum_present) {
                ++empty_gun_ids;
            } else {
                EXPECT_TRUE(check.enum_known)
                    << "level " << level << " weapon id " << gun[9];
            }
        }

        for (const std::vector<std::string>& add : adds) {
            const igi::PickupArgCheck check = igi::CheckPickupArgs("AddAmmo", add);
            ASSERT_TRUE(check.token_count_ok) << "level " << level << " AddAmmo token count";
            EXPECT_TRUE(check.enum_known) << "level " << level << " AddAmmo id " << add[3];
            EXPECT_TRUE(check.count_ok) << "level " << level << " AddAmmo count " << add[4];
            EXPECT_GT(check.count, 0) << "level " << level;
            if (add_min == 0 || check.count < add_min) add_min = check.count;
            if (check.count > add_max) add_max = check.count;
        }

        std::printf("  %5d  %7zu  %9zu  %10zu  %7zu  %s\n", level, generics.size(),
                    guns.size(), ammos.size(), adds.size(), counts.c_str());

        generic_total += static_cast<int>(generics.size());
        gun_total += static_cast<int>(guns.size());
        ammo_total += static_cast<int>(ammos.size());
        add_total += static_cast<int>(adds.size());
    }

    std::printf("  total  %7d  %9d  %10d  %7d\n", generic_total, gun_total, ammo_total,
                add_total);
    std::printf("  authored count range: AmmoPickup %d..%d, AddAmmo %d..%d\n", ammo_min,
                ammo_max, add_min, add_max);

    // Pinned retail totals for the installed EU release.
    EXPECT_EQ(generic_total, 9);
    EXPECT_EQ(gun_total, 109);
    EXPECT_EQ(ammo_total, 71);
    EXPECT_EQ(add_total, 27);
    // Exactly one GunPickup in the whole game has an empty weapon id:
    // LEVEL1 objects.qsc:601, Task_New(333, "GunPickup", ..., "").
    EXPECT_EQ(empty_gun_ids, 1);
    // Every shipped count fits an int16 comfortably, so the truncation in
    // ParamInt16_Parse 0x4F0110 (src/physobj/paramops.c:630) is latent only.
    EXPECT_EQ(ammo_min, 1);
    EXPECT_EQ(ammo_max, 200);
    EXPECT_EQ(add_min, 2);
    EXPECT_EQ(add_max, 256);
}

TEST(PickupCorpusTest, Level1AndLevel2MatchTheDecompiledText) {
    // LEVEL1 and LEVEL2 are the only levels that ship a decompiled objects.qsc
    // next to the binary OBJECTS.QVM and also place pickups. (LEVEL4 has one
    // too, but it places none: only its AddAmmo declaration survives.)
    std::vector<std::string> types(kPickupTaskTypes,
                                   kPickupTaskTypes + sizeof(kPickupTaskTypes) /
                                                           sizeof(kPickupTaskTypes[0]));
    for (int level : {1, 2}) {
        const fs::path dir = LevelDir(level);
        const fs::path qsc = dir / "objects.qsc";
        if (!fs::is_regular_file(qsc) || !fs::is_regular_file(dir / "OBJECTS.QVM")) {
            GTEST_SKIP() << "retail IGI (with objects.qsc) not found at " << GameRoot().string();
        }

        const QVMFile qvm = QVM_Parse((dir / "OBJECTS.QVM").string());
        ASSERT_TRUE(qvm.valid) << qvm.error;

        // Both sides are collected in file order, because the .qsc interleaves
        // the four pickup types while a per-type walk would group them.
        std::vector<std::vector<std::string>> from_binary;
        for (std::size_t i = 0; i < qvm.instructions.size(); ++i) {
            const QVMInstruction& in = qvm.instructions[i];
            if (!IsStringPush(in.type)) continue;
            const std::string text = StringPushText(qvm, in);
            if (std::find(types.begin(), types.end(), text) == types.end()) continue;

            std::vector<std::string> tokens = ReadCallTokens(qvm, i, 20);
            if (tokens.size() < 4) continue;
            if (IsParamTypeName(tokens[3])) continue;  // a declaration
            from_binary.push_back(std::move(tokens));
        }
        const std::vector<std::vector<std::string>> from_text =
            ParseCallsFromText(ReadFile(qsc), types);

        ASSERT_FALSE(from_binary.empty()) << "level " << level;
        ASSERT_EQ(from_binary.size(), from_text.size()) << "level " << level;

        for (std::size_t k = 0; k < from_binary.size(); ++k) {
            ASSERT_EQ(from_binary[k].size(), from_text[k].size())
                << "level " << level << " pickup " << k;
            EXPECT_EQ(from_binary[k][1], from_text[k][1]) << "level " << level;
            // The task id is synthesised for the binary walk, so compare from the
            // type token onwards; everything after it is authored data.
            for (std::size_t i = 2; i < from_binary[k].size(); ++i) {
                EXPECT_TRUE(SameToken(from_binary[k][i], from_text[k][i]))
                    << "level " << level << " pickup " << k << " token " << i
                    << ": binary \"" << from_binary[k][i] << "\" vs text \""
                    << from_text[k][i] << "\"";
            }
        }
    }
}

TEST(PickupCorpusTest, Level1PlacesTheShippedPickups) {
    const fs::path path = LevelDir(1) / "OBJECTS.QVM";
    if (!fs::is_regular_file(path)) {
        GTEST_SKIP() << "retail IGI not found at " << GameRoot().string();
    }

    const QVMFile qvm = QVM_Parse(path.string());
    ASSERT_TRUE(qvm.valid) << qvm.error;

    const std::vector<std::vector<std::string>> guns = ExtractInstances(qvm, "GunPickup");
    const std::vector<std::vector<std::string>> ammos = ExtractInstances(qvm, "AmmoPickup");

    // LEVEL1 objects.qsc:601,646,747-749,849,871 - 7 GunPickups, of which one
    // (id 333) has an empty weapon id.
    ASSERT_EQ(guns.size(), 7u);
    std::map<std::string, int> weapon_histogram;
    for (const std::vector<std::string>& gun : guns) weapon_histogram[gun[9]] += 1;
    EXPECT_EQ(weapon_histogram[""], 1);
    EXPECT_EQ(weapon_histogram["WEAPON_ID_GRENADE"], 3);
    EXPECT_EQ(weapon_histogram["WEAPON_ID_UZI"], 1);
    EXPECT_EQ(weapon_histogram["WEAPON_ID_MEDIPACK"], 1);
    EXPECT_EQ(weapon_histogram["WEAPON_ID_MINIMI"], 1);

    // LEVEL1 objects.qsc:645,776,777,850,872 - 5 AmmoPickups.
    ASSERT_EQ(ammos.size(), 5u);
    std::map<std::string, int> ammo_histogram;
    for (const std::vector<std::string>& ammo : ammos) {
        const igi::PickupArgCheck check = igi::CheckPickupArgs("AmmoPickup", ammo);
        ASSERT_TRUE(check.count_ok);
        ammo_histogram[ammo[9]] += check.count;
    }
    EXPECT_EQ(ammo_histogram["AMMO_ID_919"], 32 + 16 + 16);
    EXPECT_EQ(ammo_histogram["AMMO_ID_MEDIPACK"], 50);
    EXPECT_EQ(ammo_histogram["AMMO_ID_556"], 100);

    // The full-precision position of objects.qsc:646's Uzi, so a mis-aligned
    // token walk lands somewhere visibly different.
    ASSERT_EQ(guns[1][9], "WEAPON_ID_UZI");
    EXPECT_EQ(guns[1][3], "24553562");
    EXPECT_EQ(guns[1][4], "-55889120");
    EXPECT_EQ(guns[1][5], "174416912");

    // What the engine GRANTS for those two neighbours, computed by the module:
    // the Uzi's clip size (32) for the gun, and the authored 32 for the ammo.
    EXPECT_EQ(igi::GunPickupGrant(6, 32, true).count, 32);
    EXPECT_EQ(igi::AmmoPickupGrant(919, 32).count, 32);
}

TEST(PickupCorpusTest, RetailClipSizesMatchTheEditorWeaponTable) {
    // The retail weapon table is one DefineWeaponType 0x413F90 call per
    // WEAPONS/<name>/WEAPON.QVM (weapon/weapontype.c:33-40 runs weapon.qsc in
    // every subfolder). Argument 16 is clipSize, and it is the number a
    // GunPickup grants. The editor keeps its own copy in
    // source/weapon_system.cpp; this test is the parity check between them.
    const fs::path weapons = GameRoot() / "WEAPONS";
    if (!fs::is_directory(weapons)) {
        GTEST_SKIP() << "retail WEAPONS folder not found at " << weapons.string();
    }

    igi::WeaponSystem system;
    int compared = 0;
    std::vector<std::string> mismatches;

    std::printf("\n  weapon                  id  retail  editor   key         hud\n");
    for (const fs::directory_entry& entry : fs::directory_iterator(weapons)) {
        if (!entry.is_directory()) continue;
        const fs::path script = entry.path() / "WEAPON.QVM";
        if (!fs::is_regular_file(script)) continue;

        RetailWeapon retail;
        if (!ReadRetailWeapon(script, retail)) continue;
        if (!system.SelectWeapon(static_cast<uint32_t>(retail.id))) continue;

        const igi::WeaponDefinition& editor = system.GetActiveWeapon();
        ++compared;
        std::printf("  %-20s  %3d  %6d  %6u   %-11s %s%s\n", retail.constant.c_str(), retail.id,
                    retail.clip_size, editor.clip_capacity, retail.key.c_str(),
                    retail.hud_name.c_str(),
                    static_cast<int>(editor.clip_capacity) == retail.clip_size ? "" : "  <-- differs");

        // The editor's model_id is the retail `key` field (WeaponType + 0x61C,
        // the VirModel_Find target GunPickup_Changed uses at pickup.c:580) and
        // its display name is unk110, the HUD text GunPickup_Touch shows via
        // StatusMessageShow (pickup.c:611).
        EXPECT_EQ(editor.model_id, retail.key) << retail.constant;
        EXPECT_EQ(editor.name, retail.hud_name) << retail.constant;
        if (editor.clip_capacity != static_cast<uint32_t>(retail.clip_size)) {
            mismatches.push_back(retail.constant);
        }
    }

    // 19 of the 24 retail WEAPON.QVM folders are weapon types the editor models.
    // The other five (APC, M2HB, MIL, SENTRYGUN, T80) are vehicle/gun-holder
    // entries with a different argument-block layout that ReadRetailWeapon
    // refuses rather than mis-parses.
    EXPECT_EQ(compared, 19) << "retail weapon scripts readable";

    // KNOWN DIVERGENCE, named rather than silently accepted: the editor's table
    // stores clip_capacity as uint32_t, so retail's -1 (KNIFE) cannot be
    // represented. It matters for a GunPickup, which grants clipSize verbatim
    // (pickup.c:583). BINOCULARS used to be the second member of this list
    // (retail 10, editor 1) and is now corrected to 10.
    std::sort(mismatches.begin(), mismatches.end());
    std::printf("  clip-size mismatches: ");
    for (const std::string& name : mismatches) std::printf("%s ", name.c_str());
    std::printf("\n");
    ASSERT_EQ(mismatches.size(), 1u);
    EXPECT_EQ(mismatches[0], "WEAPON_ID_KNIFE");
}
