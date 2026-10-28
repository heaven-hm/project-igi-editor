/******************************************************************************
 * @file    test_menu_ui.cpp
 * @brief   Host-native tests for the retail-driven in-app menu model
 *          (source/menu/menu_ui.cpp) and the app wiring (source/app_menu.cpp).
 *
 * The model is built from `igi::MenuDefinition` (source/menu/qvm_image.h).  The
 * fixtures below are hand-built, but their shape is not invented: they mirror the
 * structure the retail corpus file `menusystem/ingamemenu.qvm` declares, which is
 * reproduced verbatim in the comment above `RetailShapedFixture()`.  The oracle
 * for every behaviour asserted here is IGI-RE, cited by file:line in
 * source/menu/menu_ui.h.
 *
 * Oracle: /Users/haseeb-mir/Documents/Code/IGI-RE
 *   src/menu/menumanager.c:479-535  MenuManager_ReadInput 0x418580 (wrapping nav)
 *   src/menu/menumanager.c:594-625  MenuManager_CollectItems 0x4187F0 /
 *                                   MenuManager_AddItems 0x418840 (item list)
 *   src/menu/menumanager.c:537-545  MenuManager_SetFocus 0x4186E0
 *   src/menu/menumanager.c:547-560  MenuManager_OpenScreen 0x418710
 *   src/menu/menutypes.h:41,76      items[32], screenStack[16]
 *   src/loadscreen/keys.c:7-18      KEY_UP/-DOWN/-ENTER/-ESCAPE codes
 *   src/menuscreen/menutext.c       RUN_ACTION (activation), MAKE_LABEL (size)
 *   src/widget/menuwindow.c:200-266 MenuWindow_Arrange 0x4244B0
 *   src/widget/menuwindow.c:161-176 MenuWindow_Place 0x424420
 *   src/widget/menuwindow.c:335-360 MenuWindow_HitTest 0x4248A0
 *   src/menuscreen/menuscreen.h:141-142  SCREEN_X / SCREEN_Y
 *****************************************************************************/

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>

#include <algorithm>
#include <string>
#include <vector>

#include "../source/menu/menu_ui.h"
#include "../source/runtime/pause_menu_layout.h"
#include "../source/runtime/pause_menu_rows.h"
#include "../source/level/qvm_decompiler.h"
#include "../source/level/qvm_parser.h"

namespace {

using igi::MenuDefinition;
using igi::MenuField;
using igi::MenuUiBuildOptions;
using igi::MenuUiEntry;
using igi::MenuUiModel;
using igi::MenuUiRect;
using igi::MenuUiScreen;
using igi::MenuWidget;
using igi::MenuValue;
using igi::MenuValueKind;

// ── fixture helpers ─────────────────────────────────────────────────────────

MenuValue StringValue(const std::string& text) {
    MenuValue value;
    value.kind = MenuValueKind::String;
    value.text = text;
    return value;
}

MenuValue IntValue(long long number) {
    MenuValue value;
    value.kind = MenuValueKind::Integer;
    value.integer = number;
    return value;
}

MenuValue BoolValue(bool flag) {
    MenuValue value;
    value.kind = MenuValueKind::Boolean;
    value.text = flag ? "TRUE" : "FALSE";
    return value;
}

MenuField Field(const std::string& name, MenuValue value, const std::string& type = "Int32") {
    MenuField field;
    field.name = name;
    field.type = type;
    field.values.push_back(std::move(value));
    return field;
}

MenuWidget Widget(int id, const std::string& type, const std::string& name,
                  std::vector<MenuField> fields) {
    MenuWidget widget;
    widget.id = id;
    widget.type = type;
    widget.name = name;
    widget.fields = std::move(fields);
    return widget;
}

// MenuFrame parameter order: src/widget/menuframe.c:49-57.
MenuWidget Frame(int x, int y, int w, int h, int lineup, int spacing_x, int spacing_y,
                 bool selectable, const std::string& name = "", int id = -1) {
    return Widget(id, "MenuFrame", name,
                  {Field("Sprite Resource", StringValue(""), "String32"),
                   Field("X-Pos", IntValue(x)), Field("Y-Pos", IntValue(y)),
                   Field("Width", IntValue(w)), Field("Height", IntValue(h)),
                   Field("LineUpMode", IntValue(lineup)),
                   Field("X-Spacing", IntValue(spacing_x), "Int16"),
                   Field("Y-Spacing", IntValue(spacing_y), "Int16"),
                   Field("Is Selectable", BoolValue(selectable), "bool8")});
}

// MenuText parameter order: src/menuscreen/menutext.c:296-300.
MenuWidget Text(const std::string& name, const std::string& text,
                const std::string& action = "", int colour = 2, int id = -1) {
    return Widget(id, "MenuText", name,
                  {Field("Text Resource", StringValue(text), "String32"),
                   Field("Font Resource", StringValue("font3.fnt"), "String32"),
                   Field("ColourIndex", IntValue(colour)),
                   Field("On Click Script", StringValue(action), "VarString"),
                   Field("Click Sound Resource", StringValue(""), "String32")});
}

// MenuTextConditional parameter order: src/menuscreen/menutext.c:72-78.
MenuWidget ConditionalText(const std::string& name, const std::string& text,
                           const std::string& enabled_expr, const std::string& visible_expr,
                           const std::string& action, int colour = 2, int id = -1) {
    return Widget(id, "MenuTextConditional", name,
                  {Field("Text Resource", StringValue(text), "String32"),
                   Field("Font Resource", StringValue("font3.fnt"), "String32"),
                   Field("ColourIndex", IntValue(colour)),
                   Field("isEnabled Expression", StringValue(enabled_expr), "VarString"),
                   Field("isVisible Expression", StringValue(visible_expr), "VarString"),
                   Field("On Click Script", StringValue(action), "VarString"),
                   Field("Click Sound Resource", StringValue(""), "String32")});
}

// MenuScreen parameter order: Background, Frame x0..y1, On Press Escape,
// On Init Data, On Change Data.
MenuWidget Screen(const std::string& name, int x0, int y0, int x1, int y1,
                  const std::string& on_escape, int id = -1) {
    return Widget(id, "MenuScreen", name,
                  {Field("Background", StringValue(""), "String32"),
                   Field("Frame x0", IntValue(x0)), Field("Frame y0", IntValue(y0)),
                   Field("Frame x1", IntValue(x1)), Field("Frame y1", IntValue(y1)),
                   Field("On Press Escape", StringValue(on_escape), "VarString"),
                   Field("On Init Data", StringValue(""), "VarString"),
                   Field("On Change Data", StringValue(""), "VarString")});
}

// DialogWindow parameter order: X-Pos, Y-Pos, Width, Height, SwapKeys.
MenuWidget Dialog(const std::string& name, int x, int y, int w, int h, int id = -1) {
    return Widget(id, "DialogWindow", name,
                  {Field("X-Pos", IntValue(x)), Field("Y-Pos", IntValue(y)),
                   Field("Width", IntValue(w)), Field("Height", IntValue(h)),
                   Field("SwapKeys", BoolValue(true), "bool8")});
}

// A MenuDefinition shaped exactly like the retail corpus file.  The structure is
// the decoded form of `menusystem/ingamemenu.qvm`:
//
//   Task_New(-1, "MenuManager", "Menu manager for the ingame menu", 900,
//            "LOCAL:menusystem/ingamemenu.res", FALSE,
//     Task_New(900, "MenuScreen", "Main Menu", "", 85, 80, 555, 410,
//              "MenuManager_LeaveMenus(900, MENUMANAGER_IDLE)", "", "",
//       Task_New(-1, "MenuFrame", "", "", 100, 77, 440, 26, 0, 4, 4, FALSE,
//         Task_New(-1, "MenuText", "", "Game Paused", "font3.fnt", 1, "", "")),
//       Task_New(-1, "MenuFrame", "", "", 100, 97, 440, 200, 1, 4, 4, FALSE,
//         Task_New(-1, "MenuTextConditional", "resume", "Resume Game", "font3.fnt", 2,
//                  "!LevelFlow_LevelFailed()", "TRUE",
//                  "MenuManager_LeaveMenus(900, MENUMANAGER_IDLE)", ""),
//         Task_New(-1, "MenuText", "restart", "Restart Game", "font3.fnt", 2,
//                  "MenuManager_ActivatePopupScreen(932)\n", ""),
//         Task_New(-1, "MenuText", "sound", "Sound", "font3.fnt", 2,
//                  "MenuManager_PushScreen(902)\n", ""),
//         Task_New(-1, "MenuText", "controls", "Controls", "font3.fnt", 2,
//                  "MenuManager_PushScreen(903)\n", ""),
//         Task_New(-1, "MenuText", "quit", "Quit to Main Menu", "font3.fnt", 2,
//                  "MenuManager_ActivatePopupScreen(931)\n", ""))),
//     Task_New(901, "MenuScreen", "Graphics Configuration", ...),
//     Task_New(902, "MenuScreen", "Sound Configuration", ...),
//     Task_New(932, "DialogWindow", "restart mission", 220, 180, 200, 120, TRUE, ...))
MenuDefinition RetailShapedFixture() {
    MenuDefinition definition;
    definition.ok = true;
    definition.root = Widget(-1, "MenuManager", "Menu manager for the ingame menu",
                             {Field("Current menuscreen", IntValue(900)),
                              Field("Resource path",
                                    StringValue("LOCAL:menusystem/ingamemenu.res"), "String256"),
                              Field("isOwnDisplay", BoolValue(false), "bool8")});

    MenuWidget main = Screen("Main Menu", 85, 80, 555, 410,
                             "MenuManager_LeaveMenus(900, MENUMANAGER_IDLE)", 900);
    MenuWidget title = Frame(100, 77, 440, 26, 0, 4, 4, false);
    title.children.push_back(Text("", "Game Paused", "", 1));
    MenuWidget items = Frame(100, 97, 440, 200, 1, 4, 4, false);
    items.children.push_back(ConditionalText("resume", "Resume Game",
                                             "!LevelFlow_LevelFailed()", "TRUE",
                                             "MenuManager_LeaveMenus(900, MENUMANAGER_IDLE)"));
    items.children.push_back(Text("restart", "Restart Game",
                                  "MenuManager_ActivatePopupScreen(932)\n"));
    items.children.push_back(Text("sound", "Sound", "MenuManager_PushScreen(902)\n"));
    items.children.push_back(Text("controls", "Controls", "MenuManager_PushScreen(903)\n"));
    items.children.push_back(Text("quit", "Quit to Main Menu",
                                  "MenuManager_ActivatePopupScreen(931)\n"));
    main.children.push_back(title);
    main.children.push_back(items);
    definition.root.children.push_back(main);

    MenuWidget graphics = Screen("Graphics Configuration", 85, 80, 555, 410,
                                 "MenuManager_PopScreen(FALSE)", 901);
    MenuWidget ok_cancel = Frame(470, 360, 80, 80, 1, 1, 3, false);
    ok_cancel.children.push_back(Text("OK", "OK", "MenuManager_PopScreen(TRUE)\n"));
    ok_cancel.children.push_back(Text("cancel", "Cancel", "MenuManager_PopScreen(FALSE)\n"));
    graphics.children.push_back(ok_cancel);
    definition.root.children.push_back(graphics);

    MenuWidget sound = Screen("Sound Configuration", 85, 80, 555, 410,
                              "MenuManager_PopScreen(FALSE)", 902);
    definition.root.children.push_back(sound);

    MenuWidget restart_dialog = Dialog("restart mission", 220, 180, 200, 120, 932);
    MenuWidget dialog_items = Frame(200, 200, 240, 100, 5, 4, 4, false);
    dialog_items.children.push_back(Text("", "Yes", "MenuManager_LeaveMenus(900, MENUMANAGER_RESTARTLEVEL)"));
    dialog_items.children.push_back(Text("", "No", "MenuManager_DeactivatePopuScreen()\n"));
    restart_dialog.children.push_back(dialog_items);
    definition.root.children.push_back(restart_dialog);

    return definition;
}

MenuUiBuildOptions OptionsFor(const MenuDefinition&, int width = 640, int height = 480) {
    MenuUiBuildOptions options;
    options.viewport_width = width;
    options.viewport_height = height;
    return options;
}

const MenuUiEntry* EntryByLabel(const MenuUiModel& model, const std::string& label) {
    const igi::MenuUiScreen* screen = model.CurrentScreen();
    if (!screen) {
        return nullptr;
    }
    for (const MenuUiEntry& entry : screen->entries) {
        if (entry.label == label) {
            return &entry;
        }
    }
    return nullptr;
}

}  // namespace

// ── structure ───────────────────────────────────────────────────────────────

TEST(MenuUiStructure, ScreensComeFromTheDefinitionInDeclaredOrder) {
    const MenuDefinition definition = RetailShapedFixture();
    const MenuUiModel model = igi::BuildMenuUiModel(definition, OptionsFor(definition));

    ASSERT_TRUE(model.ok) << model.error;
    ASSERT_EQ(model.screens.size(), 4u);
    EXPECT_EQ(model.screens[0].id, 900);
    EXPECT_EQ(model.screens[0].name, "Main Menu");
    EXPECT_EQ(model.screens[1].id, 901);
    EXPECT_EQ(model.screens[2].id, 902);
    EXPECT_EQ(model.screens[3].id, 932);
    EXPECT_TRUE(model.screens[3].popup);
    EXPECT_FALSE(model.screens[0].popup);

    // The manager's "Current menuscreen" picks the initial screen.
    EXPECT_EQ(model.declared_screen_id, 900);
    EXPECT_EQ(model.screen_index, 0);
}

TEST(MenuUiStructure, ScreenFrameComesFromTheDeclaredFrameRect) {
    const MenuDefinition definition = RetailShapedFixture();
    const MenuUiModel model = igi::BuildMenuUiModel(definition, OptionsFor(definition));

    ASSERT_TRUE(model.ok);
    const igi::MenuUiScreen* main = model.CurrentScreen();
    ASSERT_NE(main, nullptr);
    EXPECT_EQ(main->frame.x, 85);
    EXPECT_EQ(main->frame.y, 80);
    EXPECT_EQ(main->frame.w, 555 - 85);
    EXPECT_EQ(main->frame.h, 410 - 80);
    EXPECT_EQ(main->on_back, "MenuManager_LeaveMenus(900, MENUMANAGER_IDLE)");
}

TEST(MenuUiStructure, ItemsAreTheSelectableWidgetsInRetailCollectionOrder) {
    const MenuDefinition definition = RetailShapedFixture();
    const MenuUiModel model = igi::BuildMenuUiModel(definition, OptionsFor(definition));

    ASSERT_TRUE(model.ok);
    const igi::MenuUiScreen* main = model.CurrentScreen();
    ASSERT_NE(main, nullptr);
    ASSERT_EQ(main->selectable.size(), 5u);

    const std::vector<std::string> expected = {"Resume Game", "Restart Game", "Sound",
                                               "Controls", "Quit to Main Menu"};
    for (size_t i = 0; i < expected.size(); ++i) {
        const MenuUiEntry& entry = main->entries[static_cast<size_t>(main->selectable[i])];
        EXPECT_EQ(entry.label, expected[i]) << "item " << i;
        EXPECT_TRUE(entry.collected) << "item " << i;
        EXPECT_TRUE(entry.selectable) << "item " << i;
    }

    // The non-selectable title text is drawn but is not an item.
    EXPECT_EQ(main->entries.size(), 8u);
    const MenuUiEntry* paused = EntryByLabel(model, "Game Paused");
    ASSERT_NE(paused, nullptr);
    EXPECT_FALSE(paused->collected);
    EXPECT_FALSE(paused->selectable);
}

TEST(MenuUiStructure, RetailActionTextAndSoundAreCarriedVerbatim) {
    const MenuDefinition definition = RetailShapedFixture();
    const MenuUiModel model = igi::BuildMenuUiModel(definition, OptionsFor(definition));

    ASSERT_TRUE(model.ok);
    const MenuUiEntry* restart = EntryByLabel(model, "Restart Game");
    ASSERT_NE(restart, nullptr);
    EXPECT_EQ(restart->action, "MenuManager_ActivatePopupScreen(932)\n");
    EXPECT_EQ(restart->font, "font3.fnt");
    EXPECT_EQ(restart->colour_index, 2);
    EXPECT_EQ(restart->widget_name, "restart");
}

TEST(MenuUiStructure, SelectableFramePrunesItsDescendantsFromTheItemList) {
    // `MenuManager_AddItems` HOOK 0x418840 returns at a selectable MenuWindow, so
    // a selectable frame is the item and the text inside it never is.
    MenuDefinition definition;
    definition.ok = true;
    definition.root = Widget(-1, "MenuManager", "mgr", {Field("Current menuscreen", IntValue(900))});
    MenuWidget screen = Screen("Only", 0, 0, 640, 480, "", 900);
    MenuWidget frame = Frame(100, 100, 200, 25, 0, 4, 4, true, "Change Device");
    frame.children.push_back(Text("", "Render Device", ""));
    screen.children.push_back(frame);
    definition.root.children.push_back(screen);

    const MenuUiModel model = igi::BuildMenuUiModel(definition, OptionsFor(definition));
    ASSERT_TRUE(model.ok) << model.error;
    const igi::MenuUiScreen* only = model.CurrentScreen();
    ASSERT_NE(only, nullptr);
    ASSERT_EQ(only->selectable.size(), 1u);
    EXPECT_EQ(only->entries[static_cast<size_t>(only->selectable[0])].widget_name,
              "Change Device");
    // The frame has no text of its own, so its item label is the child's text.
    EXPECT_EQ(only->entries[static_cast<size_t>(only->selectable[0])].item_label,
              "Render Device");
    // The child is still drawn.
    EXPECT_EQ(only->entries.size(), 2u);
    EXPECT_FALSE(only->entries[1].collected);
}

TEST(MenuUiStructure, ConditionalItemIsSelectableAndFlaggedAsUnverified) {
    // MenuTextConditional_Changed HOOK 0x420640 sets selectable from the evaluated
    // "isEnabled Expression".  The editor cannot run retail script, so the item is
    // reported as selectable with the condition flagged rather than guessed.
    const MenuDefinition definition = RetailShapedFixture();
    const MenuUiModel model = igi::BuildMenuUiModel(definition, OptionsFor(definition));

    ASSERT_TRUE(model.ok);
    const MenuUiEntry* resume = EntryByLabel(model, "Resume Game");
    ASSERT_NE(resume, nullptr);
    EXPECT_TRUE(resume->selectable);
    EXPECT_TRUE(resume->collected);
    EXPECT_TRUE(resume->condition_unverified);
    EXPECT_EQ(resume->enable_expression, "!LevelFlow_LevelFailed()");
    EXPECT_EQ(resume->visible_expression, "TRUE");
    EXPECT_TRUE(resume->visible);
    EXPECT_TRUE(resume->enabled);
}

TEST(MenuUiStructure, EmptyDefinitionIsNotOkAndEveryQueryIsSafe) {
    MenuDefinition definition;  // ok == false
    MenuUiModel model = igi::BuildMenuUiModel(definition, MenuUiBuildOptions{});

    EXPECT_FALSE(model.ok);
    EXPECT_FALSE(model.error.empty());
    EXPECT_EQ(model.CurrentScreen(), nullptr);
    EXPECT_EQ(model.FocusedEntry(), nullptr);
    EXPECT_EQ(model.FocusedEntryIndex(), -1);
    EXPECT_EQ(model.SelectableCount(), 0);
    EXPECT_EQ(model.HitTest(10, 10), -1);
    EXPECT_EQ(model.Back(), nullptr);
    EXPECT_FALSE(model.MoveDown());
    EXPECT_FALSE(model.MoveUp());
    EXPECT_FALSE(model.OpenScreen(900));
    EXPECT_FALSE(model.PushScreen(900));
    EXPECT_FALSE(model.PopScreen());
    const igi::MenuUiActivation activation = model.Activate();
    EXPECT_FALSE(activation.hit);
    EXPECT_EQ(activation.entry_index, -1);
}

TEST(MenuUiStructure, DefinitionWithoutScreensIsNotOk) {
    MenuDefinition definition;
    definition.ok = true;
    definition.root = Widget(-1, "MenuManager", "mgr", {Field("Current menuscreen", IntValue(900))});
    const MenuUiModel model = igi::BuildMenuUiModel(definition, MenuUiBuildOptions{});
    EXPECT_FALSE(model.ok);
    EXPECT_NE(model.error.find("no MenuScreen"), std::string::npos);
}

// ── geometry ────────────────────────────────────────────────────────────────

TEST(MenuUiGeometry, LineUpTopPlacesItemsBySpacingAndCentresText) {
    // MenuWindow_Arrange HOOK 0x4244B0, LINEUPMODE_VERT_TOP:
    //   l.spacingY = w->spacingY; l.y = w->y + l.spacingY;
    //   place: y = l.y; l.y += h + l.spacingY;
    // MenuWindow_Place HOOK 0x424420 centres a centred child:
    //   x = ((parent->w - child->w) >> 1) + parent->x
    const MenuDefinition definition = RetailShapedFixture();
    const MenuUiModel model = igi::BuildMenuUiModel(definition, OptionsFor(definition));
    ASSERT_TRUE(model.ok);

    const igi::MenuUiScreen* main = model.CurrentScreen();
    ASSERT_NE(main, nullptr);

    // Text height is the metrics line height (15 by default); spacing is 4.
    const int expected_y[] = {101, 120, 139, 158, 177};
    const char* labels[] = {"Resume Game", "Restart Game", "Sound", "Controls",
                            "Quit to Main Menu"};
    for (size_t i = 0; i < main->selectable.size(); ++i) {
        const MenuUiEntry& entry = main->entries[static_cast<size_t>(main->selectable[i])];
        EXPECT_EQ(entry.label, labels[i]);
        EXPECT_EQ(entry.rect.y, expected_y[i]) << entry.label;
        EXPECT_EQ(entry.rect.h, 15) << entry.label;
        // 7 px per character with the default metrics.
        const int text_w = static_cast<int>(entry.label.size()) * 7;
        EXPECT_EQ(entry.rect.w, text_w) << entry.label;
        EXPECT_EQ(entry.rect.x, ((440 - text_w) >> 1) + 100) << entry.label;
    }

    // The title frame keeps its declared rect: its parent (the screen) has no
    // LineUpMode, and MenuFrame is not a centred type.
    const MenuUiEntry* title = EntryByLabel(model, "Game Paused");
    ASSERT_NE(title, nullptr);
    EXPECT_EQ(title->widget_type, "MenuText");
    EXPECT_EQ(title->rect.w, 11 * 7);                       // "Game Paused", 7 px/char
    EXPECT_EQ(title->rect.x, ((440 - 11 * 7) >> 1) + 100);  // centred in the title frame
    EXPECT_EQ(title->rect.y, ((26 - 15) >> 1) + 77);        // centred in the title frame
}

TEST(MenuUiGeometry, ViewportMappingAppliesTheRetailScreenOffset) {
    // SCREEN_X(x) = (viewport_w >> 1) - 320 + x, src/menuscreen/menuscreen.h:141.
    const MenuDefinition definition = RetailShapedFixture();
    const MenuUiBuildOptions options = OptionsFor(definition, 1280, 960);
    const MenuUiModel model = igi::BuildMenuUiModel(definition, options);
    ASSERT_TRUE(model.ok);

    const MenuUiEntry* resume = EntryByLabel(model, "Resume Game");
    ASSERT_NE(resume, nullptr);
    const MenuUiRect view = model.ToViewport(resume->rect);
    EXPECT_EQ(view.x, resume->rect.x + 320);
    EXPECT_EQ(view.y, resume->rect.y + 240);
    EXPECT_EQ(view.w, resume->rect.w);
}

TEST(MenuUiGeometry, TextMeasurementIsSuppliedByTheCaller) {
    MenuDefinition definition;
    definition.ok = true;
    definition.root = Widget(-1, "MenuManager", "mgr", {Field("Current menuscreen", IntValue(1))});
    MenuWidget screen = Screen("Only", 0, 0, 640, 480, "", 900);
    MenuWidget frame = Frame(100, 100, 200, 200, 1, 0, 4, false);
    frame.children.push_back(Text("", "ABCDE", "action"));
    screen.children.push_back(frame);
    definition.root.children.push_back(screen);

    MenuUiBuildOptions options;
    options.metrics.line_height = 20;
    options.metrics.text_width = [](const std::string& text) {
        return static_cast<int>(text.size()) * 10;
    };
    const MenuUiModel model = igi::BuildMenuUiModel(definition, options);
    ASSERT_TRUE(model.ok) << model.error;

    const MenuUiEntry* item = EntryByLabel(model, "ABCDE");
    ASSERT_NE(item, nullptr);
    EXPECT_EQ(item->rect.w, 50);
    EXPECT_EQ(item->rect.h, 20);
    EXPECT_EQ(item->rect.y, 104);  // parent y 100 + spacing 4
}

// ── navigation ──────────────────────────────────────────────────────────────

TEST(MenuUiNavigation, DownWrapsToTheFirstItemAndUpWrapsToTheLast) {
    // MenuManager_ReadInput HOOK 0x418580: KEY_DOWN (-4) does
    // `if (++i >= itemCount) i = 0;`, KEY_UP (-3) does `if (--i < 0) i = itemCount - 1;`.
    const MenuDefinition definition = RetailShapedFixture();
    MenuUiModel model = igi::BuildMenuUiModel(definition, OptionsFor(definition));
    ASSERT_TRUE(model.ok);

    EXPECT_EQ(model.item_index, 0);
    ASSERT_NE(model.FocusedEntry(), nullptr);
    EXPECT_EQ(model.FocusedEntry()->label, "Resume Game");

    EXPECT_TRUE(model.MoveDown());
    EXPECT_EQ(model.FocusedEntry()->label, "Restart Game");
    EXPECT_TRUE(model.MoveDown());
    EXPECT_TRUE(model.MoveDown());
    EXPECT_TRUE(model.MoveDown());
    EXPECT_EQ(model.FocusedEntry()->label, "Quit to Main Menu");

    EXPECT_TRUE(model.MoveDown());  // wraps
    EXPECT_EQ(model.item_index, 0);
    EXPECT_EQ(model.FocusedEntry()->label, "Resume Game");

    EXPECT_TRUE(model.MoveUp());  // wraps backwards
    EXPECT_EQ(model.item_index, 4);
    EXPECT_EQ(model.FocusedEntry()->label, "Quit to Main Menu");
}

TEST(MenuUiNavigation, ActivateReturnsTheFocusedItemAndItsRetailAction) {
    // RUN_ACTION (src/menuscreen/menutext.c) runs the focused item's
    // "On Click Script" on KEY_ENTER (-7); the model reports it and does not run it.
    const MenuDefinition definition = RetailShapedFixture();
    MenuUiModel model = igi::BuildMenuUiModel(definition, OptionsFor(definition));
    ASSERT_TRUE(model.ok);

    const igi::MenuUiActivation first = model.Activate();
    EXPECT_TRUE(first.hit);
    EXPECT_EQ(first.action, "MenuManager_LeaveMenus(900, MENUMANAGER_IDLE)");
    EXPECT_EQ(first.entry_index, model.FocusedEntryIndex());

    ASSERT_TRUE(model.MoveDown());
    const igi::MenuUiActivation second = model.Activate();
    EXPECT_TRUE(second.hit);
    EXPECT_EQ(second.action, "MenuManager_ActivatePopupScreen(932)\n");
}

TEST(MenuUiNavigation, BackReportsTheScreenOnPressEscapeText) {
    const MenuDefinition definition = RetailShapedFixture();
    MenuUiModel model = igi::BuildMenuUiModel(definition, OptionsFor(definition));
    ASSERT_TRUE(model.ok);

    const std::string* back = model.Back();
    ASSERT_NE(back, nullptr);
    EXPECT_EQ(*back, "MenuManager_LeaveMenus(900, MENUMANAGER_IDLE)");
}

TEST(MenuUiNavigation, ScreenWithNoSelectableItemsCannotMoveOrActivate) {
    MenuDefinition definition;
    definition.ok = true;
    definition.root = Widget(-1, "MenuManager", "mgr", {Field("Current menuscreen", IntValue(900))});
    MenuWidget screen = Screen("Empty", 0, 0, 640, 480, "", 900);
    MenuWidget frame = Frame(100, 100, 200, 100, 1, 0, 4, false);
    frame.children.push_back(Text("", "Are you sure?", ""));  // no action -> not selectable
    screen.children.push_back(frame);
    definition.root.children.push_back(screen);

    MenuUiModel model = igi::BuildMenuUiModel(definition, OptionsFor(definition));
    ASSERT_TRUE(model.ok) << model.error;
    ASSERT_NE(model.CurrentScreen(), nullptr);
    EXPECT_TRUE(model.CurrentScreen()->selectable.empty());
    EXPECT_EQ(model.item_index, -1);
    EXPECT_EQ(model.FocusedEntry(), nullptr);
    EXPECT_EQ(model.FocusedEntryIndex(), -1);
    EXPECT_FALSE(model.MoveDown());
    EXPECT_FALSE(model.MoveUp());
    EXPECT_EQ(model.item_index, -1);
    const igi::MenuUiActivation activation = model.Activate();
    EXPECT_FALSE(activation.hit);
    // The screen is still drawable.
    EXPECT_EQ(model.CurrentScreen()->entries.size(), 2u);
}

TEST(MenuUiNavigation, DisabledItemLeavesTheItemListAndRejoinsWhenReEnabled) {
    // MenuTextConditional_Changed HOOK 0x420640 sets `w.selectable = enabled`, so a
    // disabled item is not in the retail item array at all.
    const MenuDefinition definition = RetailShapedFixture();
    MenuUiModel model = igi::BuildMenuUiModel(definition, OptionsFor(definition));
    ASSERT_TRUE(model.ok);

    const igi::MenuUiScreen* main = model.CurrentScreen();
    ASSERT_NE(main, nullptr);
    const int sound_index = main->selectable[2];
    ASSERT_EQ(main->entries[static_cast<size_t>(sound_index)].label, "Sound");

    model.SetEntryEnabled(sound_index, false);
    ASSERT_NE(model.CurrentScreen(), nullptr);
    EXPECT_EQ(model.SelectableCount(), 4);
    EXPECT_FALSE(model.CurrentScreen()->entries[static_cast<size_t>(sound_index)].collected);
    // It is still drawn.
    EXPECT_TRUE(model.CurrentScreen()->entries[static_cast<size_t>(sound_index)].visible);

    // Navigation now steps over it.
    model.item_index = 0;
    EXPECT_TRUE(model.MoveDown());
    EXPECT_EQ(model.FocusedEntry()->label, "Restart Game");
    EXPECT_TRUE(model.MoveDown());
    EXPECT_EQ(model.FocusedEntry()->label, "Controls");

    model.SetEntryEnabled(sound_index, true);
    EXPECT_EQ(model.SelectableCount(), 5);
    EXPECT_TRUE(model.CurrentScreen()->entries[static_cast<size_t>(sound_index)].collected);
}

TEST(MenuUiNavigation, FocusByEntryIndexOnlyAcceptsItems) {
    const MenuDefinition definition = RetailShapedFixture();
    MenuUiModel model = igi::BuildMenuUiModel(definition, OptionsFor(definition));
    ASSERT_TRUE(model.ok);

    const igi::MenuUiScreen* main = model.CurrentScreen();
    ASSERT_NE(main, nullptr);
    const int title_index = 0;              // the title frame is not an item
    const int first_item = main->selectable[0];

    EXPECT_FALSE(model.FocusEntry(title_index));
    EXPECT_TRUE(model.FocusEntry(main->selectable[3]));
    EXPECT_EQ(model.FocusedEntry()->label, "Controls");
    EXPECT_FALSE(model.FocusEntry(main->selectable[3]));  // already focused
    EXPECT_TRUE(model.FocusEntry(first_item));
    EXPECT_EQ(model.FocusedEntry()->label, "Resume Game");
    EXPECT_FALSE(model.FocusEntry(9999));
    EXPECT_FALSE(model.FocusEntry(-1));
}

TEST(MenuUiNavigation, MouseHitTestUsesTheRetailRectangleTest) {
    // MenuWindow_HitTest HOOK 0x4248A0: inside = rx < x && ry < y &&
    // x < rx + w && y < ry + h — the top-left edge is exclusive.
    //
    // The screen below has no containing frame, so a hit can only be the text
    // window itself and the strict edges are observable.
    MenuDefinition definition;
    definition.ok = true;
    definition.root = Widget(-1, "MenuManager", "mgr", {Field("Current menuscreen", IntValue(900))});
    MenuWidget screen = Screen("Hit", 0, 0, 640, 480, "", 900);
    screen.children.push_back(Text("only", "Hit Me", "some_action()"));
    definition.root.children.push_back(screen);

    MenuUiModel model = igi::BuildMenuUiModel(definition, OptionsFor(definition));
    ASSERT_TRUE(model.ok) << model.error;
    const MenuUiEntry* item = EntryByLabel(model, "Hit Me");
    ASSERT_NE(item, nullptr);
    ASSERT_TRUE(item->collected);
    const MenuUiRect rect = item->rect;
    const int centre_x = rect.x + rect.w / 2;
    const int centre_y = rect.y + rect.h / 2;

    EXPECT_EQ(model.HitTest(centre_x, centre_y), 0);
    EXPECT_EQ(model.HitTest(rect.x, centre_y), -1);           // left edge is exclusive
    EXPECT_EQ(model.HitTest(rect.x + rect.w, centre_y), -1);  // right edge is exclusive
    EXPECT_EQ(model.HitTest(centre_x, rect.y), -1);           // top edge is exclusive
    EXPECT_EQ(model.HitTest(centre_x, rect.y + rect.h), -1);  // bottom edge is exclusive
    EXPECT_EQ(model.HitTest(0, 0), -1);                       // outside everything
    EXPECT_EQ(model.HitTest(-5, centre_y), -1);

    // The model starts focused on the first item; a hover reports a change only
    // when the focus actually moves (retail fires on the hover transition).
    EXPECT_EQ(model.item_index, 0);
    EXPECT_FALSE(model.FocusEntry(0));
    model.item_index = -1;
    EXPECT_TRUE(model.FocusEntry(model.HitTest(centre_x, centre_y)));
    EXPECT_EQ(model.FocusedEntry()->label, "Hit Me");
}

TEST(MenuUiNavigation, MouseHitInsideASelectableFramePrefersTheFrameOverItsLabel) {
    // The 5 retail items of screen 900 sit inside a non-selectable MenuFrame, and
    // a click anywhere in that frame's rectangle reports the frame (a non-item
    // entry) rather than a label, unless the label's own rectangle is hit.  That is
    // the retail shape: MenuFrame_Run focuses the frame, the text window inside it
    // only acts when it is itself hit.
    const MenuDefinition definition = RetailShapedFixture();
    MenuUiModel model = igi::BuildMenuUiModel(definition, OptionsFor(definition));
    ASSERT_TRUE(model.ok);

    const MenuUiEntry* resume = EntryByLabel(model, "Resume Game");
    ASSERT_NE(resume, nullptr);
    const MenuUiRect rect = resume->rect;
    const int centre_y = rect.y + rect.h / 2;

    const int centre_hit = model.HitTest(rect.x + rect.w / 2, centre_y);
    ASSERT_GE(centre_hit, 0);
    EXPECT_EQ(model.CurrentScreen()->entries[static_cast<size_t>(centre_hit)].label,
              "Resume Game");
    EXPECT_TRUE(model.CurrentScreen()->entries[static_cast<size_t>(centre_hit)].collected);

    // One pixel left of the label is still inside the surrounding MenuFrame.
    const int frame_hit = model.HitTest(rect.x - 1, centre_y);
    ASSERT_GE(frame_hit, 0);
    EXPECT_EQ(model.CurrentScreen()->entries[static_cast<size_t>(frame_hit)].widget_type,
              "MenuFrame");
    EXPECT_FALSE(model.CurrentScreen()->entries[static_cast<size_t>(frame_hit)].collected);
    // ... and it cannot take focus.
    EXPECT_FALSE(model.FocusEntry(frame_hit));
}

// ── screens ─────────────────────────────────────────────────────────────────

TEST(MenuUiScreens, OpenScreenSelectsTheDeclaredScreenAndResetsTheItemIndex) {
    // MenuManager_OpenScreen HOOK 0x418710 -> CollectItems resets itemIndex to 0.
    const MenuDefinition definition = RetailShapedFixture();
    MenuUiModel model = igi::BuildMenuUiModel(definition, OptionsFor(definition));
    ASSERT_TRUE(model.ok);

    ASSERT_TRUE(model.OpenScreen(901));
    EXPECT_EQ(model.CurrentScreen()->name, "Graphics Configuration");
    EXPECT_EQ(model.item_index, 0);
    EXPECT_EQ(model.FocusedEntry()->label, "OK");

    EXPECT_FALSE(model.OpenScreen(9999));
    EXPECT_EQ(model.CurrentScreen()->name, "Graphics Configuration");

    ASSERT_TRUE(model.OpenScreen(902));
    EXPECT_EQ(model.CurrentScreen()->name, "Sound Configuration");
    EXPECT_EQ(model.item_index, -1);  // no selectable items on that screen
}

TEST(MenuUiScreens, PushAndPopFollowTheRetailScreenStack) {
    // `screenStack[16]`, src/menu/menutypes.h:76.
    const MenuDefinition definition = RetailShapedFixture();
    MenuUiModel model = igi::BuildMenuUiModel(definition, OptionsFor(definition));
    ASSERT_TRUE(model.ok);

    ASSERT_TRUE(model.OpenScreen(902));
    ASSERT_TRUE(model.PushScreen(932));
    EXPECT_EQ(model.CurrentScreen()->id, 932);
    EXPECT_TRUE(model.CurrentScreen()->popup);
    EXPECT_TRUE(model.ScreenOnStack(902));
    EXPECT_FALSE(model.ScreenOnStack(932));

    EXPECT_TRUE(model.PopScreen());
    EXPECT_EQ(model.CurrentScreen()->id, 902);
    EXPECT_TRUE(model.screen_stack.empty());

    EXPECT_FALSE(model.PopScreen());
    EXPECT_FALSE(model.PushScreen(9999));
}

TEST(MenuUiScreens, ItemCapMatchesTheRetailItemArrayAndWarns) {
    // `items[32]`, src/menu/menutypes.h:41.  The retail collection code has no
    // bounds check; the model stops at the cap and reports it.
    MenuDefinition definition;
    definition.ok = true;
    definition.root = Widget(-1, "MenuManager", "mgr", {Field("Current menuscreen", IntValue(900))});
    MenuWidget screen = Screen("Many", 0, 0, 640, 480, "", 900);
    MenuWidget frame = Frame(0, 0, 640, 480, 1, 0, 1, false);
    for (int i = 0; i < 40; ++i) {
        frame.children.push_back(Text("item" + std::to_string(i), "Row " + std::to_string(i),
                                      "some_action()"));
    }
    screen.children.push_back(frame);
    definition.root.children.push_back(screen);

    const MenuUiModel model = igi::BuildMenuUiModel(definition, OptionsFor(definition));
    ASSERT_TRUE(model.ok) << model.error;
    EXPECT_EQ(model.SelectableCount(), 32);
    EXPECT_EQ(model.CurrentScreen()->entries.size(), 41u);
    ASSERT_FALSE(model.warnings.empty());
    EXPECT_NE(model.warnings.front().find("32"), std::string::npos);
    // The rows past the cap are drawn but not selectable.
    EXPECT_FALSE(model.CurrentScreen()->entries[33].collected);
}

TEST(MenuUiScreens, UnknownDeclaredScreenIdFallsBackToTheFirstScreenWithAWarning) {
    MenuDefinition definition = RetailShapedFixture();
    definition.root.fields[0] = Field("Current menuscreen", IntValue(4242));
    const MenuUiModel model = igi::BuildMenuUiModel(definition, OptionsFor(definition));

    ASSERT_TRUE(model.ok) << model.error;
    EXPECT_EQ(model.declared_screen_id, 4242);
    EXPECT_EQ(model.screen_index, 0);
    bool warned = false;
    for (const std::string& warning : model.warnings) {
        if (warning.find("4242") != std::string::npos) {
            warned = true;
        }
    }
    EXPECT_TRUE(warned);
}

// ── application wiring (source/app_menu.cpp) ────────────────────────────────

namespace {

// The retail corpus, as tests/test_door_params.cpp locates it.
std::string FindRetailRoot() {
    const char* candidates[] = {std::getenv("IGI_GAME_PATH"),
                                "/Users/haseeb-mir/Downloads/IGI", nullptr};
    for (int i = 0; candidates[i] != nullptr; ++i) {
        const std::string root = candidates[i];
        if (root.empty()) {
            continue;
        }
        std::ifstream probe(root + "/menusystem/ingamemenu.qvm", std::ios::binary);
        if (probe.good()) {
            return root;
        }
    }
    return {};
}

}  // namespace

TEST(MenuUiFallback, RowCountMatchesTheExistingPauseMenuRows) {
    // The fallback must not change what the pause menu shows:
    // igi::PauseMenuRowCount (source/runtime/pause_menu_layout.h:26-30) is 16
    // collapsed and 21 with the terrain options expanded, and renderer_draw.cpp
    // builds exactly that many rows.
    EXPECT_EQ(igi::app_menu::EditorFallbackRowCount(false), 16);
    EXPECT_EQ(igi::app_menu::EditorFallbackRowCount(true), 21);
    EXPECT_EQ(igi::app_menu::EditorFallbackRowCount(false),
              igi::PauseMenuRowCount(false));
    EXPECT_EQ(igi::app_menu::EditorFallbackRowCount(true), igi::PauseMenuRowCount(true));

    const MenuUiModel collapsed =
        igi::app_menu::BuildEditorFallbackMenu(false, MenuUiBuildOptions{});
    ASSERT_TRUE(collapsed.ok) << collapsed.error;
    ASSERT_NE(collapsed.CurrentScreen(), nullptr);
    EXPECT_EQ(collapsed.CurrentScreen()->entries.size(), 16u);
    EXPECT_EQ(collapsed.SelectableCount(), 16);
    EXPECT_TRUE(collapsed.fallback);
    EXPECT_EQ(collapsed.source, "fallback:editor-rows");

    const MenuUiModel expanded =
        igi::app_menu::BuildEditorFallbackMenu(true, MenuUiBuildOptions{});
    ASSERT_TRUE(expanded.ok);
    EXPECT_EQ(expanded.CurrentScreen()->entries.size(), 21u);
}

TEST(MenuUiFallback, RowOrderAndActionsMirrorTheLegacyRowList) {
    const MenuUiModel model =
        igi::app_menu::BuildEditorFallbackMenu(false, MenuUiBuildOptions{});
    ASSERT_TRUE(model.ok);
    const igi::MenuUiScreen* screen = model.CurrentScreen();
    ASSERT_NE(screen, nullptr);

    const char* expected_labels[] = {
        "Resume", "Mode", "Collision", "Font", "Select Level", "Auto Save", "Logging",
        "Log Level", "Music", "Weather", "Lightmap", "Calculate Lightmaps",
        "Terrain Options", "Reset Level", "Save Level", "Quit"};
    const igi::MenuUiAction expected_actions[] = {
        igi::MenuUiAction::Resume, igi::MenuUiAction::ToggleMode,
        igi::MenuUiAction::ToggleCollision, igi::MenuUiAction::ToggleFont,
        igi::MenuUiAction::SelectLevel, igi::MenuUiAction::ToggleAutoSave,
        igi::MenuUiAction::ToggleLogging, igi::MenuUiAction::CycleLogLevel,
        igi::MenuUiAction::ToggleMusic, igi::MenuUiAction::CycleWeather,
        igi::MenuUiAction::ToggleLightmaps, igi::MenuUiAction::CalculateLightmaps,
        igi::MenuUiAction::ToggleTerrainOptions, igi::MenuUiAction::ResetLevel,
        igi::MenuUiAction::SaveLevel, igi::MenuUiAction::Quit};
    ASSERT_EQ(screen->entries.size(), 16u);
    for (size_t i = 0; i < screen->entries.size(); ++i) {
        EXPECT_EQ(screen->entries[i].label, expected_labels[i]) << "row " << i;
        EXPECT_EQ(screen->entries[i].editor_action, expected_actions[i]) << "row " << i;
        EXPECT_TRUE(screen->entries[i].collected) << "row " << i;
        EXPECT_FALSE(screen->entries[i].inert) << "row " << i;
    }
    // The terrain rows only exist when expanded, and they sit after the header.
    const MenuUiModel expanded =
        igi::app_menu::BuildEditorFallbackMenu(true, MenuUiBuildOptions{});
    ASSERT_TRUE(expanded.ok);
    const igi::MenuUiScreen* exp = expanded.CurrentScreen();
    ASSERT_NE(exp, nullptr);
    ASSERT_EQ(exp->entries.size(), 21u);
    EXPECT_EQ(exp->entries[12].label, "Terrain Options");
    EXPECT_EQ(exp->entries[13].label, "Texture");
    EXPECT_EQ(exp->entries[14].label, "Height");
    EXPECT_EQ(exp->entries[15].label, "Discard");
    EXPECT_EQ(exp->entries[16].label, "Fog");
    EXPECT_EQ(exp->entries[17].label, "Fog Intensity");
    EXPECT_EQ(exp->entries[18].label, "Reset Level");
}

TEST(MenuUiFallback, HitBoxesReproduceTheLegacyRowBands) {
    // app_input_mouse.cpp tests each row with `igi::IsPauseMenuRowHit(layout, row,
    // mouse_y)`, i.e. centre - radius <= y <= centre + radius, and the renderer
    // draws the row at `PauseMenuRowCenter`.  Going through the model must give the
    // same bands and the same panel.
    const int viewport_w = 1280;
    const int viewport_h = 960;
    MenuUiBuildOptions options;
    options.viewport_width = viewport_w;
    options.viewport_height = viewport_h;
    const MenuUiModel model = igi::app_menu::BuildEditorFallbackMenu(false, options);
    ASSERT_TRUE(model.ok);

    const igi::PauseMenuLayout layout =
        igi::BuildPauseMenuLayout(viewport_w, viewport_h, false, options.metrics.line_height);
    const igi::MenuUiScreen* screen = model.CurrentScreen();
    ASSERT_NE(screen, nullptr);
    ASSERT_EQ(screen->entries.size(), 16u);

    // The panel rect round-trips to the legacy layout rect.
    const MenuUiRect panel = model.ToViewport(screen->frame);
    EXPECT_EQ(panel.x, layout.menu_left);
    EXPECT_EQ(panel.y, layout.menu_top);
    EXPECT_EQ(panel.w, layout.menu_width);
    EXPECT_EQ(panel.h, layout.menu_height);

    for (int row = 0; row < 16; ++row) {
        const int centre = igi::PauseMenuRowCenter(layout, row);
        const int radius = layout.row_hit_radius;
        // Every y the legacy code accepts is hit by exactly this row.
        for (int y = centre - radius; y <= centre + radius; ++y) {
            EXPECT_TRUE(igi::IsPauseMenuRowHit(layout, row, y));
            EXPECT_EQ(model.HitTest(layout.menu_left + 5, y), row) << "row " << row << " y " << y;
        }
        // One pixel outside the band is not this row.
        EXPECT_EQ(model.HitTest(layout.menu_left + 5, centre - radius - 1), -1)
            << "row " << row;
        EXPECT_EQ(model.HitTest(layout.menu_left + 5, centre + radius + 1), -1)
            << "row " << row;
    }
}

TEST(MenuUiFallback, MissingRetailFileFallsBackWithAReason) {
    const igi::app_menu::EditorMenu menu = igi::app_menu::LoadEditorMenu(
        "/nonexistent/game/root", false, MenuUiBuildOptions{});
    EXPECT_FALSE(menu.retail);
    EXPECT_TRUE(menu.fallback);
    EXPECT_FALSE(menu.reason.empty());
    EXPECT_NE(menu.reason.find("ingamemenu.qvm"), std::string::npos);
    ASSERT_TRUE(menu.model.ok);
    EXPECT_TRUE(menu.model.fallback);
    EXPECT_EQ(menu.model.fallback_reason, menu.reason);
    EXPECT_EQ(menu.model.SelectableCount(), 16);

    // An empty root is the same path, not a crash.
    const igi::app_menu::EditorMenu empty =
        igi::app_menu::LoadEditorMenu("", false, MenuUiBuildOptions{});
    EXPECT_TRUE(empty.fallback);
    EXPECT_TRUE(empty.model.ok);
}

TEST(MenuUiRetail, RealIngameMenuProducesTheDeclaredScreensAndItems) {
    // Differential test against the retail corpus: the model has to reproduce what
    // `menusystem/ingamemenu.qvm` declares.  The expectations below are the
    // definition's own values, read out of the retail container.
    const std::string root = FindRetailRoot();
    if (root.empty()) {
        GTEST_SKIP() << "retail corpus not reachable (set IGI_GAME_PATH)";
    }

    const igi::app_menu::EditorMenu menu =
        igi::app_menu::LoadEditorMenu(root, false, MenuUiBuildOptions{});
    ASSERT_TRUE(menu.retail) << "fell back: " << menu.reason;
    ASSERT_FALSE(menu.fallback);
    ASSERT_TRUE(menu.model.ok) << menu.model.error;
    EXPECT_FALSE(menu.reader.empty());
    EXPECT_EQ(menu.model.declared_screen_id, 900);
    ASSERT_EQ(menu.model.screens.size(), 9u);

    // Screens, in declaration order, with the retail ids and frame rects.
    const int expected_ids[] = {900, 901, 902, 903, 930, 931, 932, 933,
                                igi::app_menu::kEditorOptionsScreenId};
    const char* expected_names[] = {"Main Menu", "Graphics Configuration",
                                    "Sound Configuration", "Controls Configuration",
                                    "Reset Controls", "quit to main", "restart mission", "",
                                    igi::app_menu::kEditorOptionsLabel};
    for (size_t i = 0; i < menu.model.screens.size(); ++i) {
        EXPECT_EQ(menu.model.screens[i].id, expected_ids[i]) << "screen " << i;
        EXPECT_EQ(menu.model.screens[i].name, expected_names[i]) << "screen " << i;
    }
    EXPECT_FALSE(menu.model.screens[0].popup);
    EXPECT_TRUE(menu.model.screens[4].popup);
    EXPECT_EQ(menu.model.screens[4].type, "DialogWindow");

    // Screen 900: the in-game menu.  Frame 85,80..555,410 and 5 items.
    const igi::MenuUiScreen* main = menu.model.CurrentScreen();
    ASSERT_NE(main, nullptr);
    EXPECT_EQ(main->id, 900);
    EXPECT_EQ(main->name, "Main Menu");
    EXPECT_EQ(main->frame.x, 85);
    EXPECT_EQ(main->frame.y, 80);
    EXPECT_EQ(main->frame.w, 470);
    EXPECT_EQ(main->frame.h, 330);
    EXPECT_EQ(main->on_back, "MenuManager_LeaveMenus(900, MENUMANAGER_IDLE)");
    ASSERT_EQ(main->selectable.size(), 6u);  // 5 retail items + the editor entry

    const char* expected_items[] = {"Resume Game", "Restart Game", "Sound", "Controls",
                                    "Quit to Main Menu"};
    const char* expected_widget_names[] = {"resume", "restart", "sound", "controls", "quit"};
    for (size_t i = 0; i < 5; ++i) {
        const MenuUiEntry& entry = main->entries[static_cast<size_t>(main->selectable[i])];
        EXPECT_EQ(entry.label, expected_items[i]) << "item " << i;
        EXPECT_EQ(entry.widget_name, expected_widget_names[i]) << "item " << i;
        EXPECT_TRUE(entry.collected) << "item " << i;
        EXPECT_FALSE(entry.editor_added) << "item " << i;  // retail-declared
    }

    // The resume item is the conditional one, and its expression is carried, not
    // evaluated.
    const MenuUiEntry& resume = main->entries[static_cast<size_t>(main->selectable[0])];
    EXPECT_EQ(resume.widget_type, "MenuTextConditional");
    EXPECT_TRUE(resume.condition_unverified);
    EXPECT_EQ(resume.enable_expression, "!LevelFlow_LevelFailed()");
    EXPECT_EQ(resume.visible_expression, "TRUE");
    EXPECT_EQ(resume.font, "font3.fnt");
    EXPECT_EQ(resume.colour_index, 2);

    // Item rects: the lineup the definition implies (VERT_TOP, spacing 4, text
    // centred in the 440-wide frame at x=100).
    EXPECT_EQ(resume.rect.x, 281);
    EXPECT_EQ(resume.rect.y, 101);
    EXPECT_EQ(resume.rect.w, 11 * 7);
    EXPECT_EQ(resume.rect.h, 15);
    const MenuUiEntry& quit = main->entries[static_cast<size_t>(main->selectable[4])];
    EXPECT_EQ(quit.rect.y, 177);

    // The retail actions are carried verbatim.
    EXPECT_EQ(resume.action, "MenuManager_LeaveMenus(900, MENUMANAGER_IDLE)");
    EXPECT_EQ(quit.action, "MenuManager_ActivatePopupScreen(931)\n");

    // The title text is drawn but is not an item; the editor entry is the last.
    ASSERT_EQ(main->entries.size(), 9u);
    EXPECT_EQ(main->entries[1].label, "Game Paused");
    EXPECT_FALSE(main->entries[1].collected);
}

TEST(MenuUiRetail, OnlyTheRetailItemsTheEditorImplementsAreWired) {
    // The wiring decision: MENUMANAGER_IDLE -> Resume, MENUMANAGER_QUITTOMAINMENU ->
    // Quit, everything else inert with the retail action recorded.  See
    // docs/igi-re-parity/MENU_UI.md.
    const std::string root = FindRetailRoot();
    if (root.empty()) {
        GTEST_SKIP() << "retail corpus not reachable (set IGI_GAME_PATH)";
    }
    const igi::app_menu::EditorMenu menu =
        igi::app_menu::LoadEditorMenu(root, false, MenuUiBuildOptions{});
    ASSERT_TRUE(menu.retail) << "fell back: " << menu.reason;

    const igi::MenuUiScreen* main = menu.model.CurrentScreen();
    ASSERT_NE(main, nullptr);
    ASSERT_EQ(main->selectable.size(), 6u);

    const MenuUiEntry& resume = main->entries[static_cast<size_t>(main->selectable[0])];
    EXPECT_EQ(resume.editor_action, igi::MenuUiAction::Resume);
    EXPECT_FALSE(resume.inert);

    // Restart Game, Sound, Controls and Quit to Main Menu have no editor behaviour:
    // they stay in the item list and say why they are inert.
    for (size_t i = 1; i < 5; ++i) {
        const MenuUiEntry& entry = main->entries[static_cast<size_t>(main->selectable[i])];
        EXPECT_EQ(entry.editor_action, igi::MenuUiAction::None) << entry.label;
        EXPECT_TRUE(entry.inert) << entry.label;
        EXPECT_FALSE(entry.inert_reason.empty()) << entry.label;
        EXPECT_TRUE(entry.collected) << entry.label;
    }

    // The popup screen's Yes item is the one that leaves for the main menu; in the
    // editor that is the existing Quit behaviour.
    const igi::MenuUiScreen* quit_popup = menu.model.FindScreen(931);
    ASSERT_NE(quit_popup, nullptr);
    ASSERT_EQ(quit_popup->selectable.size(), 2u);
    const MenuUiEntry& yes = quit_popup->entries[static_cast<size_t>(quit_popup->selectable[0])];
    EXPECT_EQ(yes.label, "Yes");
    EXPECT_EQ(yes.editor_action, igi::MenuUiAction::Quit);
    EXPECT_FALSE(yes.inert);
    const MenuUiEntry& no = quit_popup->entries[static_cast<size_t>(quit_popup->selectable[1])];
    EXPECT_EQ(no.label, "No");
    EXPECT_TRUE(no.inert);
    EXPECT_NE(no.inert_reason.find("DeactivatePopuScreen"), std::string::npos);

    // Every collected item of every screen is either wired or inert with a reason.
    for (const igi::MenuUiScreen& screen : menu.model.screens) {
        for (int index : screen.selectable) {
            const MenuUiEntry& entry = screen.entries[static_cast<size_t>(index)];
            if (entry.editor_action == igi::MenuUiAction::None) {
                EXPECT_TRUE(entry.inert) << screen.name << "/" << entry.item_label;
                EXPECT_FALSE(entry.inert_reason.empty()) << screen.name << "/" << entry.item_label;
            } else {
                EXPECT_FALSE(entry.inert) << screen.name << "/" << entry.item_label;
            }
        }
    }
}

TEST(MenuUiRetail, BothReadersAgreeOnTheRetailFile) {
    // The retail container can be decoded two ways: `FromQvmFile` walks the parsed
    // container directly, `FromDecompiledSource` goes through the decompiler text.
    // They must produce the same tree, widget for widget -- ids, types, names,
    // field names, field types and values as text.  This is the cheapest proof that
    // neither path invents or drops structure.
    const std::string root = FindRetailRoot();
    if (root.empty()) {
        GTEST_SKIP() << "retail corpus not reachable (set IGI_GAME_PATH)";
    }
    const std::string path = root + "/menusystem/ingamemenu.qvm";

    const igi::MenuDefinition direct = igi::MenuDefinition::FromQvmFile(path);
    ASSERT_TRUE(direct.ok) << direct.error;

    const QVMFile qvm = QVM_Parse(path);
    ASSERT_TRUE(qvm.valid);
    const igi::MenuDefinition via_text =
        igi::MenuDefinition::FromDecompiledSource(QVM_DecompileToString(qvm));
    ASSERT_TRUE(via_text.ok) << via_text.error;

    ASSERT_EQ(direct.widget_count, via_text.widget_count);
    const std::vector<const MenuWidget*> a = direct.Flatten();
    const std::vector<const MenuWidget*> b = via_text.Flatten();
    ASSERT_EQ(a.size(), b.size());
    ASSERT_EQ(direct.Ids().size(), a.size());
    for (size_t i = 0; i < a.size(); ++i) {
        EXPECT_EQ(a[i]->id, b[i]->id) << "widget " << i;
        EXPECT_EQ(a[i]->id, direct.Ids()[i]) << "widget " << i;
        EXPECT_EQ(a[i]->type, b[i]->type) << "widget " << i;
        EXPECT_EQ(a[i]->name, b[i]->name) << "widget " << i;
        ASSERT_EQ(a[i]->fields.size(), b[i]->fields.size()) << "widget " << i;
        for (size_t f = 0; f < a[i]->fields.size(); ++f) {
            EXPECT_EQ(a[i]->fields[f].name, b[i]->fields[f].name) << "widget " << i;
            EXPECT_EQ(a[i]->fields[f].type, b[i]->fields[f].type) << "widget " << i;
            ASSERT_EQ(a[i]->fields[f].values.size(), b[i]->fields[f].values.size())
                << a[i]->type << "/" << a[i]->fields[f].name;
            for (size_t v = 0; v < a[i]->fields[f].values.size(); ++v) {
                EXPECT_EQ(a[i]->fields[f].values[v].AsText(), b[i]->fields[f].values[v].AsText())
                    << a[i]->type << "/" << a[i]->fields[f].name;
            }
        }
    }

    // And the model built from either reader is the same, item for item.
    const MenuUiModel from_direct = igi::BuildMenuUiModel(direct, MenuUiBuildOptions{});
    const MenuUiModel from_text = igi::BuildMenuUiModel(via_text, MenuUiBuildOptions{});
    ASSERT_TRUE(from_direct.ok);
    ASSERT_TRUE(from_text.ok);
    ASSERT_EQ(from_direct.screens.size(), from_text.screens.size());
    for (size_t s = 0; s < from_direct.screens.size(); ++s) {
        EXPECT_EQ(from_direct.screens[s].id, from_text.screens[s].id) << "screen " << s;
        ASSERT_EQ(from_direct.screens[s].selectable.size(),
                  from_text.screens[s].selectable.size()) << "screen " << s;
        for (size_t i = 0; i < from_direct.screens[s].selectable.size(); ++i) {
            const MenuUiEntry& ea = from_direct.screens[s]
                .entries[static_cast<size_t>(from_direct.screens[s].selectable[i])];
            const MenuUiEntry& eb = from_text.screens[s]
                .entries[static_cast<size_t>(from_text.screens[s].selectable[i])];
            EXPECT_EQ(ea.item_label, eb.item_label) << "screen " << s << " item " << i;
            EXPECT_EQ(ea.action, eb.action) << "screen " << s << " item " << i;
            EXPECT_EQ(ea.rect.x, eb.rect.x);
            EXPECT_EQ(ea.rect.y, eb.rect.y);
            EXPECT_EQ(ea.rect.w, eb.rect.w);
            EXPECT_EQ(ea.rect.h, eb.rect.h);
        }
    }
}

TEST(MenuUiRetail, EditorOptionsIsTheOnlyAddedEntry) {
    // The retail structure stays authoritative and the editor's own functions stay
    // reachable through exactly one labelled addition on the retail screen
    // (MENU_UI.md "Editor-added entry"), while the live menu's write side (issue
    // #74) lives on the editor's OWN screen.  This test proves both halves:
    // nothing retail-declared is changed, and exactly one retail entry and one
    // screen are marked editor-added.
    const std::string root = FindRetailRoot();
    if (root.empty()) {
        GTEST_SKIP() << "retail corpus not reachable (set IGI_GAME_PATH)";
    }
    igi::app_menu::EditorMenu menu =
        igi::app_menu::LoadEditorMenu(root, true, MenuUiBuildOptions{});
    ASSERT_TRUE(menu.retail) << "fell back: " << menu.reason;

    // The same definition through the model with no app wiring: the retail truth.
    const igi::MenuDefinition definition =
        igi::MenuDefinition::FromQvmFile(root + "/menusystem/ingamemenu.qvm");
    ASSERT_TRUE(definition.ok) << definition.error;
    const MenuUiModel direct = igi::BuildMenuUiModel(definition, MenuUiBuildOptions{});
    ASSERT_TRUE(direct.ok);
    ASSERT_EQ(direct.screens.size(), 8u);

    // Screen by screen, the retail screens are byte-identical: same ids, names,
    // frames, on-escape strings, entry count and every entry's label, rect, action
    // and selectability, in the same order.
    for (size_t s = 0; s < direct.screens.size(); ++s) {
        const MenuUiScreen& expected = direct.screens[s];
        const MenuUiScreen* actual = menu.model.FindScreen(expected.id);
        ASSERT_NE(actual, nullptr) << "screen " << expected.id;
        EXPECT_EQ(actual->name, expected.name);
        EXPECT_EQ(actual->type, expected.type);
        EXPECT_FALSE(actual->editor_added) << "screen " << expected.id;
        EXPECT_EQ(actual->on_back, expected.on_back);
        EXPECT_EQ(actual->frame.x, expected.frame.x);
        EXPECT_EQ(actual->frame.y, expected.frame.y);
        EXPECT_EQ(actual->frame.w, expected.frame.w);
        EXPECT_EQ(actual->frame.h, expected.frame.h);
        // The declared main screen carries the one added entry; every other
        // retail screen is exactly as declared.
        const size_t added_here = expected.id == direct.declared_screen_id ? 1u : 0u;
        ASSERT_EQ(actual->entries.size(), expected.entries.size() + added_here)
            << "screen " << expected.id;
        for (size_t i = 0; i < expected.entries.size(); ++i) {
            EXPECT_EQ(actual->entries[i].label, expected.entries[i].label);
            EXPECT_EQ(actual->entries[i].widget_name, expected.entries[i].widget_name);
            EXPECT_EQ(actual->entries[i].widget_type, expected.entries[i].widget_type);
            EXPECT_EQ(actual->entries[i].action, expected.entries[i].action);
            EXPECT_EQ(actual->entries[i].rect.x, expected.entries[i].rect.x);
            EXPECT_EQ(actual->entries[i].rect.y, expected.entries[i].rect.y);
            EXPECT_EQ(actual->entries[i].rect.w, expected.entries[i].rect.w);
            EXPECT_EQ(actual->entries[i].rect.h, expected.entries[i].rect.h);
            EXPECT_EQ(actual->entries[i].selectable, expected.entries[i].selectable);
            EXPECT_FALSE(actual->entries[i].editor_added)
                << expected.name << "/" << expected.entries[i].label;
        }
        ASSERT_EQ(actual->selectable.size(), expected.selectable.size() + added_here)
            << "screen " << expected.id;
        for (size_t i = 0; i < expected.selectable.size(); ++i) {
            EXPECT_EQ(actual->selectable[i], expected.selectable[i]);
        }
    }

    // Exactly one added screen, exactly one added entry on the retail main screen,
    // and every row of the added screen is editor-added: the editor's own rows plus
    // the live menu's write side (`kEditorAddedRowCount`).
    int added_screens = 0;
    int added_entries = 0;
    for (const MenuUiScreen& screen : menu.model.screens) {
        if (screen.editor_added) {
            ++added_screens;
        }
        for (const MenuUiEntry& entry : screen.entries) {
            if (entry.editor_added) {
                ++added_entries;
            }
        }
    }
    EXPECT_EQ(added_screens, 1);
    EXPECT_EQ(added_entries, 1 + igi::app_menu::EditorFallbackRowCount(true) +
                                 igi::app_menu::kEditorAddedRowCount);

    const MenuUiScreen* main = menu.model.FindScreen(900);
    ASSERT_NE(main, nullptr);
    const MenuUiEntry& editor = main->entries.back();
    EXPECT_TRUE(editor.editor_added);
    EXPECT_EQ(editor.label, igi::app_menu::kEditorOptionsLabel);
    EXPECT_EQ(editor.editor_action, igi::MenuUiAction::OpenEditorOptions);
    EXPECT_FALSE(editor.inert);
    EXPECT_EQ(main->selectable.back(), static_cast<int>(main->entries.size()) - 1);
    // It sits below the last retail item and inside the retail frame, placed with
    // retail's own centring rule.
    const MenuUiEntry& last_retail = main->entries[static_cast<size_t>(main->selectable[4])];
    EXPECT_EQ(editor.rect.y, last_retail.rect.y + last_retail.rect.h + 4);
    EXPECT_GT(editor.rect.y, 0);
    EXPECT_LT(editor.rect.y + editor.rect.h, main->frame.y + main->frame.h);
    EXPECT_EQ(editor.rect.x, ((main->frame.w - editor.rect.w) >> 1) + main->frame.x);

    // The added screen is the editor's row list, expanded as requested, plus the
    // write-side rows the live menu needs (edit the loaded tree's text, save it).
    const MenuUiScreen* options = menu.model.FindScreen(igi::app_menu::kEditorOptionsScreenId);
    ASSERT_NE(options, nullptr);
    EXPECT_TRUE(options->editor_added);
    EXPECT_EQ(options->entries.size(),
              21u + static_cast<size_t>(igi::app_menu::kEditorAddedRowCount));
    for (const MenuUiEntry& row : options->entries) {
        EXPECT_TRUE(row.editor_added);
        EXPECT_TRUE(row.collected);
        EXPECT_NE(row.editor_action, igi::MenuUiAction::None);
    }
    const size_t first_added = 21u;
    EXPECT_EQ(options->entries[first_added + 0].label, igi::app_menu::kMenuTextLabel);
    EXPECT_EQ(options->entries[first_added + 0].editor_action, igi::MenuUiAction::EditMenuText);
    EXPECT_EQ(options->entries[first_added + 1].label, igi::app_menu::kSaveMenuLabel);
    EXPECT_EQ(options->entries[first_added + 1].editor_action, igi::MenuUiAction::SaveMenu);
    // The added rows sit below the editor's own rows, inside the (grown) panel, and
    // are hit-testable like every other row.
    for (size_t i = first_added; i < options->entries.size(); ++i) {
        EXPECT_GT(options->entries[i].rect.y, options->entries[i - 1].rect.y);
        EXPECT_LT(options->entries[i].rect.y + options->entries[i].rect.h,
                  options->frame.y + options->frame.h);
    }
    // The whole editor row list is reachable from the added entry.
    ASSERT_TRUE(menu.model.OpenScreen(igi::app_menu::kEditorOptionsScreenId));
    EXPECT_EQ(menu.model.CurrentScreen()->entries.size(),
              21u + static_cast<size_t>(igi::app_menu::kEditorAddedRowCount));
    EXPECT_EQ(menu.model.SelectableCount(),
              21 + igi::app_menu::kEditorAddedRowCount);
    EXPECT_EQ(menu.model.FocusedEntryIndex(), 0);
}

// ── the save action: an edit back to a container the engine loads (#74) ──────
//
// `app_menu::SaveEditorMenu` is the editor's entry point to
// `menu/menu_qvm_writer.h`.  The engine's producer chain it translates is
// `QscGen_Compile` HOOK 0x4BBCB0 (src/qvm/qscgen.c:86-142) -> `QvmAsm_OpPush`
// HOOK 0x4C0A10 (src/qasm/asm.c:74-124) -> `QvmAsm_Assemble` HOOK 0x4BB270
// (src/qvm/qvmasm.c:103-276); the loader that has to accept the result is
// `Qvm_Load` HOOK 0x4BD220 (src/qvm/qvm.c:433-486).

TEST(MenuUiSave, AnEditSavesBackAndTheRepositoriesOwnReaderSeesIt) {
    const std::string root = FindRetailRoot();
    if (root.empty()) {
        GTEST_SKIP() << "retail corpus not reachable (set IGI_GAME_PATH)";
    }

    igi::app_menu::EditorMenu menu =
        igi::app_menu::LoadEditorMenu(root, false, MenuUiBuildOptions{});
    ASSERT_TRUE(menu.retail) << "fell back: " << menu.reason;
    ASSERT_TRUE(menu.definition.ok) << menu.definition.error;

    // The edit a menu tree makes, through the mutable accessors `qvm_image.h`
    // exposes for exactly this: the editor's own string, not the game's text.
    MenuWidget* screen = menu.definition.FindById(900);
    ASSERT_NE(screen, nullptr);
    ASSERT_EQ(screen->children.size(), 2u);
    ASSERT_EQ(screen->children[0].children.size(), 1u);
    MenuValue* text = screen->children[0].children[0].FindValue("Text Resource");
    ASSERT_NE(text, nullptr);
    ASSERT_EQ(text->text, "Game Paused");
    text->text = "IGI EDITOR";

    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "igi_menu_ui_test_saved.qvm";
    std::error_code ec;
    std::filesystem::remove(path, ec);

    std::string error;
    ASSERT_TRUE(igi::app_menu::SaveEditorMenu(menu, path.string(), &error)) << error;
    EXPECT_TRUE(error.empty());

    // The repository's own reader — the same one `LoadEditorMenu` used — reads the
    // saved container back with the edit in it.
    const MenuDefinition reloaded = MenuDefinition::FromQvmFile(path.string());
    ASSERT_TRUE(reloaded.ok) << reloaded.error;
    EXPECT_EQ(reloaded.widget_count, menu.definition.widget_count);
    const MenuWidget* reloaded_screen = reloaded.FindById(900);
    ASSERT_NE(reloaded_screen, nullptr);
    EXPECT_EQ(reloaded_screen->children[0].children[0].FieldText("Text Resource"), "IGI EDITOR");
    // …and the rest of the retail menu is untouched.
    EXPECT_EQ(reloaded_screen->FieldNumber("Frame x0"), 85.0);
    EXPECT_EQ(reloaded_screen->FieldNumber("Frame x1"), 555.0);
    EXPECT_EQ(reloaded_screen->children[1].children.size(), 5u);

    // The written container passes the engine's loader rules (`Qvm_Load`,
    // src/qvm/qvm.c:433-486): "LOOP", version 8, unk08 >= 5.
    const QVMFile qvm = QVM_Parse(path.string());
    ASSERT_TRUE(qvm.valid) << qvm.error;
    EXPECT_EQ(std::string(qvm.header.magic, 4), "LOOP");
    EXPECT_EQ(qvm.header.version, 8);
    EXPECT_GE(qvm.header.unk08, 5);

    std::filesystem::remove(path, ec);
}

TEST(MenuUiSave, AFallbackMenuIsNeverWritten) {
    const igi::app_menu::EditorMenu fallback = igi::app_menu::LoadEditorMenu(
        "/nonexistent/game/root", false, MenuUiBuildOptions{});
    ASSERT_TRUE(fallback.fallback);
    EXPECT_FALSE(fallback.definition.ok);

    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "igi_menu_ui_test_fallback.qvm";
    std::error_code ec;
    std::filesystem::remove(path, ec);

    std::string error;
    EXPECT_FALSE(igi::app_menu::SaveEditorMenu(fallback, path.string(), &error));
    EXPECT_FALSE(error.empty());
    EXPECT_FALSE(std::filesystem::exists(path, ec));

    // An empty path means "the definition's own source"; a fallback has none, so it
    // is refused too rather than guessed.
    std::string empty_error;
    EXPECT_FALSE(igi::app_menu::SaveEditorMenu(fallback, "", &empty_error));
    EXPECT_FALSE(empty_error.empty());
}

TEST(MenuUiSave, AnEmptyPathWritesTheFileTheMenuWasReadFrom) {
    const std::string root = FindRetailRoot();
    if (root.empty()) {
        GTEST_SKIP() << "retail corpus not reachable (set IGI_GAME_PATH)";
    }

    // Load from a copy, so the empty-path save writes the copy and never the install.
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "igi_menu_ui_save_source";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const std::filesystem::path copy = dir / "ingamemenu.qvm";
    std::filesystem::copy_file(
        std::filesystem::path(root) / "menusystem" / "ingamemenu.qvm", copy,
        std::filesystem::copy_options::overwrite_existing, ec);
    ASSERT_FALSE(ec) << ec.message();

    const MenuDefinition definition = MenuDefinition::FromQvmFile(copy.string());
    ASSERT_TRUE(definition.ok) << definition.error;

    igi::app_menu::EditorMenu menu;
    menu.definition = definition;
    menu.retail = true;

    std::string error;
    ASSERT_TRUE(igi::app_menu::SaveEditorMenu(menu, "", &error)) << error;
    // The unedited definition is byte-identical to what was read (the writer is
    // byte-faithful), which is what proves the empty path resolved to `source`.
    const MenuDefinition reread = MenuDefinition::FromQvmFile(copy.string());
    ASSERT_TRUE(reread.ok) << reread.error;
    EXPECT_EQ(reread.widget_count, definition.widget_count);
    EXPECT_EQ(reread.Ids(), definition.Ids());

    std::filesystem::remove_all(dir, ec);
}

// ── the live menu: the model is what the pause menu draws and edits (#74) ────
//
// The rows the live pause menu shows come from the loaded `EditorMenu` model, and
// an edit made through the mutable accessors of the loaded definition is what the
// rebuilt model draws and what `SaveEditorMenu` writes.  Everything below is the
// host-side half of that wiring; the draw and input layers are the other half
// (`source/renderer/renderer_draw.cpp`, `source/app_input_mouse.cpp`).

TEST(MenuUiLive, ThePauseMenuOpensOnTheEditorsOwnScreenAndCannotReachTheGameMenu) {
    const std::string root = FindRetailRoot();
    if (root.empty()) {
        GTEST_SKIP() << "retail corpus not reachable (set IGI_GAME_PATH)";
    }
    igi::app_menu::EditorMenu menu =
        igi::app_menu::LoadEditorMenu(root, false, MenuUiBuildOptions{});
    ASSERT_TRUE(menu.retail) << "fell back: " << menu.reason;

    // The model itself keeps the definition's declared screen (`Current menuscreen`
    // 900, which `MenuManager_New` HOOK 0x418B00 opens) ...
    EXPECT_EQ(menu.model.declared_screen_id, 900);
    ASSERT_NE(menu.model.CurrentScreen(), nullptr);
    EXPECT_EQ(menu.model.CurrentScreen()->id, 900);

    // ... and the live pause menu opens on the editor's own screen, so the editor's
    // own strings are what the user sees first.
    ASSERT_TRUE(igi::app_menu::OpenEditorPauseMenu(menu));
    const MenuUiScreen* editor = menu.model.CurrentScreen();
    ASSERT_NE(editor, nullptr);
    EXPECT_EQ(editor->id, igi::app_menu::kEditorOptionsScreenId);
    EXPECT_TRUE(editor->editor_added);
    EXPECT_EQ(menu.model.SelectableCount(),
              igi::app_menu::EditorFallbackRowCount(false) +
                  igi::app_menu::kEditorAddedRowCount);
    for (const MenuUiEntry& row : editor->entries) {
        EXPECT_TRUE(row.editor_added) << row.label;
        EXPECT_TRUE(row.collected) << row.label;
        EXPECT_NE(row.editor_action, igi::MenuUiAction::None) << row.label;
    }

    // The write-side rows are on it, with the actions the live dispatch handles.
    const MenuUiEntry* edit = EntryByLabel(menu.model, igi::app_menu::kMenuTextLabel);
    ASSERT_NE(edit, nullptr);
    EXPECT_EQ(edit->editor_action, igi::MenuUiAction::EditMenuText);
    const MenuUiEntry* save = EntryByLabel(menu.model, igi::app_menu::kSaveMenuLabel);
    ASSERT_NE(save, nullptr);
    EXPECT_EQ(save->editor_action, igi::MenuUiAction::SaveMenu);

    // ... and NO row opens the loaded retail screens.  That screen is the GAME's
    // menu ("Game Paused", "Resume Game", ...), and the user's requirement is that
    // the pause menu shows the editor's own text only, so the `Game Menu` row is
    // gone (source/app_menu.cpp `BuildModelInto`) and the action is refused if it is
    // ever reached (source/app_pause_menu.cpp `ActivateFocusedPauseMenuRow`).
    EXPECT_EQ(EntryByLabel(menu.model, igi::app_menu::kGameMenuLabel), nullptr);
    for (const MenuUiScreen& screen : menu.model.screens) {
        for (const MenuUiEntry& row : screen.entries) {
            EXPECT_NE(row.editor_action, igi::MenuUiAction::OpenGameMenu)
                << "screen " << screen.id << " row '" << row.label << "'";
        }
    }

    // The loaded definition's screen 900 is still on the model as DATA -- the editor
    // edits and saves that tree, and its rows keep the game's text verbatim in
    // `label` / `display_label` -- and the model can still be addressed to it
    // programmatically.  What the LIVE menu never does is put it on screen: no row
    // reaches it (above), and the draw layer draws `editor_label`, never the game's
    // text (MenuUiEditorOnly, below).
    ASSERT_TRUE(menu.model.PushScreen(900));
    const MenuUiScreen* main = menu.model.CurrentScreen();
    ASSERT_NE(main, nullptr);
    EXPECT_EQ(main->id, 900);
    EXPECT_FALSE(main->editor_added);
    EXPECT_EQ(main->frame.x, 85);
    EXPECT_EQ(main->frame.y, 80);
    EXPECT_EQ(main->frame.w, 470);
    EXPECT_EQ(main->frame.h, 330);
    EXPECT_EQ(menu.model.SelectableCount(), 6);  // the 5 retail items + Editor Options
    const MenuUiEntry* resume = EntryByLabel(menu.model, "Resume Game");
    ASSERT_NE(resume, nullptr);
    EXPECT_FALSE(resume->editor_added);
    EXPECT_EQ(resume->editor_action, igi::MenuUiAction::Resume);
    EXPECT_EQ(resume->editor_label, "Resume");  // the editor's own text, not the game's
    const MenuUiEntry* quit = EntryByLabel(menu.model, "Quit to Main Menu");
    ASSERT_NE(quit, nullptr);
    EXPECT_TRUE(quit->inert);
    EXPECT_FALSE(quit->inert_reason.empty());
    EXPECT_EQ(EntryByLabel(menu.model, igi::app_menu::kGameMenuLabel), nullptr)
        << "the game menu row is not on the editor's screen";

    // `MenuManager_PopScreen` HOOK 0x417710 (src/game/menumanager.c:237-247) comes
    // back to the screen that pushed.
    ASSERT_TRUE(menu.model.PopScreen());
    ASSERT_NE(menu.model.CurrentScreen(), nullptr);
    EXPECT_EQ(menu.model.CurrentScreen()->id, igi::app_menu::kEditorOptionsScreenId);
}

TEST(MenuUiLive, EditingTheLoadedTreeChangesWhatTheModelDrawsAndSavesBack) {
    const std::string root = FindRetailRoot();
    if (root.empty()) {
        GTEST_SKIP() << "retail corpus not reachable (set IGI_GAME_PATH)";
    }

    // Work on a copy: the empty-path save writes the definition's own source, and
    // that must never be the install.
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "igi_menu_ui_live_edit";
    std::error_code ec;
    std::filesystem::create_directories(dir / "menusystem", ec);
    const std::filesystem::path container = dir / "menusystem" / "ingamemenu.qvm";
    std::filesystem::copy_file(std::filesystem::path(root) / "menusystem" / "ingamemenu.qvm",
                               container, std::filesystem::copy_options::overwrite_existing, ec);
    ASSERT_FALSE(ec) << ec.message();

    igi::app_menu::EditorMenu menu =
        igi::app_menu::LoadEditorMenu(dir.string(), false, MenuUiBuildOptions{});
    ASSERT_TRUE(menu.retail) << "fell back: " << menu.reason;
    ASSERT_TRUE(igi::app_menu::OpenEditorPauseMenu(menu));

    // The title the engine draws on the retail main screen, as the model has it.
    const MenuUiScreen* main = menu.model.FindScreen(900);
    ASSERT_NE(main, nullptr);
    const MenuUiEntry* title = nullptr;
    for (const MenuUiEntry& entry : main->entries) {
        if (entry.widget_type == "MenuText" && entry.label == "Game Paused") {
            title = &entry;
        }
    }
    ASSERT_NE(title, nullptr) << "the retail main screen declares its title text";
    const int title_y = title->rect.y;

    std::string error;
    ASSERT_TRUE(igi::app_menu::EditEditorMenuText(menu, MenuUiBuildOptions{}, &error)) << error;

    // The edit lands in the loaded definition through the mutable accessor ...
    const MenuWidget* edited = menu.definition.FindById(900);
    ASSERT_NE(edited, nullptr);
    ASSERT_EQ(edited->children.size(), 2u);
    ASSERT_EQ(edited->children[0].children.size(), 1u);
    EXPECT_EQ(edited->children[0].children[0].FieldText("Text Resource"),
              igi::app_menu::kEditorMenuTitleCycle[0]);
    // ... and it is what the rebuilt model draws, at the title's own row.
    main = menu.model.FindScreen(900);
    ASSERT_NE(main, nullptr);
    const MenuUiEntry* drawn = nullptr;
    for (const MenuUiEntry& entry : main->entries) {
        if (entry.widget_type == "MenuText") {
            drawn = &entry;  // the first MenuText is the screen's title
            break;
        }
    }
    ASSERT_NE(drawn, nullptr);
    EXPECT_EQ(drawn->label, igi::app_menu::kEditorMenuTitleCycle[0]);
    EXPECT_EQ(drawn->rect.y, title_y);
    // The rebuild kept the editor's own screen: the row's own action does not move
    // the menu.
    ASSERT_NE(menu.model.CurrentScreen(), nullptr);
    EXPECT_EQ(menu.model.CurrentScreen()->id, igi::app_menu::kEditorOptionsScreenId);

    // The cycle walks the editor's own strings and wraps.
    ASSERT_TRUE(igi::app_menu::EditEditorMenuText(menu, MenuUiBuildOptions{}, &error)) << error;
    EXPECT_EQ(menu.definition.FindById(900)->children[0].children[0].FieldText("Text Resource"),
              igi::app_menu::kEditorMenuTitleCycle[1]);
    // A full cycle of the editor's strings comes back to where it was.
    for (int i = 0; i < igi::app_menu::kEditorMenuTitleCycleCount; ++i) {
        ASSERT_TRUE(igi::app_menu::EditEditorMenuText(menu, MenuUiBuildOptions{}, &error)) << error;
    }
    EXPECT_EQ(menu.definition.FindById(900)->children[0].children[0].FieldText("Text Resource"),
              igi::app_menu::kEditorMenuTitleCycle[1]);

    // What `SaveEditorMenu` writes is the edit, read back with the repository's own
    // reader, and the rest of the retail menu is untouched.
    ASSERT_TRUE(igi::app_menu::SaveEditorMenu(menu, "", &error)) << error;
    const MenuDefinition reloaded = MenuDefinition::FromQvmFile(container.string());
    ASSERT_TRUE(reloaded.ok) << reloaded.error;
    EXPECT_EQ(reloaded.widget_count, menu.definition.widget_count);
    const MenuWidget* reloaded_main = reloaded.FindById(900);
    ASSERT_NE(reloaded_main, nullptr);
    EXPECT_EQ(reloaded_main->children[0].children[0].FieldText("Text Resource"),
              igi::app_menu::kEditorMenuTitleCycle[1]);
    EXPECT_EQ(reloaded_main->FieldNumber("Frame x0"), 85.0);
    EXPECT_EQ(reloaded_main->FieldNumber("Frame x1"), 555.0);
    EXPECT_EQ(reloaded_main->children[1].children.size(), 5u);

    std::filesystem::remove_all(dir, ec);
}

TEST(MenuUiLive, ARebuildKeepsTheScreenAndTheUnsavedEdit) {
    const std::string root = FindRetailRoot();
    if (root.empty()) {
        GTEST_SKIP() << "retail corpus not reachable (set IGI_GAME_PATH)";
    }
    igi::app_menu::EditorMenu menu =
        igi::app_menu::LoadEditorMenu(root, false, MenuUiBuildOptions{});
    ASSERT_TRUE(menu.retail) << menu.reason;
    ASSERT_TRUE(igi::app_menu::OpenEditorPauseMenu(menu));

    std::string error;
    ASSERT_TRUE(igi::app_menu::EditEditorMenuText(menu, MenuUiBuildOptions{}, &error)) << error;
    const std::string edited =
        menu.definition.FindById(900)->children[0].children[0].FieldText("Text Resource");

    // The terrain toggle rebuilds the model (the editor's row list grows); the
    // definition is kept, so the unsaved edit survives, and the screen the user is
    // on is preserved.
    menu.terrain_expanded = true;
    ASSERT_TRUE(igi::app_menu::RebuildEditorMenuModel(menu, MenuUiBuildOptions{}));
    ASSERT_NE(menu.model.CurrentScreen(), nullptr);
    EXPECT_EQ(menu.model.CurrentScreen()->id, igi::app_menu::kEditorOptionsScreenId);
    EXPECT_EQ(menu.model.SelectableCount(),
              igi::app_menu::EditorFallbackRowCount(true) +
                  igi::app_menu::kEditorAddedRowCount);
    EXPECT_EQ(menu.definition.FindById(900)->children[0].children[0].FieldText("Text Resource"),
              edited);
    const MenuUiScreen* retail_main = menu.model.FindScreen(900);
    ASSERT_NE(retail_main, nullptr);
    const MenuUiEntry* retail_title = nullptr;
    for (const MenuUiEntry& entry : retail_main->entries) {
        if (entry.widget_type == "MenuText") {
            retail_title = &entry;
            break;
        }
    }
    ASSERT_NE(retail_title, nullptr);
    EXPECT_EQ(retail_title->label, edited);

    // ... and the menu goes back to the retail screen it was asked for.
    ASSERT_TRUE(menu.model.PushScreen(900));
    ASSERT_TRUE(igi::app_menu::RebuildEditorMenuModel(menu, MenuUiBuildOptions{}));
    ASSERT_NE(menu.model.CurrentScreen(), nullptr);
    EXPECT_EQ(menu.model.CurrentScreen()->id, 900);
}

TEST(MenuUiLive, AFallbackMenuHasNoWriteSide) {
    igi::app_menu::EditorMenu fallback =
        igi::app_menu::LoadEditorMenu("/nonexistent/game/root", false, MenuUiBuildOptions{});
    ASSERT_TRUE(fallback.fallback);
    ASSERT_TRUE(igi::app_menu::OpenEditorPauseMenu(fallback));
    ASSERT_NE(fallback.model.CurrentScreen(), nullptr);

    // Nothing to open, edit or save: the fallback screen is the editor's own rows
    // and no write-side row exists on it.
    EXPECT_EQ(fallback.model.SelectableCount(),
              igi::app_menu::EditorFallbackRowCount(false));
    for (const MenuUiEntry& row : fallback.model.CurrentScreen()->entries) {
        EXPECT_NE(row.editor_action, igi::MenuUiAction::OpenGameMenu) << row.label;
        EXPECT_NE(row.editor_action, igi::MenuUiAction::EditMenuText) << row.label;
        EXPECT_NE(row.editor_action, igi::MenuUiAction::SaveMenu) << row.label;
    }

    std::string error;
    EXPECT_FALSE(igi::app_menu::EditEditorMenuText(fallback, MenuUiBuildOptions{}, &error));
    EXPECT_FALSE(error.empty());
    EXPECT_FALSE(igi::app_menu::SaveEditorMenu(fallback, "", &error));
    EXPECT_FALSE(error.empty());

    // A rebuild of a fallback menu stays the fallback, and keeps its place.
    fallback.terrain_expanded = true;
    EXPECT_TRUE(igi::app_menu::RebuildEditorMenuModel(fallback, MenuUiBuildOptions{}));
    ASSERT_NE(fallback.model.CurrentScreen(), nullptr);
    EXPECT_TRUE(fallback.fallback);
    EXPECT_EQ(fallback.model.SelectableCount(), igi::app_menu::EditorFallbackRowCount(true));
}

// ── the text the editor DRAWS (source/app_menu.cpp EditorLabelForRetailEntry) ─
//
// The retail structure is kept whole: the same screens, the same rows in the same
// order, the same rects and colour indices, and the game's text verbatim in
// `label` / `display_label` (the definition is data the editor edits and saves).
// What the editor SHOWS is `editor_label`, the editor's own text for the row.
// docs/igi-re-parity/PAUSE_MENU_STYLE.md records the rule.

namespace {

const MenuUiEntry* EntryByWidget(const MenuUiScreen& screen, const std::string& widget_name,
                                 const std::string& label) {
    for (const MenuUiEntry& entry : screen.entries) {
        if (entry.widget_name == widget_name && entry.label == label) {
            return &entry;
        }
    }
    return nullptr;
}

// The screen's title is the first MenuText: the retail title frame's only child
// (ingamemenu.qvm screen 900; the fixture above mirrors it).
const MenuUiEntry* TitleEntry(const MenuUiScreen& screen) {
    for (const MenuUiEntry& entry : screen.entries) {
        if (entry.widget_type == "MenuText") {
            return &entry;
        }
    }
    return nullptr;
}

}  // namespace

TEST(MenuUiEditorText, RetailRowsShowTheEditorsOwnLabels) {
    const MenuDefinition definition = RetailShapedFixture();
    igi::app_menu::EditorMenu menu;
    menu.definition = definition;
    menu.model = igi::BuildMenuUiModel(definition, MenuUiBuildOptions{});
    ASSERT_TRUE(menu.model.ok);
    // The wiring that sets `editor_label` is `BuildModelInto`, reached through a
    // rebuild (or `LoadEditorMenu`).
    ASSERT_TRUE(igi::app_menu::RebuildEditorMenuModel(menu, MenuUiBuildOptions{}));

    const MenuUiScreen* main = menu.model.FindScreen(900);
    ASSERT_NE(main, nullptr);

    struct Expected {
        const char* widget_name;
        const char* retail_key;   // the game's text, still in the model
        const char* editor_label; // the editor's own text, drawn
    };
    const Expected expected[] = {
        {"", "Game Paused", "IGI EDITOR"},
        {"resume", "Resume Game", "Resume"},
        {"restart", "Restart Game", "Reset Level"},
        {"sound", "Sound", "Music"},
        {"controls", "Controls", "Keybindings"},
        {"quit", "Quit to Main Menu", "Quit"},
    };

    for (const Expected& row : expected) {
        const MenuUiEntry* entry = EntryByWidget(*main, row.widget_name, row.retail_key);
        ASSERT_NE(entry, nullptr) << "retail row '" << row.retail_key << "' is missing";
        // The structure and the game's text are untouched...
        EXPECT_EQ(entry->label, row.retail_key);
        EXPECT_EQ(entry->widget_name, row.widget_name);
        // ... and the editor draws its own words, never the game's.
        EXPECT_EQ(igi::app_menu::EditorLabelForRetailEntry(*entry), row.editor_label);
        EXPECT_EQ(entry->editor_label, row.editor_label);
        EXPECT_NE(entry->editor_label, row.retail_key);
    }

    // The five items keep their retail order and geometry: only the text changed.
    // (The editor's own "Editor Options" entry is appended after them, which is the
    // documented addition.)
    ASSERT_GE(main->selectable.size(), 5u);
    const char* order[] = {"resume", "restart", "sound", "controls", "quit"};
    for (size_t i = 0; i < 5; ++i) {
        EXPECT_EQ(main->entries[static_cast<size_t>(main->selectable[i])].widget_name, order[i]);
    }
}

TEST(MenuUiEditorText, EveryEditorLabelIsTheEditorsOwnRowLabel) {
    // The mapping does not invent strings: each label it returns is a label the
    // editor's own row list already draws (source/app_menu.cpp kFallbackRows /
    // kFallbackExpandedRows), except the title (the editor's own panel title) and
    // the one concept the editor names elsewhere ("Keybindings" is the title of its
    // KEYBINDINGS overlay, source/renderer/renderer_draw.cpp).
    const MenuUiModel fallback =
        igi::app_menu::BuildEditorFallbackMenu(false, MenuUiBuildOptions{});
    ASSERT_TRUE(fallback.ok);

    std::vector<std::string> own_labels;
    for (const MenuUiEntry& row : fallback.screens[0].entries) {
        own_labels.push_back(row.label);
    }

    for (const char* mapped : {"Resume", "Reset Level", "Music", "Quit"}) {
        EXPECT_NE(std::find(own_labels.begin(), own_labels.end(), mapped), own_labels.end())
            << "'" << mapped << "' is not one of the editor's own row labels";
    }
    EXPECT_EQ(std::string(igi::app_menu::kEditorKeybindingsLabel), "Keybindings");
    EXPECT_EQ(std::string(igi::app_menu::kEditorMenuTitleCycle[0]), "IGI EDITOR");
}

TEST(MenuUiEditorText, ARetailRowTheEditorHasNoCounterpartForSaysSo) {
    const MenuDefinition definition = RetailShapedFixture();
    const MenuUiModel model = igi::BuildMenuUiModel(definition, OptionsFor(definition));
    ASSERT_TRUE(model.ok);

    // Screen 901's OK row: the editor has no graphics-configuration behaviour, so
    // the row shows the editor's own "Not in Editor" rather than the retail "OK".
    const MenuUiScreen* graphics = model.FindScreen(901);
    ASSERT_NE(graphics, nullptr);
    const MenuUiEntry* ok = EntryByWidget(*graphics, "OK", "OK");
    ASSERT_NE(ok, nullptr);
    EXPECT_EQ(igi::app_menu::EditorLabelForRetailEntry(*ok),
              std::string(igi::app_menu::kRetailNotInEditorLabel));
    EXPECT_NE(igi::app_menu::kRetailNotInEditorLabel, std::string("OK"));
}

TEST(MenuUiEditorText, AWiredRetailRowIsLabelledByItsEditorAction) {
    // The confirmation popups' `Yes` carries no widget name of its own but IS wired
    // (screen 931's Yes is `MenuManager_LeaveMenus(0, MENUMANAGER_QUITTOMAINMENU)`,
    // which the editor maps to Quit).  Its label follows the behaviour, not the
    // widget, so it reads `Quit` rather than `Not in Editor`.
    MenuUiEntry yes;
    yes.widget_type = "MenuText";
    yes.widget_name = "";
    yes.label = "Yes";
    yes.has_text = true;
    yes.collected = true;
    yes.colour_index = 2;
    yes.editor_action = igi::MenuUiAction::Quit;
    EXPECT_EQ(igi::app_menu::EditorLabelForRetailEntry(yes), "Quit");

    // ... and a wired row the editor has no row label for still gets its own label
    // from the widget table or the explicit fallback, never the game's text.
    MenuUiEntry resume;
    resume.widget_name = "resume";
    resume.label = "Resume Game";
    resume.has_text = true;
    resume.collected = true;
    resume.colour_index = 2;
    resume.editor_action = igi::MenuUiAction::Resume;
    EXPECT_EQ(igi::app_menu::EditorLabelForRetailEntry(resume), "Resume");
}

TEST(MenuUiEditorText, ContainersAndEmptyRowsHaveNoEditorLabel) {
    MenuUiEntry container;
    container.widget_type = "MenuFrame";
    container.has_text = false;
    EXPECT_TRUE(igi::app_menu::EditorLabelForRetailEntry(container).empty());

    MenuUiEntry empty_text;
    empty_text.widget_type = "MenuText";
    empty_text.has_text = false;
    empty_text.label = "";
    EXPECT_TRUE(igi::app_menu::EditorLabelForRetailEntry(empty_text).empty());
}

TEST(MenuUiEditorText, EditingTheTitleKeepsTheEditorsOwnText) {
    // The `Menu Text` row edits the loaded definition's title.  The game's text
    // stops being drawn immediately, and the editor's own strings the cycle writes
    // are what the row shows -- so the edit stays visible without the game's words
    // ever coming back.
    igi::app_menu::EditorMenu menu;
    menu.definition = RetailShapedFixture();
    ASSERT_TRUE(igi::app_menu::RebuildEditorMenuModel(menu, MenuUiBuildOptions{}));

    const MenuUiScreen* main = menu.model.FindScreen(900);
    ASSERT_NE(main, nullptr);
    const MenuUiEntry* title = TitleEntry(*main);
    ASSERT_NE(title, nullptr);
    EXPECT_EQ(title->label, "Game Paused");        // the game's text is in the data
    EXPECT_EQ(title->editor_label, "IGI EDITOR");  // ... and is not what is drawn

    std::string error;
    ASSERT_TRUE(igi::app_menu::EditEditorMenuText(menu, MenuUiBuildOptions{}, &error)) << error;
    main = menu.model.FindScreen(900);
    ASSERT_NE(main, nullptr);
    title = TitleEntry(*main);
    ASSERT_NE(title, nullptr);
    EXPECT_EQ(title->label, igi::app_menu::kEditorMenuTitleCycle[0]);
    EXPECT_EQ(title->editor_label, igi::app_menu::kEditorMenuTitleCycle[0]);

    // The next cycle step is a different editor string, and it is drawn as such.
    ASSERT_TRUE(igi::app_menu::EditEditorMenuText(menu, MenuUiBuildOptions{}, &error)) << error;
    main = menu.model.FindScreen(900);
    ASSERT_NE(main, nullptr);
    title = TitleEntry(*main);
    ASSERT_NE(title, nullptr);
    EXPECT_EQ(title->editor_label, igi::app_menu::kEditorMenuTitleCycle[1]);
    EXPECT_EQ(title->label, igi::app_menu::kEditorMenuTitleCycle[1]);
}

TEST(MenuUiEditorText, EditorAddedRowsKeepTheirOwnLabels) {
    igi::app_menu::EditorMenu menu;
    menu.definition = RetailShapedFixture();
    ASSERT_TRUE(igi::app_menu::RebuildEditorMenuModel(menu, MenuUiBuildOptions{}));

    // The editor's own screen and the one entry the retail main screen gains are
    // the editor's text already: nothing may rewrite them.
    const MenuUiScreen* own = menu.model.FindScreen(igi::app_menu::kEditorOptionsScreenId);
    ASSERT_NE(own, nullptr);
    for (const MenuUiEntry& row : own->entries) {
        EXPECT_TRUE(row.editor_added);
        EXPECT_EQ(row.editor_label, row.label) << row.label;
    }
    const MenuUiScreen* main = menu.model.FindScreen(900);
    ASSERT_NE(main, nullptr);
    const MenuUiEntry* options = EntryByWidget(*main, "editor-options",
                                               igi::app_menu::kEditorOptionsLabel);
    ASSERT_NE(options, nullptr);
    EXPECT_TRUE(options->editor_added);
    EXPECT_EQ(options->editor_label, std::string(igi::app_menu::kEditorOptionsLabel));
}

// ── the editor-only rule: the pause menu draws the editor's text, never the game's ─
//
// The user's requirement, verbatim: "it has text from game igi.exe which i dont
// want i want editor only got it".  These tests hold the two structural facts that
// make it true of the LIVE model, not merely of the strings the editor happens to
// pass today:
//
//   1. no row anywhere is wired to `MenuUiAction::OpenGameMenu`, so the loaded retail
//      screens -- the game's own menu, with "Game Paused", "Resume Game", ... -- are
//      not reachable from the editor's pause menu (the `Game Menu` row was removed,
//      source/app_menu.cpp `BuildModelInto`);
//   2. every string the live draw path (source/renderer/renderer_draw.cpp:1960-2086)
//      would draw is the editor's own: never an entry's `label` (the retail script
//      key) nor its `display_label` (the language archive's answer, resolved through
//      `MenuManager_GetString` HOOK 0x418EA0, src/menu/menumanager.c:832-852).
//
// The retail definition stays on the model as data -- the editor edits and saves it
// through the `Menu Text` / `Save Menu` rows -- which is exactly why the second
// invariant compares the DRAWN strings against the GAME strings instead of trusting
// that the two sets happen not to overlap.

namespace {

// The string the live draw layer draws for an entry: an editor-added row's live label
// (`PauseMenuRowLabel`, source/runtime/pause_menu_rows.h) or a retail row's
// `editor_label` (source/app_menu.cpp `EditorLabelForRetailEntry`).  Empty means the
// row draws nothing.
std::string DrawnTextFor(const MenuUiEntry& entry) {
    const igi::PauseMenuRowState state;
    return entry.editor_added ? igi::PauseMenuRowLabel(entry.editor_action, state)
                              : entry.editor_label;
}

// Collects the strings the model would draw and the strings that come from the game's
// own data, over EVERY screen (the pause menu can be positioned on any of them), and
// asserts the two sets are disjoint.  Also asserts that a text row does not silently
// vanish: a retail row with text must have the editor's own text to draw.
void ExpectTheDrawnTextIsNeverTheGames(const MenuUiModel& model) {
    std::vector<std::string> game_strings;
    std::vector<std::string> drawn_strings;
    for (const MenuUiScreen& screen : model.screens) {
        for (const MenuUiEntry& entry : screen.entries) {
            if (!entry.editor_added) {
                if (!entry.label.empty()) {
                    game_strings.push_back(entry.label);
                }
                if (!entry.display_label.empty()) {
                    game_strings.push_back(entry.display_label);
                }
            }
            const std::string drawn = DrawnTextFor(entry);
            if (drawn.empty()) {
                if (entry.has_text && !entry.label.empty()) {
                    ADD_FAILURE() << "screen " << screen.id << " row '" << entry.label
                                  << "' draws no text";
                }
                continue;
            }
            drawn_strings.push_back(drawn);
        }
    }
    ASSERT_FALSE(drawn_strings.empty());
    for (const std::string& drawn : drawn_strings) {
        EXPECT_EQ(std::find(game_strings.begin(), game_strings.end(), drawn),
                  game_strings.end())
            << "the menu draws the game's own text: '" << drawn << "'";
    }
}

}  // namespace

TEST(MenuUiEditorOnly, NoRowOpensTheRetailScreens) {
    igi::app_menu::EditorMenu menu;
    menu.definition = RetailShapedFixture();
    ASSERT_TRUE(igi::app_menu::RebuildEditorMenuModel(menu, MenuUiBuildOptions{}));

    // No row of any screen opens the loaded definition's screen 900: the row that
    // used to (`Game Menu`) is gone, and the action is refused if it is ever reached
    // (source/app_pause_menu.cpp `ActivateFocusedPauseMenuRow`).
    for (const MenuUiScreen& screen : menu.model.screens) {
        for (const MenuUiEntry& row : screen.entries) {
            EXPECT_NE(row.editor_action, igi::MenuUiAction::OpenGameMenu)
                << "screen " << screen.id << " row '" << row.label << "'";
        }
    }

    // Opening the pause menu puts the user on the editor's own screen, and that
    // screen is a closed set: every row on it is editor-added, none opens a retail
    // screen, and none opens another screen at all.
    ASSERT_TRUE(igi::app_menu::OpenEditorPauseMenu(menu));
    const MenuUiScreen* own = menu.model.CurrentScreen();
    ASSERT_NE(own, nullptr);
    EXPECT_EQ(own->id, igi::app_menu::kEditorOptionsScreenId);
    EXPECT_EQ(menu.model.SelectableCount(),
              igi::app_menu::EditorFallbackRowCount(false) +
                  igi::app_menu::kEditorAddedRowCount);
    for (const MenuUiEntry& row : own->entries) {
        EXPECT_TRUE(row.editor_added) << row.label;
        EXPECT_NE(row.editor_action, igi::MenuUiAction::OpenGameMenu) << row.label;
        EXPECT_NE(row.editor_action, igi::MenuUiAction::OpenEditorOptions) << row.label;
    }

    // The retail screens are still the model's data, not deleted from it.
    EXPECT_NE(menu.model.FindScreen(900), nullptr);
}

TEST(MenuUiEditorOnly, TheDrawnTextIsNeverTheGames) {
    igi::app_menu::EditorMenu menu;
    menu.definition = RetailShapedFixture();
    ASSERT_TRUE(igi::app_menu::RebuildEditorMenuModel(menu, MenuUiBuildOptions{}));
    ExpectTheDrawnTextIsNeverTheGames(menu.model);
}

TEST(MenuUiEditorOnly, TheRetailCorpusDrawnTextIsNeverTheGames) {
    const std::string root = FindRetailRoot();
    if (root.empty()) {
        GTEST_SKIP() << "retail corpus not reachable (set IGI_GAME_PATH)";
    }
    igi::app_menu::EditorMenu menu =
        igi::app_menu::LoadEditorMenu(root, false, MenuUiBuildOptions{});
    ASSERT_TRUE(menu.retail) << "fell back: " << menu.reason;

    // The comparison is not vacuous: the shipped file's own strings are on the model.
    std::size_t game_strings = 0;
    for (const MenuUiScreen& screen : menu.model.screens) {
        for (const MenuUiEntry& entry : screen.entries) {
            if (!entry.editor_added && !entry.display_label.empty()) {
                ++game_strings;
            }
        }
    }
    EXPECT_GT(game_strings, 0u);
    ExpectTheDrawnTextIsNeverTheGames(menu.model);

    // The retail title is the game's ("Game Paused" in the shipped file) and what the
    // editor draws in its place is one of the editor's own titles, whatever the
    // definition currently carries (the `Menu Text` row cycles them).
    const MenuUiScreen* main = menu.model.FindScreen(900);
    ASSERT_NE(main, nullptr);
    const MenuUiEntry* title = nullptr;
    for (const MenuUiEntry& entry : main->entries) {
        if (entry.widget_type == "MenuText") {
            title = &entry;
            break;
        }
    }
    ASSERT_NE(title, nullptr);
    bool is_editor_title = false;
    for (int i = 0; i < igi::app_menu::kEditorMenuTitleCycleCount; ++i) {
        is_editor_title =
            is_editor_title || title->editor_label == igi::app_menu::kEditorMenuTitleCycle[i];
    }
    EXPECT_TRUE(is_editor_title) << "the drawn title is '" << title->editor_label << "'";
    EXPECT_FALSE(title->label.empty());

    // ... and no row of the screen the user is on reaches that screen.
    ASSERT_TRUE(igi::app_menu::OpenEditorPauseMenu(menu));
    ASSERT_NE(menu.model.CurrentScreen(), nullptr);
    for (const MenuUiEntry& row : menu.model.CurrentScreen()->entries) {
        EXPECT_NE(row.editor_action, igi::MenuUiAction::OpenGameMenu) << row.label;
    }
}
