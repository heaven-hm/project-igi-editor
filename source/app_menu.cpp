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

#include <cctype>
#include <fstream>
#include <string>
#include <vector>

#include "menu/qvm_image.h"
#include "menu/menu_text_res.h"
#include "runtime/pause_menu_layout.h"

namespace igi {
namespace app_menu {
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
    {"Lightmap", MenuUiAction::ToggleLightmaps},
    {"Calculate Lightmaps", MenuUiAction::CalculateLightmaps},
    {"Terrain Options", MenuUiAction::ToggleTerrainOptions},
    {"Reset Level", MenuUiAction::ResetLevel},
    {"Save Level", MenuUiAction::SaveLevel},
    {"Quit", MenuUiAction::Quit},
};

constexpr size_t kFallbackTerrainHeaderRow = 12;  // index of "Terrain Options"

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

}  // namespace

int EditorFallbackRowCount(bool terrain_expanded) {
    return static_cast<int>(FallbackRowsFor(terrain_expanded).size());
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

MenuUiModel BuildEditorFallbackMenu(bool terrain_expanded,
                                    const MenuUiBuildOptions& options) {
    MenuUiModel model;
    model.viewport_width = options.viewport_width;
    model.viewport_height = options.viewport_height;
    model.max_items = options.max_items > 0 ? options.max_items : 32;
    model.fallback = true;
    model.source = "fallback:editor-rows";
    model.fallback_reason =
        "the retail menu definition was not used; the editor's own row list is shown";

    const PauseMenuLayout layout = BuildPauseMenuLayout(
        options.viewport_width, options.viewport_height, terrain_expanded,
        options.metrics.line_height);

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
        const int centre = PauseMenuRowCenter(layout, static_cast<int>(i));
        // The legacy hit test is inclusive (IsPauseMenuRowHit: centre - radius <= y
        // <= centre + radius); the model uses retail's strict comparison
        // (MenuWindow_HitTest: y0 < y < y0 + h), so the box is grown by one pixel
        // on each side to keep the clickable band identical.
        const MenuUiRect viewport_rect{layout.menu_left - 1, centre - layout.row_hit_radius - 1,
                                       layout.menu_width + 2,
                                       2 * layout.row_hit_radius + 2};

        MenuUiEntry entry;
        entry.widget_type = "EditorRow";
        entry.widget_name = rows[i].label;
        entry.task_id = -1;
        entry.label = rows[i].label;
        entry.item_label = rows[i].label;
        // These rows are the editor's own, so they are already display text: there is
        // no `Text Resource` key to resolve and nothing to look up in an archive.
        entry.display_label = rows[i].label;
        entry.rect = MenuUiToRetail(viewport_rect, options.viewport_width,
                                    options.viewport_height);
        entry.has_text = true;
        entry.selectable = true;
        entry.item_candidate = true;
        entry.collected = true;
        entry.editor_added = true;
        entry.enabled = true;
        entry.visible = true;
        entry.editor_action = rows[i].action;
        entry.inert = false;
        entry.colour_index = 1;  // TEXT_WHITE, src/menuscreen/menutext.c:302-305
        screen.selectable.push_back(static_cast<int>(i));
        screen.entries.push_back(std::move(entry));
    }

    model.screens.push_back(std::move(screen));
    model.screen_index = 0;
    model.item_index = model.screens[0].selectable.empty() ? -1 : 0;
    model.ok = true;
    return model;
}

EditorMenu LoadEditorMenu(const std::string& game_root, bool terrain_expanded,
                          const MenuUiBuildOptions& options) {
    EditorMenu result;
    const std::string path = game_root + "/" + kRetailMenuRelativePath;

    if (game_root.empty() || !IsReadableFile(path)) {
        result.model = BuildEditorFallbackMenu(terrain_expanded, options);
        result.fallback = true;
        result.reason = "retail menu file not found: " + path;
        result.model.fallback_reason = result.reason;
        return result;
    }

    // One reader, the supported path (source/menu/qvm_image.h).  A second, more
    // forgiving reader here would mask a parser regression instead of surfacing it,
    // so a failure falls through to the documented fallback and the error is kept.
    MenuDefinition definition = MenuDefinition::FromQvmFile(path);
    result.reader = "MenuDefinition::FromQvmFile";

    if (!definition.ok) {
        result.model = BuildEditorFallbackMenu(terrain_expanded, options);
        result.fallback = true;
        result.reason = "retail menu file could not be read (" + path + "): " + definition.error;
        result.model.fallback_reason = result.reason;
        return result;
    }

    MenuUiModel model = BuildMenuUiModel(definition, options);
    if (!model.ok) {
        result.model = BuildEditorFallbackMenu(terrain_expanded, options);
        result.fallback = true;
        result.reason =
            "retail menu definition has no usable screen (" + path + "): " + model.error;
        result.model.fallback_reason = result.reason;
        return result;
    }

    // Keep the definition the model was built from so the menu can be edited and
    // saved back through `menu/menu_qvm_writer.h`.  A fallback never reaches here,
    // so `definition.ok` is true exactly when the retail file was read -- and the
    // writer refuses a definition that is not ok, so a fallback menu can never
    // overwrite the retail file.
    result.definition = definition;

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

    // ── the one deliberate addition ──────────────────────────────────────────
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
    MenuUiModel editor_screen = BuildEditorFallbackMenu(terrain_expanded, options);
    if (editor_screen.ok && !editor_screen.screens.empty()) {
        MenuUiScreen options_screen = std::move(editor_screen.screens.front());
        options_screen.id = kEditorOptionsScreenId;
        options_screen.type = "EditorOptionsScreen";
        options_screen.name = kEditorOptionsLabel;
        options_screen.popup = false;
        options_screen.editor_added = true;
        const int options_index = static_cast<int>(model.screens.size());
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
        if (resources.AddLanguageDirectory(game_root, resources.language_name(), res_error)) {
            result.language = resources.language_name();
            result.label_stats.language_data_loaded = true;
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
                    ++result.label_stats.labels;
                }
            }

            result.label_stats.resolved = counters.resolved();
            result.label_stats.engine_path = counters.engine_path;
            result.label_stats.final_component = counters.final_component;
            result.label_stats.engine_miss = counters.engine_miss;
            result.label_stats.no_language_data = counters.no_language_data;

            if (result.label_stats.engine_miss > 0) {
                model.warnings.push_back(
                    "language archive has no entry for " +
                    std::to_string(result.label_stats.engine_miss) +
                    " retail label(s); the engine draws its \"RM: <key>\" miss string");
            }
        } else {
            model.warnings.push_back(
                "no language archive under " + game_root + "/LANGUAGE (" +
                (res_error.empty() ? std::string("not found") : res_error) +
                "); retail labels stay as their script keys");
        }
    }

    result.model = std::move(model);
    result.retail = true;
    result.fallback = false;
    return result;
}

}  // namespace app_menu
}  // namespace igi
