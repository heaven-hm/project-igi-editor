/******************************************************************************
 * @file    source/app_menu.cpp
 * @brief   Wires the editor's in-app menu to the retail menu definition, with an
 *          explicit documented fallback to the editor's own row list.
 *
 * The retail menu is data: `menusystem/ingamemenu.qvm` declares the whole menu
 * (screens, frames, labels, actions) and `igi::MenuDefinition`
 * (source/menu/qvm_image.h) decodes it.  This file turns that definition into the
 * navigable model in source/menu/menu_ui.h and records, per item, which editor
 * behaviour it is wired to.  When the retail file is not there, the menu is built
 * from the rows the editor has always drawn, so the pause menu keeps working.
 *
 * ORACLE: IGI-RE, the verified C decompilation of retail IGI.exe.
 *   src/menu/menumanager.c:94-113   menu state enum: MENU_IDLE=9, MENU_QUIT=10,
 *                                   MENU_QUIT_TO_MAIN=11, MENU_RESTART_LEVEL=12
 *   src/game/menumanager.c:249-261  MenuManager_LeaveMenus HOOK 0x417790:
 *                                   `mm->screen = arg0; mm->state = 6;
 *                                   mm->nextState = arg1;`
 *   src/menu/menumanager.c:429-443  MenuManager_Run: MENU_IDLE -> idle(mm);
 *                                   MENU_QUIT_TO_MAIN -> Flow_SetRequest(6);
 *                                   MENU_RESTART_LEVEL -> Flow_SetRequest(5)
 *   src/menu/menumanager.c:479-535  MenuManager_ReadInput HOOK 0x418580 (the
 *                                   navigation the model implements)
 *
 * The two mappings below are the only retail items the editor has a behaviour
 * for; every other retail item is reported present-but-inert with the retail
 * action recorded, and docs/igi-re-parity/MENU_UI.md lists them.
 *
 * The fallback row list mirrors the rows source/renderer/renderer_draw.cpp has
 * always drawn (its `btn_labels`) and the hit boxes
 * source/app_input_mouse.cpp tests with `igi::IsPauseMenuRowHit`, so switching the
 * pause menu onto the model does not change what is on screen or what is
 * clickable.
 *****************************************************************************/

#include "menu/menu_ui.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include "menu/qvm_image.h"
#include "menu/menu_qvm_writer.h"
#include "menu/menu_text_res.h"
#include "runtime/pause_menu_layout.h"
#include "runtime/pause_menu_style.h"

namespace igi {
namespace app_menu {

// The live menu grows the panel by `App::PauseMenuExtraRows`, which returns
// `kEditorAddedRowCount`; `kPauseMenuEditorExtraRows` is the same number as the layout
// constant the layout tests assert against.  They must agree, or the panel is sized
// for a different number of rows than it draws.
static_assert(kEditorAddedRowCount == igi::kPauseMenuEditorExtraRows,
              "kEditorAddedRowCount and kPauseMenuEditorExtraRows must stay equal "
              "(App::PauseMenuExtraRows passes the former to BuildPauseMenuLayout)");

namespace {

std::string Squashed(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (char c : text) {
        if (!std::isspace(static_cast<unsigned char>(c))) {
            out.push_back(c);
        }
    }
    return out;
}

bool IsReadableFile(const std::string& path) {
    std::ifstream probe(path, std::ios::binary);
    return probe.good();
}

// `MenuManager_LeaveMenus(<screen id>, <mode>)` with the given mode, ignoring
// whitespace.  The screen id is not part of the decision: the mode is what
// MenuManager_LeaveMenus stores as the next state (src/game/menumanager.c:249-261).
bool IsLeaveMenusInto(const std::string& squashed_action, const char* mode) {
    const std::string prefix = "MenuManager_LeaveMenus(";
    if (squashed_action.compare(0, prefix.size(), prefix) != 0) {
        return false;
    }
    const std::string suffix = std::string(",") + mode + ")";
    if (squashed_action.size() <= prefix.size() + suffix.size()) {
        return false;
    }
    if (squashed_action.compare(squashed_action.size() - suffix.size(), suffix.size(),
                                suffix) != 0) {
        return false;
    }
    // The middle has to be the screen id, not more script.
    const std::string middle =
        squashed_action.substr(prefix.size(), squashed_action.size() - prefix.size() - suffix.size());
    if (middle.empty()) {
        return false;
    }
    for (char c : middle) {
        if (!std::isdigit(static_cast<unsigned char>(c)) && c != '-') {
            return false;
        }
    }
    return true;
}

// The editor's own rows, in the order renderer_draw.cpp builds them.  The labels
// are the rows' state-independent base names: the live state suffix is appended by
// the draw layer, which is where it lives today.
struct FallbackRow {
    const char* label;
    MenuUiAction action;
};

// renderer_draw.cpp: `Resume` ... `Terrain Options` header, then (expanded) the
// five terrain rows, then `Reset Level`, `Save Level`, `Quit`.
const FallbackRow kFallbackRows[] = {
    {"Resume", MenuUiAction::Resume},
    {"Mode", MenuUiAction::ToggleMode},
    {"Collision", MenuUiAction::ToggleCollision},
    {"Font", MenuUiAction::ToggleFont},
    {"Select Level", MenuUiAction::SelectLevel},
    {"Auto Save", MenuUiAction::ToggleAutoSave},
    {"Logging", MenuUiAction::ToggleLogging},
    {"Log Level", MenuUiAction::CycleLogLevel},
    {"Music", MenuUiAction::ToggleMusic},
    {"Weather", MenuUiAction::CycleWeather},
    // The editor's weather preview speed (igi::WeatherSpeed). Editor-only: the
    // engine's fall speed is fixed (src/dxinit/rain.c:122-123,572).
    {"Weather Speed", MenuUiAction::CycleWeatherSpeed},
    {"Lightmap", MenuUiAction::ToggleLightmaps},
    {"Calculate Lightmaps", MenuUiAction::CalculateLightmaps},
    {"Terrain Options", MenuUiAction::ToggleTerrainOptions},
    {"Reset Level", MenuUiAction::ResetLevel},
    {"Save Level", MenuUiAction::SaveLevel},
    {"Quit", MenuUiAction::Quit},
};

constexpr size_t kFallbackTerrainHeaderRow = 13;  // index of "Terrain Options"

const FallbackRow kFallbackExpandedRows[] = {
    {"Texture", MenuUiAction::ToggleTerrainTexture},
    {"Height", MenuUiAction::ToggleTerrainHeight},
    {"Discard", MenuUiAction::ToggleTerrainDiscard},
    {"Fog", MenuUiAction::ToggleTerrainFog},
    {"Fog Intensity", MenuUiAction::AdjustFogIntensity},
};

std::vector<FallbackRow> FallbackRowsFor(bool terrain_expanded) {
    std::vector<FallbackRow> rows;
    rows.reserve(static_cast<size_t>(PauseMenuRowCount(terrain_expanded)));
    for (size_t i = 0; i < sizeof(kFallbackRows) / sizeof(kFallbackRows[0]); ++i) {
        rows.push_back(kFallbackRows[i]);
        if (terrain_expanded && i == kFallbackTerrainHeaderRow) {
            for (const FallbackRow& row : kFallbackExpandedRows) {
                rows.push_back(row);
            }
        }
    }
    return rows;
}

// The editor's OWN label for an editor action: the label its row list gives that
// action (source/renderer/renderer_draw.cpp has always drawn those strings).  A
// state-independent label, so the mapping below cannot depend on the editor's
// live state.
const char* EditorOwnRowLabel(MenuUiAction action) {
    for (const FallbackRow& row : kFallbackRows) {
        if (row.action == action) {
            return row.label;
        }
    }
    for (const FallbackRow& row : kFallbackExpandedRows) {
        if (row.action == action) {
            return row.label;
        }
    }
    return nullptr;
}

// A retail widget name -> the editor row whose own label that retail row shows.
// The name is the retail script's own task name (ingamemenu.qvm), which the
// editor never edits, so the mapping survives a `Menu Text` edit of the row's
// `Text Resource`.  The action's label is taken from the editor's own row list
// (`EditorOwnRowLabel`), so the strings cannot drift from the rows they name.
struct RetailEditorLabel {
    const char* widget_name;  // the retail widget's task name, verbatim
    MenuUiAction action;      // the editor row whose label this row shows
    const char* literal;      // the label to use when `action == None`
};

const RetailEditorLabel kRetailEditorLabels[] = {
    // Screen 900's five items, in retail order (ingamemenu.qvm).
    {"resume", MenuUiAction::Resume, nullptr},        // -> "Resume"
    {"restart", MenuUiAction::ResetLevel, nullptr},   // -> "Reset Level"
    {"sound", MenuUiAction::ToggleMusic, nullptr},    // -> "Music"
    // The editor has no controls screen; its own name for input configuration is
    // its KEYBINDINGS overlay (source/renderer/renderer_draw.cpp).
    {"controls", MenuUiAction::None, kEditorKeybindingsLabel},
    {"quit", MenuUiAction::Quit, nullptr},            // -> "Quit"
};

}  // namespace

int EditorFallbackRowCount(bool terrain_expanded) {
    return static_cast<int>(FallbackRowsFor(terrain_expanded).size());
}

std::string EditorLabelForRetailEntry(const MenuUiEntry& entry) {
    if (!entry.has_text || entry.label.empty()) {
        return std::string();
    }

    // 1. A retail screen's TITLE.  The engine draws it as a plain MenuText inside
    //    the title MenuFrame at the title colour index (`MenuText_Init` HOOK
    //    0x420CC0 registers TEXT_WHITE = 1 as the first named entry,
    //    src/menuscreen/menutext.c:301-304; screen 900's title is ColourIndex 1,
    //    901-903's titles and the popups' prompts likewise).  It is not an item,
    //    so it is not collected.  The editor shows its OWN panel title there.
    //
    //    The `Menu Text` row cycles the definition's title through
    //    `kEditorMenuTitleCycle`; when the loaded title is already one of those
    //    editor strings the editor shows it, so that edit stays visible.  The
    //    retail text ("Game Paused") is not one of them and is never drawn.
    if (!entry.collected && (entry.colour_index & 0x0F) == kRetailTitleColourIndex) {
        for (int i = 0; i < kEditorMenuTitleCycleCount; ++i) {
            if (entry.label == kEditorMenuTitleCycle[i]) {
                return entry.label;
            }
        }
        return kEditorMenuTitleCycle[0];  // "IGI EDITOR"
    }

    // 2. A retail row the editor is WIRED to shows the editor's own label for the
    //    behaviour it drives: the label names what the row does in the editor, not
    //    what the game called it.  This covers the confirmation popups' `Yes`
    //    (wired to Quit) as well as screen 900's `resume`, whose retail widget name
    //    is also in the table below.
    if (entry.editor_action != MenuUiAction::None) {
        const char* own = EditorOwnRowLabel(entry.editor_action);
        if (own != nullptr) {
            return own;
        }
    }

    // 3. A retail row the editor has a function for but does not wire, keyed on
    //    the retail widget's own script name, shows the editor's own row label for
    //    that function.
    for (const RetailEditorLabel& mapping : kRetailEditorLabels) {
        if (entry.widget_name != mapping.widget_name) {
            continue;
        }
        if (mapping.action != MenuUiAction::None) {
            const char* own = EditorOwnRowLabel(mapping.action);
            if (own != nullptr) {
                return own;
            }
        }
        if (mapping.literal != nullptr) {
            return mapping.literal;
        }
    }

    // 4. Everything else: the editor has no counterpart for the row.  The label
    //    says so in the editor's own words instead of repeating the game's.
    return kRetailNotInEditorLabel;
}

MenuUiAction EditorActionForRetailItem(const MenuUiEntry& entry) {
    const std::string action = Squashed(entry.action);
    if (action.empty()) {
        return MenuUiAction::None;
    }
    // MENUMANAGER_IDLE (9): MenuManager_Run calls idle(mm), which is the menu
    // closing and the game continuing -- the editor's "Resume".
    if (IsLeaveMenusInto(action, "MENUMANAGER_IDLE")) {
        return MenuUiAction::Resume;
    }
    // MENUMANAGER_QUITTOMAINMENU (11): Flow_SetRequest(6) leaves for the main
    // menu.  The editor has no main menu, so this is the same "leave the program"
    // the editor's Quit row already performs (app_input_mouse.cpp:590 calls
    // glutLeaveMainLoop).
    if (IsLeaveMenusInto(action, "MENUMANAGER_QUITTOMAINMENU")) {
        return MenuUiAction::Quit;
    }
    return MenuUiAction::None;
}

// One row of the editor's own list, as the live menu builds it: the same geometry
// the renderer has always used (`PauseMenuRowCenter`, source/runtime/
// pause_menu_layout.h) so the model and the draw layer cannot drift apart.
MenuUiEntry MakeEditorRow(const PauseMenuLayout& layout, const MenuUiBuildOptions& options,
                          int row_index, const char* label, MenuUiAction action) {
    const int centre = PauseMenuRowCenter(layout, row_index);
    // The legacy hit test is inclusive (IsPauseMenuRowHit: centre - radius <= y
    // <= centre + radius); the model uses retail's strict comparison
    // (MenuWindow_HitTest: y0 < y < y0 + h), so the box is grown by one pixel
    // on each side to keep the clickable band identical.
    const MenuUiRect viewport_rect{layout.menu_left - 1, centre - layout.row_hit_radius - 1,
                                   layout.menu_width + 2,
                                   2 * layout.row_hit_radius + 2};

    MenuUiEntry entry;
    entry.widget_type = "EditorRow";
    entry.widget_name = label;
    entry.task_id = -1;
    entry.label = label;
    entry.item_label = label;
    // These rows are the editor's own, so they are already display text: there is
    // no `Text Resource` key to resolve and nothing to look up in an archive.
    entry.display_label = label;
    entry.editor_label = label;
    entry.rect = MenuUiToRetail(viewport_rect, options.viewport_width,
                                options.viewport_height);
    entry.has_text = true;
    entry.selectable = true;
    entry.item_candidate = true;
    entry.collected = true;
    entry.editor_added = true;
    entry.enabled = true;
    entry.visible = true;
    entry.editor_action = action;
    entry.inert = false;
    entry.colour_index = 1;  // TEXT_WHITE, src/menuscreen/menutext.c:301-304
    return entry;
}

MenuUiModel BuildEditorFallbackMenu(bool terrain_expanded,
                                    const MenuUiBuildOptions& options,
                                    const std::vector<EditorAddedRow>& added_rows) {
    MenuUiModel model;
    model.viewport_width = options.viewport_width;
    model.viewport_height = options.viewport_height;
    model.max_items = options.max_items > 0 ? options.max_items : 32;
    model.fallback = true;
    model.source = "fallback:editor-rows";
    model.fallback_reason =
        "the retail menu definition was not used; the editor's own row list is shown";

    // The panel holds the editor's rows plus the added ones, so it grows by them
    // (`kPauseMenuEditorExtraRows`, source/runtime/pause_menu_layout.h).
    const PauseMenuLayout layout = BuildPauseMenuLayout(
        options.viewport_width, options.viewport_height, terrain_expanded,
        options.metrics.line_height, static_cast<int>(added_rows.size()));

    MenuUiScreen screen;
    screen.id = 0;
    screen.type = "EditorPauseMenu";
    screen.name = "Pause Menu";
    screen.on_back = "";
    screen.editor_added = true;
    screen.frame = MenuUiToRetail(
        MenuUiRect{layout.menu_left, layout.menu_top, layout.menu_width, layout.menu_height},
        options.viewport_width, options.viewport_height);

    const std::vector<FallbackRow> rows = FallbackRowsFor(terrain_expanded);
    for (size_t i = 0; i < rows.size(); ++i) {
        MenuUiEntry entry = MakeEditorRow(layout, options, static_cast<int>(i),
                                          rows[i].label, rows[i].action);
        screen.selectable.push_back(static_cast<int>(i));
        screen.entries.push_back(std::move(entry));
    }

    // The editor-added rows, after the editor's own list, in the order given.
    int next_row = PauseMenuRowCount(terrain_expanded);
    for (const EditorAddedRow& row : added_rows) {
        MenuUiEntry entry = MakeEditorRow(layout, options, next_row, row.label, row.action);
        if (row.widget_name != nullptr && row.widget_name[0] != '\0') {
            entry.widget_name = row.widget_name;
        }
        screen.selectable.push_back(static_cast<int>(screen.entries.size()));
        screen.entries.push_back(std::move(entry));
        ++next_row;
    }

    model.screens.push_back(std::move(screen));
    model.screen_index = 0;
    model.item_index = model.screens[0].selectable.empty() ? -1 : 0;
    model.ok = true;
    return model;
}

// Fills `menu.model` from `menu.definition` (and `menu.game_root` /
// `menu.terrain_expanded`), recording in `menu` whether that was the retail tree
// or the documented fallback.  `menu.definition` is never re-read here, so an
// unsaved edit survives a rebuild.
void BuildModelInto(EditorMenu& menu, const MenuUiBuildOptions& options) {
    menu.model = MenuUiModel{};
    menu.retail = false;
    menu.fallback = false;
    menu.language.clear();
    menu.label_stats = EditorMenuLabelStats{};

    if (!menu.definition.ok) {
        menu.model = BuildEditorFallbackMenu(menu.terrain_expanded, options);
        menu.fallback = true;
        menu.model.fallback_reason = menu.reason;
        return;
    }

    MenuUiModel model = BuildMenuUiModel(menu.definition, options);
    if (!model.ok) {
        const std::string definition_error = model.error;
        menu.reason = "retail menu definition has no usable screen (" +
                      menu.definition.source + "): " + definition_error;
        menu.model = BuildEditorFallbackMenu(menu.terrain_expanded, options);
        menu.fallback = true;
        menu.model.fallback_reason = menu.reason;
        return;
    }

    // Editor wiring.  A retail item with no editor behaviour stays in the item
    // list and reports why it is inert.
    for (MenuUiScreen& screen : model.screens) {
        for (MenuUiEntry& entry : screen.entries) {
            if (!entry.collected) {
                continue;
            }
            entry.editor_action = EditorActionForRetailItem(entry);
            if (entry.editor_action == MenuUiAction::None) {
                entry.inert = true;
                entry.inert_reason = entry.action.empty()
                    ? "retail item has no On Click Script"
                    : "retail action is not wired in the editor: " + Squashed(entry.action);
            }
        }
    }

    // ── the text the editor draws ────────────────────────────────────────────
    // The retail structure is untouched -- the same screens, the same rows in the
    // same order, the same rects, the same colour indices -- but what the editor
    // DRAWS is its own text: `EditorLabelForRetailEntry` gives every retail text
    // row the editor's label for its slot, and the game's words are never shown
    // (docs/igi-re-parity/PAUSE_MENU_STYLE.md "The editor's own strings come
    // first").  `label` / `display_label` keep the game's text verbatim: the
    // definition is data the editor edits and saves, and the reader and label
    // tests read those fields.
    for (MenuUiScreen& screen : model.screens) {
        for (MenuUiEntry& entry : screen.entries) {
            if (entry.editor_added) {
                continue;
            }
            entry.editor_label = EditorLabelForRetailEntry(entry);
        }
    }

    // ── the deliberate additions ─────────────────────────────────────────────
    // The retail definition declares no editor function, so a retail-only menu
    // would make Save Level, Weather, Lightmap, Logging and the rest unreachable
    // on every real install.  The editor's functions are therefore kept reachable
    // by exactly ONE explicitly-labelled entry on the retail main screen, opening
    // the editor's own screen (the row list below).  It is marked `editor_added`
    // so it can never be mistaken for retail data, and the retail entries keep
    // their order, labels and geometry: the entry is appended after them, placed
    // with retail's own centring rule (centreX=1) and the retail frame's own
    // Y-Spacing, and nothing retail-declared is moved or edited.
    int main_screen = -1;
    for (size_t i = 0; i < model.screens.size(); ++i) {
        if (model.screens[i].id == model.declared_screen_id) {
            main_screen = static_cast<int>(i);
            break;
        }
    }

    // The editor's own screen, plus the rows that are the live menu's write side
    // (issue #74): edit the loaded tree and save it back.  They exist only when a
    // retail definition was loaded -- with no definition there is nothing to edit
    // or save, which is why the fallback proper has none of them.
    //
    // There is deliberately NO row that opens the loaded retail screens.  The
    // `Game Menu` row used to push the definition's declared screen (900) and draw
    // it; that screen is the GAME's menu, and the user's requirement is that the
    // pause menu shows the editor's own text only ("it has text from game igi.exe
    // which i dont want i want editor only").  Even with every retail row relabelled
    // by `EditorLabelForRetailEntry`, opening the game's menu is what the user
    // objected to, so the row is gone: no retail screen is reachable from the
    // editor's pause menu, and `MenuUiAction::OpenGameMenu` is refused in
    // `App::ActivateFocusedPauseMenuRow` (source/app_pause_menu.cpp).  The loaded
    // definition's screens stay in the model as data -- the `Menu Text` edit and
    // `Save Menu` below operate on it -- but nothing draws them.
    // The user's requirement: "in the pause menu theres in the last two sections there
    // are two options which you need to remove after the quit so the last option should
    // be quit ... you need to remove them".  The `Menu Text` and `Save Menu` rows that
    // used to follow `Quit` are therefore GONE, so `Quit` is the last row.  They were
    // the editor's QVM write side (cycle the definition's title / save the definition
    // back to ingamemenu.qvm); that capability is no longer reachable from the pause
    // menu, which is the trade the requirement asks for.  Nothing else about the
    // definition changed: it is still loaded, still on the model as data, and still
    // unwritten by simply opening the menu.
    const std::vector<EditorAddedRow> editor_added_rows = {};
    MenuUiModel editor_screen =
        BuildEditorFallbackMenu(menu.terrain_expanded, options, editor_added_rows);
    if (editor_screen.ok && !editor_screen.screens.empty()) {
        MenuUiScreen options_screen = std::move(editor_screen.screens.front());
        options_screen.id = kEditorOptionsScreenId;
        options_screen.type = "EditorOptionsScreen";
        options_screen.name = kEditorOptionsLabel;
        options_screen.popup = false;
        options_screen.editor_added = true;
        model.screens.push_back(std::move(options_screen));

        if (main_screen >= 0) {
            MenuUiScreen& main = model.screens[static_cast<size_t>(main_screen)];
            MenuUiEntry entry;
            entry.widget_type = "EditorRow";
            entry.widget_name = "editor-options";
            entry.task_id = -1;
            entry.label = kEditorOptionsLabel;
            entry.item_label = kEditorOptionsLabel;
            // The editor's own label, not a retail key: there is no archive entry to
            // resolve, and pushing it through the lookup would yield "RM: Editor Options".
            entry.display_label = kEditorOptionsLabel;
            entry.editor_label = kEditorOptionsLabel;
            entry.has_text = true;
            entry.selectable = true;
            entry.item_candidate = true;
            entry.collected = true;
            entry.editor_added = true;
            entry.enabled = true;
            entry.visible = true;
            entry.editor_action = MenuUiAction::OpenEditorOptions;
            entry.inert = false;
            entry.colour_index = 1;

            const int label_w = options.metrics.text_width
                                    ? options.metrics.text_width(entry.label)
                                    : static_cast<int>(entry.label.size()) * 7;
            const int line_h = options.metrics.line_height;
            entry.rect.w = label_w;
            entry.rect.h = line_h;
            // Below the last retail item, using the frame's declared Y-Spacing.
            if (!main.selectable.empty()) {
                const MenuUiEntry& last = main.entries[static_cast<size_t>(main.selectable.back())];
                entry.rect.y = last.rect.y + last.rect.h + 4;
            } else {
                entry.rect.y = main.frame.y + 4;
            }
            entry.rect.x = ((main.frame.w - entry.rect.w) >> 1) + main.frame.x;

            main.selectable.push_back(static_cast<int>(main.entries.size()));
            main.entries.push_back(std::move(entry));
            model.warnings.push_back(
                std::string("editor-added entry \"") + kEditorOptionsLabel +
                "\" appended to retail screen " + std::to_string(main.id) +
                " (opens screen " + std::to_string(kEditorOptionsScreenId) + ")");
        } else {
            model.warnings.push_back(
                "editor options screen added but no retail main screen to hang its entry on");
        }
    }

    // ── label text, from the game's language .RES archives ───────────────────
    // A retail item's `Text Resource` is a KEY, not the string the game draws.
    // The engine resolves it through `MenuManager_GetString` (HOOK 0x418EA0,
    // src/menu/menumanager.c:832-852) against "LOCAL:<LanguageName>/<key>", and the
    // English archive answers e.g. "Restart Mission" for the key "Restart Game".
    // Resolving here is what makes the retail menu readable rather than a list of
    // script keys.  It is deliberately additive: `label`/`item_label` keep the
    // verbatim key and the resolved text goes in `display_label`, so no existing
    // consumer's meaning changes.
    //
    // A missing install is not an error: with no archive every label falls back to
    // `NoLanguageData` (the raw key), which is exactly what the editor drew before,
    // and `label_stats.language_data_loaded` records that it happened.
    {
        menu_text::LanguageResources resources;
        std::string res_error;
        if (resources.AddLanguageDirectory(menu.game_root, resources.language_name(), res_error)) {
            menu.language = resources.language_name();
            menu.label_stats.language_data_loaded = true;
            menu_text::LookupCounters counters;

            for (MenuUiScreen& screen : model.screens) {
                for (MenuUiEntry& entry : screen.entries) {
                    // Containers and the editor-added entry carry no `Text Resource`.
                    // The editor's own rows are already display text and must NOT be
                    // pushed through the archive, or "Save Level" would be replaced by
                    // the engine's "RM: Save Level" miss string.
                    if (!entry.has_text || entry.label.empty() || entry.editor_added) {
                        continue;
                    }
                    const menu_text::TextResult r = resources.Lookup(entry.label);
                    entry.label_key = entry.label;
                    entry.display_label = r.text;
                    entry.label_resolved = r.resolved();
                    counters.Add(r);
                    ++menu.label_stats.labels;
                }
            }

            menu.label_stats.resolved = counters.resolved();
            menu.label_stats.engine_path = counters.engine_path;
            menu.label_stats.final_component = counters.final_component;
            menu.label_stats.engine_miss = counters.engine_miss;
            menu.label_stats.no_language_data = counters.no_language_data;

            if (menu.label_stats.engine_miss > 0) {
                model.warnings.push_back(
                    "language archive has no entry for " +
                    std::to_string(menu.label_stats.engine_miss) +
                    " retail label(s); the engine draws its \"RM: <key>\" miss string");
            }
        } else {
            model.warnings.push_back(
                "no language archive under " + menu.game_root + "/LANGUAGE (" +
                (res_error.empty() ? std::string("not found") : res_error) +
                "); retail labels stay as their script keys");
        }
    }

    menu.model = std::move(model);
    menu.retail = true;
    menu.fallback = false;
    menu.reason.clear();
    menu.model.fallback_reason.clear();
}

bool RebuildEditorMenuModel(EditorMenu& menu, const MenuUiBuildOptions& options) {
    // Where the user is, so a rebuild caused by a row's own action does not move
    // the menu (the terrain toggle and the font toggle both rebuild).
    int current_screen_id = 0;
    int current_item = menu.model.item_index;
    std::vector<int> screen_stack = menu.model.screen_stack;
    if (const MenuUiScreen* current = menu.model.CurrentScreen()) {
        current_screen_id = current->id;
    }

    const MenuUiModel previous = menu.model;
    BuildModelInto(menu, options);
    if (!menu.model.ok) {
        menu.model = previous;
        return false;
    }

    // `MenuManager_RequestScreen` / `MenuManager_PushScreen` address a screen by
    // its task id (src/game/menumanager.c:216-235); the editor's own fallback
    // screen carries id 0, which is not addressable and is the only screen there.
    if (current_screen_id != 0) {
        menu.model.OpenScreen(current_screen_id);
    }
    const int count = menu.model.SelectableCount();
    if (count == 0) {
        menu.model.item_index = -1;
    } else {
        menu.model.item_index = std::max(0, std::min(current_item, count - 1));
    }
    menu.model.screen_stack = std::move(screen_stack);
    return true;
}

bool OpenEditorPauseMenu(EditorMenu& menu) {
    // A retail definition's model carries the editor's own screen under
    // `kEditorOptionsScreenId`; the fallback model's only screen is already the
    // editor's row list (id 0, which `MenuUiModel::OpenScreen` refuses by design).
    if (menu.model.FindScreen(kEditorOptionsScreenId) == nullptr) {
        return menu.model.ok;
    }
    return menu.model.OpenScreen(kEditorOptionsScreenId);
}

namespace {

// The first plain `MenuText` with a `Text Resource` value, depth first.  On the
// retail in-game menu's main screen that is the title the engine draws inside the
// title `MenuFrame` (`MAKE_LABEL`, src/menuscreen/menutext.c:126-149).
MenuWidget* FirstTextWidget(MenuWidget& widget) {    if (widget.type == "MenuText" && widget.FindValue(menu_field::kTextResource) != nullptr) {
        return &widget;
    }
    for (MenuWidget& child : widget.children) {
        if (MenuWidget* found = FirstTextWidget(child)) {
            return found;
        }
    }
    return nullptr;
}

}  // namespace

bool EditEditorMenuText(EditorMenu& menu, const MenuUiBuildOptions& options,
                        std::string* error) {
    if (!menu.definition.ok) {
        if (error != nullptr) {
            *error = "the menu was not built from a retail container (" +
                     (menu.reason.empty() ? std::string("no definition") : menu.reason) + ")";
        }
        return false;
    }

    // The screen the manager opens: the script's own "Current menuscreen", which
    // `MenuManager_New` HOOK 0x418B00 opens (src/menu/menumanager.c:713-790).
    MenuWidget* screen = nullptr;
    if (menu.model.declared_screen_id != 0) {
        screen = menu.definition.FindById(menu.model.declared_screen_id);
    }
    if (screen == nullptr) {
        screen = menu.definition.FindByType("MenuScreen");
    }
    if (screen == nullptr) {
        if (error != nullptr) {
            *error = "the retail definition declares no MenuScreen to edit";
        }
        return false;
    }

    MenuWidget* text = FirstTextWidget(*screen);
    if (text == nullptr) {
        if (error != nullptr) {
            *error = "the retail main screen declares no MenuText to edit";
        }
        return false;
    }
    MenuValue* value = text->FindValue(menu_field::kTextResource);
    if (value == nullptr || value->kind != MenuValueKind::String) {
        if (error != nullptr) {
            *error = "the retail main screen's text widget has no string Text Resource";
        }
        return false;
    }

    // The next string of the editor's own cycle.  The current value is looked up
    // first, so the edit cycles the editor's strings and never guesses at what the
    // game's text should become.
    const char* next = kEditorMenuTitleCycle[0];
    for (int i = 0; i < kEditorMenuTitleCycleCount; ++i) {
        if (value->text == kEditorMenuTitleCycle[i]) {
            next = kEditorMenuTitleCycle[(i + 1) % kEditorMenuTitleCycleCount];
            break;
        }
    }
    value->text = next;

    if (!RebuildEditorMenuModel(menu, options)) {
        if (error != nullptr) {
            *error = "the edited definition could not be built into a menu model";
        }
        return false;
    }
    return true;
}

EditorMenu LoadEditorMenu(const std::string& game_root, bool terrain_expanded,
                          const MenuUiBuildOptions& options) {
    EditorMenu result;
    result.game_root = game_root;
    result.terrain_expanded = terrain_expanded;
    const std::string path = game_root + "/" + kRetailMenuRelativePath;

    if (game_root.empty() || !IsReadableFile(path)) {
        result.reason = "retail menu file not found: " + path;
        BuildModelInto(result, options);
        return result;
    }

    // One reader, the supported path (source/menu/qvm_image.h).  A second, more
    // forgiving reader here would mask a parser regression instead of surfacing it,
    // so a failure falls through to the documented fallback and the error is kept.
    MenuDefinition definition = MenuDefinition::FromQvmFile(path);
    result.reader = "MenuDefinition::FromQvmFile";

    if (!definition.ok) {
        result.reason = "retail menu file could not be read (" + path + "): " + definition.error;
        BuildModelInto(result, options);
        return result;
    }

    // Keep the definition the model was built from so the menu can be edited and
    // saved back through `menu/menu_qvm_writer.h`.  A fallback never reaches here,
    // so `definition.ok` is true exactly when the retail file was read -- and the
    // writer refuses a definition that is not ok, so a fallback menu can never
    // overwrite the retail file.
    result.definition = std::move(definition);
    BuildModelInto(result, options);
    return result;
}

bool SaveEditorMenu(const EditorMenu& menu, const std::string& path, std::string* error) {
    // A fallback menu has no definition (`definition.ok` is false exactly then), so
    // this refuses before the writer is reached: the retail file cannot be replaced
    // by the editor's own row list.
    if (!menu.definition.ok) {
        if (error != nullptr) {
            *error = "the menu was not built from a retail container (" +
                     (menu.reason.empty() ? std::string("no definition") : menu.reason) + ")";
        }
        return false;
    }

    const std::string target = path.empty() ? menu.definition.source : path;
    if (target.empty()) {
        if (error != nullptr) {
            *error = "no path to save the menu to, and the definition has no source file";
        }
        return false;
    }

    const MenuQvmDocument document = MenuQvmDocumentFromDefinition(menu.definition);
    if (!document.ok) {
        if (error != nullptr) {
            *error = document.error;
        }
        return false;
    }
    return MenuQvmWriteFile(document, target, error);
}

}  // namespace app_menu
}  // namespace igi
