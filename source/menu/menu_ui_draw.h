#pragma once

// Drawing for the in-app menu model (source/menu/menu_ui.h).
//
// This is the editor-side half of task-14's split: `menu_ui.*` is pure data and
// navigation, this file is the OpenGL presentation.  It draws with the primitives
// the pause menu already uses (`glColor4f` quads, line loops and bitmap text) and
// reuses the fonts the pause menu already loads
// (source/runtime/pause_menu_font.h), so the menu keeps the look it has.
//
// The retail focus rule is the retail one: `MenuWindow_DrawEnd` HOOK 0x424980
// (src/widget/menuwindow.c:366+) draws a focused window's outline with
// r = b = 0.5, g = 1.0 and an unfocused one with r = b = 0, g = 1.
//
// Editor-only: no test links this file.

#include <string>

#include "menu/menu_ui.h"

namespace igi {

// Text metrics that match the fonts this file draws with, so a model built with
// them places text exactly where it will be rendered.
MenuUiMetrics MenuUiMetricsForFonts(bool use_editor_font, int system_font_size);

struct MenuUiDrawStyle {
    bool use_editor_font = false;
    int system_font_size = 12;
    int viewport_width = 640;
    int viewport_height = 480;
    // Editor furniture.  When `title` is set the header the editor's pause menu has
    // always shown is drawn ("IGI EDITOR" over "PAUSED", plus its separator line).
    // The retail definition declares no title, so pass nullptr for the retail menu.
    const char* title = nullptr;
    const char* subtitle = nullptr;
};

// Uploads the font atlas this file draws with.  Cheap after the first call;
// requires a current GL context.
void MenuUiDrawPrepareFonts(bool use_editor_font);

// Draws one screen of `model`: the panel for `screen.frame`, then its entries in
// arrangement order (containers are skipped: retail draws their sprite resources,
// which the editor does not resolve).
void DrawMenuUiScreen(const MenuUiModel& model, const MenuUiScreen& screen,
                      const MenuUiDrawStyle& style);

// Draws the model's current screen.
void DrawMenuUi(const MenuUiModel& model, const MenuUiDrawStyle& style);

}  // namespace igi
