#pragma once

/******************************************************************************
 * @file    source/runtime/pause_menu_rows.h
 * @brief   The live text and the spinner geometry of the editor's own
 *          pause-menu rows, from the row's editor action.
 *
 * The live pause menu's rows come from the loaded menu tree
 * (`igi::MenuUiModel`, source/menu/menu_ui.h): each row carries the editor
 * behaviour it is wired to (`MenuUiEntry::editor_action`) and the row's
 * state-INDEPENDENT base name.  This header is the one place that turns that
 * action into the text the menu draws with its live state appended ("Mode:
 * [Editor]", "Weather: [Rain]", "[X] Music", ...) and into the width of a
 * spinner row's value box.
 *
 * It exists because the DRAW layer (source/renderer/renderer_draw.cpp) and the
 * CLICK test (source/app_pause_menu.cpp) must agree exactly: the spinner group is
 * laid out from the rendered label's width, so a row that is drawn with one label
 * and hit-tested with another would put its [-] and [+] buttons somewhere other
 * than where they are drawn.  Both take the label and the value width from here.
 *
 * Nothing here is a game string: every label is one the editor has always drawn.
 * The retail rows of the loaded definition are drawn from the definition itself
 * (`MenuUiEntry::label` / `display_label`), which is why `MenuUiAction::None`
 * returns an empty string.
 *****************************************************************************/

#include <string>

#include "menu/menu_ui.h"
#include "runtime/log_policy.h"

namespace igi {

// The live state the editor's rows render.  Only the fields the labels need, so
// the mapping is pure and can be tested without the application.
struct PauseMenuRowState {
    bool in_game_mode = false;
    bool noclip = true;
    bool use_editor_font = false;
    bool auto_save_enabled = false;
    bool logging_enabled = false;
    int log_level = kLogLevelInfo;
    bool music_on = false;
    int weather_mode = 0;  // Config::weatherMode: 0 default, 1 rain, 2 snow, 3 off
    std::string lightmap_mode = "Default";
    bool terrain_expanded = false;
    bool terrain_texture = false;
    bool terrain_height = false;
    bool terrain_discard = false;
    bool terrain_fog = false;
};

// The label the live menu draws for a row, keyed on the editor behaviour the row
// is wired to.  Empty for a retail row (`MenuUiAction::None`) and for a row whose
// label the definition owns.
std::string PauseMenuRowLabel(MenuUiAction action, const PauseMenuRowState& state);

// The value-box width of the row's [-] [value] [+] group, or 0 when the row is
// not a spinner.  The widths are the editor's own (the same ones the pause menu
// has always used).
int PauseMenuSpinnerValueWidth(MenuUiAction action);

// True when the row draws a spinner group.
bool PauseMenuRowHasSpinner(MenuUiAction action);

// The log-level name the spinner's value box shows, the same mapping
// `igi::LogLevelLabel` (runtime/log_policy.h) gives.
const char* PauseMenuLogLevelName(int level);

}  // namespace igi
