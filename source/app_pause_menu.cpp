/******************************************************************************
 * @file    source/app_pause_menu.cpp
 * @brief   App: the live pause menu, driven by the loaded menu tree (#74).
 *
 * The pause menu's rows are NOT a hard-coded list.  The tree is
 * `igi::app_menu::EditorMenu` (source/menu/menu_ui.h), built from
 * `<IGI>/menusystem/ingamemenu.qvm` -- the compiled `ingamemenu.qsc` the engine's
 * own in-game menu is:
 *
 *   src/levelflow/levelflow.c:139
 *       lf->menu = MenuManager_New(*(void **)(Game_Get() + 0x44),
 *                                  "LOCAL:menusystem/ingamemenu.qsc", 0, 0, 0);
 *   `MenuManager_New` HOOK 0x418B00 (src/menu/menumanager.c:713-790) loads the
 *   script, runs it with `Task_New` bound to the parent task and finds the
 *   `MenuManager` it created; `MenuManager_OpenScreen` HOOK 0x418710
 *   (src/menu/menumanager.c:547-581) then opens the script's "Current menuscreen".
 *
 * What this file implements, and where each rule comes from:
 *
 *   * hit test      `MenuWindow_HitTest` HOOK 0x4248A0 (src/widget/menuwindow.c:335-360),
 *                   on the pointer the manager converts into the 640x480 menu space
 *                   (src/menu/menumanager.c:390-392).  The model does both
 *                   (`MenuUiModel::HitTest`).
 *   * focus         `MenuManager_SetFocus` HOOK 0x4186E0 (src/menu/menumanager.c:537-562):
 *                   only a collected item can take focus, so a click on a frame or a
 *                   label inside it activates nothing.
 *   * activation    `RUN_ACTION` in src/menuscreen/menutext.c (`MenuText_Run`
 *                   HOOK 0x421070, `MenuTextConditional_Run` HOOK 0x4207E0): the
 *                   clicked/ENTERed item runs its action.  The model reports the item
 *                   and the editor behaviour it is wired to; this file runs that.
 *   * navigation    `MenuManager_ReadInput` HOOK 0x418580 (src/menu/menumanager.c:479-551):
 *                   KEY_DOWN (-4) advances with wraparound to 0, KEY_UP (-3) steps
 *                   back with wraparound to itemCount-1, KEY_ENTER (-7) goes to the
 *                   focused window, KEY_ESCAPE (-8) is the "back" flag.
 *   * screens       `MenuManager_PushScreen` HOOK 0x417690 / `MenuManager_PopScreen`
 *                   HOOK 0x417710 (src/game/menumanager.c:226-247): a stack of 16
 *                   screen ids (`screenStack[16]`, src/menu/menutypes.h:76).  Escape
 *                   pops while the stack is not empty; otherwise the menu closes,
 *                   which is what screen 900's own `On Press Escape`
 *                   (`MenuManager_LeaveMenus(900, MENUMANAGER_IDLE)`,
 *                   HOOK 0x417790, src/game/menumanager.c:249-261) does in retail.
 *
 * The spinner rows (font size, level, auto-save, log level, fog intensity) are the
 * editor's own furniture, not menu items: their [-] / value / [+] sub-rectangles
 * are tested before the row's activation, with the same geometry the draw layer
 * uses (`igi::BuildPauseMenuSpinner`, source/runtime/pause_menu_layout.h, from the
 * label `runtime/pause_menu_rows.h` gives).
 *****************************************************************************/

#include "app_internal.h"

#include "menu/menu_ui_draw.h"
#include "renderer/object_lightmap.h"
#include "runtime/log_policy.h"
#include "runtime/pause_menu_font.h"
#include "runtime/pause_menu_rows.h"
#include "runtime/pause_menu_state.h"

namespace {

constexpr int kSpinnerButtonWidth = 22;

}  // namespace

igi::MenuUiBuildOptions App::PauseMenuBuildOptions() const {
	igi::MenuUiBuildOptions options;
	options.viewport_width = window_state_.viewport_width_;
	options.viewport_height = window_state_.viewport_height_;
	// The model sizes a text window from its rendered label (`MAKE_LABEL`,
	// src/menuscreen/menutext.c:126-149), so it must measure text exactly the way
	// the draw layer does.
	options.metrics = igi::MenuUiMetricsForFonts(Config::Get().useEditorFont,
	                                             Config::Get().systemFontSize);
	return options;
}

int App::PauseMenuExtraRows() const {
	// The write-side rows exist only when a retail definition was loaded
	// (source/app_menu.cpp), and the panel grows by them.
	return pause_menu_.retail ? igi::app_menu::kEditorAddedRowCount : 0;
}

igi::PauseMenuLayout App::PauseMenuLayoutNow() const {
	const PauseMenuFontMetrics metrics = GetPauseMenuFontMetrics(
		Config::Get().useEditorFont, Config::Get().systemFontSize);
	return igi::BuildPauseMenuLayout(window_state_.viewport_width_,
	                                 window_state_.viewport_height_,
	                                 pause_terrain_expanded_, metrics.lineHeight,
	                                 PauseMenuExtraRows());
}

igi::PauseMenuRowState App::PauseMenuRowStateNow() const {
	// The live state the editor's own rows render (source/runtime/pause_menu_rows.h):
	// the same state the pause menu has always appended to its row names.
	return igi::PauseMenuRowState{
		.in_game_mode = in_game_mode_,
		.noclip = noclip_mode_,
		.use_editor_font = Config::Get().useEditorFont,
		.auto_save_enabled = auto_save_enabled_,
		.logging_enabled = Config::Get().enableLogging,
		.log_level = Config::Get().logLevelThreshold,
		.music_on = music_playing_,
		.weather_mode = Config::Get().weatherMode,
		.lightmap_mode = igi::ObjectLightmapManager::Get().GetRenderModeName(),
		.terrain_expanded = pause_terrain_expanded_,
		.terrain_texture = (terrain_mod_options_ & TERRAIN_TEXTURE_MOD) != 0,
		.terrain_height = (terrain_mod_options_ & TERRAIN_HEIGHT_MOD) != 0,
		.terrain_discard = (terrain_mod_options_ & TERRAIN_DISCARD_MOD) != 0,
		.terrain_fog = (GetTerrainDrawOptions() & Renderer_Terrain::DRAW_TERRAIN_OPT_FOG) != 0,
	};
}

void App::EnsurePauseMenu() {
	if (pause_menu_loaded_) {
		return;
	}
	pause_menu_ = igi::app_menu::LoadEditorMenu(Utils::GetIGIRootPath(),
	                                            pause_terrain_expanded_,
	                                            PauseMenuBuildOptions());
	pause_menu_loaded_ = true;
	// The live menu's provenance, so a run's log says which tree the rows came
	// from (the retail container or the documented fallback) and how many rows it
	// has.  This is the line that proves the live UI path, not a test, built the
	// model.
	if (pause_menu_.retail) {
		const igi::MenuUiScreen* screen = pause_menu_.model.CurrentScreen();
		Logger::Get().Log(LogLevel::INFO,
		                  "pause menu: loaded " + pause_menu_.model.source + " (" +
		                      std::to_string(pause_menu_.model.screens.size()) +
		                      " screens, screen " +
		                      std::to_string(screen != nullptr ? screen->id : 0) + " has " +
		                      std::to_string(pause_menu_.model.SelectableCount()) +
		                      " rows, editor screen " +
		                      std::to_string(pause_menu_.model.FindScreen(
		                                         igi::app_menu::kEditorOptionsScreenId) != nullptr
		                                         ? igi::app_menu::kEditorOptionsScreenId
		                                         : 0) +
		                      ")");
		// The panel and row geometry of the current viewport, so a smoke run (or a
		// user report) says exactly where each row is without a screenshot.
		const igi::PauseMenuLayout layout = PauseMenuLayoutNow();
		Logger::Get().Log(LogLevel::INFO,
		                  "pause menu: viewport " +
		                      std::to_string(window_state_.viewport_width_) + "x" +
		                      std::to_string(window_state_.viewport_height_) + " panel " +
		                      std::to_string(layout.menu_left) + "," +
		                      std::to_string(layout.menu_top) + " " +
		                      std::to_string(layout.menu_width) + "x" +
		                      std::to_string(layout.menu_height) + " rows " +
		                      std::to_string(layout.row_count) + " pitch " +
		                      std::to_string(layout.row_height) + " first row y " +
		                      std::to_string(igi::PauseMenuRowCenter(layout, 0)) +
		                      " scale " + std::to_string(layout.text_scale));
	} else {
		Logger::Get().Log(LogLevel::WARNING,
		                  std::string("pause menu: fallback rows (") + pause_menu_.reason + ")");
	}
}

void App::RebuildPauseMenu() {
	if (!pause_menu_loaded_) {
		EnsurePauseMenu();
		return;
	}
	pause_menu_.terrain_expanded = pause_terrain_expanded_;
	if (!igi::app_menu::RebuildEditorMenuModel(pause_menu_, PauseMenuBuildOptions())) {
		Logger::Get().Log(LogLevel::WARNING, "pause menu: the model could not be rebuilt");
	}
}

bool App::HandlePauseMenuSpinner(const igi::MenuUiEntry& row, int x) {
	const int value_width = igi::PauseMenuSpinnerValueWidth(row.editor_action);
	if (value_width <= 0) {
		return false;
	}
	// The row's own y, from the model's rect (the draw layer draws at the same y).
	const igi::MenuUiRect rect = pause_menu_.model.ToViewport(row.rect);
	const int row_y = rect.y + rect.h / 2;
	const igi::PauseMenuLayout layout = PauseMenuLayoutNow();
	const std::string label =
		igi::PauseMenuRowLabel(row.editor_action, PauseMenuRowStateNow());
	const int label_width = PauseMenuTextWidth(label.c_str(), Config::Get().useEditorFont,
	                                           Config::Get().systemFontSize,
	                                           layout.text_scale);
	const igi::PauseMenuSpinner spinner = igi::BuildPauseMenuSpinner(
		layout.menu_left, layout.menu_width, row_y, layout.row_hit_radius, label_width,
		value_width);
	if (!igi::IsPauseMenuSpinnerHit(spinner, x)) {
		return false;
	}

	const int minus_x = spinner.minus_left;
	const int plus_x = spinner.plus_left;
	switch (row.editor_action) {
	case igi::MenuUiAction::ToggleFont: {
		int& size = Config::Get().systemFontSize;
		if (x >= minus_x && x < minus_x + kSpinnerButtonWidth) {
			size = (std::max)(8, size - 1);
			Config::Save();
		} else if (x >= plus_x && x < plus_x + kSpinnerButtonWidth) {
			size = (std::min)(32, size + 1);
			Config::Save();
		} else {
			// The label half toggles the font itself, as it always has.
			Config::Get().useEditorFont = !Config::Get().useEditorFont;
			Config::Save();
		}
		// The metrics changed, so the model's text windows are rebuilt.
		RebuildPauseMenu();
		return true;
	}
	case igi::MenuUiAction::SelectLevel: {
		int current = pause_level_input_.empty() ? 1 : std::atoi(pause_level_input_.c_str());
		if (x >= minus_x && x < minus_x + kSpinnerButtonWidth) {
			current = current > 1 ? current - 1 : 1;
			pause_level_input_ = std::to_string(current);
		} else if (x >= plus_x && x < plus_x + kSpinnerButtonWidth) {
			current = current < 14 ? current + 1 : 14;
			pause_level_input_ = std::to_string(current);
		}
		pause_active_input_ = igi::kPauseMenuLevelRow;
		return true;
	}
	case igi::MenuUiAction::ToggleAutoSave:
		if (x >= minus_x && x < minus_x + kSpinnerButtonWidth) {
			AdjustAutoSaveInterval(-10);
		} else if (x >= plus_x && x < plus_x + kSpinnerButtonWidth) {
			AdjustAutoSaveInterval(10);
		} else {
			ToggleAutoSave();
		}
		return true;
	case igi::MenuUiAction::CycleLogLevel: {
		int& level = Config::Get().logLevelThreshold;
		if (x >= minus_x && x < minus_x + kSpinnerButtonWidth) {
			level = (std::max)(igi::kLogLevelDebug, level - 1);
		} else if (x >= plus_x && x < plus_x + kSpinnerButtonWidth) {
			level = (std::min)(igi::kLogLevelFatal, level + 1);
		} else {
			level = igi::ClampLogLevel(level);
		}
		Config::Get().debugLogging = level == igi::kLogLevelDebug;
		Config::Save();
		status_message_ = std::string("Log level: ") + igi::LogLevelLabel(level);
		return true;
	}
	case igi::MenuUiAction::AdjustFogIntensity: {
		int& intensity = Config::Get().fogIntensity;
		if (x >= minus_x && x < minus_x + kSpinnerButtonWidth) {
			intensity = (std::max)(0, intensity - 100);
			SetFogIntensity(intensity);
			Config::Save();
		} else if (x >= plus_x && x < plus_x + kSpinnerButtonWidth) {
			intensity = (std::min)(1000, intensity + 100);
			SetFogIntensity(intensity);
			Config::Save();
		}
		return true;
	}
	default:
		return false;
	}
}

bool App::WheelPauseMenuSpinner(int x, int y, int direction) {
	EnsurePauseMenu();
	igi::MenuUiModel& model = pause_menu_.model;
	const int entry = model.HitTest(x, y);
	const igi::MenuUiScreen* screen = model.CurrentScreen();
	if (entry < 0 || screen == nullptr || entry >= static_cast<int>(screen->entries.size())) {
		return false;
	}
	const igi::MenuUiEntry& row = screen->entries[static_cast<size_t>(entry)];
	if (!row.collected || !igi::PauseMenuRowHasSpinner(row.editor_action)) {
		return false;
	}
	const igi::MenuUiRect rect = model.ToViewport(row.rect);
	const int row_y = rect.y + rect.h / 2;
	const igi::PauseMenuLayout layout = PauseMenuLayoutNow();
	const std::string label =
		igi::PauseMenuRowLabel(row.editor_action, PauseMenuRowStateNow());
	const int label_width = PauseMenuTextWidth(label.c_str(), Config::Get().useEditorFont,
	                                           Config::Get().systemFontSize,
	                                           layout.text_scale);
	const igi::PauseMenuSpinner spinner = igi::BuildPauseMenuSpinner(
		layout.menu_left, layout.menu_width, row_y, layout.row_hit_radius, label_width,
		igi::PauseMenuSpinnerValueWidth(row.editor_action));
	if (!igi::IsPauseMenuSpinnerHit(spinner, x)) {
		return false;
	}

	switch (row.editor_action) {
	case igi::MenuUiAction::ToggleFont: {
		int& size = Config::Get().systemFontSize;
		size = igi::AdjustPauseMenuSpinner(size, direction, 8, 32, 1);
		Config::Save();
		RebuildPauseMenu();
		return true;
	}
	case igi::MenuUiAction::SelectLevel: {
		int level = pause_level_input_.empty() ? 1 : std::atoi(pause_level_input_.c_str());
		level = igi::AdjustPauseMenuSpinner(level, direction, 1, 14, 1);
		pause_level_input_ = std::to_string(level);
		return true;
	}
	case igi::MenuUiAction::ToggleAutoSave: {
		const int interval = igi::AdjustPauseMenuSpinner(
			auto_save_interval_seconds_, direction, 10, 3600, 10);
		AdjustAutoSaveInterval(interval - auto_save_interval_seconds_);
		return true;
	}
	case igi::MenuUiAction::CycleLogLevel: {
		int& level = Config::Get().logLevelThreshold;
		level = igi::AdjustPauseMenuSpinner(level, direction, igi::kLogLevelDebug,
		                                    igi::kLogLevelFatal, 1);
		Config::Get().debugLogging = level == igi::kLogLevelDebug;
		Config::Save();
		return true;
	}
	case igi::MenuUiAction::AdjustFogIntensity: {
		int& intensity = Config::Get().fogIntensity;
		intensity = igi::AdjustPauseMenuSpinner(intensity, direction, 0, 1000, 100);
		SetFogIntensity(intensity);
		Config::Save();
		return true;
	}
	default:
		return false;
	}
}

bool App::MovePauseMenuFocus(int direction) {
	if (direction > 0) {
		return pause_menu_.model.MoveDown();
	}
	if (direction < 0) {
		return pause_menu_.model.MoveUp();
	}
	return false;
}

bool App::ActivateFocusedPauseMenuRow() {
	const igi::MenuUiEntry* row = pause_menu_.model.FocusedEntry();
	if (row == nullptr) {
		return false;
	}
	const igi::MenuUiActivation activation = pause_menu_.model.Activate();
	if (!activation.hit) {
		return false;
	}
	// One line per user activation: a smoke run (or a user's report) can say which
	// row the live menu ran without a screenshot.
	Logger::Get().Log(LogLevel::INFO,
	                  std::string("pause menu: activated row '") + row->label + "' (" +
	                      igi::MenuUiActionName(activation.editor_action) + ", screen " +
	                      std::to_string(pause_menu_.model.CurrentScreen() != nullptr
	                                         ? pause_menu_.model.CurrentScreen()->id
	                                         : 0) +
	                      ")");

	switch (activation.editor_action) {
	case igi::MenuUiAction::Resume:
		TogglePauseMenu();
		return true;
	case igi::MenuUiAction::ToggleMode:
		ToggleGamePlayMode();
		TogglePauseMenu();
		return true;
	case igi::MenuUiAction::ToggleCollision:
		noclip_mode_ = !noclip_mode_;
		return true;
	case igi::MenuUiAction::SelectLevel:
		pause_active_input_ = igi::kPauseMenuLevelRow;
		return true;
	case igi::MenuUiAction::ToggleLogging:
		Config::Get().enableLogging = !Config::Get().enableLogging;
		Config::Save();
		status_message_ = Config::Get().enableLogging ? "Logging: ON" : "Logging: OFF";
		return true;
	case igi::MenuUiAction::ToggleMusic:
		ToggleMusic();
		return true;
	case igi::MenuUiAction::CycleWeather:
		CycleWeatherMode();
		return true;
	case igi::MenuUiAction::ToggleLightmaps:
		igi::ObjectLightmapManager::Get().CycleRenderMode();
		return true;
	case igi::MenuUiAction::CalculateLightmaps:
		CalculateLightmapsForAllObjects();
		return true;
	case igi::MenuUiAction::ToggleTerrainOptions:
		pause_terrain_expanded_ = !pause_terrain_expanded_;
		RebuildPauseMenu();
		return true;
	case igi::MenuUiAction::ToggleTerrainTexture:
		ToggleTerrainModOption(TERRAIN_TEXTURE_MOD);
		return true;
	case igi::MenuUiAction::ToggleTerrainHeight:
		ToggleTerrainModOption(TERRAIN_HEIGHT_MOD);
		return true;
	case igi::MenuUiAction::ToggleTerrainDiscard:
		ToggleTerrainModOption(TERRAIN_DISCARD_MOD);
		return true;
	case igi::MenuUiAction::ToggleTerrainFog: {
		ToggleTerrainDrawOption(Renderer_Terrain::DRAW_TERRAIN_OPT_FOG);
		const bool fog_on =
			(GetTerrainDrawOptions() & Renderer_Terrain::DRAW_TERRAIN_OPT_FOG) != 0;
		SetFogEnabled(fog_on);
		Config::Get().enableFog = fog_on;
		Config::Save();
		return true;
	}
	case igi::MenuUiAction::ResetLevel:
		ResetLevel();
		TogglePauseMenu();
		return true;
	case igi::MenuUiAction::SaveLevel:
		SaveCurrentLevel();
		return true;
	case igi::MenuUiAction::Quit:
		glutLeaveMainLoop();
		return true;
	case igi::MenuUiAction::OpenEditorOptions:
		// `MenuManager_PushScreen` HOOK 0x417690: the current screen id goes on the
		// stack and the requested one opens.
		pause_menu_.model.PushScreen(igi::app_menu::kEditorOptionsScreenId);
		return true;
	case igi::MenuUiAction::OpenGameMenu:
		pause_menu_.model.PushScreen(pause_menu_.model.declared_screen_id);
		return true;
	case igi::MenuUiAction::EditMenuText: {
		std::string error;
		if (igi::app_menu::EditEditorMenuText(pause_menu_, PauseMenuBuildOptions(), &error)) {
			status_message_ = "Menu text edited (not saved yet)";
		} else {
			status_message_ = "Menu text not edited: " + error;
		}
		Logger::Get().Log(LogLevel::INFO, "pause menu: " + status_message_);
		return true;
	}
	case igi::MenuUiAction::SaveMenu: {
		std::string error;
		if (igi::app_menu::SaveEditorMenu(pause_menu_, "", &error)) {
			status_message_ = "Menu saved: " + pause_menu_.definition.source;
		} else {
			status_message_ = "Menu not saved: " + error;
		}
		Logger::Get().Log(LogLevel::INFO, "pause menu: " + status_message_);
		return true;
	}
	case igi::MenuUiAction::None:
		// A retail row the editor has no behaviour for: it stays visible and
		// focusable, and says why it does nothing.
		status_message_ = row->inert_reason.empty()
			? "This menu item is not wired in the editor"
			: row->inert_reason;
		return true;
	case igi::MenuUiAction::ToggleFont:
	case igi::MenuUiAction::ToggleAutoSave:
	case igi::MenuUiAction::CycleLogLevel:
	case igi::MenuUiAction::AdjustFogIntensity:
		// Spinner rows act through their boxes, not through the row itself (the
		// keyboard has no pointer, so ENTER on them is a no-op).
		return true;
	}
	return false;
}

bool App::HandlePauseMenuClick(int x, int y) {
	EnsurePauseMenu();
	igi::MenuUiModel& model = pause_menu_.model;

	// A click outside the panel closes any focused text input and does nothing else.
	const int entry = model.HitTest(x, y);
	if (entry < 0) {
		pause_active_input_ = -1;
		return false;
	}

	const igi::MenuUiScreen* screen = model.CurrentScreen();
	if (screen == nullptr || entry >= static_cast<int>(screen->entries.size())) {
		return false;
	}
	// `MenuManager_SetFocus` HOOK 0x4186E0 only takes a collected item: a click on
	// a frame or on a label inside it focuses nothing and activates nothing.
	if (!screen->entries[static_cast<size_t>(entry)].collected) {
		pause_active_input_ = -1;
		return true;
	}
	model.FocusEntry(entry);
	// Only the row that claims the text input (the level selector) sets this; a
	// click anywhere else ends the previous row's input mode, exactly as the
	// editor's own click handling always did.
	pause_active_input_ = -1;

	const igi::MenuUiEntry* row = model.FocusedEntry();
	if (row == nullptr) {
		return false;
	}
	// The spinner boxes first: they are inside the row's own rectangle and are not
	// menu items, so the row's activation must not swallow them.
	if (HandlePauseMenuSpinner(*row, x)) {
		return true;
	}
	return ActivateFocusedPauseMenuRow();
}

void App::PauseMenuBack() {
	// `MenuManager_PopScreen` HOOK 0x417710: the stack holds the screens the menu
	// pushed (`screenStack[16]`, src/menu/menutypes.h:76).
	if (pause_menu_.model.PopScreen()) {
		pause_active_input_ = -1;
		return;
	}
	// Nothing to pop: the menu closes.  Screen 900's own `On Press Escape` is
	// `MenuManager_LeaveMenus(900, MENUMANAGER_IDLE)` (ingamemenu.qvm), which is
	// the editor's Resume (`MenuUiAction::Resume`).
	pause_active_input_ = -1;
	TogglePauseMenu();
}
