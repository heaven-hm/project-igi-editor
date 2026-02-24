/******************************************************************************
 * @file    main.cpp
 * @brief   setup glut framework
 *****************************************************************************/

#include "pch.h"
#include <freeglut.h>

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

// exit
constexpr int MENU_CLOSE = 61;

// menus
static int g_menu_draw_parts;
static int g_menu_terrain_draw_opts;
static int g_menu_terrain_modifier_opts;
static int g_menu_choose_level;
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
constexpr int UPDATE_MENU_CHOOSE_LEVEL = FLAG_BIT(4);

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

static void OnMotion(int x, int y) {
	g_app.Input_OnMotion(x, y);
}

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

static void OnDisplay() {
	g_app.OnDisplay();
}

static void UpdateOverlayWireframeMenuText();
static void UpdateDrawPartsMenuText();
static void UpdateTerrainDrawOptionsMenuText();
static void UpdateTerrainModOptionsMenuText();
static void UpdateChooseLevelMenuText();

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
# if defined(_DEBUG) && defined(HOOK_ALLOC)
	_CrtSetAllocHook(NULL);
# endif
#endif
}

static void UpdateOverlayWireframeMenuText() {
	glutSetMenu(g_main_menu);

	glutChangeToMenuEntry(1,
		g_app.GetOverlayWireframe() ? "Wireframe (*)" : "Wireframe    ",
		MENU_OVERLAY_WIREFRAME);
}

static void UpdateDrawPartsMenuText() {
	int draw_parts = g_app.GetDrawParts();

	glutSetMenu(g_menu_draw_parts);

	if (draw_parts & Renderer::DRAW_SKYDOME) {
		glutChangeToMenuEntry(1, "Skydome      [+]", MENU_DRAW_SKYDOME);
	}
	else {
		glutChangeToMenuEntry(1, "Skydome      [-]", MENU_DRAW_SKYDOME);
	}

	if (draw_parts & Renderer::DRAW_FLAT_SKY_LAYER) {
		glutChangeToMenuEntry(2, "FlatSkyLayer [+]", MENU_DRAW_FLAT_SKY_LAYER);
	}
	else {
		glutChangeToMenuEntry(2, "FlatSkyLayer [-]", MENU_DRAW_FLAT_SKY_LAYER);
	}

	if (draw_parts & Renderer::DRAW_TERRAIN) {
		glutChangeToMenuEntry(3, "Terrain      [+]", MENU_DRAW_TERRAIN);
	}
	else {
		glutChangeToMenuEntry(3, "Terrain      [-]", MENU_DRAW_TERRAIN);
	}
}

static void UpdateTerrainDrawOptionsMenuText() {
	int opts = g_app.GetTerrainDrawOptions();

	glutSetMenu(g_menu_terrain_draw_opts);

	if (opts & Renderer_Terrain::DRAW_TERRAIN_OPT_MAT) {
		glutChangeToMenuEntry(1, "Texture   [+]", MENU_DRAW_TERRAIN_OPT_MAT);
	}
	else {
		glutChangeToMenuEntry(1, "Texture   [-]", MENU_DRAW_TERRAIN_OPT_MAT);
	}

	if (opts & Renderer_Terrain::DRAW_TERRAIN_OPT_LMP) {
		glutChangeToMenuEntry(2, "Light Map [+]", MENU_DRAW_TERRAIN_OPT_LMP);
	}
	else {
		glutChangeToMenuEntry(2, "Light Map [-]", MENU_DRAW_TERRAIN_OPT_LMP);
	}

	if (opts & Renderer_Terrain::DRAW_TERRAIN_OPT_FOG) {
		glutChangeToMenuEntry(3, "Fog       [+]", MENU_DRAW_TERRAIN_OPT_FOG);
	}
	else {
		glutChangeToMenuEntry(3, "Fog       [-]", MENU_DRAW_TERRAIN_OPT_FOG);
	}
}

static void UpdateTerrainModOptionsMenuText() {
	int opts = g_app.GetTerrainModOptions();

	glutSetMenu(g_menu_terrain_modifier_opts);

	if (opts & TERRAIN_TEXTURE_MOD) {
		glutChangeToMenuEntry(1, "Texture Modifier [+]", MENU_TERRAIN_TEX_MOD);
	}
	else {
		glutChangeToMenuEntry(1, "Texture Modifier [-]", MENU_TERRAIN_TEX_MOD);
	}

	if (opts & TERRAIN_HEIGHT_MOD) {
		glutChangeToMenuEntry(2, "Height Map       [+]", MENU_TERRAIN_HEIGHT_MOD);
	}
	else {
		glutChangeToMenuEntry(2, "Height Map       [-]", MENU_TERRAIN_HEIGHT_MOD);
	}

	if (opts & TERRAIN_DISCARD_MOD) {
		glutChangeToMenuEntry(3, "Discard Terrain  [+]", MENU_TERRAIN_DISCARD_MOD);
	}
	else {
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
		}
		else {
			Str_SPrintf(s, 64, "Level %2d    ", i);
		}

		glutChangeToMenuEntry(i, s, i);
	}
}

static void OnMenu(int menu) {
	if (menu >= MENU_LEVEL_FIRST && menu <= MENU_LEVEL_LAST) {
		g_app.LoadLevel(menu);

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
	case MENU_CLOSE:
		glutLeaveMainLoop();
		break;
	}
}

#define HOOK_ALLOC

#if defined(_WIN32) &&  defined(_DEBUG) && defined(HOOK_ALLOC)
static int CustomAllocHook(int alloc_type, void* user_data, size_t size, int
	block_type, long request_number, const unsigned char* filename, int line_number)
{
	int bk = 0;

	if (_CRT_BLOCK == block_type) { // _CRT_BLOCK used by c runtime library, must be ignore
		return TRUE;
	}

	if (_HOOK_ALLOC == alloc_type) {
		bk = alloc_type;
	}
	else if (_HOOK_REALLOC == alloc_type) {
		bk = alloc_type;
	}
	else if (_HOOK_FREE == alloc_type) {
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
#if defined(_WIN32) && defined(_DEBUG)
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);	// detect memory leak
# if defined(HOOK_ALLOC)
	_CrtSetAllocHook(CustomAllocHook);
# endif
#endif

	// setup path of res and shaders folders
	Folders_Init();

	// read window width & height from command line
	int wnd_w = Arg_ReadInt(argc, argv, "-w", 800);
	int wnd_h = Arg_ReadInt(argc, argv, "-h", 600);

	glutInit(&argc, argv);
	glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGBA | GLUT_DEPTH);
	
	// center display
	int screen_cx = glutGet(GLUT_SCREEN_WIDTH);
	int screen_cy = glutGet(GLUT_SCREEN_HEIGHT);
	int pos_x = (screen_cx - wnd_w) >> 1;
	int pos_y = (screen_cy - wnd_h) >> 1;
	glutInitWindowPosition(pos_x, pos_y);
	glutInitWindowSize(wnd_w, wnd_h);
	glutCreateWindow("Terrain Demo [right button to bring the context menu]");

	if (!GL_Init()) {
		return 1;
	}

	if (g_gl_info.support_version_45_) {
		Str_Cat(g_folders.shader_folder_, count_of(g_folders.shader_folder_), "/45");
	}
	else {
		// try version 4.1
		Str_Cat(g_folders.shader_folder_, count_of(g_folders.shader_folder_), "/41");
	}

	const char* HINT =
		"Alt+Enter:  toggle fullscreen mode\n"
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

	// setup glut callbacks
	glutReshapeFunc(OnReshape);
	glutMouseFunc(OnMouse);
	glutMotionFunc(OnMotion);
	glutPassiveMotionFunc(OnMotion); // trace cursor movement even cursor outside the viewport
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

	g_main_menu = glutCreateMenu(OnMenu);
	glutAddMenuEntry("", MENU_OVERLAY_WIREFRAME);
	glutAddSubMenu("Draw Parts", g_menu_draw_parts);
	glutAddSubMenu("Terrain Draw Options", g_menu_terrain_draw_opts);
	glutAddSubMenu("Terrain Mod Options", g_menu_terrain_modifier_opts);
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

	try {
		// enter main loop
		glutMainLoop();
	}
	catch (const std::exception & e) {
#if defined(_WIN32)
		MessageBoxA(NULL, e.what(), "Fatal", MB_ICONSTOP);
#else
		fprintf(stderr, "Fatal: %s\n", e.what());
#endif
	}

	g_app.Shutdown();

	Mem_FreeAll();	// force free allocated memory

#if defined(_WIN32) && defined(_DEBUG) && defined(HOOK_ALLOC)
	_CrtSetAllocHook(NULL);
#endif

	return 0;
}
