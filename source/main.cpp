/******************************************************************************
 * @file    main.cpp
 * @brief   setup glut framework
 *****************************************************************************/

#include "cli/cli_handler.h"
#include "config.h"
#include "logger.h"
#include "pch.h"
#include "utils.h"
#include <freeglut.h>
#include <filesystem>

/*
================================================================================
 global variables & constants
================================================================================
*/
static App g_app;

// menu ids

//  choose level
constexpr int MENU_LEVEL_FIRST = MIN_LEVEL_NO;
constexpr int MENU_LEVEL_LAST = MAX_LEVEL_NO;

// overlay a wireframe on top of mesh
constexpr int MENU_OVERLAY_WIREFRAME = 21;

// draw parts
constexpr int MENU_DRAW_SKYDOME = 31;
constexpr int MENU_DRAW_FLAT_SKY_LAYER = 32;
constexpr int MENU_DRAW_TERRAIN = 33;

// apply terrain tiled texture / light map / fog
constexpr int MENU_DRAW_TERRAIN_OPT_MAT = 41;
constexpr int MENU_DRAW_TERRAIN_OPT_LMP = 42;
constexpr int MENU_DRAW_TERRAIN_OPT_FOG = 43;

// apply terrain modifiers
constexpr int MENU_TERRAIN_TEX_MOD = 51;
constexpr int MENU_TERRAIN_HEIGHT_MOD = 52;
constexpr int MENU_TERRAIN_DISCARD_MOD = 53;

constexpr int MENU_EDITOR_TOGGLE = 71;
constexpr int MENU_EDIT_OBJECTS = 72;
constexpr int MENU_EDIT_TERRAIN = 73;
constexpr int MENU_TERRAIN_BRUSH_RAISE = 74;
constexpr int MENU_TERRAIN_BRUSH_LOWER = 75;
constexpr int MENU_EDITOR_SAVE = 76;
constexpr int MENU_TOGGLE_TASK_TREE = 77;
constexpr int MENU_IGI_LIVE_DATA = 78;
constexpr int MENU_SEARCH_MODEL_BY_ID = 95;
constexpr int MENU_SEARCH_MODEL_BY_NAME = 96;
constexpr int MENU_COPY_MODEL_NAME = 97;
constexpr int MENU_COPY_MODEL_ID = 98;
constexpr int MENU_SCALE_0_1 = 81;
constexpr int MENU_SCALE_0_5 = 82;
constexpr int MENU_SCALE_1 = 83;
constexpr int MENU_SCALE_2 = 84;
constexpr int MENU_SCALE_5 = 85;
constexpr int MENU_SCALE_10 = 86;
constexpr int MENU_SCALE_20 = 87;
constexpr int MENU_SHOW_ALL = 88;
constexpr int MENU_SHOW_OBJECTS_ONLY = 89;
constexpr int MENU_SHOW_BUILDINGS_ONLY = 90;

// load options
constexpr int MENU_LOAD_ALL = 91;
constexpr int MENU_LOAD_OBJECTS = 92;
constexpr int MENU_LOAD_BUILDINGS = 93;
constexpr int MENU_LOAD_AI = 94;

constexpr int BRUSH_RAISE = 0;
constexpr int BRUSH_LOWER = 1;

// exit
constexpr int MENU_CLOSE = 61;

// menus
static int g_menu_draw_parts;
static int g_menu_terrain_draw_opts;
static int g_menu_terrain_modifier_opts;
static int g_menu_choose_level;
static int g_menu_editor_tools;
static int g_menu_model_lookup;
static int g_menu_object_scale;
static int g_menu_load_options;
static int g_menu_terrain_brush;
static int g_main_menu;

// glut (ubuntu): Menu manipulation not allowed while menus in use.
//  we set g_update_menu_flags in OnMenu function
//  then check g_update_menu_flags and update menu text in OnIdle function.
static int g_update_menu_flags;

// update menu flags
constexpr int UPDATE_MENU_OVERLAY_WIREFRAME = FLAG_BIT(0);
constexpr int UPDATE_MENU_DRAW_PARTS = FLAG_BIT(1);
constexpr int UPDATE_MENU_TERRAIN_DRAW_OPTS = FLAG_BIT(2);
constexpr int UPDATE_MENU_TERRAIN_MODIFIER_OPTS = FLAG_BIT(3);
constexpr int UPDATE_MENU_EDITOR_TOOLS = FLAG_BIT(4);
constexpr int UPDATE_MENU_SHOW_OPTIONS = FLAG_BIT(5);
constexpr int UPDATE_MENU_CHOOSE_LEVEL = FLAG_BIT(6);

/*
================================================================================
 glut callbacks
================================================================================
*/

static void OnReshape(int width, int height) {
  g_app.OnWindowResize(width, height);
}

static void OnMouse(int button, int state, int x, int y) {
  g_app.Input_OnMouse(button, state, x, y);
}

static void OnMouseWheel(int wheel, int direction, int x, int y) {
  g_app.Input_OnMouseWheel(wheel, direction, x, y);
}

static void OnMotion(int x, int y) { g_app.Input_OnMotion(x, y); }

static void OnMenu(int menu);

static void OnSpecial(int key, int x, int y) {
  if (key == GLUT_KEY_F2) {
    OnMenu(MENU_OVERLAY_WIREFRAME);
    return;
  }

  g_app.Input_OnSpecial(key, x, y);
}

static void OnSpecialUp(int key, int x, int y) {
  g_app.Input_OnSpecialUp(key, x, y);
}

static void OnKeyboard(unsigned char key, int x, int y) {
  g_app.Input_OnKeyboard(key, x, y);
}

static void OnKeyboardUp(unsigned char key, int x, int y) {
  g_app.Input_OnKeyboardUp(key, x, y);
}

static void OnDisplay() { g_app.OnDisplay(); }

static void UpdateOverlayWireframeMenuText();
static void UpdateDrawPartsMenuText();
static void UpdateTerrainDrawOptionsMenuText();
static void UpdateTerrainModOptionsMenuText();
static void UpdateChooseLevelMenuText();
static void UpdateEditorToolsMenuText();
static void UpdateIGILiveDataMenuText();
static void UpdateScaleMenuText();

static void OnIdle() {
  g_app.OnIdle();

  // update menu text
  if (g_update_menu_flags) {
    if (g_update_menu_flags & UPDATE_MENU_OVERLAY_WIREFRAME) {
      UpdateOverlayWireframeMenuText();
    }

    if (g_update_menu_flags & UPDATE_MENU_DRAW_PARTS) {
      UpdateDrawPartsMenuText();
    }

    if (g_update_menu_flags & UPDATE_MENU_TERRAIN_DRAW_OPTS) {
      UpdateTerrainDrawOptionsMenuText();
    }

    if (g_update_menu_flags & UPDATE_MENU_TERRAIN_MODIFIER_OPTS) {
      UpdateTerrainModOptionsMenuText();
    }

    if (g_update_menu_flags & UPDATE_MENU_CHOOSE_LEVEL) {
      UpdateChooseLevelMenuText();
    }

    if (g_update_menu_flags & UPDATE_MENU_EDITOR_TOOLS) {
      UpdateEditorToolsMenuText();
      UpdateIGILiveDataMenuText();
      UpdateScaleMenuText();
    }

    if (g_update_menu_flags & UPDATE_MENU_SHOW_OPTIONS) {
      // Update show options menu text if needed
    }

    // clear menu update flags
    g_update_menu_flags = 0;
  }
}

static void OnClose() {
  g_app.Shutdown();

  // check memory leak
  Mem_Print();

  glutLeaveMainLoop();

#if defined(_WIN32)
#if defined(_DEBUG) && defined(HOOK_ALLOC)
  _CrtSetAllocHook(NULL);
#endif
#endif
}

static void UpdateOverlayWireframeMenuText() {
  glutSetMenu(g_main_menu);

  glutChangeToMenuEntry(
      1, g_app.GetOverlayWireframe() ? "Wireframe (*)" : "Wireframe    ",
      MENU_OVERLAY_WIREFRAME);
}

static void UpdateDrawPartsMenuText() {
  int draw_parts = g_app.GetDrawParts();

  glutSetMenu(g_menu_draw_parts);

  if (draw_parts & Renderer::DRAW_SKYDOME) {
    glutChangeToMenuEntry(1, "Skydome      [+]", MENU_DRAW_SKYDOME);
  } else {
    glutChangeToMenuEntry(1, "Skydome      [-]", MENU_DRAW_SKYDOME);
  }

  if (draw_parts & Renderer::DRAW_FLAT_SKY_LAYER) {
    glutChangeToMenuEntry(2, "FlatSkyLayer [+]", MENU_DRAW_FLAT_SKY_LAYER);
  } else {
    glutChangeToMenuEntry(2, "FlatSkyLayer [-]", MENU_DRAW_FLAT_SKY_LAYER);
  }

  if (draw_parts & Renderer::DRAW_TERRAIN) {
    glutChangeToMenuEntry(3, "Terrain      [+]", MENU_DRAW_TERRAIN);
  } else {
    glutChangeToMenuEntry(3, "Terrain      [-]", MENU_DRAW_TERRAIN);
  }
}

static void UpdateTerrainDrawOptionsMenuText() {
  int opts = g_app.GetTerrainDrawOptions();

  glutSetMenu(g_menu_terrain_draw_opts);

  if (opts & Renderer_Terrain::DRAW_TERRAIN_OPT_MAT) {
    glutChangeToMenuEntry(1, "Texture   [+]", MENU_DRAW_TERRAIN_OPT_MAT);
  } else {
    glutChangeToMenuEntry(1, "Texture   [-]", MENU_DRAW_TERRAIN_OPT_MAT);
  }

  if (opts & Renderer_Terrain::DRAW_TERRAIN_OPT_LMP) {
    glutChangeToMenuEntry(2, "Light Map [+]", MENU_DRAW_TERRAIN_OPT_LMP);
  } else {
    glutChangeToMenuEntry(2, "Light Map [-]", MENU_DRAW_TERRAIN_OPT_LMP);
  }

  if (opts & Renderer_Terrain::DRAW_TERRAIN_OPT_FOG) {
    glutChangeToMenuEntry(3, "Fog       [+]", MENU_DRAW_TERRAIN_OPT_FOG);
  } else {
    glutChangeToMenuEntry(3, "Fog       [-]", MENU_DRAW_TERRAIN_OPT_FOG);
  }
}

static void UpdateTerrainModOptionsMenuText() {
  int opts = g_app.GetTerrainModOptions();

  glutSetMenu(g_menu_terrain_modifier_opts);

  if (opts & TERRAIN_TEXTURE_MOD) {
    glutChangeToMenuEntry(1, "Texture Modifier [+]", MENU_TERRAIN_TEX_MOD);
  } else {
    glutChangeToMenuEntry(1, "Texture Modifier [-]", MENU_TERRAIN_TEX_MOD);
  }

  if (opts & TERRAIN_HEIGHT_MOD) {
    glutChangeToMenuEntry(2, "Height Map       [+]", MENU_TERRAIN_HEIGHT_MOD);
  } else {
    glutChangeToMenuEntry(2, "Height Map       [-]", MENU_TERRAIN_HEIGHT_MOD);
  }

  if (opts & TERRAIN_DISCARD_MOD) {
    glutChangeToMenuEntry(3, "Discard Terrain  [+]", MENU_TERRAIN_DISCARD_MOD);
  } else {
    glutChangeToMenuEntry(3, "Discard Terrain  [-]", MENU_TERRAIN_DISCARD_MOD);
  }
}

static void UpdateChooseLevelMenuText() {
  int cur_level_no = g_app.GetCurLevelNo();

  glutSetMenu(g_menu_choose_level);
  for (int i = MENU_LEVEL_FIRST; i <= MENU_LEVEL_LAST; ++i) {
    char s[64];

    if (i == cur_level_no) {
      Str_SPrintf(s, 64, "Level %2d (*)", i);
    } else {
      Str_SPrintf(s, 64, "Level %2d    ", i);
    }

    glutChangeToMenuEntry(i, s, i);
  }
}

static void UpdateEditorToolsMenuText() {
  glutSetMenu(g_menu_editor_tools);

  glutChangeToMenuEntry(1, "Task Tree Always On", MENU_TOGGLE_TASK_TREE);

  if (g_app.GetTerrainEditEnabled()) {
    glutChangeToMenuEntry(2, "Edit Objects [ ]", MENU_EDIT_OBJECTS);
    glutChangeToMenuEntry(3, "Edit Terrain [*]", MENU_EDIT_TERRAIN);
  } else {
    glutChangeToMenuEntry(2, "Edit Objects [*]", MENU_EDIT_OBJECTS);
    glutChangeToMenuEntry(3, "Edit Terrain [ ]", MENU_EDIT_TERRAIN);
  }

  glutSetMenu(g_menu_terrain_brush);
  if (g_app.GetEditBrush() == BRUSH_RAISE) {
    glutChangeToMenuEntry(1, "Brush: Raise Terrain [*]",
                          MENU_TERRAIN_BRUSH_RAISE);
    glutChangeToMenuEntry(2, "Brush: Lower Terrain [ ]",
                          MENU_TERRAIN_BRUSH_LOWER);
  } else {
    glutChangeToMenuEntry(1, "Brush: Raise Terrain [ ]",
                          MENU_TERRAIN_BRUSH_RAISE);
    glutChangeToMenuEntry(2, "Brush: Lower Terrain [*]",
                          MENU_TERRAIN_BRUSH_LOWER);
  }

  // Editor Tools menu no longer has a separate IGI Live Data entry here.
}

static void UpdateIGILiveDataMenuText() {
  // Keep this function for call-site compatibility; live data toggle isn't a
  // visible menu entry.
}

static void UpdateScaleMenuText() {
  float s = g_app.GetSelectedObjectScale();
  glutSetMenu(g_menu_object_scale);
  glutChangeToMenuEntry(1, (s == 0.1f) ? "0.1x (*)" : "0.1x", MENU_SCALE_0_1);
  glutChangeToMenuEntry(2, (s == 0.5f) ? "0.5x (*)" : "0.5x", MENU_SCALE_0_5);
  glutChangeToMenuEntry(3, (s == 1.0f) ? "1.0x (*)" : "1.0x", MENU_SCALE_1);
  glutChangeToMenuEntry(4, (s == 2.0f) ? "2.0x (*)" : "2.0x", MENU_SCALE_2);
  glutChangeToMenuEntry(5, (s == 5.0f) ? "5.0x (*)" : "5.0x", MENU_SCALE_5);
  glutChangeToMenuEntry(6, (s == 10.0f) ? "10.0x (*)" : "10.0x", MENU_SCALE_10);
  glutChangeToMenuEntry(7, (s == 20.0f) ? "20.0x (*)" : "20.0x", MENU_SCALE_20);
}

static void OnMenu(int menu) {
  if (menu >= MENU_LEVEL_FIRST && menu <= MENU_LEVEL_LAST) {
    g_app.LoadLevel(menu);
    g_app.SetGameLevel(menu);

    g_update_menu_flags |= UPDATE_MENU_CHOOSE_LEVEL;
    return;
  }

  switch (menu) {
  case MENU_OVERLAY_WIREFRAME:
    g_app.ToggleOverlayWireframe();
    g_update_menu_flags |= UPDATE_MENU_OVERLAY_WIREFRAME;
    break;
  case MENU_DRAW_SKYDOME:
    g_app.ToggleDrawParts(Renderer::DRAW_SKYDOME);
    g_update_menu_flags |= UPDATE_MENU_DRAW_PARTS;
    break;
  case MENU_DRAW_FLAT_SKY_LAYER:
    g_app.ToggleDrawParts(Renderer::DRAW_FLAT_SKY_LAYER);
    g_update_menu_flags |= UPDATE_MENU_DRAW_PARTS;
    break;
  case MENU_DRAW_TERRAIN:
    g_app.ToggleDrawParts(Renderer::DRAW_TERRAIN);
    g_update_menu_flags |= UPDATE_MENU_DRAW_PARTS;
    break;
  case MENU_DRAW_TERRAIN_OPT_MAT:
    g_app.ToggleTerrainDrawOption(Renderer_Terrain::DRAW_TERRAIN_OPT_MAT);
    g_update_menu_flags |= UPDATE_MENU_TERRAIN_DRAW_OPTS;
    break;
  case MENU_DRAW_TERRAIN_OPT_LMP:
    g_app.ToggleTerrainDrawOption(Renderer_Terrain::DRAW_TERRAIN_OPT_LMP);
    g_update_menu_flags |= UPDATE_MENU_TERRAIN_DRAW_OPTS;
    break;
  case MENU_DRAW_TERRAIN_OPT_FOG:
    g_app.ToggleTerrainDrawOption(Renderer_Terrain::DRAW_TERRAIN_OPT_FOG);
    g_update_menu_flags |= UPDATE_MENU_TERRAIN_DRAW_OPTS;
    break;
  case MENU_TERRAIN_TEX_MOD:
    g_app.ToggleTerrainModOption(TERRAIN_TEXTURE_MOD);
    g_update_menu_flags |= UPDATE_MENU_TERRAIN_MODIFIER_OPTS;
    break;
  case MENU_TERRAIN_HEIGHT_MOD:
    g_app.ToggleTerrainModOption(TERRAIN_HEIGHT_MOD);
    g_update_menu_flags |= UPDATE_MENU_TERRAIN_MODIFIER_OPTS;
    break;
  case MENU_TERRAIN_DISCARD_MOD:
    g_app.ToggleTerrainModOption(TERRAIN_DISCARD_MOD);
    g_update_menu_flags |= UPDATE_MENU_TERRAIN_MODIFIER_OPTS;
    break;
  case MENU_TOGGLE_TASK_TREE:
    g_app.SetShowHUD(true);
    g_update_menu_flags |= UPDATE_MENU_EDITOR_TOOLS;
    break;
  case MENU_EDIT_OBJECTS:
    g_app.SetTerrainEditEnabled(false);
    g_update_menu_flags |= UPDATE_MENU_EDITOR_TOOLS;
    break;
  case MENU_EDIT_TERRAIN:
    g_app.SetTerrainEditEnabled(true);
    g_update_menu_flags |= UPDATE_MENU_EDITOR_TOOLS;
    break;
  case MENU_TERRAIN_BRUSH_RAISE:
    g_app.SetEditBrush(BRUSH_RAISE);
    g_update_menu_flags |= UPDATE_MENU_EDITOR_TOOLS;
    break;
  case MENU_TERRAIN_BRUSH_LOWER:
    g_app.SetEditBrush(BRUSH_LOWER);
    g_update_menu_flags |= UPDATE_MENU_EDITOR_TOOLS;
    break;
  case MENU_EDITOR_SAVE:
    g_app.SaveCurrentLevel();
    break;
  case MENU_SEARCH_MODEL_BY_ID:
    g_app.SearchModelById();
    break;
  case MENU_SEARCH_MODEL_BY_NAME:
    g_app.SearchModelByName();
    break;
  case MENU_COPY_MODEL_NAME:
    g_app.CopySelectedModelName();
    break;
  case MENU_COPY_MODEL_ID:
    g_app.CopySelectedModelId();
    break;
  case MENU_IGI_LIVE_DATA:
    g_app.ToggleShowHUD();
    g_update_menu_flags |= UPDATE_MENU_EDITOR_TOOLS;
    break;
  case MENU_SCALE_0_1:
    g_app.SetSelectedObjectScale(0.1f);
    g_update_menu_flags |= UPDATE_MENU_EDITOR_TOOLS;
    break;
  case MENU_SCALE_0_5:
    g_app.SetSelectedObjectScale(0.5f);
    g_update_menu_flags |= UPDATE_MENU_EDITOR_TOOLS;
    break;
  case MENU_SCALE_1:
    g_app.SetSelectedObjectScale(1.0f);
    g_update_menu_flags |= UPDATE_MENU_EDITOR_TOOLS;
    break;
  case MENU_SCALE_2:
    g_app.SetSelectedObjectScale(2.0f);
    g_update_menu_flags |= UPDATE_MENU_EDITOR_TOOLS;
    break;
  case MENU_SCALE_5:
    g_app.SetSelectedObjectScale(5.0f);
    g_update_menu_flags |= UPDATE_MENU_EDITOR_TOOLS;
    break;
  case MENU_SCALE_10:
    g_app.SetSelectedObjectScale(10.0f);
    g_update_menu_flags |= UPDATE_MENU_EDITOR_TOOLS;
    break;
  case MENU_SCALE_20:
    g_app.SetSelectedObjectScale(20.0f);
    g_update_menu_flags |= UPDATE_MENU_EDITOR_TOOLS;
    break;
  case MENU_SHOW_ALL:
    // Toggle between all objects ON and all objects OFF
    if ((g_app.GetDrawParts() & Renderer::DRAW_OBJECTS) &&
        (g_app.GetDrawParts() & Renderer::DRAW_BUILDINGS)) {
      // Both on -> turn all object rendering off
      g_app.SetDrawParts(g_app.GetDrawParts() &
                         ~(Renderer::DRAW_OBJECTS | Renderer::DRAW_BUILDINGS |
                           Renderer::DRAW_PROPS));
    } else {
      // Turn all object rendering on
      g_app.SetDrawParts(g_app.GetDrawParts() |
                         (Renderer::DRAW_OBJECTS | Renderer::DRAW_BUILDINGS |
                          Renderer::DRAW_PROPS));
    }
    g_update_menu_flags |= UPDATE_MENU_SHOW_OPTIONS;
    break;
  case MENU_SHOW_OBJECTS_ONLY:
    // Objects (props) ON, Buildings OFF. Clear DRAW_OBJECTS so individual
    // checks work.
    g_app.SetDrawParts((g_app.GetDrawParts() &
                        ~(Renderer::DRAW_OBJECTS | Renderer::DRAW_BUILDINGS)) |
                       Renderer::DRAW_PROPS);
    g_update_menu_flags |= UPDATE_MENU_SHOW_OPTIONS;
    break;
  case MENU_SHOW_BUILDINGS_ONLY:
    // Buildings ON, Objects (props) OFF. Clear DRAW_OBJECTS so individual
    // checks work.
    g_app.SetDrawParts((g_app.GetDrawParts() &
                        ~(Renderer::DRAW_OBJECTS | Renderer::DRAW_PROPS)) |
                       Renderer::DRAW_BUILDINGS);
    g_update_menu_flags |= UPDATE_MENU_SHOW_OPTIONS;
    break;
  case MENU_LOAD_ALL:
    // Toggle between everything ON and everything OFF
    if ((g_app.GetDrawParts() & Renderer::DRAW_OBJECTS) &&
        (g_app.GetDrawParts() & Renderer::DRAW_BUILDINGS) &&
        (g_app.GetDrawParts() & Renderer::DRAW_AI)) {
      g_app.SetDrawParts(g_app.GetDrawParts() &
                         ~(Renderer::DRAW_OBJECTS | Renderer::DRAW_BUILDINGS |
                           Renderer::DRAW_PROPS | Renderer::DRAW_AI));
    } else {
      g_app.SetDrawParts(g_app.GetDrawParts() |
                         (Renderer::DRAW_OBJECTS | Renderer::DRAW_BUILDINGS |
                          Renderer::DRAW_PROPS | Renderer::DRAW_AI));
    }
    g_update_menu_flags |= UPDATE_MENU_SHOW_OPTIONS;
    break;
  case MENU_LOAD_OBJECTS:
    g_app.SetDrawParts((g_app.GetDrawParts() &
                        ~(Renderer::DRAW_BUILDINGS | Renderer::DRAW_AI)) |
                       Renderer::DRAW_PROPS);
    g_update_menu_flags |= UPDATE_MENU_SHOW_OPTIONS;
    break;
  case MENU_LOAD_BUILDINGS:
    g_app.SetDrawParts(
        (g_app.GetDrawParts() &
         ~(Renderer::DRAW_OBJECTS | Renderer::DRAW_PROPS | Renderer::DRAW_AI)) |
        Renderer::DRAW_BUILDINGS);
    g_update_menu_flags |= UPDATE_MENU_SHOW_OPTIONS;
    break;
  case MENU_LOAD_AI:
    g_app.SetDrawParts(g_app.GetDrawParts() | Renderer::DRAW_AI);
    g_update_menu_flags |= UPDATE_MENU_SHOW_OPTIONS;
    // Load AI models from ai\levelX folder
    g_app.LoadAIModelsFromFolder(g_app.GetCurLevelNo());
    break;
  case MENU_CLOSE:
    glutLeaveMainLoop();
    break;
  }
}

#define HOOK_ALLOC

#if defined(_WIN32) && defined(_DEBUG) && defined(HOOK_ALLOC)
static int CustomAllocHook(int alloc_type, void *user_data, size_t size,
                           int block_type, long request_number,
                           const unsigned char *filename, int line_number) {
  int bk = 0;

  if (_CRT_BLOCK ==
      block_type) { // _CRT_BLOCK used by c runtime library, must be ignore
    return TRUE;
  }

  if (_HOOK_ALLOC == alloc_type) {
    bk = alloc_type;
  } else if (_HOOK_REALLOC == alloc_type) {
    bk = alloc_type;
  } else if (_HOOK_FREE == alloc_type) {
    bk = alloc_type;
  }

  return TRUE;
}
#endif

/*
================================================================================
 entrance
================================================================================
*/
int main(int argc, char **argv) {
  std::string version = Utils::GetVersionString();
#if defined(_WIN32) && defined(_DEBUG)
  // Allocate console for debug mode
  AllocConsole();
  freopen("CONIN$", "r", stdin);
  freopen("CONOUT$", "w", stdout);
  freopen("CONOUT$", "w", stderr);
  std::string consoleTitle =
      "IGI Editor v " + version + " BETA - Heaven-HM - Debug Console";
  SetConsoleTitleA(consoleTitle.c_str());

  _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF |
                 _CRTDBG_LEAK_CHECK_DF); // detect memory leak
#if defined(HOOK_ALLOC)
  _CrtSetAllocHook(CustomAllocHook);
#endif
#endif

  // Initialize logger early (handles both GUI and Headless modes)
  std::string exeDir = Utils::GetExeDirectory();
  Logger::Get().Init(exeDir + "\\igi1ed.log");

  // Initialize config first (before Folders_Init which uses Config)
  Config::Init();
  Logger::Get().Log(
      LogLevel::INFO,
      "[Main] Config initialized. Logging enabled: " +
          std::string(Config::Get().enableLogging ? "TRUE" : "FALSE") +
          ", Debug: " +
          std::string(Config::Get().debugLogging ? "TRUE" : "FALSE"));

  // Intercept headless CLI commands before any GUI/folder setup
  if (CLIHandler::IsCLICommand(argc, argv)) {
    g_isCLIMode = true;
    int result = CLIHandler::Process(argc, argv);
#if defined(_WIN32) && defined(_DEBUG)
    system("pause");
#endif
    return result;
  }

  // Check if the game executable (igi.exe) exists in the configured game path
  std::string igiRoot = Utils::GetIGIRootPath();
  std::string igiExePath = igiRoot + "\\igi.exe";
  if (!std::filesystem::exists(igiExePath)) {
    std::string errorMsg = "Fatal Error: 'igi.exe' not found in game directory:\n" + igiRoot +
                           "\n\nPlease make sure the editor is placed in the main Project IGI game directory next to 'igi.exe'.";
#if defined(_WIN32)
    Utils::LogAndShowError(errorMsg, "IGI Editor - Launch Error");
#else
    fprintf(stderr, "%s\n", errorMsg.c_str());
#endif
    return 1;
  }

  // setup path of res and shaders folders (GUI mode only — creates QEditor dirs)
  Folders_Init();

#if defined(_WIN32) && !defined(_DEBUG)
  // If NOT in CLI mode and NOT in Debug, hide the console window for a clean
  // GUI experience (Since we are using /SUBSYSTEM:CONSOLE to support CLI
  // output)
  if (GetConsoleWindow() != NULL) {
    // Only hide if we aren't being run from an existing terminal
    // (Check if we are the only process attached to this console)
    DWORD processList[2];
    if (GetConsoleProcessList(processList, 2) == 1) {
      ShowWindow(GetConsoleWindow(), SW_HIDE);
    }
  }
#endif

  // read window width & height from command line
  int wnd_w = Arg_ReadInt(argc, argv, "-w", 800);
  int wnd_h = Arg_ReadInt(argc, argv, "-h", 600);

  // read level from command line
  int level_no = Arg_ReadInt(argc, argv, "-level", 1);

  // read draw_parts from command line (bitmask: 1=terrain, 2=sky, 4=objects,
  // 8=flat_sky, 16=buildings, 32=props) Example: 17 = 1 (terrain) + 16
  // (buildings) We automatically add skydome (2) and flat sky (8) when
  // draw_parts is specified
  int draw_parts = Arg_ReadInt(argc, argv, "-draw_parts", 0);
  if (draw_parts != 0) {
    draw_parts |= Renderer::DRAW_SKYDOME | Renderer::DRAW_FLAT_SKY_LAYER;
  }

  // read stick_to_ground flag
  bool stick_to_ground = (Arg_OptionIdx(argc, argv, "-stick_to_ground") > -1);

  glutInit(&argc, argv);
  glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGBA | GLUT_DEPTH);

  // center display
  int screen_cx = glutGet(GLUT_SCREEN_WIDTH);
  int screen_cy = glutGet(GLUT_SCREEN_HEIGHT);
  int pos_x = (screen_cx - wnd_w) >> 1;
  int pos_y = (screen_cy - wnd_h) >> 1;
  glutInitWindowPosition(pos_x, pos_y);
  glutInitWindowSize(wnd_w, wnd_h);
  std::string windowTitle = "IGI Editor v" + version;
  glutCreateWindow(windowTitle.c_str());

#if defined(_WIN32)
  // Load icon from file and set it
  char iconPath[MAX_PATH];
  GetModuleFileNameA(NULL, iconPath, MAX_PATH);
  std::string iconExeDir(iconPath);
  size_t lastSlash = iconExeDir.find_last_of("\\/");
  if (lastSlash != std::string::npos) {
    iconExeDir = iconExeDir.substr(0, lastSlash);
  }
  std::string iconFilePath =
      iconExeDir + "\\..\\..\\assets\\igi-editor.ico";

  HICON hIcon = (HICON)LoadImageA(
      NULL, iconFilePath.c_str(), IMAGE_ICON, GetSystemMetrics(SM_CXICON),
      GetSystemMetrics(SM_CYICON), LR_LOADFROMFILE | LR_DEFAULTSIZE);

  if (!hIcon) {
    // Fallback to resource
    hIcon = (HICON)LoadImageA(GetModuleHandleA(NULL), MAKEINTRESOURCEA(101),
                              IMAGE_ICON, GetSystemMetrics(SM_CXICON),
                              GetSystemMetrics(SM_CYICON),
                              LR_DEFAULTSIZE | LR_SHARED);
  }

  if (hIcon) {
    HWND hwnd = GetActiveWindow();
    if (!hwnd) {
      hwnd = FindWindowA(NULL, windowTitle.c_str());
    }
    if (!hwnd) {
      int glutWindow = glutGetWindow();
      if (glutWindow) {
        hwnd = GetForegroundWindow();
      }
    }

    if (hwnd) {
      SendMessageA(hwnd, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
      SendMessageA(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
      SetClassLongPtrA(hwnd, GCLP_HICON, (LONG_PTR)hIcon);
      SetClassLongPtrA(hwnd, GCLP_HICONSM, (LONG_PTR)hIcon);
    }
  }
#endif

  if (!GL_Init()) {
    return 1;
  }

  if (g_gl_info.support_version_45_) {
    Str_Cat(g_folders.shader_folder_, count_of(g_folders.shader_folder_),
            "/45");
  } else {
    // try version 4.1
    Str_Cat(g_folders.shader_folder_, count_of(g_folders.shader_folder_),
            "/41");
  }

  const char *HINT = "Alt+Enter:  toggle fullscreen mode\n"
                     "F2:         toggle overlay wireframe\n"
                     "F3:         toggle clip\n"
                     "F4:         show/hide cursor\n"
                     "PageUp:     increase move speed\n"
                     "PageDown:   decrease move speed\n"
                     "Left/Right: change roll\n";

  printf("%s", HINT);

  if (!g_app.Init(argc, argv)) {
    return 2;
  }

  // Apply command line settings
  if (draw_parts != 0) {
    g_app.SetInitialDrawParts(draw_parts);
  }
  if (stick_to_ground) {
    g_app.SetInitialStickToGround(true);
  }
  if (level_no > 0) {
    g_app.LoadLevel(level_no);
    g_app.SetGameLevel(level_no);
  }

  // setup glut callbacks
  glutReshapeFunc(OnReshape);
  glutMouseFunc(OnMouse);
  glutMouseWheelFunc(OnMouseWheel);
  glutMotionFunc(OnMotion);
  glutPassiveMotionFunc(
      OnMotion); // trace cursor movement even cursor outside the viewport
  glutSpecialFunc(OnSpecial);
  glutSpecialUpFunc(OnSpecialUp);
  glutKeyboardFunc(OnKeyboard);
  glutKeyboardUpFunc(OnKeyboardUp);
  glutDisplayFunc(OnDisplay);
  glutIdleFunc(OnIdle);
  glutCloseFunc(OnClose);

  // create glut context menu
  g_menu_draw_parts = glutCreateMenu(OnMenu);
  glutAddMenuEntry("", MENU_DRAW_SKYDOME);
  glutAddMenuEntry("", MENU_DRAW_FLAT_SKY_LAYER);
  glutAddMenuEntry("", MENU_DRAW_TERRAIN);

  g_menu_terrain_draw_opts = glutCreateMenu(OnMenu);
  glutAddMenuEntry("", MENU_DRAW_TERRAIN_OPT_MAT);
  glutAddMenuEntry("", MENU_DRAW_TERRAIN_OPT_LMP);
  glutAddMenuEntry("", MENU_DRAW_TERRAIN_OPT_FOG);

  g_menu_terrain_modifier_opts = glutCreateMenu(OnMenu);
  glutAddMenuEntry("", MENU_TERRAIN_TEX_MOD);
  glutAddMenuEntry("", MENU_TERRAIN_HEIGHT_MOD);
  glutAddMenuEntry("", MENU_TERRAIN_DISCARD_MOD);

  g_menu_choose_level = glutCreateMenu(OnMenu);
  for (int i = MENU_LEVEL_FIRST; i <= MENU_LEVEL_LAST; ++i) {
    glutAddMenuEntry("", i);
  }

  g_menu_terrain_brush = glutCreateMenu(OnMenu);
  glutAddMenuEntry("Brush: Raise Terrain", MENU_TERRAIN_BRUSH_RAISE);
  glutAddMenuEntry("Brush: Lower Terrain", MENU_TERRAIN_BRUSH_LOWER);

  g_menu_editor_tools = glutCreateMenu(OnMenu);
  glutAddMenuEntry("Task Tree Always On", MENU_TOGGLE_TASK_TREE);
  glutAddMenuEntry("Edit Objects", MENU_EDIT_OBJECTS);
  glutAddMenuEntry("Edit Terrain", MENU_EDIT_TERRAIN);

  g_menu_model_lookup = glutCreateMenu(OnMenu);
  glutAddMenuEntry("By ID", MENU_SEARCH_MODEL_BY_ID);
  glutAddMenuEntry("By Name", MENU_SEARCH_MODEL_BY_NAME);
  glutAddMenuEntry("Copy Model Name", MENU_COPY_MODEL_NAME);
  glutAddMenuEntry("Copy Model ID", MENU_COPY_MODEL_ID);

  glutSetMenu(
      g_menu_editor_tools); // Set back to editor tools before adding submenus
  glutAddSubMenu("Terrain Brush", g_menu_terrain_brush);
  glutAddMenuEntry("Save Changes", MENU_EDITOR_SAVE);

  g_menu_object_scale = glutCreateMenu(OnMenu);
  glutAddMenuEntry("0.1x", MENU_SCALE_0_1);
  glutAddMenuEntry("0.5x", MENU_SCALE_0_5);
  glutAddMenuEntry("1.0x", MENU_SCALE_1);
  glutAddMenuEntry("2.0x", MENU_SCALE_2);
  glutAddMenuEntry("5.0x", MENU_SCALE_5);
  glutAddMenuEntry("10.0x", MENU_SCALE_10);
  glutAddMenuEntry("20.0x", MENU_SCALE_20);

  g_menu_load_options = glutCreateMenu(OnMenu);
  glutAddMenuEntry("Load All", MENU_LOAD_ALL);
  glutAddMenuEntry("Load Objects", MENU_LOAD_OBJECTS);
  glutAddMenuEntry("Load Buildings", MENU_LOAD_BUILDINGS);
  glutAddMenuEntry("Load AI", MENU_LOAD_AI);

  g_main_menu = glutCreateMenu(OnMenu);
  glutAddMenuEntry("", MENU_OVERLAY_WIREFRAME);
  glutAddSubMenu("Draw Parts", g_menu_draw_parts);
  glutAddSubMenu("Terrain Draw Options", g_menu_terrain_draw_opts);
  glutAddSubMenu("Terrain Mod Options", g_menu_terrain_modifier_opts);
  glutAddSubMenu("Editor Tools", g_menu_editor_tools);
  glutAddSubMenu("Model Search", g_menu_model_lookup);
  glutAddSubMenu("Object Scale", g_menu_object_scale);
  glutAddSubMenu("Load Options", g_menu_load_options);
  glutAddSubMenu("Choose Level", g_menu_choose_level);
  glutAddMenuEntry("Close", MENU_CLOSE);

  // bind to right button
  glutAttachMenu(GLUT_RIGHT_BUTTON);

  // init menu text
  UpdateOverlayWireframeMenuText();
  UpdateDrawPartsMenuText();
  UpdateTerrainDrawOptionsMenuText();
  UpdateTerrainModOptionsMenuText();
  UpdateChooseLevelMenuText();
  UpdateEditorToolsMenuText();
  UpdateScaleMenuText();

  try {
    // enter main loop
    glutMainLoop();
  } catch (const std::exception &e) {
#if defined(_WIN32)
    Utils::LogAndShowError(e.what(), "Fatal Error");
#else
    fprintf(stderr, "Fatal: %s\n", e.what());
#endif
  }

  g_app.Shutdown();

  Mem_FreeAll(); // force free allocated memory

#if defined(_WIN32) && defined(_DEBUG) && defined(HOOK_ALLOC)
  _CrtSetAllocHook(NULL);
#endif

  return 0;
}
