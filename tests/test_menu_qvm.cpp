/******************************************************************************
 * @file    test_menu_qvm.cpp
 * @brief   The retail menu system as data (source/menu/qvm_image.cpp).
 *
 * The reader turns a parsed "LOOP" container (`QVM_Parse`, source/level/qvm_parser.h)
 * into an `igi::MenuDefinition`.  These tests assert it against the two retail menu
 * containers, byte for byte where the operand layouts are concerned, and against
 * hand-written `QVM_DecompileToString`-format text for the second entry point.
 *
 * Oracle: IGI-RE, a verified C decompilation of retail IGI.exe.
 *   src/qvm/qvm.c:641-689      Qvm_OpCall 0x4BD880 -- CALL = count at +1, one int32
 *                              code offset per argument, resume at pc + argc*4 + 5
 *   src/qvm/qvm.c:433-486      Qvm_Load 0x4BD220 -- "LOOP", version 8, unk08 >= 5
 *   src/qvm/qscgen.c:296-345   the emitted statement: name push, CALL, BRA, one
 *                              BRK-terminated block per argument
 *   src/qasm/asm.c:62-126      Emit writes `n - 1` operand bytes per opcode
 *   src/qsc/qvmop.c:277-459    how each push operand is read back
 *   src/level/level.c:156-166  Task_DeclareParameters is a no-op that returns 0
 *   src/level/level.c:168-246  Level_NewTask 0x4F0780 -- args 0/1/2 are the id,
 *                              type and name (187-190); parameters are parsed in
 *                              ParamTable order (214-221); the REMAINING arguments
 *                              are then evaluated with the new task as the Task_New
 *                              symbol context (234-239), which is what makes a
 *                              nested Task_New a child; gLevelTaskNewEnabled is 0
 *                              while parameters are parsed (182-185, 209), so a
 *                              nested call cannot appear in a parameter position
 *   src/menu/menumanager.c:713-800  MenuManager_New 0x418B00 -- the engine's use
 *
 * Corpus: $IGI_GAME_PATH (scripts/run-host-tests.sh exports it, default
 * /Users/haseeb-mir/Downloads/IGI), files menusystem/ingamemenu.qvm and
 * menusystem/MAINMENU.QVM.  The corpus tests skip when it is absent.
 *****************************************************************************/

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "level/qvm_decompiler.h"
#include "level/qvm_parser.h"
#include "menu/menu_qvm_writer.h"
#include "menu/qvm_image.h"

namespace {

// ─── corpus ──────────────────────────────────────────────────────────────────

std::filesystem::path CorpusRoot() {
    if (const char* env = std::getenv("IGI_GAME_PATH"); env != nullptr && *env != '\0') {
        return std::filesystem::path(env);
    }
    return std::filesystem::path("/Users/haseeb-mir/Downloads/IGI");
}

std::filesystem::path IngameMenuPath() {
    return CorpusRoot() / "menusystem" / "ingamemenu.qvm";
}

std::filesystem::path MainMenuPath() {
    return CorpusRoot() / "menusystem" / "MAINMENU.QVM";
}

bool CorpusAvailable() {
    std::error_code ec;
    return std::filesystem::exists(IngameMenuPath(), ec) && std::filesystem::exists(MainMenuPath(), ec);
}

std::vector<unsigned char> ReadFileBytes(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<unsigned char>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

int32_t ReadI32(const std::vector<unsigned char>& bytes, size_t offset) {
    int32_t value = 0;
    std::memcpy(&value, bytes.data() + offset, 4);
    return value;
}

int CountCalls(const QVMFile& qvm) {
    int count = 0;
    for (const QVMInstruction& in : qvm.instructions) {
        if (in.type == QVMOpType::CALL) {
            ++count;
        }
    }
    return count;
}

std::map<std::string, int> TypeCounts(const igi::MenuDefinition& def) {
    std::map<std::string, int> counts;
    for (const igi::MenuWidget* widget : def.Flatten()) {
        ++counts[widget->type];
    }
    return counts;
}

std::string Join(const std::vector<std::string>& values) {
    std::string out;
    for (const std::string& value : values) {
        if (!out.empty()) {
            out += ", ";
        }
        out += value;
    }
    return out;
}

std::string JoinInts(const std::vector<int>& values) {
    std::string out;
    for (int value : values) {
        if (!out.empty()) {
            out += ", ";
        }
        out += std::to_string(value);
    }
    return out;
}

// The types of a widget's descendants, in Flatten order.
std::vector<std::string> DescendantTypes(const igi::MenuWidget& widget) {
    std::vector<std::string> types;
    for (const igi::MenuWidget& child : widget.children) {
        types.push_back(child.type);
        for (const std::string& type : DescendantTypes(child)) {
            types.push_back(type);
        }
    }
    return types;
}

// The declared parameter count each retail container carries, read from its own
// Task_DeclareParameters statements (see the dump in docs/igi-re-parity/MENU_SYSTEM.md).
const std::map<std::string, int>& IngameMenuDeclaredFields() {
    static const std::map<std::string, int> declared = {
        {"MenuManager", 3},  {"MenuFrame", 9},   {"MenuScreen", 8},     {"MenuText", 5},
        {"SlideBar", 8},     {"MenuTextConditional", 7}, {"ListBox", 7}, {"ToggleBox", 2},
        {"DialogWindow", 5},
    };
    return declared;
}

const std::map<std::string, int>& MainMenuDeclaredFields() {
    static const std::map<std::string, int> declared = {
        {"MenuManager", 3},  {"MenuFrame", 9},  {"MenuScreen", 8},      {"MenuText", 5},
        {"MenuTextConditional", 7}, {"SlideBar", 8}, {"ListBox", 7},    {"ToggleBox", 2},
        {"InputBox", 6},     {"AsciiBox", 4},   {"ScrollListBox", 7},   {"PictureBox", 4},
        {"TextBox", 5},      {"TypeWriterBox", 2}, {"DialogWindow", 5},
    };
    return declared;
}

// ─── synthetic containers ────────────────────────────────────────────────────

// A valid "LOOP" 8.5 container with empty tables and a single BRK, so it parses
// but declares no menu.
std::vector<unsigned char> EmptyLoopContainer() {
    std::vector<unsigned char> bytes(61, 0);
    std::memcpy(bytes.data(), "LOOP", 4);
    auto put32 = [&bytes](size_t offset, uint32_t value) {
        bytes[offset] = static_cast<unsigned char>(value & 0xff);
        bytes[offset + 1] = static_cast<unsigned char>((value >> 8) & 0xff);
        bytes[offset + 2] = static_cast<unsigned char>((value >> 16) & 0xff);
        bytes[offset + 3] = static_cast<unsigned char>((value >> 24) & 0xff);
    };
    put32(0x04, 8);   // ver_major, which QVM_Parse requires to be 8
    put32(0x08, 5);   // ver_minor, which QVM_Parse requires to be 5
    put32(0x0C, 60);  // of_itable
    put32(0x10, 60);  // of_ivalue
    put32(0x14, 0);   // sz_itable
    put32(0x18, 0);   // sz_ivalue
    put32(0x1C, 60);  // of_stable
    put32(0x20, 60);  // of_svalue
    put32(0x24, 0);   // sz_stable
    put32(0x28, 0);   // sz_svalue
    put32(0x2C, 60);  // of_ctable
    put32(0x30, 1);   // sz_ctable
    bytes[60] = 0x00;  // BRK
    return bytes;
}

std::filesystem::path WriteTempFile(const std::string& name, const std::vector<unsigned char>& bytes) {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / name;
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    out.close();
    return path;
}

// ─── decompiler-format text ──────────────────────────────────────────────────
//
// The shape QVM_DecompileToString prints (source/level/qvm_decompiler.cpp:159-183,
// 295-342, 556-566): one `Callee(args);` statement per top-level call, a nested call
// argument on its own indented line, strings quoted with \n and \" escaped, numbers
// in decimal, identifiers bare.
const char* kDecompiledFixture =
    "Task_DeclareParameters(\"MenuManager\", \"Current menuscreen\", \"Int32\", \"Resource path\", "
    "\"String256\", \"isOwnDisplay\", \"bool8\");\n"
    "Task_DeclareParameters(\"MenuScreen\", \"Background\", \"String32\", \"Frame x0\", \"Int32\");\n"
    "Task_DeclareParameters(\"MenuText\", \"Text Resource\", \"String32\", \"Font Resource\", \"String32\", "
    "\"ColourIndex\", \"Int32\");\n"
    "Task_New(-1, \"MenuManager\", \"Menu manager for the fixture\", 900, "
    "\"LOCAL:menusystem/ingamemenu.res\", FALSE, \n"
    "\tTask_New(900, \"MenuScreen\", \"Main Menu\", \"back.tga\", 10, \n"
    "\t\tTask_New(940, \"MenuText\", \"resume\", \"Resume Game\\nsecond line\", \"font3.fnt\", -1)));\n";

}  // namespace

// ─── the corpus the reader is built on ───────────────────────────────────────

TEST(MenuQvm, CorpusContainersParse) {
    if (!CorpusAvailable()) {
        GTEST_SKIP() << "no menu corpus at " << IngameMenuPath().parent_path();
    }

    const QVMFile ingame = QVM_Parse(IngameMenuPath().string());
    ASSERT_TRUE(ingame.valid) << ingame.error;
    ASSERT_EQ(ingame.identifiers.size(), 4u);
    EXPECT_EQ(Join(ingame.identifiers), "Task_DeclareParameters, Task_New, FALSE, TRUE");
    EXPECT_EQ(ingame.strings.size(), 139u);
    EXPECT_EQ(ingame.instructions.size(), 2225u);
    EXPECT_EQ(CountCalls(ingame), 90);
    ASSERT_FALSE(ingame.instructions.empty());
    EXPECT_EQ(ingame.instructions.front().address, 0u);
    const QVMInstruction& last = ingame.instructions.back();
    EXPECT_EQ(last.address + last.size, static_cast<size_t>(ingame.header.codeSize));

    const QVMFile main = QVM_Parse(MainMenuPath().string());
    ASSERT_TRUE(main.valid) << main.error;
    ASSERT_EQ(main.identifiers.size(), 4u);
    // The name tables are in a different order in the two files, so a decoder that
    // assumed an index would be wrong for one of them.
    EXPECT_EQ(Join(main.identifiers), "Task_DeclareParameters, Task_New, TRUE, FALSE");
    EXPECT_EQ(main.strings.size(), 249u);
    EXPECT_EQ(main.instructions.size(), 5014u);
    EXPECT_EQ(CountCalls(main), 206);
}

// ─── operand layouts, pinned by the retail bytes ─────────────────────────────
//
// Every operand size the reader relies on is checked against the file itself, so a
// wrong size cannot hide behind a decoder that agrees with it.

TEST(MenuQvm, OperandLayoutsPinnedByTheRetailBytes) {
    if (!CorpusAvailable()) {
        GTEST_SKIP() << "no menu corpus at " << IngameMenuPath().parent_path();
    }

    const std::vector<unsigned char> bytes = ReadFileBytes(IngameMenuPath());
    const QVMFile qvm = QVM_Parse(IngameMenuPath().string());
    ASSERT_TRUE(qvm.valid) << qvm.error;
    const size_t code = static_cast<size_t>(qvm.header.code);
    ASSERT_LE(code + static_cast<size_t>(qvm.header.codeSize), bytes.size());

    // The first statement is the MenuManager declaration: PUSHIIB <name>, CALL, BRA,
    // then one PUSHSIB + BRK block per argument.
    EXPECT_EQ(bytes[code + 0], 0x0d);  // PUSHIIB: 2 bytes, so it is opcode + index
    EXPECT_EQ(bytes[code + 1], 0x00);  // identifiers[0] == "Task_DeclareParameters"
    EXPECT_EQ(bytes[code + 2], 0x18);  // CALL
    const int32_t argc = ReadI32(bytes, code + 3);
    EXPECT_EQ(argc, 7);

    // CALL is 5 + 4*argc bytes and the offsets are the arguments' code addresses.
    const std::vector<int32_t> expected_targets = {40, 43, 46, 49, 52, 55, 58};
    std::vector<int32_t> targets;
    for (int32_t i = 0; i < argc; ++i) {
        targets.push_back(ReadI32(bytes, code + 7 + 4 * static_cast<size_t>(i)));
    }
    EXPECT_EQ(JoinInts(std::vector<int>(targets.begin(), targets.end())), JoinInts(std::vector<int>(expected_targets.begin(), expected_targets.end())));

    // Each argument is PUSHSIB <index> BRK: 3 bytes, contiguous, and the index is
    // resolved through the container's string table.
    const size_t bra = code + 7 + 4 * static_cast<size_t>(argc);
    for (size_t i = 0; i < expected_targets.size(); ++i) {
        const size_t at = code + static_cast<size_t>(expected_targets[i]);
        EXPECT_EQ(bytes[at], 0x09) << "argument " << i;      // PUSHSIB
        EXPECT_EQ(bytes[at + 2], 0x00) << "argument " << i;  // BRK
    }
    EXPECT_EQ(bytes[code + 58], 0x09);
    EXPECT_EQ(bytes[code + 59], 0x06);
    EXPECT_EQ(bytes[code + 60], 0x00);
    EXPECT_EQ(qvm.strings[bytes[code + 41]], "MenuManager");
    EXPECT_EQ(qvm.strings[bytes[code + 59]], "bool8");

    // BRA is 5 bytes and its displacement is relative to the instruction after it:
    // it skips to the end of the last argument's code.
    EXPECT_EQ(bytes[bra], 0x14);  // BRA
    EXPECT_EQ(ReadI32(bytes, bra + 1), 21);
    EXPECT_EQ(bra + 5 + 21, code + 61);
    EXPECT_EQ(bytes[code + 61], 0x12);  // POP: 1 byte, the statement's result is dropped
    EXPECT_EQ(bytes[code + 62], 0x0d);  // the next statement begins

    // The MenuManager instance: name push at 936, CALL at 938, 14 arguments.
    EXPECT_EQ(bytes[code + 936], 0x0d);
    EXPECT_EQ(bytes[code + 937], 0x01);  // identifiers[1] == "Task_New"
    EXPECT_EQ(bytes[code + 938], 0x18);
    EXPECT_EQ(ReadI32(bytes, code + 939), 14);

    // Argument 0 is PUSH1 MINUS BRK: the integer -1, three bytes.
    const size_t arg0 = code + 1004;
    EXPECT_EQ(bytes[arg0], 0x10);      // PUSH1
    EXPECT_EQ(bytes[arg0 + 1], 0x2c);  // MINUS
    EXPECT_EQ(bytes[arg0 + 2], 0x00);  // BRK
    // Argument 1 is PUSHSIB 0 -> strings[0] == "MenuManager".
    EXPECT_EQ(bytes[code + 1007], 0x09);
    EXPECT_EQ(bytes[code + 1008], 0x00);
    EXPECT_EQ(qvm.strings[bytes[code + 1008]], "MenuManager");
    // Argument 5 is PUSHIIB 2 -> identifiers[2] == "FALSE", a Boolean value.
    EXPECT_EQ(bytes[code + 1020], 0x0d);
    EXPECT_EQ(bytes[code + 1021], 0x02);
    EXPECT_EQ(qvm.identifiers[bytes[code + 1021]], "FALSE");
}

// ─── the ingame menu ─────────────────────────────────────────────────────────

TEST(MenuQvm, IngameMenuDefinition) {
    if (!CorpusAvailable()) {
        GTEST_SKIP() << "no menu corpus at " << IngameMenuPath().parent_path();
    }

    const igi::MenuDefinition def = igi::MenuDefinition::FromQvmFile(IngameMenuPath().string());
    ASSERT_TRUE(def.ok) << def.error;
    EXPECT_EQ(def.source, IngameMenuPath().string());

    // 81 widgets, all reachable from the root.
    EXPECT_EQ(def.widget_count, 81);
    ASSERT_EQ(def.Flatten().size(), 81u);

    const std::map<std::string, int> counts = TypeCounts(def);
    const std::map<std::string, int> expected = {
        {"MenuManager", 1},  {"MenuScreen", 4},   {"MenuFrame", 28},   {"MenuText", 34},
        {"MenuTextConditional", 1}, {"ListBox", 2}, {"SlideBar", 5},  {"ToggleBox", 1},
        {"ControlsMenu", 1}, {"DialogWindow", 4},
    };
    EXPECT_EQ(counts, expected);
    EXPECT_EQ(counts.size(), 10u);

    // The root is the MenuManager, with its three declared parameters.
    EXPECT_EQ(def.root.type, "MenuManager");
    EXPECT_EQ(def.root.id, -1);
    EXPECT_EQ(def.root.name, "Menu manager for the ingame menu");
    ASSERT_EQ(def.root.fields.size(), 3u);
    EXPECT_EQ(def.root.fields[0].name, "Current menuscreen");
    EXPECT_EQ(def.root.fields[0].type, "Int32");
    EXPECT_EQ(def.root.FieldNumber("Current menuscreen"), 900.0);
    EXPECT_EQ(def.root.FieldText("Resource path"), "LOCAL:menusystem/ingamemenu.res");
    const igi::MenuValue* own_display = def.root.FindValue("isOwnDisplay");
    ASSERT_NE(own_display, nullptr);
    EXPECT_EQ(own_display->kind, igi::MenuValueKind::Boolean);
    EXPECT_EQ(own_display->AsText(), "FALSE");
    EXPECT_EQ(own_display->AsNumber(), 0.0);
    EXPECT_EQ(def.root.FieldNumber("missing field", -5.0), -5.0);

    // The screens and the dialog windows are the root's direct children.
    ASSERT_EQ(def.root.children.size(), 8u);
    EXPECT_EQ(def.root.children[0].type, "MenuScreen");
    EXPECT_EQ(def.root.children[3].type, "MenuScreen");
    EXPECT_EQ(def.root.children[4].type, "DialogWindow");
    EXPECT_EQ(def.root.children[7].type, "DialogWindow");

    const std::vector<int> ids = def.Ids();
    ASSERT_GE(ids.size(), 11u);
    // The root, then the whole subtree of screen 900 (8 widgets), then screen 901.
    EXPECT_EQ(JoinInts(std::vector<int>(ids.begin(), ids.begin() + 11)),
              "-1, 900, -1, -1, -1, -1, -1, -1, -1, -1, 901");
    const igi::MenuWidget* screen = def.FindById(900);
    ASSERT_NE(screen, nullptr);
    EXPECT_EQ(screen->type, "MenuScreen");
    EXPECT_EQ(screen->name, "Main Menu");
    EXPECT_EQ(screen->FieldText("Background"), "");
    EXPECT_EQ(screen->FieldNumber("Frame x0"), 85.0);
    EXPECT_EQ(screen->FieldNumber("Frame x1"), 555.0);
    EXPECT_EQ(screen->FieldText("On Press Escape"), "MenuManager_LeaveMenus(900, MENUMANAGER_IDLE)");
    EXPECT_EQ(def.FindById(901)->name, "Graphics Configuration");
    EXPECT_EQ(def.FindById(902)->name, "Sound Configuration");
    EXPECT_EQ(def.FindById(903)->name, "Controls Configuration");
    EXPECT_EQ(def.FindById(930)->type, "DialogWindow");
    EXPECT_EQ(def.FindById(930)->name, "Reset Controls");
    EXPECT_EQ(def.FindById(932)->name, "restart mission");
    EXPECT_EQ(def.FindById(954)->type, "ControlsMenu");
    EXPECT_EQ(def.FindById(950)->type, "ListBox");
    EXPECT_EQ(def.FindById(956)->type, "SlideBar");
    EXPECT_EQ(def.FindById(957)->type, "ToggleBox");
    EXPECT_EQ(def.FindById(1234), nullptr);

    // The five selectable items of the main menu: two levels below the screen,
    // because a MenuScreen holds MenuFrames and the frames hold the items.
    ASSERT_EQ(screen->children.size(), 2u);
    EXPECT_EQ(screen->children[0].type, "MenuFrame");
    EXPECT_EQ(screen->children[0].children.size(), 1u);
    EXPECT_EQ(screen->children[0].children[0].FieldText("Text Resource"), "Game Paused");
    ASSERT_EQ(screen->children[1].children.size(), 5u);
    EXPECT_EQ(Join(DescendantTypes(*screen)),
              "MenuFrame, MenuText, MenuFrame, MenuTextConditional, MenuText, MenuText, MenuText, MenuText");
    EXPECT_EQ(screen->children[1].children[0].name, "resume");
    EXPECT_EQ(screen->children[1].children[0].type, "MenuTextConditional");
    EXPECT_EQ(screen->children[1].children[0].FieldText("Text Resource"), "Resume Game");
    EXPECT_EQ(screen->children[1].children[0].FieldNumber("ColourIndex"), 2.0);
    // The VarString parameters hold the *source text* of an expression, so the
    // container passes the string "TRUE" here (PUSHSIB), not the identifier TRUE.
    // Where a bool8 parameter is passed, the identifier is used and the value is a
    // Boolean (see isOwnDisplay on the root above).
    EXPECT_EQ(screen->children[1].children[0].FindValue("isVisible Expression")->kind,
              igi::MenuValueKind::String);
    EXPECT_EQ(screen->children[1].children[0].FieldText("isVisible Expression"), "TRUE");
    EXPECT_EQ(screen->children[1].children[0].FieldText("isEnabled Expression"), "!LevelFlow_LevelFailed()");
    EXPECT_EQ(screen->children[1].children[1].FieldText("Text Resource"), "Restart Game");
    EXPECT_EQ(screen->children[1].children[2].FieldText("Text Resource"), "Sound");
    EXPECT_EQ(screen->children[1].children[3].FieldText("Text Resource"), "Controls");
    EXPECT_EQ(screen->children[1].children[4].FieldText("Text Resource"), "Quit to Main Menu");
    EXPECT_EQ(screen->children[1].children[4].FieldText("On Click Script"),
              "MenuManager_ActivatePopupScreen(931)\n");

    EXPECT_EQ(def.FindByType("MenuText")->type, "MenuText");
    EXPECT_EQ(def.FindByType("NoSuchType"), nullptr);
}

// ─── the main menu ───────────────────────────────────────────────────────────

TEST(MenuQvm, MainMenuDefinition) {
    if (!CorpusAvailable()) {
        GTEST_SKIP() << "no menu corpus at " << IngameMenuPath().parent_path();
    }

    const igi::MenuDefinition def = igi::MenuDefinition::FromQvmFile(MainMenuPath().string());
    ASSERT_TRUE(def.ok) << def.error;
    EXPECT_EQ(def.source, MainMenuPath().string());
    EXPECT_EQ(def.widget_count, 191);
    ASSERT_EQ(def.Flatten().size(), 191u);

    const std::map<std::string, int> counts = TypeCounts(def);
    const std::map<std::string, int> expected = {
        {"MenuManager", 1},   {"MenuScreen", 12},  {"MenuFrame", 68},     {"MenuText", 79},
        {"MenuTextConditional", 2}, {"ListBox", 3}, {"SlideBar", 5},      {"ToggleBox", 5},
        {"ControlsMenu", 1},  {"PictureBox", 2},   {"InputBox", 3},       {"AsciiBox", 1},
        {"TextBox", 1},       {"ScrollListBox", 1}, {"TypeWriterBox", 1}, {"DialogWindow", 6},
    };
    EXPECT_EQ(counts, expected);
    EXPECT_EQ(counts.size(), 16u);

    EXPECT_EQ(def.root.type, "MenuManager");
    EXPECT_EQ(def.root.id, -1);
    EXPECT_EQ(def.root.name, "Menu manager for the main menus");
    EXPECT_EQ(def.root.FieldText("Resource path"), "LOCAL:menusystem/menusystem.res");
    EXPECT_EQ(def.root.FindValue("isOwnDisplay")->AsText(), "TRUE");
    EXPECT_EQ(def.root.FindValue("isOwnDisplay")->AsNumber(), 1.0);

    // The main menu's screen ids, in file order.
    std::vector<int> screen_ids;
    for (const igi::MenuWidget* widget : def.Flatten()) {
        if (widget->type == "MenuScreen") {
            screen_ids.push_back(widget->id);
        }
    }
    EXPECT_EQ(JoinInts(screen_ids), "900, 904, 905, 906, 907, 910, 911, 912, 913, 914, 915, 916");
    ASSERT_NE(def.FindById(900), nullptr);
    EXPECT_EQ(def.FindById(900)->name, "Main Menu");

    std::vector<std::string> labels;
    for (const igi::MenuWidget* widget : def.Flatten()) {
        if (widget->type == "MenuText" || widget->type == "MenuTextConditional") {
            labels.push_back(widget->FieldText("Text Resource"));
        }
    }
    EXPECT_NE(std::find(labels.begin(), labels.end(), "Single Player"), labels.end());
    EXPECT_NE(std::find(labels.begin(), labels.end(), "Content Control"), labels.end());
    EXPECT_NE(std::find(labels.begin(), labels.end(), "Credits"), labels.end());
}

// ─── the two entry points must agree on the real files ──────────────────────
//
// `FromQvm` walks the instruction stream; `FromDecompiledSource` reads what
// `QVM_DecompileToString` prints.  Nothing links them, so this differential is the
// only thing that proves the text path against real data instead of against a
// hand-written fixture.

TEST(MenuQvm, DecompiledSourceAndContainerPathsAgree) {
    if (!CorpusAvailable()) {
        GTEST_SKIP() << "no menu corpus at " << IngameMenuPath().parent_path();
    }

    for (const std::filesystem::path& path : {IngameMenuPath(), MainMenuPath()}) {
        const QVMFile qvm = QVM_Parse(path.string());
        ASSERT_TRUE(qvm.valid) << qvm.error;
        const std::string text = QVM_DecompileToString(qvm);
        ASSERT_FALSE(text.empty()) << path.filename().string();

        const igi::MenuDefinition container = igi::MenuDefinition::FromQvm(qvm);
        const igi::MenuDefinition decompiled = igi::MenuDefinition::FromDecompiledSource(text);
        ASSERT_TRUE(container.ok) << path.filename().string() << ": " << container.error;
        ASSERT_TRUE(decompiled.ok) << path.filename().string() << ": " << decompiled.error
                                   << "\ntext starts: " << text.substr(0, 300);

        EXPECT_EQ(decompiled.widget_count, container.widget_count) << path.filename().string();
        const std::vector<const igi::MenuWidget*> a = container.Flatten();
        const std::vector<const igi::MenuWidget*> b = decompiled.Flatten();
        ASSERT_EQ(a.size(), b.size()) << path.filename().string();
        ASSERT_EQ(a.size(), static_cast<size_t>(container.widget_count));
        for (size_t i = 0; i < a.size(); ++i) {
            EXPECT_EQ(a[i]->id, b[i]->id) << path.filename().string() << " widget " << i;
            EXPECT_EQ(a[i]->type, b[i]->type) << path.filename().string() << " widget " << i;
            EXPECT_EQ(a[i]->name, b[i]->name) << path.filename().string() << " widget " << i;
            ASSERT_EQ(a[i]->fields.size(), b[i]->fields.size())
                << path.filename().string() << " widget " << i << " (" << a[i]->type << ")";
            for (size_t f = 0; f < a[i]->fields.size(); ++f) {
                EXPECT_EQ(a[i]->fields[f].name, b[i]->fields[f].name);
                EXPECT_EQ(a[i]->fields[f].type, b[i]->fields[f].type);
                ASSERT_EQ(a[i]->fields[f].values.size(), b[i]->fields[f].values.size());
                // The printer keeps numbers, quoted strings and identifiers
                // distinguishable, so the kinds have to match as well as the text.
                EXPECT_EQ(a[i]->fields[f].values[0].kind, b[i]->fields[f].values[0].kind)
                    << a[i]->type << "." << a[i]->fields[f].name;
                EXPECT_EQ(a[i]->fields[f].values[0].AsText(), b[i]->fields[f].values[0].AsText())
                    << a[i]->type << "." << a[i]->fields[f].name;
            }
            ASSERT_EQ(a[i]->children.size(), b[i]->children.size())
                << path.filename().string() << " widget " << i << " (" << a[i]->type << ")";
        }
    }
}

// ─── the schema the container declares ───────────────────────────────────────

TEST(MenuQvm, EveryDeclaredTypeGetsItsDeclaredParameterCount) {
    if (!CorpusAvailable()) {
        GTEST_SKIP() << "no menu corpus at " << IngameMenuPath().parent_path();
    }

    struct Case {
        const char* name;
        std::filesystem::path path;
        const std::map<std::string, int>* declared;
    };
    const std::vector<Case> cases = {
        {"ingamemenu.qvm", IngameMenuPath(), &IngameMenuDeclaredFields()},
        {"MAINMENU.QVM", MainMenuPath(), &MainMenuDeclaredFields()},
    };

    for (const Case& test_case : cases) {
        const igi::MenuDefinition def = igi::MenuDefinition::FromQvmFile(test_case.path.string());
        ASSERT_TRUE(def.ok) << test_case.name << ": " << def.error;
        for (const igi::MenuWidget* widget : def.Flatten()) {
            const auto it = test_case.declared->find(widget->type);
            if (it != test_case.declared->end()) {
                EXPECT_EQ(static_cast<int>(widget->fields.size()), it->second)
                    << test_case.name << ": " << widget->type << " id " << widget->id;
                for (const igi::MenuField& field : widget->fields) {
                    ASSERT_EQ(field.values.size(), 1u)
                        << test_case.name << ": " << widget->type << " field " << field.name;
                    EXPECT_FALSE(field.name.empty());
                    EXPECT_FALSE(field.type.empty());
                    // A nested call in a parameter position is an error in the
                    // runtime (level.c:209) and the reader reports it, so it can
                    // never appear as a parameter value here.
                    EXPECT_NE(field.values[0].kind, igi::MenuValueKind::Call)
                        << test_case.name << ": " << widget->type << " field " << field.name;
                }
            } else {
                // ControlsMenu is declared in C, not in either container
                // (src/widget/controlsmenu.c:81-96), so its instance carries no
                // parameters in the file: Task_New(954, "ControlsMenu", "").
                EXPECT_EQ(widget->type, "ControlsMenu") << test_case.name;
                EXPECT_TRUE(widget->fields.empty());
                EXPECT_TRUE(widget->children.empty());
            }
        }
    }
}

TEST(MenuQvm, WidgetTypesAndNamesAreNeverEmpty) {
    if (!CorpusAvailable()) {
        GTEST_SKIP() << "no menu corpus at " << IngameMenuPath().parent_path();
    }

    for (const std::filesystem::path& path : {IngameMenuPath(), MainMenuPath()}) {
        const igi::MenuDefinition def = igi::MenuDefinition::FromQvmFile(path.string());
        ASSERT_TRUE(def.ok) << def.error;
        for (const igi::MenuWidget* widget : def.Flatten()) {
            EXPECT_FALSE(widget->type.empty());
            // Only the root MenuManager has a non-negative id besides the screens,
            // dialog windows and the controls; everything else is -1.
            EXPECT_GE(widget->id, -1);
            for (const igi::MenuWidget& child : widget->children) {
                EXPECT_FALSE(child.type.empty());
            }
        }
    }
}

// ─── the decompiled-source path ──────────────────────────────────────────────

TEST(MenuQvm, DecompiledSourceRecoversTheSameModel) {
    const igi::MenuDefinition def = igi::MenuDefinition::FromDecompiledSource(kDecompiledFixture);
    ASSERT_TRUE(def.ok) << def.error;
    EXPECT_EQ(def.widget_count, 3);
    EXPECT_EQ(JoinInts(def.Ids()), "-1, 900, 940");

    EXPECT_EQ(def.root.type, "MenuManager");
    EXPECT_EQ(def.root.id, -1);
    EXPECT_EQ(def.root.name, "Menu manager for the fixture");
    ASSERT_EQ(def.root.fields.size(), 3u);
    EXPECT_EQ(def.root.FieldNumber("Current menuscreen"), 900.0);
    EXPECT_EQ(def.root.FieldText("Resource path"), "LOCAL:menusystem/ingamemenu.res");
    EXPECT_EQ(def.root.FindValue("isOwnDisplay")->kind, igi::MenuValueKind::Boolean);

    ASSERT_EQ(def.root.children.size(), 1u);
    const igi::MenuWidget& screen = def.root.children[0];
    EXPECT_EQ(screen.id, 900);
    EXPECT_EQ(screen.type, "MenuScreen");
    EXPECT_EQ(screen.name, "Main Menu");
    EXPECT_EQ(screen.FieldText("Background"), "back.tga");
    EXPECT_EQ(screen.FieldNumber("Frame x0"), 10.0);

    ASSERT_EQ(screen.children.size(), 1u);
    const igi::MenuWidget& text = screen.children[0];
    EXPECT_EQ(text.id, 940);
    EXPECT_EQ(text.type, "MenuText");
    EXPECT_EQ(text.name, "resume");
    // The decompiler escapes a newline inside a string as \n
    // (source/level/qvm_decompiler.cpp:36-44).
    EXPECT_EQ(text.FieldText("Text Resource"), "Resume Game\nsecond line");
    EXPECT_EQ(text.FieldText("Font Resource"), "font3.fnt");
    EXPECT_EQ(text.FieldNumber("ColourIndex"), -1.0);
    EXPECT_EQ(def.FindById(940), &text);
}

TEST(MenuQvm, ValueKindsAndAccessors) {
    const char* source =
        "Task_DeclareParameters(\"MenuManager\", \"A\", \"Int32\", \"B\", \"Real\", \"C\", \"bool8\", \"D\", "
        "\"VarString\");\n"
        "Task_New(-1, \"MenuManager\", \"n\", 1, 2.5, TRUE, \"text\");\n";
    const igi::MenuDefinition def = igi::MenuDefinition::FromDecompiledSource(source);
    ASSERT_TRUE(def.ok) << def.error;
    ASSERT_EQ(def.root.fields.size(), 4u);
    EXPECT_EQ(def.root.fields[0].values[0].kind, igi::MenuValueKind::Integer);
    EXPECT_EQ(def.root.fields[1].values[0].kind, igi::MenuValueKind::Real);
    EXPECT_EQ(def.root.fields[2].values[0].kind, igi::MenuValueKind::Boolean);
    EXPECT_EQ(def.root.fields[3].values[0].kind, igi::MenuValueKind::String);
    EXPECT_EQ(def.root.fields[0].values[0].AsText(), "1");
    EXPECT_EQ(def.root.fields[1].values[0].AsText(), "2.5");
    EXPECT_EQ(def.root.fields[2].values[0].AsText(), "TRUE");
    EXPECT_EQ(def.root.fields[3].values[0].AsText(), "text");
    EXPECT_TRUE(def.root.fields[0].values[0].IsNumber());
    EXPECT_TRUE(def.root.fields[1].values[0].IsNumber());
    EXPECT_TRUE(def.root.fields[2].values[0].IsNumber());
    EXPECT_FALSE(def.root.fields[3].values[0].IsNumber());
    EXPECT_EQ(def.root.FieldNumber("B"), 2.5);
    EXPECT_EQ(def.root.FieldNumber("C"), 1.0);
    // A string field is not numeric, so the fallback is returned.
    EXPECT_EQ(def.root.FieldNumber("D", -7.0), -7.0);
    EXPECT_EQ(def.root.FindValue("D")->AsNumber(), 0.0);
    EXPECT_EQ(def.root.FieldText("missing"), "");
    EXPECT_EQ(def.root.FindValue("missing"), nullptr);
    EXPECT_EQ(def.root.FindField("missing"), nullptr);
    EXPECT_NE(def.root.FindField("A"), nullptr);
}

TEST(MenuQvm, UnsupportedSourceIsReportedNotGuessed) {
    struct Case {
        const char* source;
        const char* what;
        int widgets;  // widgets the partial model holds when the error is raised
    };
    const std::vector<Case> cases = {
        {"while(1)\n{\n}\n", "'while' blocks are not supported", 0},
        {"Task_New(1, \"A\", \"B\");\nif(1)\n{\n}\n", "'if' blocks are not supported", 0},
        {"Task_New(1 + 2, \"A\", \"B\");\n", "the operator '+' is not supported", 0},
        {"Task_New(1, \"A\", \"B);\n", "unterminated string literal", 0},
        {"Task_New(1, \"A\", \"B\"\n", "unterminated argument list", 0},
        {"Task_DeclareParameters(\"MenuManager\", \"A\");\n", "name/type pairs", 0},
        {"Task_DeclareParameters(\"MenuManager\", \"A\", \"Int32\");\n", "no top-level Task_New", 0},
        {"", "no top-level Task_New", 0},
        {"Task_New(-1, \"MenuScreen\", \"n\", 1);\n", "not a MenuManager", 1},
    };
    for (const Case& test_case : cases) {
        const igi::MenuDefinition def = igi::MenuDefinition::FromDecompiledSource(test_case.source);
        EXPECT_FALSE(def.ok) << "source: " << test_case.source;
        EXPECT_NE(def.error.find(test_case.what), std::string::npos)
            << "source: " << test_case.source << "\nerror: " << def.error;
        EXPECT_EQ(def.widget_count, test_case.widgets) << "source: " << test_case.source;
        EXPECT_EQ(def.ok, false);
    }
}

// ─── containers that are not menus ───────────────────────────────────────────

TEST(MenuQvm, EmptyAndInvalidContainersAreReported) {
    const std::filesystem::path empty_path = WriteTempFile("igi_menu_qvm_test_empty.qvm", EmptyLoopContainer());
    const QVMFile empty = QVM_Parse(empty_path.string());
    ASSERT_TRUE(empty.valid) << empty.error;
    EXPECT_EQ(empty.instructions.size(), 1u);
    EXPECT_TRUE(empty.identifiers.empty());
    EXPECT_TRUE(empty.strings.empty());

    const igi::MenuDefinition empty_def = igi::MenuDefinition::FromQvmFile(empty_path.string());
    EXPECT_FALSE(empty_def.ok);
    EXPECT_NE(empty_def.error.find("no top-level Task_New"), std::string::npos) << empty_def.error;
    EXPECT_EQ(empty_def.source, empty_path.string());
    EXPECT_TRUE(empty_def.Flatten().empty());

    // FromQvm takes the already-parsed container and reports the same thing.
    const igi::MenuDefinition from_qvm = igi::MenuDefinition::FromQvm(empty);
    EXPECT_FALSE(from_qvm.ok);
    EXPECT_EQ(from_qvm.error, empty_def.error);
    EXPECT_TRUE(from_qvm.source.empty());

    const std::filesystem::path garbage_path = WriteTempFile("igi_menu_qvm_test_garbage.qvm",
                                                             std::vector<unsigned char>(80, 0xff));
    const igi::MenuDefinition garbage = igi::MenuDefinition::FromQvmFile(garbage_path.string());
    EXPECT_FALSE(garbage.ok);
    EXPECT_NE(garbage.error.find("LOOP"), std::string::npos) << garbage.error;

    const igi::MenuDefinition missing =
        igi::MenuDefinition::FromQvmFile((std::filesystem::temp_directory_path() / "igi_menu_qvm_absent.qvm").string());
    EXPECT_FALSE(missing.ok);
    EXPECT_FALSE(missing.error.empty());

    std::error_code ec;
    std::filesystem::remove(empty_path, ec);
    std::filesystem::remove(garbage_path, ec);
}

// ─── the recovered inventory, printed ────────────────────────────────────────

TEST(MenuQvm, PrintRecoveredInventory) {
    if (!CorpusAvailable()) {
        GTEST_SKIP() << "no menu corpus at " << IngameMenuPath().parent_path();
    }

    for (const std::filesystem::path& path : {IngameMenuPath(), MainMenuPath()}) {
        const QVMFile qvm = QVM_Parse(path.string());
        ASSERT_TRUE(qvm.valid) << qvm.error;
        const igi::MenuDefinition def = igi::MenuDefinition::FromQvmFile(path.string());
        ASSERT_TRUE(def.ok) << def.error;

        std::cout << "[menu-qvm] " << path.filename().string() << ": " << qvm.strings.size() << " strings, "
                  << qvm.identifiers.size() << " names, " << qvm.instructions.size() << " instructions, "
                  << CountCalls(qvm) << " calls, " << def.widget_count << " widgets\n";
        std::cout << "[menu-qvm]   root: id " << def.root.id << " '" << def.root.name << "' resource '"
                  << def.root.FieldText("Resource path") << "'\n";
        std::cout << "[menu-qvm]   screens: ";
        for (const igi::MenuWidget* widget : def.Flatten()) {
            if (widget->type == "MenuScreen") {
                std::cout << widget->id << " '" << widget->name << "'  ";
            }
        }
        std::cout << "\n[menu-qvm]   widgets:";
        for (const auto& entry : TypeCounts(def)) {
            std::cout << " " << entry.first << "=" << entry.second;
        }
        std::cout << std::endl;
    }
}

// ─── the write side: the model back to a container the engine loads ──────────
//
// `menu/menu_qvm_writer.h` is the SAVE half of issue #74: the menu model back to
// the "LOOP" container.  The engine's file producer is a three-stage chain and the
// tests below hold the writer to all three:
//
//   1. `QscGen_Compile`   HOOK 0x4BBCB0  src/qvm/qscgen.c:86-142 -- one statement
//      per `Task_DeclareParameters` / `Task_New` (a BLOCK_EXPR is the expression
//      then `OP_POP`, :259-264) and ONE trailing `OP_BRK` (:134);
//   2. `QvmAsm_OpPush`    HOOK 0x4C0A10  src/qasm/asm.c:74-124 -- the shortest
//      push encoding; `Asm_AddString` / `Asm_AddName` HOOK 0x4BB030 / 0x4BB180
//      (src/script/asm.c:137-157, :183-200) assign pool indices on first use;
//   3. `QvmAsm_Assemble`  HOOK 0x4BB270  src/qvm/qvmasm.c:103-276 -- the header at
//      :227-251 ("LOOP", version 8, unk08 5) and the two offset/text tables.
//
// The loader that has to accept the result is `Qvm_Load` HOOK 0x4BD220
// (src/qvm/qvm.c:433-486: "LOOP", version 8, unk08 >= 5), and the repository's own
// reader for it is `QVM_Parse` + `MenuDefinition::FromQvm`, which every round-trip
// below goes through.

namespace {

std::string KindName(igi::MenuValueKind kind) {
    switch (kind) {
    case igi::MenuValueKind::Integer: return "int";
    case igi::MenuValueKind::Real: return "real";
    case igi::MenuValueKind::String: return "string";
    case igi::MenuValueKind::Name: return "name";
    case igi::MenuValueKind::Boolean: return "bool";
    case igi::MenuValueKind::Call: return "call";
    }
    return "?";
}

void DumpWidget(const igi::MenuWidget& widget, const std::string& indent, std::string* out) {
    *out += indent + "id=" + std::to_string(widget.id) + " type='" + widget.type + "' name='" +
            widget.name + "'\n";
    for (const igi::MenuField& field : widget.fields) {
        *out += indent + "  " + field.name + ":" + field.type + " =";
        for (const igi::MenuValue& value : field.values) {
            *out += " " + KindName(value.kind) + "(" + value.AsText() + ")";
        }
        *out += "\n";
    }
    for (const igi::MenuWidget& child : widget.children) {
        DumpWidget(child, indent + "  ", out);
    }
}

// The whole definition as text: the declaration statements in file order, then the
// widget tree with every field, kind and value.  Two definitions compare equal only
// when they are the same menu.
std::string StructureDump(const igi::MenuDefinition& def) {
    std::string out;
    for (const igi::MenuTypeDecl& decl : def.declarations) {
        out += "declare " + decl.name;
        for (const igi::MenuField& field : decl.fields) {
            out += " " + field.name + ":" + field.type;
        }
        out += "\n";
    }
    DumpWidget(def.root, "", &out);
    return out;
}

// The widget tree only, for the case where the declaration list is recovered rather
// than read from the file (its order is then not the file's).
std::string WidgetDump(const igi::MenuDefinition& def) {
    std::string out;
    DumpWidget(def.root, "", &out);
    return out;
}

std::string FirstDifference(const std::vector<unsigned char>& a,
                            const std::vector<unsigned char>& b) {
    const size_t common = std::min(a.size(), b.size());
    for (size_t i = 0; i < common; ++i) {
        if (a[i] != b[i]) {
            return "first differing byte at offset " + std::to_string(i);
        }
    }
    if (a.size() != b.size()) {
        return "same prefix, different length (" + std::to_string(a.size()) + " vs " +
               std::to_string(b.size()) + ")";
    }
    return "identical";
}

igi::MenuField Field(const std::string& name, const std::string& type) {
    igi::MenuField field;
    field.name = name;
    field.type = type;
    return field;
}

igi::MenuValue IntValue(long long value) {
    igi::MenuValue v;
    v.kind = igi::MenuValueKind::Integer;
    v.integer = value;
    return v;
}

igi::MenuValue RealValue(double value) {
    igi::MenuValue v;
    v.kind = igi::MenuValueKind::Real;
    v.real = value;
    return v;
}

igi::MenuValue StrValue(const std::string& text) {
    igi::MenuValue v;
    v.kind = igi::MenuValueKind::String;
    v.text = text;
    return v;
}

igi::MenuValue NameValue(const std::string& text) {
    igi::MenuValue v;
    v.kind = igi::MenuValueKind::Name;
    v.text = text;
    return v;
}

igi::MenuValue BoolValue(const std::string& text) {
    igi::MenuValue v;
    v.kind = igi::MenuValueKind::Boolean;
    v.text = text;
    return v;
}

igi::MenuValue CallValue(const std::string& text) {
    igi::MenuValue v;
    v.kind = igi::MenuValueKind::Call;
    v.text = text;
    return v;
}

// One instance of a declared type, with `values` in declaration order.  A value
// beyond the declaration is dropped, so a field-count mismatch has to be built by
// hand (see the refusal test).
igi::MenuWidget WidgetWith(const igi::MenuQvmTypeDecl& decl, const std::vector<igi::MenuValue>& values) {
    igi::MenuWidget widget;
    widget.id = -1;
    widget.type = decl.name;
    widget.name = "instance";
    for (size_t i = 0; i < decl.fields.size(); ++i) {
        igi::MenuField field = decl.fields[i];
        if (i < values.size()) {
            field.values.push_back(values[i]);
        }
        widget.fields.push_back(std::move(field));
    }
    return widget;
}

// The two-integer document used by the refusal cases: one declared type, one
// instance, both well formed until the case makes them not.
igi::MenuQvmTypeDecl TwoFieldType(const std::string& name = "MenuManager") {
    igi::MenuQvmTypeDecl decl;
    decl.name = name;
    decl.fields = {Field("First", "Int32"), Field("Second", "Int32")};
    return decl;
}

}  // namespace

TEST(MenuQvmWrite, RetailContainersRoundTripByteForByte) {
    if (!CorpusAvailable()) {
        GTEST_SKIP() << "no menu corpus at " << IngameMenuPath().parent_path();
    }

    for (const std::filesystem::path& path : {IngameMenuPath(), MainMenuPath()}) {
        const igi::MenuDefinition def = igi::MenuDefinition::FromQvmFile(path.string());
        ASSERT_TRUE(def.ok) << path.filename().string() << ": " << def.error;

        const igi::MenuQvmDocument document = igi::MenuQvmDocumentFromDefinition(def);
        ASSERT_TRUE(document.ok) << path.filename().string() << ": " << document.error;
        // The declaration list is the file's own, not a recovered one: that is what
        // makes the string pool order (and so the bytes) match.
        EXPECT_EQ(document.declarations.size(), def.declarations.size());

        const igi::MenuQvmWriteResult written = igi::MenuQvmWrite(document);
        ASSERT_TRUE(written.ok) << path.filename().string() << ": " << written.error;

        const std::vector<unsigned char> retail = ReadFileBytes(path);
        const std::vector<unsigned char> got(written.bytes.begin(), written.bytes.end());
        EXPECT_EQ(got.size(), retail.size()) << path.filename().string();
        EXPECT_TRUE(got == retail) << path.filename().string() << ": " << FirstDifference(got, retail);
        // The sizes the corpus was measured at, so a file that changed on disk cannot
        // silently pass.
        if (path == IngameMenuPath()) {
            EXPECT_EQ(got.size(), 11308u);
        } else {
            EXPECT_EQ(got.size(), 25549u);
        }
    }
}

TEST(MenuQvmWrite, WrittenContainerSatisfiesTheEngineLoaderRules) {
    if (!CorpusAvailable()) {
        GTEST_SKIP() << "no menu corpus at " << IngameMenuPath().parent_path();
    }

    const igi::MenuDefinition def = igi::MenuDefinition::FromQvmFile(IngameMenuPath().string());
    ASSERT_TRUE(def.ok) << def.error;
    const igi::MenuQvmWriteResult written = igi::MenuQvmWrite(igi::MenuQvmDocumentFromDefinition(def));
    ASSERT_TRUE(written.ok) << written.error;

    const std::filesystem::path path =
        WriteTempFile("igi_menu_qvm_test_written.qvm", written.bytes);
    const std::vector<unsigned char> bytes = ReadFileBytes(path);
    ASSERT_EQ(bytes.size(), written.bytes.size());

    // `Qvm_Load` (HOOK 0x4BD220, src/qvm/qvm.c:433-486) refuses anything whose magic
    // is not "LOOP", whose version is not 8, or whose unk08 is below 5.
    EXPECT_EQ(std::string(bytes.begin(), bytes.begin() + 4), "LOOP");
    EXPECT_EQ(ReadI32(bytes, 0x04), 8);
    EXPECT_GE(ReadI32(bytes, 0x08), 5);

    const QVMFile qvm = QVM_Parse(path.string());
    ASSERT_TRUE(qvm.valid) << qvm.error;
    // `QvmAsm_Assemble` writes the header, then the two offset/text tables, then the
    // code, with nothing aligned and nothing padded (src/qvm/qvmasm.c:227-251), so
    // every section must abut the next and the code must end the file.
    const QVMHeader& h = qvm.header;
    EXPECT_EQ(h.symOffsets, 60);  // sizeof(QvmHeader)
    EXPECT_EQ(h.symText, h.symOffsets + h.symTableSize);
    EXPECT_EQ(h.strOffsets, h.symText + h.symTextSize);
    EXPECT_EQ(h.strText, h.strOffsets + h.strTableSize);
    EXPECT_EQ(h.code, h.strText + h.strTextSize);
    EXPECT_EQ(h.code + h.codeSize, static_cast<int32_t>(bytes.size()));
    EXPECT_EQ(h.stackSize, 0);
    EXPECT_EQ(h.typeStackSize, 0);

    // The written container carries the shipped container's own pools and code.
    EXPECT_EQ(Join(qvm.identifiers), "Task_DeclareParameters, Task_New, FALSE, TRUE");
    EXPECT_EQ(qvm.strings.size(), 139u);
    EXPECT_EQ(qvm.instructions.size(), 2225u);
    EXPECT_EQ(CountCalls(qvm), 90);

    // …and decodes to the shipped container's structure, field for field: the
    // authored geometry of PAUSE_MENU_STYLE.md is what the engine's reader sees in
    // the editor's file.
    const igi::MenuDefinition shipped = igi::MenuDefinition::FromQvmFile(IngameMenuPath().string());
    ASSERT_TRUE(shipped.ok) << shipped.error;
    const igi::MenuDefinition reloaded = igi::MenuDefinition::FromQvmFile(path.string());
    ASSERT_TRUE(reloaded.ok) << reloaded.error;
    EXPECT_EQ(reloaded.widget_count, shipped.widget_count);
    EXPECT_EQ(JoinInts(reloaded.Ids()), JoinInts(shipped.Ids()));
    EXPECT_EQ(StructureDump(reloaded), StructureDump(shipped));

    const igi::MenuWidget* screen = reloaded.FindById(900);
    ASSERT_NE(screen, nullptr);
    EXPECT_EQ(screen->type, "MenuScreen");
    EXPECT_EQ(screen->name, "Main Menu");
    EXPECT_EQ(screen->FieldNumber("Frame x0"), 85.0);
    EXPECT_EQ(screen->FieldNumber("Frame y0"), 80.0);
    EXPECT_EQ(screen->FieldNumber("Frame x1"), 555.0);
    EXPECT_EQ(screen->FieldNumber("Frame y1"), 410.0);
    ASSERT_EQ(screen->children.size(), 2u);
    const igi::MenuWidget& title = screen->children[0];
    EXPECT_EQ(title.FieldNumber("X-Pos"), 100.0);
    EXPECT_EQ(title.FieldNumber("Y-Pos"), 77.0);
    EXPECT_EQ(title.FieldNumber("Width"), 440.0);
    EXPECT_EQ(title.FieldNumber("Height"), 26.0);
    EXPECT_EQ(title.FieldNumber("LineUpMode"), 0.0);
    ASSERT_EQ(title.children.size(), 1u);
    EXPECT_EQ(title.children[0].FieldText("Text Resource"), "Game Paused");
    EXPECT_EQ(title.children[0].FieldText("Font Resource"), "font3.fnt");
    const igi::MenuWidget& items = screen->children[1];
    EXPECT_EQ(items.FieldNumber("Y-Pos"), 97.0);
    EXPECT_EQ(items.FieldNumber("Height"), 200.0);
    EXPECT_EQ(items.FieldNumber("LineUpMode"), 1.0);
    EXPECT_EQ(items.FieldNumber("Y-Spacing"), 4.0);
    ASSERT_EQ(items.children.size(), 5u);

    std::error_code ec;
    std::filesystem::remove(path, ec);
}

TEST(MenuQvmWrite, AnEditSurvivesSaveAndReload) {
    if (!CorpusAvailable()) {
        GTEST_SKIP() << "no menu corpus at " << IngameMenuPath().parent_path();
    }

    const std::vector<unsigned char> retail = ReadFileBytes(IngameMenuPath());
    igi::MenuDefinition def = igi::MenuDefinition::FromQvmFile(IngameMenuPath().string());
    ASSERT_TRUE(def.ok) << def.error;
    const int widgets_before = def.widget_count;
    const std::string ids_before = JoinInts(def.Ids());

    // The edits a menu tree makes: one text and one geometry field, both through the
    // mutable accessors `qvm_image.h` exposes for exactly this.
    igi::MenuWidget* screen = def.FindById(900);
    ASSERT_NE(screen, nullptr);
    ASSERT_EQ(screen->children.size(), 2u);
    ASSERT_EQ(screen->children[0].children.size(), 1u);
    igi::MenuWidget* title = &screen->children[0].children[0];
    ASSERT_EQ(title->type, "MenuText");
    igi::MenuValue* text = title->FindValue("Text Resource");
    ASSERT_NE(text, nullptr);
    EXPECT_EQ(text->text, "Game Paused");
    text->text = "IGI EDITOR";  // the editor's own string; no game text is imported

    igi::MenuValue* lineup = screen->children[1].FindValue("LineUpMode");
    ASSERT_NE(lineup, nullptr);
    EXPECT_EQ(lineup->integer, 1);
    lineup->integer = 0;

    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "igi_menu_qvm_test_edited.qvm";
    std::error_code ec;
    std::filesystem::remove(path, ec);
    std::string error;
    const igi::MenuQvmDocument document = igi::MenuQvmDocumentFromDefinition(def);
    ASSERT_TRUE(document.ok) << document.error;
    ASSERT_TRUE(igi::MenuQvmWriteFile(document, path.string(), &error)) << error;

    const std::vector<unsigned char> written = ReadFileBytes(path);
    EXPECT_FALSE(written == retail) << "the save did not change the file";
    EXPECT_GT(written.size(), 0u);

    const igi::MenuDefinition reloaded = igi::MenuDefinition::FromQvmFile(path.string());
    ASSERT_TRUE(reloaded.ok) << reloaded.error;
    // The edit is in the reloaded file…
    const igi::MenuWidget* reloaded_screen = reloaded.FindById(900);
    ASSERT_NE(reloaded_screen, nullptr);
    ASSERT_EQ(reloaded_screen->children.size(), 2u);
    ASSERT_EQ(reloaded_screen->children[0].children.size(), 1u);
    EXPECT_EQ(reloaded_screen->children[0].children[0].FieldText("Text Resource"), "IGI EDITOR");
    EXPECT_EQ(reloaded_screen->children[1].FieldNumber("LineUpMode"), 0.0);
    // …and the game's text it replaced is gone from the container.
    const QVMFile qvm = QVM_Parse(path.string());
    ASSERT_TRUE(qvm.valid) << qvm.error;
    EXPECT_NE(std::find(qvm.strings.begin(), qvm.strings.end(), "IGI EDITOR"), qvm.strings.end());
    EXPECT_EQ(std::find(qvm.strings.begin(), qvm.strings.end(), "Game Paused"), qvm.strings.end());
    // …and nothing else moved: same widgets, same ids, same field values everywhere
    // but the two edited ones.
    EXPECT_EQ(reloaded.widget_count, widgets_before);
    EXPECT_EQ(JoinInts(reloaded.Ids()), ids_before);
    EXPECT_EQ(WidgetDump(reloaded), WidgetDump(def));

    std::filesystem::remove(path, ec);
}

TEST(MenuQvmWrite, AHandBuiltDefinitionStillRoundTrips) {
    igi::MenuDefinition def = igi::MenuDefinition::FromDecompiledSource(kDecompiledFixture);
    ASSERT_TRUE(def.ok) << def.error;
    // A definition built by hand has no declaration list; the writer recovers one
    // from the widgets' own fields (see menu_qvm_writer.h), which is what this test
    // exercises.
    def.declarations.clear();

    const igi::MenuQvmDocument document = igi::MenuQvmDocumentFromDefinition(def);
    ASSERT_TRUE(document.ok) << document.error;
    ASSERT_EQ(document.declarations.size(), 3u);
    EXPECT_EQ(document.declarations[0].name, "MenuManager");
    EXPECT_EQ(document.declarations[1].name, "MenuScreen");
    EXPECT_EQ(document.declarations[2].name, "MenuText");

    const igi::MenuQvmWriteResult written = igi::MenuQvmWrite(document);
    ASSERT_TRUE(written.ok) << written.error;
    const std::filesystem::path path =
        WriteTempFile("igi_menu_qvm_test_handbuilt.qvm", written.bytes);

    const QVMFile qvm = QVM_Parse(path.string());
    ASSERT_TRUE(qvm.valid) << qvm.error;
    const igi::MenuDefinition reloaded = igi::MenuDefinition::FromQvmFile(path.string());
    ASSERT_TRUE(reloaded.ok) << reloaded.error;
    EXPECT_EQ(reloaded.widget_count, def.widget_count);
    EXPECT_EQ(JoinInts(reloaded.Ids()), JoinInts(def.Ids()));
    // The recovered declaration order is the widget tree's, not the file's, so the
    // byte image differs from a retail one — but the menu it describes is the same.
    EXPECT_EQ(WidgetDump(reloaded), WidgetDump(def));
    EXPECT_EQ(reloaded.FindById(940)->FieldText("Text Resource"), "Resume Game\nsecond line");
    EXPECT_EQ(reloaded.FindById(940)->FieldNumber("ColourIndex"), -1.0);

    std::error_code ec;
    std::filesystem::remove(path, ec);
}

TEST(MenuQvmWrite, NegativeAndShortenedLiteralsFollowTheEngineAssembler) {
    const igi::MenuQvmTypeDecl decl = [&] {
        igi::MenuQvmTypeDecl d;
        d.name = "MenuManager";
        d.fields = {Field("I", "Int32"),   Field("R0", "Real32"), Field("R1", "Real32"),
                    Field("R2", "Real32"), Field("R3", "Real32"), Field("R4", "Real32")};
        return d;
    }();
    igi::MenuQvmDocument document;
    document.declarations = {decl};
    document.root = WidgetWith(decl, {IntValue(-1), RealValue(0.0), RealValue(1.0),
                                      RealValue(2.5), RealValue(-1.0), RealValue(-2.5)});
    document.ok = true;

    const igi::MenuQvmWriteResult written = igi::MenuQvmWrite(document);
    ASSERT_TRUE(written.ok) << written.error;
    const std::filesystem::path path =
        WriteTempFile("igi_menu_qvm_test_literals.qvm", written.bytes);
    const QVMFile qvm = QVM_Parse(path.string());
    ASSERT_TRUE(qvm.valid) << qvm.error;

    // `QvmAsm_OpPush`'s `v == -1 -> PUSHM` arm (src/qasm/asm.c:79-83) is never
    // reached by the engine's compiler: a negative literal is a magnitude push plus
    // `MINUS` (src/qvm/qscgen.c:382-397).  Measured over the installed corpus:
    // 41497 MINUS opcodes and no PUSHM at all.
    for (const QVMInstruction& instruction : qvm.instructions) {
        EXPECT_NE(instruction.type, QVMOpType::PUSHM);
    }
    // Four negative values: the widget's own id (-1, which `Task_New`'s first
    // argument always carries) plus the three negative fields.  The magnitude of a
    // -1.0 goes through the assembler's float arm, which shortens it to PUSH1
    // (src/qasm/asm.c:100-105) exactly as the positive 1.0 does, so that one is
    // PUSH1 + MINUS and only -2.5 is PUSHF + MINUS.
    int minus_count = 0;
    int push1_minus = 0;
    int pushf_minus = 0;
    for (size_t i = 1; i < qvm.instructions.size(); ++i) {
        if (qvm.instructions[i].type != QVMOpType::MINUS) {
            continue;
        }
        ++minus_count;
        if (qvm.instructions[i - 1].type == QVMOpType::PUSH1) {
            ++push1_minus;
        } else if (qvm.instructions[i - 1].type == QVMOpType::PUSHF) {
            EXPECT_FLOAT_EQ(qvm.instructions[i - 1].operand_float, 2.5f);
            ++pushf_minus;
        } else {
            ADD_FAILURE() << "MINUS preceded by " << QVM_OpName(qvm.instructions[i - 1].type);
        }
    }
    EXPECT_EQ(minus_count, 4);
    EXPECT_EQ(push1_minus, 3);  // the id -1, the field -1 and the field -1.0
    EXPECT_EQ(pushf_minus, 1);  // the field -2.5

    const igi::MenuDefinition reloaded = igi::MenuDefinition::FromQvmFile(path.string());
    ASSERT_TRUE(reloaded.ok) << reloaded.error;
    EXPECT_EQ(reloaded.root.FindValue("I")->kind, igi::MenuValueKind::Integer);
    EXPECT_EQ(reloaded.root.FindValue("I")->integer, -1);
    // 0.0 and 1.0 take `QvmAsm_OpPush`'s PUSH0 / PUSH1 arms (src/qasm/asm.c:100-105);
    // those opcodes carry no type, so they read back as integers, and so does the
    // -1.0 whose magnitude took the same arm.  The engine's own parameter parse does
    // not care: `ParamReal32_Parse` takes the argument's value through
    // `Script_GetArgReal` whatever push produced it
    // (HOOK 0x4EF9E0, src/physobj/paramops.c:347-353).
    EXPECT_EQ(reloaded.root.FieldText("R0"), "0");
    EXPECT_EQ(reloaded.root.FieldText("R1"), "1");
    EXPECT_EQ(reloaded.root.FindValue("R2")->kind, igi::MenuValueKind::Real);
    EXPECT_EQ(reloaded.root.FieldNumber("R2"), 2.5);
    EXPECT_EQ(reloaded.root.FindValue("R3")->kind, igi::MenuValueKind::Integer);
    EXPECT_EQ(reloaded.root.FieldNumber("R3"), -1.0);
    EXPECT_EQ(reloaded.root.FindValue("R4")->kind, igi::MenuValueKind::Real);
    EXPECT_EQ(reloaded.root.FieldNumber("R4"), -2.5);

    std::error_code ec;
    std::filesystem::remove(path, ec);
}

TEST(MenuQvmWrite, RefusesADefinitionTheReaderRejected) {
    // The fallback menu case: `app_menu.cpp` builds a definition with ok == false when
    // the retail file is missing, and a save action must never turn that into a file.
    igi::MenuDefinition broken;
    broken.error = "the container did not parse";
    const igi::MenuQvmDocument document = igi::MenuQvmDocumentFromDefinition(broken);
    EXPECT_FALSE(document.ok);
    EXPECT_EQ(document.error, "the container did not parse");

    const igi::MenuQvmWriteResult written = igi::MenuQvmWrite(document);
    EXPECT_FALSE(written.ok);
    EXPECT_FALSE(written.error.empty());
    EXPECT_TRUE(written.bytes.empty());

    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "igi_menu_qvm_test_refused.qvm";
    std::error_code ec;
    std::filesystem::remove(path, ec);
    std::string error;
    EXPECT_FALSE(igi::MenuQvmWriteFile(document, path.string(), &error));
    EXPECT_FALSE(error.empty());
    // Nothing was created: a refused save leaves no partial container behind.
    EXPECT_FALSE(std::filesystem::exists(path, ec));

    // A definition with no error text still refuses, with a message of its own.
    igi::MenuDefinition silent;
    const igi::MenuQvmDocument silent_document = igi::MenuQvmDocumentFromDefinition(silent);
    EXPECT_FALSE(silent_document.ok);
    EXPECT_FALSE(silent_document.error.empty());
    EXPECT_FALSE(igi::MenuQvmWrite(silent_document).ok);
}

TEST(MenuQvmWrite, RefusesADocumentTheEngineCouldNotHaveProduced) {
    struct Case {
        const char* what;  // a fragment of the writer's message
        igi::MenuQvmDocument document;
    };

    // A widget with one field more than its declaration.
    igi::MenuQvmDocument too_many_fields;
    too_many_fields.declarations = {TwoFieldType()};
    too_many_fields.root = WidgetWith(too_many_fields.declarations[0], {IntValue(1), IntValue(2)});
    too_many_fields.root.fields.push_back(Field("Third", "Int32"));
    too_many_fields.ok = true;

    // A widget of a type nothing declares.
    igi::MenuQvmDocument undeclared;
    {
        igi::MenuQvmTypeDecl decl = TwoFieldType("MenuText");
        undeclared.root = WidgetWith(decl, {IntValue(1), IntValue(2)});
    }
    undeclared.ok = true;

    // A declared parameter whose value is a call: `Level_NewTask` disables the
    // Task_New symbol while it parses parameters (src/level/level.c:182-185, :209),
    // so this cannot come from an engine-run script.
    igi::MenuQvmDocument call_parameter;
    call_parameter.declarations = {TwoFieldType()};
    call_parameter.root =
        WidgetWith(call_parameter.declarations[0], {CallValue("SomeCall"), IntValue(2)});
    call_parameter.ok = true;

    // A declared parameter with no value at all.
    igi::MenuQvmDocument missing_value;
    missing_value.declarations = {TwoFieldType()};
    missing_value.root = WidgetWith(missing_value.declarations[0], {IntValue(1)});
    missing_value.ok = true;

    // A declared parameter with two values.
    igi::MenuQvmDocument two_values;
    two_values.declarations = {TwoFieldType()};
    two_values.root = WidgetWith(two_values.declarations[0], {IntValue(1), IntValue(2)});
    two_values.root.fields[0].values.push_back(IntValue(3));
    two_values.ok = true;

    // The same type declared twice.
    igi::MenuQvmDocument duplicate;
    duplicate.declarations = {TwoFieldType(), TwoFieldType()};
    duplicate.root = WidgetWith(duplicate.declarations[0], {IntValue(1), IntValue(2)});
    duplicate.ok = true;

    // A declaration with no type name.
    igi::MenuQvmDocument unnamed;
    unnamed.declarations = {TwoFieldType("")};
    unnamed.root = WidgetWith(unnamed.declarations[0], {IntValue(1), IntValue(2)});
    unnamed.ok = true;

    // A widget with no type name.
    igi::MenuQvmDocument untyped;
    untyped.declarations = {TwoFieldType()};
    untyped.root = WidgetWith(untyped.declarations[0], {IntValue(1), IntValue(2)});
    untyped.root.type.clear();
    untyped.ok = true;

    // An integer whose magnitude does not fit the assembler's 32-bit literal
    // (`QvmAsm_OpPush`'s `crt_atoi`, src/qasm/asm.c:83-98).
    igi::MenuQvmDocument too_large;
    too_large.declarations = {TwoFieldType()};
    too_large.root = WidgetWith(too_large.declarations[0], {IntValue(2147483648LL), IntValue(2)});
    too_large.ok = true;

    igi::MenuQvmDocument too_negative;
    too_negative.declarations = {TwoFieldType()};
    too_negative.root =
        WidgetWith(too_negative.declarations[0], {IntValue(-2147483649LL), IntValue(2)});
    too_negative.ok = true;

    // A bool8 whose identifier is neither TRUE nor FALSE.
    igi::MenuQvmDocument bad_bool;
    {
        igi::MenuQvmTypeDecl decl;
        decl.name = "MenuManager";
        decl.fields = {Field("Flag", "bool8")};
        bad_bool.declarations = {decl};
        bad_bool.root = WidgetWith(decl, {BoolValue("YES")});
    }
    bad_bool.ok = true;

    const std::vector<Case> cases = {
        {"has 3 field(s) but its declaration has 2", too_many_fields},
        {"no Task_DeclareParameters declares it", undeclared},
        {"cannot be a call", call_parameter},
        {"has 0 value(s)", missing_value},
        {"has 2 value(s)", two_values},
        {"is declared twice", duplicate},
        {"has no type name", unnamed},
        {"a widget has no type", untyped},
        {"does not fit the engine's 32-bit literal", too_large},
        {"magnitude does not fit the engine's 32-bit literal", too_negative},
        {"the engine's scripts use exactly TRUE and FALSE", bad_bool},
    };

    for (const Case& test_case : cases) {
        const igi::MenuQvmWriteResult written = igi::MenuQvmWrite(test_case.document);
        EXPECT_FALSE(written.ok) << test_case.what;
        EXPECT_NE(written.error.find(test_case.what), std::string::npos) << written.error;
        EXPECT_TRUE(written.bytes.empty()) << test_case.what;
    }
}

// A positive control for the same rules: the well-formed two-integer document the
// refusal cases start from does serialise and read back.
TEST(MenuQvmWrite, AWellFormedHandBuiltDocumentSerialises) {
    const igi::MenuQvmTypeDecl decl = TwoFieldType();
    igi::MenuQvmDocument document;
    document.declarations = {decl};
    document.root = WidgetWith(decl, {IntValue(7), NameValue("FALSE")});
    document.ok = true;

    const igi::MenuQvmWriteResult written = igi::MenuQvmWrite(document);
    ASSERT_TRUE(written.ok) << written.error;
    const std::filesystem::path path =
        WriteTempFile("igi_menu_qvm_test_wellformed.qvm", written.bytes);

    const igi::MenuDefinition reloaded = igi::MenuDefinition::FromQvmFile(path.string());
    ASSERT_TRUE(reloaded.ok) << reloaded.error;
    EXPECT_EQ(reloaded.root.type, "MenuManager");
    EXPECT_EQ(reloaded.root.fields.size(), 2u);
    EXPECT_EQ(reloaded.root.FieldNumber("First"), 7.0);
    // FALSE is an identifier in the name table, exactly as in both retail files, and
    // the reader reports those two identifiers as booleans (see ValueKindsAndAccessors).
    EXPECT_EQ(reloaded.root.FindValue("Second")->kind, igi::MenuValueKind::Boolean);
    EXPECT_EQ(reloaded.root.FieldText("Second"), "FALSE");

    std::error_code ec;
    std::filesystem::remove(path, ec);
}
