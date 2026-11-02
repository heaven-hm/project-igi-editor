/******************************************************************************
 * @file    source/runtime/pause_menu_rows.cpp
 * @brief   See pause_menu_rows.h.  The live labels the editor's pause-menu rows
 *          have always been drawn with, keyed on the editor action the loaded
 *          menu tree wires the row to.
 *****************************************************************************/

#include "runtime/pause_menu_rows.h"

#include <cstdio>

#include "runtime/log_policy.h"
#include "runtime/weather_mode.h"

namespace igi {
namespace {

// The editor's own fixed row labels, for the rows the live menu adds on top of
// the row list (source/menu/menu_ui.h, app_menu::k*Label).
const char* FixedLabelFor(MenuUiAction action) {
    switch (action) {
        case MenuUiAction::Resume: return "Resume";
        case MenuUiAction::SelectLevel: return "Select Level";
        case MenuUiAction::CycleLogLevel: return "Log Level";
        case MenuUiAction::CalculateLightmaps: return "Calculate Lightmaps";
        case MenuUiAction::AdjustFogIntensity: return "Fog Intensity";
        case MenuUiAction::ResetLevel: return "Reset Level";
        case MenuUiAction::SaveLevel: return "Save Level";
        case MenuUiAction::Quit: return "Quit";
        case MenuUiAction::OpenEditorOptions: return app_menu::kEditorOptionsLabel;
        case MenuUiAction::OpenGameMenu: return app_menu::kGameMenuLabel;
        case MenuUiAction::EditMenuText: return app_menu::kMenuTextLabel;
        case MenuUiAction::SaveMenu: return app_menu::kSaveMenuLabel;
        default: return nullptr;
    }
}

// The editor's checkbox rows: "[X] Music" is centred as it stands, and the
// terrain rows keep the two-space indent they have always been drawn with.
std::string CheckboxLabel(bool on, const char* text, const char* indent = "") {
    std::string out(indent != nullptr ? indent : "");
    out += on ? "[X] " : "[ ] ";
    if (text != nullptr) {
        out += text;
    }
    return out;
}

}  // namespace

const char* PauseMenuLogLevelName(int level) {
    return LogLevelLabel(level);
}

std::string PauseMenuRowLabel(MenuUiAction action, const PauseMenuRowState& state) {
    char buffer[96];
    switch (action) {
        case MenuUiAction::ToggleMode:
            std::snprintf(buffer, sizeof(buffer), "Mode: [%s]",
                          state.in_game_mode ? "Game Play" : "Editor");
            return buffer;
        case MenuUiAction::ToggleCollision:
            std::snprintf(buffer, sizeof(buffer), "Collision: [%s]",
                          state.noclip ? "Clip" : "Normal");
            return buffer;
        case MenuUiAction::ToggleFont:
            std::snprintf(buffer, sizeof(buffer), "Font: %s",
                          state.use_editor_font ? "Editor" : "System");
            return buffer;
        case MenuUiAction::ToggleAutoSave:
            return state.auto_save_enabled ? "Save Enable" : "Save Disable";
        case MenuUiAction::ToggleLogging:
            return CheckboxLabel(state.logging_enabled, "Logging");
        case MenuUiAction::ToggleMusic:
            return CheckboxLabel(state.music_on, "Music");
        case MenuUiAction::CycleWeather: {
            const char* mode = "Default";
            if (state.weather_mode == 1) {
                mode = "Rain";
            } else if (state.weather_mode == 2) {
                mode = "Snow";
            } else if (state.weather_mode == 3) {
                mode = "OFF";
            }
            std::snprintf(buffer, sizeof(buffer), "Weather: [%s]", mode);
            return buffer;
        }
        // The editor's preview speed step. The row says "Preview" because the
        // engine has no speed control at all: its fall speed is the fixed pair
        // of constants at src/dxinit/rain.c:122-123, selected by `r->snow` at
        // src/dxinit/rain.c:572 (Rain_Update, HOOK 0x499CF0).
        case MenuUiAction::CycleWeatherSpeed:
            std::snprintf(buffer, sizeof(buffer), "Weather Speed: [%s]",
                          WeatherSpeedLabel(state.weather_speed));
            return buffer;
        case MenuUiAction::ToggleLightmaps:
            std::snprintf(buffer, sizeof(buffer), "Lightmap: [%s]",
                          state.lightmap_mode.empty() ? "Default" : state.lightmap_mode.c_str());
            return buffer;
        case MenuUiAction::ToggleTerrainOptions:
            return state.terrain_expanded ? "Terrain Options: [-]"
                                          : "Terrain Options: [+]";
        case MenuUiAction::ToggleTerrainTexture:
            return CheckboxLabel(state.terrain_texture, "Texture", "  ");
        case MenuUiAction::ToggleTerrainHeight:
            return CheckboxLabel(state.terrain_height, "Height", "  ");
        case MenuUiAction::ToggleTerrainDiscard:
            return CheckboxLabel(state.terrain_discard, "Discard", "  ");
        case MenuUiAction::ToggleTerrainFog:
            return CheckboxLabel(state.terrain_fog, "Fog", "  ");
        default:
            break;
    }
    const char* fixed = FixedLabelFor(action);
    return fixed != nullptr ? std::string(fixed) : std::string();
}

int PauseMenuSpinnerValueWidth(MenuUiAction action) {
    switch (action) {
        case MenuUiAction::ToggleFont:
        case MenuUiAction::SelectLevel:
        case MenuUiAction::ToggleAutoSave:
            return 44;
        case MenuUiAction::CycleLogLevel:
            return 78;
        case MenuUiAction::AdjustFogIntensity:
            return 56;
        default:
            return 0;
    }
}

bool PauseMenuRowHasSpinner(MenuUiAction action) {
    return PauseMenuSpinnerValueWidth(action) > 0;
}

}  // namespace igi
