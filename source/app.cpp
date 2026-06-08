/******************************************************************************
 * @file    app.cpp
 * @brief   application class
 *****************************************************************************/

#include "pch.h"
#include <cstdlib>
#include <stdexcept>
#include <freeglut.h>
#include "logger.h"
#include "utils.h"
#include "parsers/qsc_lexer.h"
#include "parsers/qsc_parser.h"
#include "parsers/qvm_compiler.h"
#include "parsers/qvm_parser.h"
#include "parsers/qvm_decompiler.h"
#include "cli/asset_extractor.h"
#include "parsers/dat_parser.h"
#include "parsers/tex_parser.h"
#include "parsers/res_parser.h"
#include "renderer/gl_helper.h"
#include "level/task_schema.h"
using namespace TaskSchemaNS;
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <unordered_map>

static glm::dmat3 BuildRotMatZXY(const glm::dvec3& euler);
static glm::dvec3 ExtractEulerZXY(const glm::dmat3& M);
/*
================================================================================
 Game monitor thread — blocks until game process exits, then signals main thread
================================================================================
*/
struct GameMonitorParam {
	HANDLE             hProcess;
	std::atomic<bool>* pExited;
};

static DWORD WINAPI GameMonitorProc(LPVOID param) {
	auto* p = static_cast<GameMonitorParam*>(param);
	HANDLE h      = p->hProcess;
	auto*  pExited = p->pExited;
	delete p;
	WaitForSingleObject(h, INFINITE);
	pExited->store(true, std::memory_order_release);
	return 0;
}

// ── Global hotkey support ────────────────────────────────────────────────────
// We subclass GLUT's window so WM_HOTKEY messages reach our code even when
// the editor is iconified and the game has keyboard focus.
constexpr int HOTKEY_ID_TOGGLE_GAME = 0x47; // arbitrary non-conflicting ID

static WNDPROC g_origEditorWndProc = nullptr;
static App*    g_appForHotkey      = nullptr;

static LRESULT CALLBACK EditorSubclassWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	if (msg == WM_HOTKEY && static_cast<int>(wParam) == HOTKEY_ID_TOGGLE_GAME) {
		Logger::Get().Log(LogLevel::INFO, "[ToggleGame] Global hotkey fired");
		if (g_appForHotkey) g_appForHotkey->LaunchGame();
		return 0;
	}
	return CallWindowProc(g_origEditorWndProc, hwnd, msg, wParam, lParam);
}

/*
================================================================================
 App
================================================================================
*/
constexpr float	MOUSE_SENSITIVE = 0.2f;

// movement
constexpr float		VIEW_HEIGHT = 7000.0f;
constexpr float		GRAVITE = 10.0f * WORLD_UNITS_PER_METER;
constexpr float		MIN_MOVE_SPEED = 8.0f * WORLD_UNITS_PER_METER;
constexpr float		MAX_MOVE_SPEED = 8192.0f * WORLD_UNITS_PER_METER;
constexpr float		MIN_JUMP_SPEED = 4.0f * WORLD_UNITS_PER_METER;
constexpr float		MAX_JUMP_SPEED = 512.0f * WORLD_UNITS_PER_METER;

// movement key down flags
constexpr int MK_FORWARD		= FLAG_BIT(0);
constexpr int MK_BACKWARD		= FLAG_BIT(1);
constexpr int MK_LEFT			= FLAG_BIT(2);
constexpr int MK_RIGHT			= FLAG_BIT(3);
constexpr int MK_STRAIGHT_UP	= FLAG_BIT(4);
constexpr int MK_STRAIGHT_DOWN	= FLAG_BIT(5);
constexpr int MK_JUMP			= FLAG_BIT(6);
constexpr int MK_ROLL_INC		= FLAG_BIT(7);
constexpr int MK_ROLL_DEC		= FLAG_BIT(8);

// IGI 2 Style Manipulation Flags
constexpr int MK_MANIP_A		= FLAG_BIT(10);
constexpr int MK_MANIP_B		= FLAG_BIT(11);
constexpr int MK_MANIP_G		= FLAG_BIT(12);
constexpr int MK_MANIP_S		= FLAG_BIT(13);
constexpr int MK_MANIP_O		= FLAG_BIT(14);
constexpr int MK_MANIP_SPACE	= FLAG_BIT(15);

App::App():
	frame_(0),
	terrain_mod_options_(-1),
	edit_mode_(true), // Enable by default as requested
	terrain_edit_enabled_(false),
	pause_mode_(false),
	edit_brush_(0), // 0: raise, 1: lower
	selected_object_index_(0),
	hover_object_index_(-1),
	show_hud_(true),
	show_debug_(false),
	show_help_(false),
	show_magic_obj_spheres_(false),
	tree_scroll_offset_(0),
	tree_decl_expanded_(false),
	status_message_(),
	noclip_mode_(true), // By default true as requested by user
	prior_frame_time_(0),
	skip_input_on_motion_once_(false)
{
	view_define_.pos_ = glm::vec3(0.0f);
	view_define_.forward_ = VEC3_Y_DIR;
	view_define_.right_ = VEC3_X_DIR;
	view_define_.up_ = VEC3_Z_DIR;
	view_define_.fovx_ = glm::radians(FOVY_IN_DEGREE);
	view_define_.fovy_ = glm::radians(FOVY_IN_DEGREE);
	view_define_.render_z_near_ = RENDER_Z_NEAR;
	view_define_.render_z_far_ = RENDER_Z_FAR;
	view_define_.render_min_depth_ = RENDER_DEPTH_MIN;
	view_define_.render_max_depth_ = RENDER_DEPTH_MAX;
	view_define_.viewport_width_ = 1;
	view_define_.viewport_height_ = 1;

	draw_params_.view_define_ = &view_define_;
	draw_params_.overlay_wireframe_ = false;
	draw_params_.draw_parts_ = -1;
	draw_params_.draw_terrain_options_ = -1;
	draw_params_.flat_sky_layer_is_visible_ = true;
	draw_params_.num_terrain_render_chunk_ = 0;
	draw_params_.selected_object_index_ = -1;

	memset(&window_state_, 0, sizeof(window_state_));
	memset(&mouse_state_, 0, sizeof(mouse_state_));
	memset(&input_, 0, sizeof(input_));

	window_state_.cursor_visible_ = true;

	memset(&viewer_, 0, sizeof(viewer_));
	viewer_.clip_to_z_ = false;
	viewer_.move_speed_ = MIN_MOVE_SPEED;
	viewer_.jump_speed_ = MIN_JUMP_SPEED;
	window_state_.cursor_visible_ = true;
}

App::~App() {
	Shutdown();
}

bool App::Init(int argc, char** argv) {
	// Initialize logger with absolute path to exe directory
	std::string exeDir = Utils::GetExeDirectory();
	Logger::Get().Init(exeDir + "\\igi1ed.log");
	Logger::Get().Log(LogLevel::INFO, "IGI Editor Initializing...");

	if (!renderer_.Init()) {
		return false;
	}

	ConfigData& cfg = Config::Get();


	// read options from command line
	draw_params_.overlay_wireframe_ = Arg_OptionIdx(argc, argv, "-wireframe") > 0;
	draw_params_.draw_parts_ = Arg_ReadInt(argc, argv, "-draw_parts", -1);
	draw_params_.draw_terrain_options_ = Arg_ReadInt(argc, argv, "-draw_terrain_opts", -1);
	terrain_mod_options_ = Arg_ReadInt(argc, argv, "-terrain_mod_opts", terrain_mod_options_);
	stick_to_ground_ = Arg_OptionIdx(argc, argv, "-stick_to_ground") > 0;

	int start_level = Arg_ReadInt(argc, argv, "-level", cfg.level);
	if (start_level >= MIN_LEVEL_NO && start_level <= MAX_LEVEL_NO) {
		try {
			LoadLevel(start_level);
		}
		catch (const std::exception& e) {
			std::string errorMsg = "Failed to load level " + std::to_string(start_level) + ":\n" + std::string(e.what());
			Utils::LogAndShowError(errorMsg, "IGI Editor - Error");
			Logger::Get().Log(LogLevel::ERR, errorMsg);
		}
		catch (...) {
			std::string errorMsg = "Failed to load level " + std::to_string(start_level) + ":\nUnknown error occurred.";
			Utils::LogAndShowError(errorMsg, "IGI Editor - Error");
			Logger::Get().Log(LogLevel::ERR, errorMsg);
		}
	}


	if (Arg_OptionIdx(argc, argv, "-yaw") > -1) {
		// override yaw
		viewer_.yaw_ = Arg_ReadFloat(argc, argv, "-yaw", 0.0f);
		UpdateViewerVectors();
	}

	if (Arg_OptionIdx(argc, argv, "-pitch") > -1) {
		// override pitch
		viewer_.pitch_ = Arg_ReadFloat(argc, argv, "-pitch", 0.0f);
		UpdateViewerVectors();
	}

	int wnd_w = Arg_ReadInt(argc, argv, "-w", 800);
	int wnd_h = Arg_ReadInt(argc, argv, "-h", 600);
	OnWindowResize(wnd_w, wnd_h);

	prior_frame_time_ = Sys_Milliseconds();

	bridge_.SetEnabled(show_hud_);
	bridge_.Start();


	// Set initial cursor state
	LoadAllCursors();
	LoadHelpEntries();
	LoadAutoCompleteKeywords();
	glutSetCursor(GLUT_CURSOR_NONE);

	// Cache editor HWND for minimize/restore around game launch
	editor_hwnd_ = Utils::FindWindow("IGI Editor");
	if (!editor_hwnd_) editor_hwnd_ = GetActiveWindow();

	// Subclass GLUT's window so WM_HOTKEY messages reach EditorSubclassWndProc
	// even when the editor is iconified and the game holds keyboard focus.
	if (editor_hwnd_) {
		g_appForHotkey     = this;
		g_origEditorWndProc = reinterpret_cast<WNDPROC>(
			SetWindowLongPtr(editor_hwnd_, GWLP_WNDPROC,
			                 reinterpret_cast<LONG_PTR>(EditorSubclassWndProc)));
		Logger::Get().Log(LogLevel::INFO, "[App] Editor window subclassed for global hotkey (HWND=" +
		                  std::to_string(reinterpret_cast<uintptr_t>(editor_hwnd_)) + ")");
	} else {
		Logger::Get().Log(LogLevel::WARNING, "[App] editor_hwnd_ is NULL — global hotkey will not work");
	}

	return true;
}

void App::Shutdown() {
	if (game_process_.running) {
		// Wait briefly for monitor thread (it's blocking on the game process handle)
		if (game_process_.hMonitorThread) {
			WaitForSingleObject(game_process_.hMonitorThread, 500);
			CloseHandle(game_process_.hMonitorThread);
		}
		CloseHandle(game_process_.hProcess);
		CloseHandle(game_process_.hThread);
		game_process_ = {};
	}
	bridge_.Stop();
	level_.Unload();
	level_.FreeTerrainCubeDataPools();
	renderer_.Shutdown();
	if (!g_isCLIMode) {
		AssetExtractor::CleanupExtractedAssets(Utils::GetExeDirectory());
	}
}

// ── C1: Custom SPR cursor — multi-mode ────────────────────────────────────────

static GLuint LoadOneSpr(const char* path, int& w, int& h) {
	TEXFile tex = TEX_Parse(path);
	if (!tex.valid || tex.images.empty()) return 0;
	const TEXImage& img = tex.images[0];

	std::vector<uint8_t> rgba;
	if (img.mode == 2) { // RGB565 — no alpha channel; chroma-key dark pixels as transparent
		rgba.reserve(img.width * img.height * 4);
		for (size_t i = 0; i + 1 < img.pixels.size(); i += 2) {
			uint16_t p = img.pixels[i] | ((uint16_t)img.pixels[i + 1] << 8);
			uint8_t r = ((p >> 11) & 0x1F) << 3;
			uint8_t g = ((p >> 5)  & 0x3F) << 2;
			uint8_t b =  (p        & 0x1F) << 3;
			// Chroma-key: near-black pixels become transparent background
			uint8_t a = (r < 30 && g < 30 && b < 30) ? 0 : 255;
			rgba.push_back(r);
			rgba.push_back(g);
			rgba.push_back(b);
			rgba.push_back(a);
		}
	} else { // ARGB8888 (modes 3, 67) — swizzle BGRA → RGBA
		rgba.resize(img.pixels.size());
		for (size_t i = 0; i + 3 < img.pixels.size(); i += 4) {
			rgba[i + 0] = img.pixels[i + 2]; // R ← B
			rgba[i + 1] = img.pixels[i + 1]; // G
			rgba[i + 2] = img.pixels[i + 0]; // B ← R
			rgba[i + 3] = img.pixels[i + 3]; // A
		}
	}

	pic_s pic;
	pic.width_  = (int)img.width;
	pic.height_ = (int)img.height;
	pic.pixels_ = rgba.data();
	w = (int)img.width;
	h = (int)img.height;
	return GL_RegisterTexture(&pic, GL_CLAMP_TO_EDGE, GL_LINEAR, GL_LINEAR, false);
}

void App::LoadAllCursors() {
	// Order must match CursorMode enum values (0..9)
	const char* paths[NUM_CURSORS] = {
		"content\\qed\\TerrainEditIcon_Pointer.spr",     // 0 Default
		"content\\qed\\highlighttool.spr",               // 1 Hover
		"content\\qed\\activetool.spr",                  // 2 Selected
		"content\\qed\\TerrainEditIcon_Lift.spr",        // 3 TerrainLift
		"content\\qed\\TerrainEditIcon_Lower.spr",       // 4 TerrainLower
		"content\\qed\\TerrainEditIcon_Flatten.spr",     // 5 TerrainFlatten
		"content\\qed\\TerrainEditIcon_FlattenLine.spr", // 6 TerrainFlattenLine
		"content\\qed\\TerrainEditIcon_Drop.spr",        // 7 TerrainDrop
		"content\\qed\\TerrainEditIcon_Soften.spr",      // 8 TerrainSoften
		"content\\qed\\inactivetool.spr",                // 9 Inactive
		"content\\qed\\editor_camera.spr",               // 10 Camera (ALT held)
		"content\\qed\\editor_move.spr",                 // 11 Move (ALT held + moving)
	};
	cursor_loaded_count_ = 0;
	for (int i = 0; i < NUM_CURSORS; ++i) {
		cursor_tex_ids_[i] = LoadOneSpr(paths[i], cursor_tex_ws_[i], cursor_tex_hs_[i]);
		if (cursor_tex_ids_[i]) cursor_loaded_count_++;
	}

	// Cache AITYPE_ model IDs from IGIModels.json
	ai_model_ids_.clear();
	{
		std::string jsonPath = Utils::GetExeDirectory() + "\\content\\tools\\IGIModels.json";
		std::ifstream jf(jsonPath);
		if (jf) {
			std::string content((std::istreambuf_iterator<char>(jf)), {});
			size_t pos = 0;
			while ((pos = content.find("\"AITYPE_", pos)) != std::string::npos) {
				size_t idPos = content.find("\"ModelId\"", pos);
				if (idPos != std::string::npos && idPos - pos < 200) {
					size_t q1 = content.find('"', idPos + 9);
					size_t q2 = (q1 != std::string::npos) ? content.find('"', q1 + 1) : std::string::npos;
					if (q1 != std::string::npos && q2 != std::string::npos)
						ai_model_ids_.insert(content.substr(q1 + 1, q2 - q1 - 1));
				}
				pos++;
			}
		}
		Logger::Get().Log(LogLevel::INFO, "[App] Loaded " + std::to_string(ai_model_ids_.size()) + " AITYPE_ model IDs");
	}
}

void App::LoadHelpEntries() {
	help_entries_.clear();
	std::vector<std::string> paths = {
		Utils::GetExeDirectory() + "\\content\\qed\\qedkeybindings.qsc",
		Utils::GetExeDirectory() + "\\qed\\qedkeybindings.qsc",
		Utils::GetIGIRootPath() + "\\content\\qed\\qedkeybindings.qsc",
		Utils::GetIGIRootPath() + "\\qed\\qedkeybindings.qsc",
		"content\\qed\\qedkeybindings.qsc",
		"qed\\qedkeybindings.qsc"
	};
	std::ifstream f;
	for (const auto& p : paths) {
		f.open(p);
		if (f.is_open()) {
			Logger::Get().Log(LogLevel::INFO, "[App] Loaded keybindings from: " + p);
			break;
		}
		f.clear();
	}
	if (!f.is_open()) {
		Logger::Get().Log(LogLevel::WARNING, "[App] Could not load help keybindings file: qedkeybindings.qsc");
		return;
	}
	std::string line;
	while (std::getline(f, line)) {
		// Strip leading whitespace
		size_t start = line.find_first_not_of(" \t\r\n");
		if (start == std::string::npos) continue;
		line = line.substr(start);
		if (line.empty() || line[0] == '/' ) continue; // skip blank/comment lines
		// Keep SetEventBinding lines; format them as "  Key  =>  Event"
		if (line.find("SetEventBinding") == 0 || line.find("QEDSetEventBinding") == 0) {
			// extract: SetEventBinding("EventName", "KeyCombo") or QEDSetEventBinding(...)
			size_t q1 = line.find('"');
			size_t q2 = (q1 != std::string::npos) ? line.find('"', q1 + 1) : std::string::npos;
			size_t q3 = (q2 != std::string::npos) ? line.find('"', q2 + 1) : std::string::npos;
			size_t q4 = (q3 != std::string::npos) ? line.find('"', q3 + 1) : std::string::npos;
			if (q1 != std::string::npos && q2 != std::string::npos &&
			    q3 != std::string::npos && q4 != std::string::npos) {
				std::string evtName = line.substr(q1 + 1, q2 - q1 - 1);
				std::string keyCombo = line.substr(q3 + 1, q4 - q3 - 1);
				// Replace angle-bracket modifiers for readability
				auto repl = [](std::string s, const std::string& from, const std::string& to) {
					size_t p = 0;
					while ((p = s.find(from, p)) != std::string::npos) { s.replace(p, from.size(), to); p += to.size(); }
					return s;
				};
				keyCombo = repl(keyCombo, "<Alt>", "Alt+");
				keyCombo = repl(keyCombo, "<Ctrl>", "Ctrl+");
				keyCombo = repl(keyCombo, "<Control>", "Ctrl+");
				keyCombo = repl(keyCombo, "<Shift>", "Shift+");
				keyCombo = repl(keyCombo, "<", "");
				keyCombo = repl(keyCombo, ">", "");
				help_entries_.push_back(keyCombo + "  =>  " + evtName);
			}
		}
	}
}

void App::LoadAutoCompleteKeywords() {
	autocomplete_keywords_.clear();
	std::string path = Utils::GetExeDirectory() + "\\content\\tools\\AutoCompleteKeywords.txt";
	std::ifstream f(path);
	if (!f.is_open()) {
		// try IGI root fallback
		path = Utils::GetIGIRootPath() + "\\content\\tools\\AutoCompleteKeywords.txt";
		f.open(path);
	}
	if (!f.is_open()) { Logger::Get().Log(LogLevel::WARNING, "[App] AutoCompleteKeywords.txt not found"); return; }
	std::string line;
	while (std::getline(f, line)) {
		if (!line.empty() && line.back() == '\r') line.pop_back();
		if (!line.empty()) autocomplete_keywords_.push_back(line);
	}
	Logger::Get().Log(LogLevel::INFO, "[App] Loaded " + std::to_string(autocomplete_keywords_.size()) + " autocomplete keywords");
}

void App::SaveTaskSubtreeToFile(int idx, const std::string& path) {
	auto& objects = level_.GetLevelObjects().GetObjects();
	if (idx < 0 || idx >= (int)objects.size()) { status_message_ = "Save task: no task selected"; return; }
	// Serialize ONLY the selected task + its descendants as a proper nested QSC block.
	// (Previously this wrote each object's raw qscLine, but a parent's qscLine holds the
	//  whole original nested block, so it exported the entire object.)
	level_.GetLevelObjects().SaveSubtreeToQSC(idx, path);
	status_message_ = "Saved task to: " + path;
	Config::Get().taskFileName = path;
}

void App::ConfirmFileDialog() {
	std::string path = file_dialog_path_;
	FileDialogMode mode = file_dialog_mode_;
	file_dialog_mode_ = FileDialogMode::None;
	if (mode != FileDialogMode::SaveObjectFile)
		Config::Get().taskFileName = path; // remember last sub-task path only

	if (mode == FileDialogMode::SaveObjectFile) {
		// Ctrl+S: write the live objects QSC to the user-specified path.
		level_.GetLevelObjects().SaveToQSC(path);
		status_message_ = "Saved objects file: " + path;
	} else if (mode == FileDialogMode::SaveSubTask) {
		SaveTaskSubtreeToFile(selected_object_index_, path);
	} else if (mode == FileDialogMode::SaveSubTaskParent) {
		auto& objects = level_.GetLevelObjects().GetObjects();
		if (selected_object_index_ >= 0) {
			int par = objects[selected_object_index_].parentIndex;
			if (par >= 0) SaveTaskSubtreeToFile(par, path);
		}
	} else if (mode == FileDialogMode::LoadSubTask) {
		std::ifstream f(path);
		if (!f.is_open()) { status_message_ = "Error: cannot read " + path; return; }
		std::string block((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
		f.close();
		block = Utils::Trim(block);
		if (block.empty()) { status_message_ = "LoadSubTask: file is empty"; return; }

		PushUndoState();
		// Flush current live objects to the temp QSC, append the loaded task block as a
		// new top-level task, then reparse the whole level from the merged QSC. Using the
		// real parser keeps nested subtrees intact.
		std::string tempQsc = Utils::GetExeDirectory() + "\\content\\qed\\temp\\objects.qsc";
		level_.GetLevelObjects().SaveToQSC(tempQsc);
		{
			std::ofstream out(tempQsc, std::ios::app);
			if (!out.is_open()) { status_message_ = "LoadSubTask: cannot write temp QSC"; return; }
			out << "\n" << block;
			if (block.back() != ';') out << ";";
			out << "\n";
		}
		level_.ReloadObjectsFromFile(tempQsc);
		EvaluateTrainTrackPositions();
		SnapObjectsToTerrain();
		RebuildLevelModelIds();
		// Object list changed — keep selection valid.
		if (selected_object_index_ >= (int)level_.GetLevelObjects().GetObjects().size())
			selected_object_index_ = -1;
		status_message_ = "Loaded task from: " + path + " (added as top-level task)";
		Logger::Get().Log(LogLevel::INFO, "[App] LoadSubTask: loaded " + path);
	}
}

void App::UpdateCursorMode() {
	if (terrain_edit_enabled_) {
		// Drop is the neutral terrain cursor; Lift/Lower for raise/lower brush
		if (edit_brush_ == 0)
			current_cursor_mode_ = CursorMode::TerrainLift;  // BRUSH_RAISE
		else if (edit_brush_ == 1)
			current_cursor_mode_ = CursorMode::TerrainLower; // BRUSH_LOWER
		else
			current_cursor_mode_ = CursorMode::TerrainDrop;  // any other terrain mode
		return;
	}
	// Camera mode (ALT held)
	bool enableCameraMode = Utils::IsKeyBindingPressed(Config::Get().keyEnableCamera);
	if (enableCameraMode) {
		// ALT + left button held = camera look (editor_camera.spr)
		// ALT + any movement = lateral move (editor_move.spr)
		current_cursor_mode_ = mouse_state_.left_button_down_ ? CursorMode::Camera : CursorMode::Move;
		camera_mode_moved_ = false;
		return;
	}
	current_cursor_mode_ = CursorMode::Default;
}

void App::DrawCustomCursor() {
	UpdateCursorMode();
	int idx = (int)current_cursor_mode_;
	if (idx < 0 || idx >= NUM_CURSORS || !cursor_tex_ids_[idx]) {
		idx = 0; // fallback to Default
		if (!cursor_tex_ids_[0]) return;
	}
	int mx = mouse_state_.prior_x_;
	int my = mouse_state_.prior_y_;
	int vw = window_state_.viewport_width_;
	int vh = window_state_.viewport_height_;
	int w  = cursor_tex_ws_[idx];
	int h  = cursor_tex_hs_[idx];

	// Match the renderer's fixed-function HUD state: a shader/VAO left bound from
	// the 3D pass makes immediate-mode textured quads draw nothing.
	glUseProgram(0);
	glBindVertexArray(0);
	glBindBufferBase(GL_UNIFORM_BUFFER, 0, 0);
	glActiveTexture(GL_TEXTURE0);

	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	glDisable(GL_LIGHTING);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity();
	glOrtho(0, vw, vh, 0, -1, 1);   // y=0 at top (top-down to match mouse coords)
	glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity();

	glEnable(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, cursor_tex_ids_[idx]);
	glColor4f(1.f, 1.f, 1.f, 1.f);
	glBegin(GL_QUADS);
	  glTexCoord2f(0, 0); glVertex2i(mx,     my);
	  glTexCoord2f(1, 0); glVertex2i(mx + w, my);
	  glTexCoord2f(1, 1); glVertex2i(mx + w, my + h);
	  glTexCoord2f(0, 1); glVertex2i(mx,     my + h);
	glEnd();
	glBindTexture(GL_TEXTURE_2D, 0);
	glDisable(GL_TEXTURE_2D);

	glMatrixMode(GL_MODELVIEW);  glPopMatrix();
	glMatrixMode(GL_PROJECTION); glPopMatrix();
	glEnable(GL_DEPTH_TEST);
}

// ─────────────────────────────────────────────────────────────────────────────

void App::LoadLevel(int level_no) {
	try {
		Logger::Get().Log(LogLevel::INFO, "[App] ==========================================");
		Logger::Get().Log(LogLevel::INFO, "[App] LoadLevel() START for level " + std::to_string(level_no));
		Logger::Get().Log(LogLevel::INFO, "[App] ==========================================");

		// Track in-session level switches. The FIRST load (last_loaded_level_ < 0) is
		// equivalent to a fresh process; any later load is a MENU/RELOAD switch whose
		// only difference from a fresh process is leftover per-level state.
		const bool is_switch = (last_loaded_level_ >= 0 && last_loaded_level_ != level_no);
		if (is_switch) {
			Logger::Get().Log(LogLevel::INFO, "[App] MENU/RELOAD switch from level " +
				std::to_string(last_loaded_level_) + " to " + std::to_string(level_no) +
				" — performing full previous-level teardown");
			AssetExtractor::ClearLevelAssets(last_loaded_level_, Utils::GetExeDirectory());
		} else {
			Logger::Get().Log(LogLevel::INFO, "[App] Initial level load (level " +
				std::to_string(level_no) + ")");
		}

		AssetExtractor::ClearLevelAssets(level_no, Utils::GetExeDirectory());

		// Verify level number is valid
		if (level_no < MIN_LEVEL_NO || level_no > MAX_LEVEL_NO) {
			std::string errorMsg = "Invalid level number: " + std::to_string(level_no) + " (valid range: " + std::to_string(MIN_LEVEL_NO) + "-" + std::to_string(MAX_LEVEL_NO) + ")";
			Utils::LogAndShowError(errorMsg, "IGI Editor - Error");
			Logger::Get().Log(LogLevel::ERR, errorMsg);
			// Exit the application safely; destructor will be called automatically
			std::exit(EXIT_FAILURE);
		}
		if (Config::Get().enableBackup) {
			std::string gameLevelDir = Utils::GetIGIRootPath() + "\\missions\\location0\\level" + std::to_string(level_no);
			std::string backupLevelDir = Utils::GetExeDirectory() + "\\content\\backup\\level" + std::to_string(level_no);
			if (!std::filesystem::exists(backupLevelDir) && std::filesystem::exists(gameLevelDir)) {
				try {
					std::filesystem::create_directories(backupLevelDir);
					std::filesystem::copy(gameLevelDir, backupLevelDir, std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing);
					Logger::Get().Log(LogLevel::INFO, "[App] Backup created for level " + std::to_string(level_no) + " at " + backupLevelDir);
				} catch (const std::exception& e) {
					Logger::Get().Log(LogLevel::ERR, "[App] Failed to create level backup: " + std::string(e.what()));
				}
			}
		}
		
		selected_object_index_ = -1;
		hover_object_index_ = -1;
		status_message_.clear();

		// NOTE: We must NOT purge the previous level's extracted assets here. The
		// texture/model resolver (FindTextureFile step 6 + lazy cross-level extraction)
		// deliberately searches *other* levels' extracted folders to resolve cross-level
		// references (e.g. a level-6 object using a level-14 texture). Deleting them on
		// switch removes legitimate fallback sources. The renderer caches are still fully
		// torn down by BeginLoadLevel()/ClearCaches() below.
		renderer_.SetLevel(level_no);
		renderer_.BeginLoadLevel();
		Logger::Get().Log(LogLevel::INFO, "[App] After BeginLoadLevel teardown: renderer meshCache=" +
			std::to_string(renderer_.GetMeshCacheCount()) + " textureCache=" +
			std::to_string(renderer_.GetTextureCacheCount()) + " (both should be 0)");
		renderer_.SetSplineTerrainQuery([this](double x, double y, float& z) {
			return level_.GetTerrainZ(x, y, z);
		});

		Level::load_params_s level_load_params_s = {
			.level_no_ = level_no,
			.render_res_loader_ = &renderer_
		};

		// ── Loading overlay ──────────────────────────────────────────────────
		// Staged progress: the load is synchronous, so we redraw + swap the bar at
		// each milestone (10% → 100%). Reusable lambda fills proportionally to pct.
		auto drawLoadBar = [&](int pct, const char* stage) {
			pct = std::max(0, std::min(100, pct));
			int vw = window_state_.viewport_width_;
			int vh = window_state_.viewport_height_;
			glViewport(0, 0, vw, vh);
			glClearColor(0.02f, 0.06f, 0.02f, 1.0f);
			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
			glDisable(GL_DEPTH_TEST);
			glMatrixMode(GL_PROJECTION);
			glPushMatrix(); glLoadIdentity();
			glOrtho(0, vw, 0, vh, -1, 1);
			glMatrixMode(GL_MODELVIEW);
			glPushMatrix(); glLoadIdentity();

			int pw = 340, ph = 84;
			int px = (vw - pw) / 2, py = (vh - ph) / 2;
			glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			glColor4f(0.0f, 0.15f, 0.0f, 0.9f);
			glBegin(GL_QUADS);
			glVertex2i(px, py); glVertex2i(px+pw, py);
			glVertex2i(px+pw, py+ph); glVertex2i(px, py+ph);
			glEnd();
			glColor3f(0.0f, 0.8f, 0.0f);
			glBegin(GL_LINE_LOOP);
			glVertex2i(px, py); glVertex2i(px+pw, py);
			glVertex2i(px+pw, py+ph); glVertex2i(px, py+ph);
			glEnd();
			// Progress bar track
			int bx = px+20, by = py+18, bw = pw-40, bh = 12;
			glColor3f(0.0f, 0.3f, 0.0f);
			glBegin(GL_QUADS);
			glVertex2i(bx, by); glVertex2i(bx+bw, by);
			glVertex2i(bx+bw, by+bh); glVertex2i(bx, by+bh);
			glEnd();
			// Proportional fill
			int fw = bw * pct / 100;
			glColor3f(0.1f, 0.95f, 0.1f);
			glBegin(GL_QUADS);
			glVertex2i(bx, by); glVertex2i(bx+fw, by);
			glVertex2i(bx+fw, by+bh); glVertex2i(bx, by+bh);
			glEnd();
			glDisable(GL_BLEND);

			char load_msg[96];
			snprintf(load_msg, sizeof(load_msg), "Loading Level %d  -  %d%%  (%s)", level_no, pct, stage ? stage : "");
			glColor3f(0.0f, 0.9f, 0.0f);
			glRasterPos2i(px + 20, py + ph - 18);
			for (const char* c = load_msg; *c; ++c)
				glutBitmapCharacter(GLUT_BITMAP_HELVETICA_12, *c);

			glMatrixMode(GL_PROJECTION); glPopMatrix();
			glMatrixMode(GL_MODELVIEW);  glPopMatrix();
			glEnable(GL_DEPTH_TEST);
			glutSwapBuffers();
		};
		drawLoadBar(10, "reading level");
		// ─────────────────────────────────────────────────────────────────────

		drawLoadBar(25, "models & terrain");
		glm::vec3 start_pos;
		float start_yaw;
		if (level_.Load(level_load_params_s, start_pos, start_yaw)) {
			const auto& config = Config::Get();
			viewer_.pos_ = (config.cameraPosX != 0.0f || config.cameraPosY != 0.0f || config.cameraPosZ != 0.0f) ?
				glm::vec3(config.cameraPosX, config.cameraPosY, config.cameraPosZ) : start_pos;

			bool hasConfigOri = (config.cameraOriX != 0.0f || config.cameraOriY != 0.0f || config.cameraOriZ != 0.0f);
			if (hasConfigOri) {
				viewer_.yaw_   = config.cameraOriX;
				viewer_.pitch_ = config.cameraOriY;
				viewer_.roll_  = config.cameraOriZ;
			} else {
				// Convert game yaw (radians) to viewer yaw (degrees, 0=+Y north, CW).
				// Add 180° so the editor camera faces toward objects rather than with them.
				viewer_.yaw_   = glm::degrees(atan2f(-cosf(start_yaw), sinf(start_yaw))) + 180.0f;
				viewer_.pitch_ = 10.0f;
				viewer_.roll_  = 0.0f;
			}

			UpdateViewerVectors();
			Logger::Get().Log(LogLevel::INFO, "[App] Level " + std::to_string(level_no) + " loaded. Viewer start=(" + std::to_string(viewer_.pos_.x) + "," + std::to_string(viewer_.pos_.y) + "," + std::to_string(viewer_.pos_.z) + ") yaw=" + std::to_string(viewer_.yaw_));
			last_loaded_level_ = level_no;
		}
		else {
			std::string errorMsg = "Failed to load level " + std::to_string(level_no) + "\n\nPlease check if the terrain files exist in the correct location.";
			Utils::ShowError(errorMsg, "IGI Editor - Error");
			Logger::Get().Log(LogLevel::ERR, "[App] Failed to load level " + std::to_string(level_no));
		}


		drawLoadBar(60, "tracks");
		// Step 2: Track Evaluation (Dynamic object placement along paths)
		EvaluateTrainTrackPositions();

		drawLoadBar(75, "snapping objects");
		// Step 3: Always snap objects to terrain after any level load
		Logger::Get().Log(LogLevel::INFO, "[App] Step 3: Snapping objects to terrain...");
		SnapObjectsToTerrain();
		drawLoadBar(90, "finalizing");

		// AI rotation override: AI models (HumanSoldier, HumanAI) only have horizontal rotation
		auto& objects = level_.GetLevelObjects().GetObjects();

		// Build runtime parameter schemas from this level's Task_DeclareParameters so
		// the property editor can expose EVERY declared field of any task type.
		TaskSchemaNS::ClearRegisteredSchemas();
		for (const auto& obj : objects) {
			if (obj.type == "Task_DeclareParameters" && obj.argTokens.size() >= 3) {
				std::string typeName = obj.argTokens[0];
				if (typeName.size() >= 2 && typeName.front() == '"' && typeName.back() == '"')
					typeName = typeName.substr(1, typeName.size() - 2);
				TaskSchemaNS::RegisterSchema(typeName, TaskSchemaNS::ParseDeclaration(obj.argTokens));
			}
		}

		for (auto& obj : objects) {
			if (obj.modelId == "000_01_1") continue; // Skip Player Jones
			if (obj.type == "HumanSoldier" || obj.type == "HumanAI" || obj.type.find("AITYPE") == 0) {
				obj.rot.x = 0.0;           // PITCH = 0
				obj.rot.y = 0.0;           // ROLL = 0
				// Preserve existing rotation if it's already set, otherwise default to a full circle
				if (obj.rot.z == 0.0) obj.rot.z = 6.28318;
				Logger::Get().Log(LogLevel::INFO, "[App] Applied AI rotation override (horizontal only) for " + obj.name + " (" + obj.type + ")");
			}
		}

		// Log all loaded objects for verification script
		for (const auto& obj : objects) {
			if (obj.isSplineWaypoint || !obj.segmentModelId.empty()) {
			    Logger::Get().Log(LogLevel::INFO, "[App_Debug] Found waypoint/segment. modelId=" + obj.modelId + " segmentModelId=" + obj.segmentModelId);
			}
			std::string mId = !obj.modelId.empty() ? obj.modelId : obj.segmentModelId;
			if (obj.deleted || mId.empty()) continue;
			Logger::Get().Log(LogLevel::INFO, "[LevelLoader] Object Loaded: ModelID=" + mId +
				", Type=" + obj.type + ", Name=" + obj.name + ", Pos=(" +
				std::to_string(obj.pos.x) + ", " + std::to_string(obj.pos.y) + ", " + std::to_string(obj.pos.z) + ")" +
				", Ori=(" + std::to_string(obj.rot.x) + ", " + std::to_string(obj.rot.y) + ", " + std::to_string(obj.rot.z) + ")" +
				", Tex=" + mId + ", Model=" + mId);		}
		
		RebuildLevelModelIds();

		// Build the set of models packed in this level's .res (names only — stream so we
		// never buffer the whole 200+MB archive in the 32-bit process). (issue 2)
		{
			std::string gameRes = Utils::GetIGIRootPath() +
				"\\missions\\location0\\level" + std::to_string(level_no) +
				"\\models\\level" + std::to_string(level_no) + ".res";
			level_res_models_ = ResModelSet();
			std::string resErr;
			size_t entryCount = 0;
			bool ok = RES_ForEachEntry(gameRes,
				[&](const std::string& name, const uint8_t*, size_t) {
					level_res_models_.AddEntry(name);
					++entryCount;
				}, resErr);
			Logger::Get().Log(LogLevel::INFO, std::string("[App] Level .res model set: ") +
				(ok ? std::to_string(entryCount) + " entries (streamed)"
				    : "UNAVAILABLE (" + gameRes + "): " + resErr));
		}
		Logger::Get().Log(LogLevel::INFO, "[App] ==========================================");
		Logger::Get().Log(LogLevel::INFO, "[App] LoadLevel() COMPLETE for level " + std::to_string(level_no));
		Logger::Get().Log(LogLevel::INFO, "[App] ==========================================");
		drawLoadBar(100, "done");
	}
	catch (const std::exception& e) {
		std::string errorMsg = "Error loading level " + std::to_string(level_no) + ":\n" + std::string(e.what());
		Utils::LogAndShowError(errorMsg, "IGI Editor - Error");
		Logger::Get().Log(LogLevel::ERR, errorMsg);
	}
	catch (...) {
		std::string errorMsg = "Unknown error loading level " + std::to_string(level_no);
		Utils::LogAndShowError(errorMsg, "IGI Editor - Error");
		Logger::Get().Log(LogLevel::ERR, errorMsg);
	}
}

void App::SetGameLevel(int level_no) {
	bridge_.SetGameLevel(level_no);
}



static bool containsIgnoreCase(const std::string& str, const std::string& substr) {
    if (substr.empty()) return true;
    auto it = std::search(
        str.begin(), str.end(),
        substr.begin(), substr.end(),
        [](char ch1, char ch2) { return std::tolower(ch1) == std::tolower(ch2); }
    );
    return it != str.end();
}





void App::FlushAttaProxiesToMef() {
	for (auto& obj : level_.GetLevelObjects().GetObjects()) {
		if (!obj.isAttaProxy || !obj.modified) continue;

		// Position: convert world pos → local pos via stored inverse parent matrix.
		glm::vec4 lp = obj.attaInvParentMat * glm::vec4(glm::vec3(obj.pos), 1.0f);
		glm::vec3 localPos(lp);

		// Rotation: rebuild world rotation matrix from Euler angles (Rz * Rx * Ry),
		// then convert to local rotation: localRot = parentRotInv * worldRot.
		// The upper-left 3×3 of attaInvParentMat IS parentRotInv.
		glm::mat4 wr(1.0f);
		wr = glm::rotate(wr, (float)obj.rot.z, glm::vec3(0,0,1));
		wr = glm::rotate(wr, (float)obj.rot.x, glm::vec3(1,0,0));
		wr = glm::rotate(wr, (float)obj.rot.y, glm::vec3(0,1,0));
		glm::mat3 worldRot3(wr);
		glm::mat3 parentRotInv = glm::mat3(obj.attaInvParentMat);
		glm::mat3 localRot = parentRotInv * worldRot3;

		renderer_.UpdateAttaLocalPosInMef(obj.attaParentModelId, obj.attaIsBuilding,
		                                  obj.attaRecordIndex, localPos, localRot);
		obj.modified = false;
	}
}

void App::SaveCurrentLevel() {
	try {
		Logger::Get().Log(LogLevel::INFO, "[App] SaveCurrentLevel() called");
		FlushAttaProxiesToMef();
		level_.SaveChanges();

		// Compile edited AI script (.qsc text -> .qvm file) before saving the level QVM.
		if (ai_script_dirty_ && !ai_script_path_.empty()) {
			Logger::Get().Log(LogLevel::INFO, "[App] Compiling modified AI script to: " + ai_script_path_);
			auto lexResult   = qsc::Lex(ai_script_text_);
			auto parseResult = lexResult.ok ? qsc::Parse(lexResult.tokens) : qsc::ParseResult{};
			std::string compileErr;
			bool ok = lexResult.ok && parseResult.ok &&
			          qvm::CompileToFile(*parseResult.program, ai_script_path_, &compileErr);
			if (ok) {
				QVMFile check = QVM_Parse(ai_script_path_);
				if (check.valid) {
					ai_script_dirty_ = false;
					status_message_ = "AI script compiled: " + ai_script_path_;
					Logger::Get().Log(LogLevel::INFO, "[App] AI script compiled OK");
				} else {
					Logger::Get().Log(LogLevel::ERR, "[App] AI script round-trip failed -- file may be corrupt");
					status_message_ = "AI script compile: round-trip failed";
				}
			} else {
				std::string detail = compileErr.empty() ? "(no detail)" : compileErr;
				Logger::Get().Log(LogLevel::ERR, "[App] AI script compile error: " + detail);
				status_message_ = "AI script compile error: " + detail;
			}
		}

		Logger::Get().Log(LogLevel::INFO, "[App] Calling SaveAndCompile()");
		SaveAndCompile();
	}
	catch (const std::exception& e) {
		std::string errorMsg = "Error saving level:\n" + std::string(e.what());
		Utils::LogAndShowError(errorMsg, "IGI Editor - Error");
		Logger::Get().Log(LogLevel::ERR, errorMsg);
	}
	catch (...) {
		std::string errorMsg = "Unknown error saving level";
		Utils::LogAndShowError(errorMsg, "IGI Editor - Error");
		Logger::Get().Log(LogLevel::ERR, errorMsg);
	}
}

void App::ExportTextureMap() {
	int levelNo = level_.GetLevelNo();
	const std::string root = Utils::GetIGIRootPath();
	const std::string datPath = root + "\\missions\\location0\\level" +
	    std::to_string(levelNo) + "\\level" + std::to_string(levelNo) + ".dat";
	const std::string outPath = Utils::GetExeDirectory() +
	    "\\level" + std::to_string(levelNo) + "_texmap.json";

	Logger::Get().Log(LogLevel::INFO,
	    "[App] ExportTextureMap level=" + std::to_string(levelNo) +
	    " dat=" + datPath + " out=" + outPath);

	DATFile dat = DAT_Parse(datPath);
	if (!dat.valid) {
		Utils::ShowError("Failed to parse DAT:\n" + dat.error, "Export Texture Map");
		return;
	}
	if (DAT_WriteJSON(dat, outPath)) {
		Utils::ShowInfo(
		    "Texture map exported (JSON):\n" + outPath +
		    "\nModels: " + std::to_string(dat.models.size()) +
		    "  Textures: " + std::to_string(dat.allTextures.size()),
		    "Export Texture Map");
	} else {
		Utils::ShowError("Could not write to:\n" + outPath, "Export Texture Map");
	}
}

int App::GetCurLevelNo() const {
	return level_.GetLevelNo();
}

void App::ToggleOverlayWireframe() {
	draw_params_.overlay_wireframe_ = !draw_params_.overlay_wireframe_;
}

void App::ToggleDrawParts(int part) {
	if (draw_params_.draw_parts_ & part) {
		draw_params_.draw_parts_ &= ~part;
	}
	else {
		draw_params_.draw_parts_ |= part;
	}
}

void App::SetDrawParts(int parts) {
	draw_params_.draw_parts_ = parts;
}

void App::ToggleTerrainDrawOption(int opt) {
	if (draw_params_.draw_terrain_options_ & opt) {
		draw_params_.draw_terrain_options_ &= ~opt;
	}
	else {
		draw_params_.draw_terrain_options_ |= opt;
	}
}

void App::ToggleTerrainModOption(int opt) {
	if (terrain_mod_options_ & opt) {
		terrain_mod_options_ &= ~opt;
	}
	else {
		terrain_mod_options_ |= opt;
	}
}

bool App::GetOverlayWireframe() const {
	return draw_params_.overlay_wireframe_;
}

int	App::GetDrawParts() const {
	return draw_params_.draw_parts_;
}

int	App::GetTerrainDrawOptions() const {
	return draw_params_.draw_terrain_options_;
}

int	App::GetTerrainModOptions() const {
	return terrain_mod_options_;
}

// events
void App::OnWindowResize(int width, int height) {
	window_state_.viewport_width_ = std::max(1, width);
	window_state_.viewport_height_ = std::max(1, height);

	view_define_.viewport_width_ = window_state_.viewport_width_;
	view_define_.viewport_height_ = window_state_.viewport_height_;

	glViewport(0, 0, width, height);

	// update fovx_
	float h = std::tan(view_define_.fovy_ * 0.5f);
	float w = h * width / height;
	view_define_.fovx_ = std::atan(w) * 2.0f;

	float tan_half_fovx = (float)std::tan(view_define_.fovx_ * 0.5);
	float tan_half_fovy = (float)std::tan(view_define_.fovy_ * 0.5);

	view_define_.tan_half_fovx_ = tan_half_fovx;
	view_define_.tan_half_fovy_ = tan_half_fovy;

	view_define_.half_viewport_width_div_tan_half_fovx_ = window_state_.viewport_width_ * 0.5f / tan_half_fovx;
	view_define_.half_viewport_height_div_tan_half_fovy_ = window_state_.viewport_height_ * 0.5f / tan_half_fovy;

}

void App::OnDisplay() {
	// ignore
}

// AI text editor helpers — must be defined before Input_OnMouse and Input_OnSpecial.

// Returns flat text offsets of each visual line start.
// Lines split on '\n'; lines longer than max_chars wrap to the next visual line.
static std::vector<int> AiTextLineStarts(const std::string& txt, int max_chars) {
	std::vector<int> s;
	s.push_back(0);
	for (int i = 0; i < (int)txt.size(); ) {
		if (txt[i] == '\n') { s.push_back(i + 1); i++; }
		else {
			int cnt = 0;
			while (i < (int)txt.size() && txt[i] != '\n' && cnt < max_chars) { i++; cnt++; }
			if (i < (int)txt.size() && txt[i] != '\n') s.push_back(i);
		}
	}
	return s;
}

static int AiScriptMaxChars() {
	return std::max(1, (PropPanel::kWidth - 2 * PropPanel::kPad - 6) / 7);
}

// input
void App::Input_OnMouseWheel(int wheel, int direction, int x, int y) {
	if (show_help_) {
		// Scroll keybindings help panel
		if (direction > 0) { if (help_scroll_offset_ > 0) help_scroll_offset_--; }
		else               { help_scroll_offset_++; }
		return;
	}
	// Over property panel: scroll it
	if (prop_editor_open_ &&
	    x >= PropPanel::kLeft && x <= PropPanel::kLeft + PropPanel::kWidth) {
		const int kScrollStep = PropPanel::kBoxH + 4;
		if (direction > 0) prop_panel_scroll_ = std::max(0, prop_panel_scroll_ - kScrollStep);
		else               prop_panel_scroll_ += kScrollStep;
		return;
	}
	if (show_hud_ && x < 350) { // Over TreeView
		if (direction > 0) {
			if (tree_scroll_offset_ > 0) tree_scroll_offset_--;
		} else {
			tree_scroll_offset_++;
		}
		Logger::Get().Log(LogLevel::INFO, "[App] Tree scroll offset: " + std::to_string(tree_scroll_offset_));
	}
}

void App::Input_OnMouse(int button, int state, int x, int y) {
	bool enableCameraMode = Utils::IsKeyBindingPressed(Config::Get().keyEnableCamera);

	// Update mouse position first so EditorProcessClick uses correct coords
	mouse_state_.prior_x_ = x;
	mouse_state_.prior_y_ = y;

	if (button == GLUT_LEFT_BUTTON) {
		if (GLUT_DOWN == state) {
			mouse_state_.left_button_down_ = true;
			
			if (enableCameraMode) {
				int cx = window_state_.viewport_width_ >> 1;
				int cy = window_state_.viewport_height_ >> 1;
				int targetIdx = selected_object_index_;
				if (targetIdx < 0) targetIdx = hover_object_index_;
				if (targetIdx < 0) targetIdx = PickObjectAtScreenPos(cx, cy);
				if (targetIdx >= Renderer::kAttaPickBase) targetIdx = -1; // ATTA: no orbit target

				if (targetIdx >= 0) {
					const auto& obj = level_.GetLevelObjects().GetObjects()[targetIdx];
					orbit_active_ = true;
					orbit_target_pos_ = glm::vec3(obj.pos);
					orbit_distance_ = glm::distance(viewer_.pos_, orbit_target_pos_);
					if (orbit_distance_ < 0.1f) orbit_distance_ = 1.0f;
					Logger::Get().Log(LogLevel::INFO, "[App] Orbit mode activated around object: " + obj.type);
					Logger::Get().Log(LogLevel::WARNING, "[App] Orbit target pos: " + std::to_string(orbit_target_pos_.x) + ", " + std::to_string(orbit_target_pos_.y) + ", " + std::to_string(orbit_target_pos_.z) + " | Dist: " + std::to_string(orbit_distance_));
				} else {
					orbit_active_ = false;
				}
				return;
			}
			
			// C2: Property editor click handling (IGI2-style left panel)
			if (prop_editor_open_ && selected_object_index_ >= 0) {
				auto& objects = level_.GetLevelObjects().GetObjects();
				if (selected_object_index_ < (int)objects.size()) {
					LevelObject& obj = objects[selected_object_index_];
					const TaskSchema* scp = GetSchema(obj.type);
					if (scp) {
						const TaskSchema& schema = *scp;
						bool is_ai = ai_model_ids_.count(obj.modelId) > 0;
						// Gather editable child task sections (same order as the renderer).
						std::vector<std::pair<int, const TaskSchema*>> children;
						for (int ci : obj.childrenIndices) {
							if (ci < 0 || ci >= (int)objects.size()) continue;
							if (objects[ci].deleted) continue;
							const TaskSchema* cscp = GetSchema(objects[ci].type);
							if (cscp && !cscp->empty()) children.push_back({ci, cscp});
						}
						PropPanel::Layout L = PropPanel::BuildLayout(schema, is_ai, children);
						// Apply the same vertical scroll the renderer uses so hit-tests align.
						if (prop_panel_scroll_ > 0)
							for (auto& w : L.widgets) { w.y1 -= prop_panel_scroll_; w.y2 -= prop_panel_scroll_; }
						// The property panel is a foreground overlay: ANY click anywhere in
						// the left panel strip is consumed so background 3D objects are never
						// selected/manipulated while the editor is open.
						if (x >= 0 && x <= L.panel_x + L.panel_w) {
							// +4px tolerance on all sides for pixel-perfect feel at any DPI
							auto inRect = [&](const PropPanel::Widget& w) {
								return x >= w.x1 - 4 && x <= w.x2 + 4 && y >= w.y1 - 4 && y <= w.y2 + 4;
							};
							// Commit any in-progress text edit before switching focus.
							CommitPropTextEdit();
							for (const auto& w : L.widgets) {
								if (!inRect(w)) continue;
								using K = PropPanel::WidgetKind;
								if (w.kind == K::ChildHeader) { return; } // separator: not interactive

								// Resolve the widget's target object: the selected parent, or
								// one of its child tasks (w.objIndex). Edits/drags route there so
								// children get the identical interface.
								int tIdx = (w.objIndex >= 0) ? w.objIndex : selected_object_index_;
								if (tIdx < 0 || tIdx >= (int)objects.size()) return;
								LevelObject& tobj = objects[tIdx];
								const TaskSchema* tscp = (tIdx == selected_object_index_) ? scp : GetSchema(tobj.type);
								if (!tscp) return;
								const TaskSchema& tsch = *tscp;
								auto fieldArg = [&](int fidx, int comp) -> int {
									return (fidx >= 0 && fidx < (int)tsch.size()) ? tsch[fidx].argOffset + comp : -1;
								};
								auto tTok = [&](int idx) -> float {
									if (idx >= 0 && idx < (int)tobj.argTokens.size()) {
										try { return std::stof(tobj.argTokens[idx]); } catch(...) {}
									}
									return 0.f;
								};

								if (w.kind == K::NoteBox) {
									prop_edit_obj_index_ = tIdx;
									prop_text_edit_field_ = -2; // sentinel: editing note
									prop_text_buf_ = tobj.name;
									prop_text_caret_ = (int)prop_text_buf_.size();
								} else if (w.kind == K::SnapGround || w.kind == K::SnapObject) {
									// Snap acts on the selected object's marker manipulation.
									if (tIdx == selected_object_index_) {
										int mk = (w.kind == K::SnapGround) ? MK_MANIP_S : MK_MANIP_O;
										PushUndoState();
										input_.keys_ |= mk;
										UpdateMarkerManipulation();
										input_.keys_ &= ~mk;
										mouse_state_.left_button_down_ = false; // prevent drag overwriting snap
									}
								} else if (w.kind == K::StringBox) {
									prop_edit_obj_index_ = tIdx;
									prop_text_edit_field_ = w.fieldIndex * 3 + w.comp;
									int argIdx = fieldArg(w.fieldIndex, w.comp);
									prop_text_buf_ = (argIdx >= 0 && argIdx < (int)tobj.argTokens.size()) ? StripQuotes(tobj.argTokens[argIdx]) : "";
									prop_text_caret_ = (int)prop_text_buf_.size();
								} else if (w.kind == K::NumBox) {
									// Click to type AND arm scrub-drag (motion will scrub).
									prop_edit_obj_index_ = tIdx;
									prop_text_edit_field_ = w.fieldIndex * 3 + w.comp;
									int argIdx = fieldArg(w.fieldIndex, w.comp);
									prop_text_buf_ = (argIdx >= 0 && argIdx < (int)tobj.argTokens.size()) ? tobj.argTokens[argIdx] : "";
									prop_text_caret_ = (int)prop_text_buf_.size();
									prop_drag_obj_index_ = tIdx;
									prop_field_index_  = w.fieldIndex * 3 + w.comp;
									prop_drag_start_x_ = x;
									prop_drag_start_y_ = y;
									PushUndoState();
									prop_drag_start_val_ = tTok(argIdx);
								} else if (w.kind == K::Checkbox) {
									int argIdx = fieldArg(w.fieldIndex, w.comp);
									if (argIdx >= 0 && argIdx < (int)tobj.argTokens.size()) {
										int cur = 0; try { cur = std::stoi(tobj.argTokens[argIdx]); } catch(...) {}
										tobj.argTokens[argIdx] = (cur == 0) ? "1" : "0";
										tobj.modified = true;
										level_.GetLevelObjects().UpdateCoordinatesInLine(tobj);
									}
								} else if (w.kind == K::PosPad) {
									// drag X and Y simultaneously — store field, use comp=0 marker
									prop_drag_obj_index_ = tIdx;
									prop_field_index_  = w.fieldIndex * 3 + 0;
									prop_drag_start_x_ = x;
									prop_drag_start_y_ = y;
									PushUndoState();
									int off = fieldArg(w.fieldIndex, 0);
									prop_drag_start_val_  = tTok(off + 0);
									prop_drag_start_val2_ = tTok(off + 1);
								} else if (w.kind == K::AIScriptPath) {
									prop_edit_obj_index_ = -1;
									prop_text_edit_field_ = PropPanel::kAIScriptPathField;
									prop_text_buf_ = ai_script_path_;
									{
										int cc = std::max(0, (x - w.x1 - 3) / 7);
										prop_text_caret_ = std::min((int)prop_text_buf_.size(),
										                            ai_script_path_hscroll_ + cc);
									}
								} else if (w.kind == K::AIScriptText) {
									prop_edit_obj_index_ = -1;
									prop_text_edit_field_ = PropPanel::kAIScriptTextField;
									prop_text_buf_ = ai_script_text_;
									{
										const int mc = AiScriptMaxChars();
										int click_row = std::max(0, (y - w.y1) / PropPanel::kBoxH);
										int click_col = std::max(0, (x - w.x1 - 3) / 7);
										int abs_line  = ai_script_vscroll_ + click_row;
										auto lstarts  = AiTextLineStarts(ai_script_text_, mc);
										abs_line = std::max(0, std::min(abs_line, (int)lstarts.size() - 1));
										int ls   = lstarts[abs_line];
										int next = (abs_line + 1 < (int)lstarts.size()) ? lstarts[abs_line + 1] : (int)ai_script_text_.size();
										int len  = next - ls;
										if (len > 0 && (ls + len) <= (int)ai_script_text_.size() &&
										    ai_script_text_[ls + len - 1] == '\n') --len;
										prop_text_caret_ = ls + std::min(click_col, len);
									}
								} else {
									// PosZSlider / OriSlider / RgbSlider / NumSlider — single-value drag
									prop_drag_obj_index_ = tIdx;
									prop_field_index_  = w.fieldIndex * 3 + w.comp;
									prop_drag_start_x_ = x;
									prop_drag_start_y_ = y;
									PushUndoState();
									prop_drag_start_val_ = tTok(fieldArg(w.fieldIndex, w.comp));
								}
								return;
							}
								return; // consume any click in the panel strip (foreground overlay)
						}
					}
				}
			}

			if (pause_mode_) {
				// *** MUST match renderer.cpp pause menu constants exactly ***
				const int menu_w = 380;
				const int menu_h = 280;
				const int menu_x = (window_state_.viewport_width_  - menu_w) / 2;
				const int screen_menu_top = (window_state_.viewport_height_ - menu_h) / 2;

				// Buttons start at screen_menu_top + 85, spaced 35px
			// Visual quad spans btn_y-12 to btn_y+16 in screen coords; use ±18 for generous hit area
				auto btn_hit = [&](int idx, int mouse_y) -> bool {
					int btn_y = screen_menu_top + 85 + idx * 35;
					return (mouse_y >= btn_y - 18 && mouse_y <= btn_y + 18);
				};

				if (x >= menu_x && x <= menu_x + menu_w &&
				    y >= screen_menu_top && y <= screen_menu_top + menu_h) {
					mouse_state_.left_button_down_ = false;
					if      (btn_hit(0, y)) { TogglePauseMenu(); }                    // Resume
					else if (btn_hit(1, y)) {                                         // Font row: toggle + [-] size [+]
						// Sub-region X bounds MUST match renderer.cpp centered font row layout.
						const int sz_box_w = 34, btn_w = 22, gap = 6, label_w = 96, label_gap = 16;
						const int controls_w = btn_w + gap + sz_box_w + gap + btn_w;
						const int group_w = label_w + label_gap + controls_w;
						int gx = menu_x + (menu_w - group_w) / 2;
						int minus_x = gx + label_w + label_gap;
						int box_x   = minus_x + btn_w + gap;
						int plus_x  = box_x + sz_box_w + gap;
						int& fs = Config::Get().systemFontSize;
						if (x >= minus_x && x < minus_x + 22) {        // [-] decrease by 1
							fs = std::max(8, fs - 1);
							Config::Save();
						} else if (x >= plus_x && x < plus_x + 22) {   // [+] increase by 1
							fs = std::min(32, fs + 1);
							Config::Save();
						} else if (x < minus_x) {                      // left label: toggle font type
							Config::Get().useEditorFont = !Config::Get().useEditorFont;
							Config::Save();
						}
					}
					else if (btn_hit(2, y)) { ResetLevel(); TogglePauseMenu(); }      // Reset Level
					else if (btn_hit(3, y)) { SaveCurrentLevel(); }                   // Save Level
					else if (btn_hit(4, y)) { exit(0); }                              // Quit
				}
				return; // Block all other interactions while paused
			}

			// Task picker mouse click: left-click on an item selects it
			if (task_picker_open_) {
				const int picker_x = 20;
				const int picker_w = 520;
				const int picker_items_top_y = 50; // items start below header (screen coords)
				const int row_h = 16;

				if (x >= picker_x && x <= picker_x + picker_w && y >= picker_items_top_y) {
					int row = (y - picker_items_top_y) / row_h;
					int target_idx = task_picker_scroll_offset_ + row;

					// Build picker list (same logic as Input_OnSpecial picker handling)
					auto& objects = level_.GetLevelObjects().GetObjects();
					std::vector<int> picker_to_objects;
					std::string search_lower = task_picker_search_;
					std::transform(search_lower.begin(), search_lower.end(), search_lower.begin(),
					               [](unsigned char c) { return std::tolower(c); });
					std::set<std::string> seen_types;
					for (int i = 0; i < (int)objects.size(); ++i) {
						if (!objects[i].deleted) {
							const auto& obj = objects[i];
							if (obj.type == "Task_DeclareParameters") continue; // picker excludes declare params
							std::string type_lower = obj.type;
							std::transform(type_lower.begin(), type_lower.end(), type_lower.begin(),
							               [](unsigned char c) { return std::tolower(c); });
							if (seen_types.count(type_lower) > 0) continue;
							seen_types.insert(type_lower);
							std::string label = obj.type + "()";
							std::string label_lower = label;
							std::transform(label_lower.begin(), label_lower.end(), label_lower.begin(),
							               [](unsigned char c) { return std::tolower(c); });
							if (search_lower.empty() || label_lower.find(search_lower) != std::string::npos)
								picker_to_objects.push_back(i);
						}
					}

					if (target_idx >= 0 && target_idx < (int)picker_to_objects.size()) {
						task_picker_selected_idx_ = target_idx;
					}
					mouse_state_.left_button_down_ = false;
					return;
				}
			}

			// Priority: TreeView HUD interaction
			if (show_hud_ && x < 350 && !enableCameraMode) { // Tree is on the left
				ProcessTreeViewClick(x, y);

			}
			else if (edit_mode_ && !enableCameraMode) {
				EditorProcessClick(); 
				// Update manipulation data AFTER selection, so we get the newly selected object
				marker_manip_.start_x_ = x;
				marker_manip_.start_y_ = y;
				if (selected_object_index_ >= 0) {
					auto& obj = level_.GetLevelObjects().GetObjects()[selected_object_index_];
					marker_manip_.start_pos_ = obj.pos;
					marker_manip_.start_rot_ = obj.rot;
				}
			}
		}
		else if (GLUT_UP == state) {
			mouse_state_.left_button_down_ = false;
			edit_dragging_ = false;
			orbit_active_ = false;
			prop_field_index_ = -1; // C2: stop dragging property field
			prop_drag_obj_index_ = -1;
			prop_drag_speed_ = 0.f;
			prop_last_drag_dx_ = 0;
			prop_last_drag_dy_ = 0;
			status_message_.clear(); // Clear movement telemetry status when mouse is released

			if (window_state_.cursor_visible_) {
				input_.mouse_delta_x_ = 0;
				input_.mouse_delta_y_ = 0;
			}
		}
	}

	// Right-click: open property editor for the object under cursor
	if (button == GLUT_RIGHT_BUTTON && state == GLUT_DOWN && !pause_mode_ && !enableCameraMode) {
		int target = hover_object_index_ >= 0 ? hover_object_index_ : selected_object_index_;
		if (target >= 0) {
			selected_object_index_ = target;
			prop_editor_open_ = true; prop_panel_scroll_ = 0; prop_text_edit_field_ = -1; prop_edit_obj_index_ = -1;
			LoadAIScriptForSelected();
		} else {
			prop_editor_open_ = false;
		}
	}

	// Update cursor instantly on click/release — keep NONE if SPR cursor is active
	if (window_state_.cursor_visible_ && !pause_mode_) {
		glutSetCursor(GLUT_CURSOR_NONE);
	}
}

void App::Input_OnMotion(int x, int y) {
	int dx = x - mouse_state_.prior_x_;
	int dy = y - mouse_state_.prior_y_;

	bool enableCameraMode = Utils::IsKeyBindingPressed(Config::Get().keyEnableCamera);
	if (enableCameraMode && (dx != 0 || dy != 0))
		camera_mode_moved_ = true;

	// Always update mouse coordinates for hover tooltip/interaction
	mouse_state_.prior_x_ = x;
	mouse_state_.prior_y_ = y;

	// Priority 1: TreeView Hover
	if (show_hud_ && x < 350 && !enableCameraMode) {
		hover_tree_index_ = -1; // Reset before check
		ProcessTreeViewHover(x, y);
		hover_object_index_ = -1; // Suppress 3D tooltips while over UI
	} else {
		hover_tree_index_ = -1;
		// Priority 2: 3D Object Hover — deferred to Frame() to avoid blocking GPU sync on every mouse event
	}

	if (window_state_.cursor_visible_) {
		glutSetCursor(GLUT_CURSOR_NONE);
	}

	if (window_state_.cursor_visible_ && !enableCameraMode) {
		if (mouse_state_.left_button_down_) {
			input_.mouse_delta_x_ = dx;
			input_.mouse_delta_y_ = dy;

			if (edit_mode_ && selected_object_index_ >= 0 && !terrain_edit_enabled_) {
				UpdateMarkerManipulation();
			}
		}
	}
	else {
		if (skip_input_on_motion_once_) {
			skip_input_on_motion_once_ = false;
			return;
		}

		int center_x = window_state_.viewport_width_ >> 1;
		int center_y = window_state_.viewport_height_ >> 1;

		input_.mouse_delta_x_ += x - center_x;
		input_.mouse_delta_y_ += y - center_y;

		mouse_state_.prior_x_ = x;
		mouse_state_.prior_y_ = y;

		glutWarpPointer(center_x, center_y); // move cursor to view center
		skip_input_on_motion_once_ = true;   // skip next cursor motion event caused by glutWarpPointer
	}

	if (edit_dragging_) {
		int box_x = (window_state_.viewport_width_ - edit_box_w_) / 2;
		int rel_x = x - (box_x + 20);
		int char_w = 9;
		edit_cursor_pos_ = std::max(0, std::min((int)edit_string_.size(), edit_scroll_x_ + (rel_x / char_w)));
		edit_selection_end_ = edit_cursor_pos_;
	}

	// C2: Property editor drag (2D pad / Z slider / orientation+numeric sliders)
	if (prop_field_index_ >= 0 && !enableCameraMode && selected_object_index_ >= 0) {
		auto& objects = level_.GetLevelObjects().GetObjects();
		// The drag may target the selected parent or one of its child tasks.
		int dragIdx = (prop_drag_obj_index_ >= 0) ? prop_drag_obj_index_ : selected_object_index_;
		if (dragIdx >= 0 && dragIdx < (int)objects.size()) {
			auto& obj = objects[dragIdx];
			const TaskSchema* scp = GetSchema(obj.type);
			if (scp) {
				const TaskSchema& schema = *scp;
				int fi   = prop_field_index_ / 3;
				int comp = prop_field_index_ % 3;
				// A real drag cancels any pending NumBox text-edit (scrub wins).
				if ((std::abs(x - prop_drag_start_x_) > 2 || std::abs(y - prop_drag_start_y_) > 2) &&
				    prop_text_edit_field_ == prop_field_index_) {
					prop_text_edit_field_ = -1;
				}
				if (fi < (int)schema.size()) {
					const FieldDef& fd = schema[fi];
					const std::string& tn = fd.typeName;
					bool is_pos = (tn == "ObjectPos"); // only actual position type gets pad/camera-follow
					bool is_ori = (tn == "Real32x9");
						int dxp = x - prop_drag_start_x_;

						if (is_pos) {
							// Position pad / Z slider use a per-frame velocity model
							// (ApplyPropPositionDrag) so the object accelerates while held in a
							// direction and keeps moving even when the cursor is pinned at the
							// window edge. Raw motion events do nothing for position here.
						} else {
						glm::dvec3 oldPos = obj.pos;
						glm::dvec3 oldRot = obj.rot;
						// Orientation / RGB / numeric horizontal slider or NumBox scrub.
						bool isRgb = (tn == "RGB" || tn == "Colour");
						bool isInt = (tn == "Int16" || tn == "Int32" || tn == "EnumInt32");
						float sens = is_ori ? 0.01f : (isRgb ? 0.005f : (isInt ? 0.1f : 0.05f));
						float nv = prop_drag_start_val_ + dxp * sens;
						if (isRgb) nv = std::max(0.f, std::min(1.f, nv));
						int argIdx = fd.argOffset + comp;
						if (argIdx >= 0 && argIdx < (int)obj.argTokens.size()) {
							char buf[64];
							if (isInt) snprintf(buf, sizeof(buf), "%d", (int)std::lround(nv));
							else       snprintf(buf, sizeof(buf), "%.6f", nv);
							obj.argTokens[argIdx] = buf;
						}
						if (is_ori) { // mirror to obj.rot (Alpha/Beta/Gamma -> x/y/z)
							if      (comp == 0) obj.rot.x = nv;
							else if (comp == 1) obj.rot.y = nv;
							else                obj.rot.z = nv;
						}
						// Sync single-rotation Real32 fields (Gamma/Heading) to obj.rot.z so
						// UpdateCoordinatesInLine doesn't overwrite argTokens with stale obj.rot.
						// Gamma/Heading are stored as Angle/Degrees/Real32 — any of these
						// must drive obj.rot.z so the 3D object actually turns when dragged.
						bool isSingleRot = ((tn == "Real32" || tn == "Angle" || tn == "Degrees") &&
						                    (fd.name == "Gamma" || fd.name == "Heading"));
						if (isSingleRot) obj.rot.z = (double)nv;
						obj.modified = true;
						if (is_ori) {
							glm::dmat3 deltaWorld = BuildRotMatZXY(obj.rot) * glm::transpose(BuildRotMatZXY(oldRot));
							PropagateTransformToChildren(dragIdx, glm::dvec3(0), deltaWorld, oldPos);
						}
						level_.GetLevelObjects().UpdateCoordinatesInLine(obj);
					}
				}
			}
		}
	}
}

// Per-frame velocity-ramped position drag for the property editor's 2D pad and Z
// slider. Called once per frame while the field is held. The object accelerates
// the longer the cursor is held away from the drag-start point (mirrors the
// ALT+SHIFT camera boost) and keeps moving even when the cursor is pinned at the
// window edge. The camera is intentionally NOT moved, so the object visibly
// travels through the world.
void App::ApplyPropPositionDrag() {
	if (prop_field_index_ < 0) return;
	auto& objects = level_.GetLevelObjects().GetObjects();
	int dragIdx = (prop_drag_obj_index_ >= 0) ? prop_drag_obj_index_ : selected_object_index_;
	if (dragIdx < 0 || dragIdx >= (int)objects.size()) return;
	auto& obj = objects[dragIdx];
	const TaskSchema* scp = GetSchema(obj.type);
	if (!scp) return;
	const TaskSchema& schema = *scp;
	int fi = prop_field_index_ / 3;
	int comp = prop_field_index_ % 3;
	if (fi >= (int)schema.size()) return;
	const FieldDef& fd = schema[fi];
	if (fd.typeName != "ObjectPos") return;   // only the pad / Z slider use this model

	int dispx = mouse_state_.prior_x_ - prop_drag_start_x_;
	int dispy = mouse_state_.prior_y_ - prop_drag_start_y_;
	const int   kDead   = 5;        // cursor dead zone around the drag start
	const float kBase   = 25.0f;    // units/frame just past the dead zone
	const float kGrowth = 1.05f;    // per-frame acceleration multiplier
	const float kMax    = 8000.0f;  // speed cap (units/frame)

	auto curArg = [&](int idx) -> double {
		return (idx >= 0 && idx < (int)obj.argTokens.size()) ? std::atof(obj.argTokens[idx].c_str()) : 0.0;
	};
	auto writeArg = [&](int idx, double v) {
		if (idx >= 0 && idx < (int)obj.argTokens.size()) {
			char buf[64]; snprintf(buf, sizeof(buf), "%.6f", v);
			obj.argTokens[idx] = buf;
		}
	};

	glm::dvec3 oldPos = obj.pos;
	bool moved = false;

	if (comp == 0) {            // 2D pad → X / Y
		float len = std::sqrt((float)(dispx * dispx + dispy * dispy));
		if (len < (float)kDead) { prop_drag_speed_ = 0.f; return; }
		prop_drag_speed_ = (prop_drag_speed_ < kBase) ? kBase : std::min(kMax, prop_drag_speed_ * kGrowth);
		float ux = dispx / len, uy = dispy / len;
		double nx = curArg(fd.argOffset + 0) + ux * prop_drag_speed_;
		double ny = curArg(fd.argOffset + 1) - uy * prop_drag_speed_;  // screen-y down → world-y up
		writeArg(fd.argOffset + 0, nx);
		writeArg(fd.argOffset + 1, ny);
		obj.pos.x = nx; obj.pos.y = ny;
		moved = true;
	} else if (comp == 1) {     // Y numeric box → Y only
		if (std::abs(dispy) < kDead) { prop_drag_speed_ = 0.f; return; }
		prop_drag_speed_ = (prop_drag_speed_ < kBase) ? kBase : std::min(kMax, prop_drag_speed_ * kGrowth);
		float uy = (dispy < 0) ? 1.f : -1.f;   // drag up → +Y
		double ny = curArg(fd.argOffset + 1) + uy * prop_drag_speed_;
		writeArg(fd.argOffset + 1, ny);
		obj.pos.y = ny;
		moved = true;
	} else if (comp == 2) {     // Z slider
		if (std::abs(dispy) < kDead) { prop_drag_speed_ = 0.f; return; }
		prop_drag_speed_ = (prop_drag_speed_ < kBase) ? kBase : std::min(kMax, prop_drag_speed_ * kGrowth);
		float uy = (dispy < 0) ? 1.f : -1.f;   // drag up → +Z
		double nz = curArg(fd.argOffset + 2) + uy * prop_drag_speed_;
		writeArg(fd.argOffset + 2, nz);
		obj.pos.z = nz;
		moved = true;
	}

	if (moved) {
		obj.modified = true;
		glm::dvec3 deltaPos = obj.pos - oldPos;
		PropagateTransformToChildren(dragIdx, deltaPos, glm::dmat3(1.0), oldPos);
		level_.GetLevelObjects().UpdateCoordinatesInLine(obj);
		// Camera follows the object so it stays in view as it accelerates/travels.
		viewer_.pos_ += glm::vec3(deltaPos);
	}
}

void App::Input_OnSpecial(int key, int x, int y) {
	auto& config = Config::Get();

	// Autocomplete task picker navigation
	if (ac_task_picker_open_) {
		// Build filtered list size
		std::vector<std::string> filtered;
		std::string fl = ac_task_filter_;
		std::transform(fl.begin(), fl.end(), fl.begin(), [](unsigned char c){ return std::tolower(c); });
		for (const auto& item : ac_task_items_) {
			if (fl.empty()) { filtered.push_back(item); }
			else {
				std::string il = item;
				std::transform(il.begin(), il.end(), il.begin(), [](unsigned char c){ return std::tolower(c); });
				if (il.find(fl) != std::string::npos) filtered.push_back(item);
			}
		}
		int count = (int)filtered.size();
		if (count > 0) {
			if (key == GLUT_KEY_UP)        ac_task_selected_idx_ = std::max(0, ac_task_selected_idx_ - 1);
			else if (key == GLUT_KEY_DOWN) ac_task_selected_idx_ = std::min(count - 1, ac_task_selected_idx_ + 1);
			else if (key == GLUT_KEY_PAGE_UP)   ac_task_selected_idx_ = std::max(0, ac_task_selected_idx_ - 10);
			else if (key == GLUT_KEY_PAGE_DOWN) ac_task_selected_idx_ = std::min(count - 1, ac_task_selected_idx_ + 10);
			const int row_h = 16, panel_h = window_state_.viewport_height_ - 100;
			const int max_vis = std::max(1, panel_h / row_h);
			if (ac_task_selected_idx_ < ac_task_scroll_offset_)
				ac_task_scroll_offset_ = ac_task_selected_idx_;
			else if (ac_task_selected_idx_ >= ac_task_scroll_offset_ + max_vis)
				ac_task_scroll_offset_ = ac_task_selected_idx_ - max_vis + 1;
		}
		return;
	}

	// Model picker navigation
	if (model_picker_open_) {
		std::vector<std::string> filtered;
		std::string fl = model_picker_filter_;
		std::transform(fl.begin(), fl.end(), fl.begin(), [](unsigned char c){ return std::tolower(c); });
		for (const auto& id : level_model_ids_) {
			if (fl.empty()) { filtered.push_back(id); }
			else {
				std::string idl = id;
				std::transform(idl.begin(), idl.end(), idl.begin(), [](unsigned char c){ return std::tolower(c); });
				if (idl.find(fl) != std::string::npos) filtered.push_back(id);
			}
		}
		int count = (int)filtered.size();
		if (count > 0) {
			if (key == GLUT_KEY_UP)        model_picker_selected_ = std::max(0, model_picker_selected_ - 1);
			else if (key == GLUT_KEY_DOWN) model_picker_selected_ = std::min(count - 1, model_picker_selected_ + 1);
			else if (key == GLUT_KEY_PAGE_UP)   model_picker_selected_ = std::max(0, model_picker_selected_ - 10);
			else if (key == GLUT_KEY_PAGE_DOWN) model_picker_selected_ = std::min(count - 1, model_picker_selected_ + 10);
			const int row_h = 16, panel_h = window_state_.viewport_height_ - 100;
			const int max_vis = std::max(1, panel_h / row_h);
			if (model_picker_selected_ < model_picker_scroll_)
				model_picker_scroll_ = model_picker_selected_;
			else if (model_picker_selected_ >= model_picker_scroll_ + max_vis)
				model_picker_scroll_ = model_picker_selected_ - max_vis + 1;
		}
		return;
	}

	// Arrow key navigation inside AI script / path text boxes
	if (prop_text_edit_field_ == PropPanel::kAIScriptTextField ||
	    prop_text_edit_field_ == PropPanel::kAIScriptPathField) {
		bool isScript = (prop_text_edit_field_ == PropPanel::kAIScriptTextField);
		const int mc = AiScriptMaxChars();

		if (key == GLUT_KEY_LEFT) {
			if (prop_text_caret_ > 0) --prop_text_caret_;
			if (isScript) UpdateAIScriptScroll(); else UpdateAIScriptPathHScroll();
			return;
		}
		if (key == GLUT_KEY_RIGHT) {
			if (prop_text_caret_ < (int)prop_text_buf_.size()) ++prop_text_caret_;
			if (isScript) UpdateAIScriptScroll(); else UpdateAIScriptPathHScroll();
			return;
		}
		if (isScript) {
			if (key == GLUT_KEY_UP || key == GLUT_KEY_DOWN) {
				auto starts = AiTextLineStarts(prop_text_buf_, mc);
				int cl = (int)(std::upper_bound(starts.begin(), starts.end(), prop_text_caret_) - starts.begin()) - 1;
				cl = std::max(0, std::min(cl, (int)starts.size() - 1));
				int col = prop_text_caret_ - starts[cl];
				int nl = cl + (key == GLUT_KEY_DOWN ? 1 : -1);
				nl = std::max(0, std::min(nl, (int)starts.size() - 1));
				int next_end = (nl + 1 < (int)starts.size()) ? starts[nl + 1] : (int)prop_text_buf_.size();
				int line_len = next_end - starts[nl];
				if (line_len > 0 && (starts[nl] + line_len) <= (int)prop_text_buf_.size() &&
				    prop_text_buf_[starts[nl] + line_len - 1] == '\n') --line_len;
				prop_text_caret_ = starts[nl] + std::min(col, line_len);
				UpdateAIScriptScroll();
				return;
			}
			if (key == GLUT_KEY_PAGE_UP) {
				ai_script_vscroll_ = std::max(0, ai_script_vscroll_ - 12);
				return;
			}
			if (key == GLUT_KEY_PAGE_DOWN) {
				auto starts = AiTextLineStarts(prop_text_buf_, mc);
				ai_script_vscroll_ = std::min(std::max(0, (int)starts.size() - 12),
				                              ai_script_vscroll_ + 12);
				return;
			}
		}
	}

	if (task_picker_open_) {
		auto& objects = level_.GetLevelObjects().GetObjects();
		std::vector<int> picker_to_objects;
		
		std::string search_lower = task_picker_search_;
		std::transform(search_lower.begin(), search_lower.end(), search_lower.begin(), [](unsigned char c) { return std::tolower(c); });
		
		std::set<std::string> seen_types;
		for (int i = 0; i < (int)objects.size(); ++i) {
			if (!objects[i].deleted) {
				const auto& obj = objects[i];
				if (obj.type == "Task_DeclareParameters") continue; // picker excludes declare params
				std::string type_lower = obj.type;
				std::transform(type_lower.begin(), type_lower.end(), type_lower.begin(),
				               [](unsigned char c) { return std::tolower(c); });
				if (seen_types.count(type_lower) > 0) continue;
				seen_types.insert(type_lower);
				std::string label = obj.type + "()";

				std::string label_lower = label;
				std::transform(label_lower.begin(), label_lower.end(), label_lower.begin(), [](unsigned char c) { return std::tolower(c); });

				if (search_lower.empty() || label_lower.find(search_lower) != std::string::npos) {
					picker_to_objects.push_back(i);
				}
			}
		}

		if (!picker_to_objects.empty()) {
			int count = (int)picker_to_objects.size();
			if (key == GLUT_KEY_UP) {
				if (task_picker_selected_idx_ > 0) {
					task_picker_selected_idx_--;
				} else {
					task_picker_selected_idx_ = count - 1;
				}
			}
			else if (key == GLUT_KEY_DOWN) {
				if (task_picker_selected_idx_ < count - 1) {
					task_picker_selected_idx_++;
				} else {
					task_picker_selected_idx_ = 0;
				}
			}
			else if (key == GLUT_KEY_PAGE_UP) {
				task_picker_selected_idx_ = std::max(0, task_picker_selected_idx_ - 10);
			}
			else if (key == GLUT_KEY_PAGE_DOWN) {
				task_picker_selected_idx_ = std::min(count - 1, task_picker_selected_idx_ + 10);
			}
			else if (key == GLUT_KEY_HOME) {
				task_picker_selected_idx_ = 0;
			}
			else if (key == GLUT_KEY_END) {
				task_picker_selected_idx_ = count - 1;
			}

			int row_h = 16;
			int picker_h = window_state_.viewport_height_ - 100;
			int max_visible = std::max(1, picker_h / row_h);
			if (task_picker_selected_idx_ < task_picker_scroll_offset_) {
				task_picker_scroll_offset_ = task_picker_selected_idx_;
			}
			else if (task_picker_selected_idx_ >= task_picker_scroll_offset_ + max_visible) {
				task_picker_scroll_offset_ = task_picker_selected_idx_ - max_visible + 1;
			}
		}
		return;
	}

	// C2: caret movement while a property text/numeric box is being edited.
	if (prop_text_edit_field_ != -1) {
		int n = (int)prop_text_buf_.size();
		if (prop_text_caret_ < 0) prop_text_caret_ = 0;
		if (prop_text_caret_ > n) prop_text_caret_ = n;
		if (key == GLUT_KEY_LEFT)  prop_text_caret_ = std::max(0, prop_text_caret_ - 1);
		if (key == GLUT_KEY_RIGHT) prop_text_caret_ = std::min(n, prop_text_caret_ + 1);
		if (key == GLUT_KEY_HOME)  prop_text_caret_ = 0;
		if (key == GLUT_KEY_END)   prop_text_caret_ = n;
		return;
	}

	if (show_hud_ && window_state_.cursor_visible_) {
		if (key == GLUT_KEY_UP || key == GLUT_KEY_DOWN || key == GLUT_KEY_LEFT || key == GLUT_KEY_RIGHT) {
			auto visibleList = GetVisibleTreeNodes();
			if (!visibleList.empty()) {
				int current_row = -1;
				for (int i = 0; i < (int)visibleList.size(); ++i) {
					if (visibleList[i] == selected_object_index_) {
						current_row = i;
						break;
					}
				}

				if (key == GLUT_KEY_UP) {
					if (current_row > 0) {
						selected_object_index_ = visibleList[current_row - 1];
						current_row--;
					} else {
						selected_object_index_ = visibleList.back();
						current_row = (int)visibleList.size() - 1;
					}
				}
				else if (key == GLUT_KEY_DOWN) {
					if (current_row >= 0 && current_row < (int)visibleList.size() - 1) {
						selected_object_index_ = visibleList[current_row + 1];
						current_row++;
					} else {
						selected_object_index_ = visibleList.front();
						current_row = 0;
					}
				}
				else if (key == GLUT_KEY_LEFT) {
					if (selected_object_index_ == -2) {
						tree_decl_expanded_ = false;
						Logger::Get().Log(LogLevel::INFO, "[App] Collapsed Mission Declarations");
					} else if (selected_object_index_ >= 0) {
						auto& obj = level_.GetLevelObjects().GetObjects()[selected_object_index_];
						if (obj.isContainer && obj.expanded) {
							auto& nonConstObj = const_cast<LevelObject&>(obj);
							nonConstObj.expanded = false;
							Logger::Get().Log(LogLevel::INFO, "[App] Collapsed: " + obj.type);
						} else if (obj.parentIndex != -1) {
							selected_object_index_ = obj.parentIndex;
							for (int i = 0; i < (int)visibleList.size(); ++i) {
								if (visibleList[i] == selected_object_index_) {
									current_row = i;
									break;
								}
							}
						}
					}
				}
				else if (key == GLUT_KEY_RIGHT) {
					if (selected_object_index_ == -2) {
						tree_decl_expanded_ = true;
						Logger::Get().Log(LogLevel::INFO, "[App] Expanded Mission Declarations");
					} else if (selected_object_index_ >= 0) {
						auto& obj = level_.GetLevelObjects().GetObjects()[selected_object_index_];
						if (obj.isContainer) {
							if (!obj.expanded) {
								auto& nonConstObj = const_cast<LevelObject&>(obj);
								nonConstObj.expanded = true;
								Logger::Get().Log(LogLevel::INFO, "[App] Expanded: " + obj.type);
							} else if (!obj.childrenIndices.empty()) {
								int firstChild = -1;
								for (int childIdx : obj.childrenIndices) {
									if (childIdx >= 0 && childIdx < (int)level_.GetLevelObjects().GetObjects().size()) {
										if (!level_.GetLevelObjects().GetObjects()[childIdx].deleted) {
											firstChild = childIdx;
											break;
										}
									}
								}
								if (firstChild != -1) {
									selected_object_index_ = firstChild;
									auto newVisibleList = GetVisibleTreeNodes();
									for (int i = 0; i < (int)newVisibleList.size(); ++i) {
										if (newVisibleList[i] == selected_object_index_) {
											current_row = i;
											break;
										}
									}
								}
							}
						}
					}
				}

				// Auto scroll to keep selected item visible
				int row_h = 16;
				int start_y = 30;
				int max_rows = (window_state_.viewport_height_ - 50 - start_y) / row_h;
				if (max_rows > 0) {
					if (current_row < tree_scroll_offset_) {
						tree_scroll_offset_ = current_row;
					} else if (current_row >= tree_scroll_offset_ + max_rows) {
						tree_scroll_offset_ = current_row - max_rows + 1;
					}
				}
			}
			return;
		}
	}

	// F2 always toggles TaskTree — checked before any config key overrides
	if (key == GLUT_KEY_F2) {
		show_hud_ = !show_hud_;
		bridge_.SetEnabled(show_hud_);
		Logger::Get().Log(LogLevel::INFO, std::string("[App] TaskTree ") + (show_hud_ ? "shown" : "hidden"));
		return;
	}

	// All other F-key/special-key bindings go through DispatchEventBindings (qedkeybindings.qsc only)

	if (key == config.keyMoveForward) {
		input_.keys_ |= MK_FORWARD;
		return;
	}

	if (key == config.keyMoveBackward) {
		input_.keys_ |= MK_BACKWARD;
		return;
	}

	if (key == config.keyMoveLeft) {
		input_.keys_ |= MK_LEFT;
		return;
	}

	if (key == config.keyMoveRight) {
		input_.keys_ |= MK_RIGHT;
		return;
	}

	if (key == GLUT_KEY_PAGE_UP) {
		// Shift/Ctrl+PageUp = task tree operations → fall through to DispatchEventBindings
		if (!(glutGetModifiers() & (GLUT_ACTIVE_SHIFT | GLUT_ACTIVE_CTRL))) {
			viewer_.jump_speed_ *= 2.0f;
			if (viewer_.jump_speed_ > MAX_JUMP_SPEED) viewer_.jump_speed_ = MAX_JUMP_SPEED;
			printf("current jump speed set to %d\n", (int)viewer_.jump_speed_);
			return;
		}
	}

	if (key == GLUT_KEY_PAGE_DOWN) {
		// Shift/Ctrl+PageDown = task tree operations → fall through to DispatchEventBindings
		if (!(glutGetModifiers() & (GLUT_ACTIVE_SHIFT | GLUT_ACTIVE_CTRL))) {
			viewer_.jump_speed_ *= 0.5f;
			if (viewer_.jump_speed_ < MIN_JUMP_SPEED) viewer_.jump_speed_ = MIN_JUMP_SPEED;
			printf("current jump speed set to %d\n", (int)viewer_.jump_speed_);
			return;
		}
	}

	if (key == GLUT_KEY_LEFT) {
		input_.keys_ |= MK_ROLL_DEC;
		return;
	}

	if (key == GLUT_KEY_RIGHT) {
		input_.keys_ |= MK_ROLL_INC;
		return;
	}

	if (key == GLUT_KEY_F11) {
		if (selected_object_index_ >= 0) {
			auto& obj = level_.GetLevelObjects().GetObjects()[selected_object_index_];
			viewer_.pos_ = glm::vec3(obj.pos);
			UpdateViewerVectors();
			printf("Teleported to Object [%d]\n", selected_object_index_);
		}
		return;
	}

	DispatchEventBindings();
}

void App::Input_OnSpecialUp(int key, int x, int y) {
	auto& config = Config::Get();

	if (key == config.keyMoveForward) {
		input_.keys_ &= ~MK_FORWARD;
		return;
	}

	if (key == config.keyMoveBackward) {
		input_.keys_ &= ~MK_BACKWARD;
		return;
	}

	if (key == config.keyMoveLeft) {
		input_.keys_ &= ~MK_LEFT;
		return;
	}

	if (key == config.keyMoveRight) {
		input_.keys_ &= ~MK_RIGHT;
		return;
	}

	if (key == GLUT_KEY_LEFT) {
		input_.keys_ &= ~MK_ROLL_DEC;
		return;
	}

	if (key == GLUT_KEY_RIGHT) {
		input_.keys_ &= ~MK_ROLL_INC;
		return;
	}
}

struct movement_key_s {
	char	lower_case_;
	char	upper_case_;
	int		key_flag_;
};

static constexpr movement_key_s MOVEMENT_KEYS[] = {
	'q', 'Q', MK_STRAIGHT_UP,
	'z', 'Z', MK_STRAIGHT_DOWN
};

// Manip keys are now handled directly in Input_OnKeyboard using config values

void App::Input_OnKeyboard(unsigned char key, int x, int y) {
	auto& config = Config::Get();

	// Autocomplete Ctrl combos — intercept before prop text editor so they work while editing.
	// Detect Ctrl via GLUT *and* GetAsyncKeyState: GLUT modifiers are occasionally not
	// reported for Ctrl+Space (key 0/space), which silently dropped inline autocomplete.
	bool ctrlDown = (glutGetModifiers() & GLUT_ACTIVE_CTRL) != 0 ||
	                (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
	bool shiftDown = (glutGetModifiers() & GLUT_ACTIVE_SHIFT) != 0 ||
	                 (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
	if (ctrlDown) {
		if (key == 14 && !shiftDown) { // Ctrl+N only (not Ctrl+Shift+N — that's TaskFindByTaskNote)
			// Only open when a property text box is focused, so Enter knows which
			// field to insert the chosen item into.
			if (prop_text_edit_field_ == -1) {
				status_message_ = "Click a text box first, then Ctrl+N to pick a task type";
				return;
			}
			picker_target_field_  = prop_text_edit_field_;
			picker_target_obj_    = prop_edit_obj_index_;
			picker_target_caret_  = prop_text_caret_;
			ac_task_items_ = autocomplete_keywords_;
			ac_task_picker_open_ = true; ac_task_selected_idx_ = 0; ac_task_scroll_offset_ = 0; ac_task_filter_.clear();
			return;
		}
		if (key == 15) { // Ctrl+O → Model ID picker (only open when active text box has XXX_XX_X content)
			if (prop_text_edit_field_ == -1) {
				status_message_ = "Click a text box first, then Ctrl+O to pick a model";
				return;
			}
			// Check if current text looks like a model ID (digits and underscores)
			bool looksLikeModelId = true;
			for (char c : prop_text_buf_)
				if (!isdigit(c) && c != '_' && c != ' ') { looksLikeModelId = false; break; }
			if (!looksLikeModelId && !prop_text_buf_.empty()) {
				return; // Only open from model ID text boxes
			}
			picker_target_field_ = prop_text_edit_field_; picker_target_obj_ = prop_edit_obj_index_;
			model_picker_open_ = true; model_picker_selected_ = 0; model_picker_scroll_ = 0;
			model_picker_filter_.clear(); // show ALL model IDs; user types to filter
			return;
		}
		if (key == 0 || key == ' ') { // Ctrl+Space → AutoComplete inline
			if (prop_text_edit_field_ == -1) {
				status_message_ = "Autocomplete: click a text box first";
			} else if (autocomplete_keywords_.empty()) {
				status_message_ = "Autocomplete: no keywords loaded (content/tools/AutoCompleteKeywords.txt)";
			} else {
				int ws = prop_text_caret_;
				while (ws > 0 && (isalnum((unsigned char)prop_text_buf_[ws-1]) || prop_text_buf_[ws-1] == '_')) ws--;
				std::string prefix = prop_text_buf_.substr(ws, prop_text_caret_ - ws);
				std::string pl = prefix;
				std::transform(pl.begin(), pl.end(), pl.begin(), [](unsigned char c){ return std::tolower(c); });
				bool done = false;
				if (!pl.empty()) {
					for (auto& kw : autocomplete_keywords_) {
						std::string kwl = kw;
						std::transform(kwl.begin(), kwl.end(), kwl.begin(), [](unsigned char c){ return std::tolower(c); });
						if (kwl.size() >= pl.size() && kwl.compare(0, pl.size(), pl) == 0) {
							prop_text_buf_.replace(ws, prefix.size(), kw);
							prop_text_caret_ = ws + (int)kw.size();
							status_message_ = "Autocompleted: " + kw;
							done = true;
							// Sync AI script state immediately (inline autocomplete keeps editing)
							if (prop_text_edit_field_ == PropPanel::kAIScriptTextField) {
								ai_script_text_  = prop_text_buf_;
								ai_script_dirty_ = true;
								UpdateAIScriptScroll();
							}
							break;
						}
					}
				}
				if (!done) status_message_ = prefix.empty() ? "Autocomplete: type a prefix first"
				                                             : ("No keyword starts with '" + prefix + "'");
			}
			return;
		}
	}

	// C2: Property text editor input (caret-based).
	// Skip while a picker or file dialog is open so their keystrokes (filter typing,
	// Enter to insert/confirm) reach their own handlers below instead of being eaten
	// here — otherwise typing edits the box and Enter commits+closes the field.
	if (prop_text_edit_field_ != -1 && !ac_task_picker_open_ && !model_picker_open_ &&
	    file_dialog_mode_ == FileDialogMode::None) {
		int& caret = prop_text_caret_;
		if (caret < 0) caret = 0;
		if (caret > (int)prop_text_buf_.size()) caret = (int)prop_text_buf_.size();
		bool multiline = IsPropFieldMultiline(prop_text_edit_field_);
		if (key == 27) { // ESC — cancel (revert)
			prop_text_edit_field_ = -1;
			return;
		}
		if (key == 13) { // Enter
			if (multiline) {              // VarString/String256: insert newline
				prop_text_buf_.insert(prop_text_buf_.begin() + caret, '\n');
				caret++;
				UpdateAIScriptScroll();
			} else {                      // single-line: commit
				CommitPropTextEdit();
			}
			return;
		}
		if (key == 8) { // Backspace — delete before caret
			if (caret > 0) {
				prop_text_buf_.erase(prop_text_buf_.begin() + (caret - 1));
				caret--;
				UpdateAIScriptScroll();
				UpdateAIScriptPathHScroll();
			}
			return;
		}
		if (key == 127) { // Delete — delete at caret
			if (caret < (int)prop_text_buf_.size())
				prop_text_buf_.erase(prop_text_buf_.begin() + caret);
			UpdateAIScriptScroll();
			return;
		}
		if (key >= 32 && key <= 126) { // printable: insert at caret
			prop_text_buf_.insert(prop_text_buf_.begin() + caret, (char)key);
			caret++;
			UpdateAIScriptScroll();
			UpdateAIScriptPathHScroll();
			return;
		}
		return;
	}

	// File dialog input (SaveSubTask / LoadSubTask)
	if (file_dialog_mode_ != FileDialogMode::None) {
		if (key == 27) { file_dialog_mode_ = FileDialogMode::None; return; }
		if (key == 13) { ConfirmFileDialog(); return; }
		if (key == 8) {
			if (file_dialog_caret_ > 0) {
				file_dialog_path_.erase(file_dialog_path_.begin() + file_dialog_caret_ - 1);
				file_dialog_caret_--;
			}
		} else if (key >= 32 && key <= 126) {
			file_dialog_path_.insert(file_dialog_path_.begin() + file_dialog_caret_, (char)key);
			file_dialog_caret_++;
		}
		return;
	}

	// Autocomplete task picker input (Ctrl+N panel)
	if (ac_task_picker_open_) {
		if (key == 27) { ac_task_picker_open_ = false; return; }
		if (key == 13) {
			// Build filtered list and get selected item
			std::vector<std::string> filtered;
			std::string fl = ac_task_filter_;
			std::transform(fl.begin(), fl.end(), fl.begin(), [](unsigned char c){ return std::tolower(c); });
			for (auto& item : ac_task_items_) {
				if (fl.empty()) { filtered.push_back(item); }
				else {
					std::string il = item; std::transform(il.begin(), il.end(), il.begin(), [](unsigned char c){ return std::tolower(c); });
					if (il.find(fl) != std::string::npos) filtered.push_back(item);
				}
			}
			ac_task_picker_open_ = false;
			// Restore field, obj, and caret position captured when the picker opened.
			if (picker_target_field_ != -1) {
				prop_text_edit_field_ = picker_target_field_;
				prop_edit_obj_index_  = picker_target_obj_;
			}
			if (prop_text_edit_field_ != -1 && ac_task_selected_idx_ < (int)filtered.size()) {
				const std::string& item = filtered[ac_task_selected_idx_];
				bool isAI = (prop_text_edit_field_ == PropPanel::kAIScriptTextField);
				if (isAI) {
					// For AI script: INSERT at saved caret position (don't replace entire script)
					int ins = std::max(0, std::min(picker_target_caret_, (int)prop_text_buf_.size()));
					prop_text_buf_.insert(ins, item);
					prop_text_caret_ = ins + (int)item.size();
					// Commit: store into ai_script_text_ and mark dirty, keep editing
					ai_script_text_  = prop_text_buf_;
					ai_script_dirty_ = true;
					UpdateAIScriptScroll();
				} else {
					prop_text_buf_  = item;       // REPLACE field content (existing behaviour)
					prop_text_caret_ = (int)prop_text_buf_.size();
					CommitPropTextEdit();
				}
			}
			picker_target_field_ = -1; picker_target_obj_ = -1; picker_target_caret_ = -1;
			return;
		}
		if (key == 8) {
			if (!ac_task_filter_.empty()) { ac_task_filter_.pop_back(); ac_task_selected_idx_ = 0; ac_task_scroll_offset_ = 0; }
		} else if (key >= 32 && key <= 126) {
			ac_task_filter_ += (char)key;
			ac_task_selected_idx_ = 0;
			ac_task_scroll_offset_ = 0;
		}
		return;
	}

	// Model picker input (Ctrl+O panel)
	if (model_picker_open_) {
		if (key == 27) { model_picker_open_ = false; return; }
		if (key == 13) {
			// Build filtered list from level_model_ids_
			std::vector<std::string> filtered;
			std::string fl = model_picker_filter_;
			std::transform(fl.begin(), fl.end(), fl.begin(), [](unsigned char c){ return std::tolower(c); });
			for (const auto& id : level_model_ids_) {
				if (fl.empty()) { filtered.push_back(id); }
				else {
					std::string idl = id;
					std::transform(idl.begin(), idl.end(), idl.begin(), [](unsigned char c){ return std::tolower(c); });
					if (idl.find(fl) != std::string::npos) filtered.push_back(id);
				}
			}
			model_picker_open_ = false;
			// Restore the field captured when the picker opened, so the choice lands in
			// the exact text box the cursor was in (even if focus changed meanwhile).
			if (picker_target_field_ >= 0) { prop_text_edit_field_ = picker_target_field_; prop_edit_obj_index_ = picker_target_obj_; }
			if (prop_text_edit_field_ >= 0 && model_picker_selected_ < (int)filtered.size()) {
				prop_text_buf_ = filtered[model_picker_selected_]; // CLEAR and insert
				prop_text_caret_ = (int)prop_text_buf_.size();
				CommitPropTextEdit();              // apply the chosen model to the field
			}
			picker_target_field_ = -1; picker_target_obj_ = -1;
			return;
		}
		if (key == 8) {
			if (!model_picker_filter_.empty()) { model_picker_filter_.pop_back(); model_picker_selected_ = 0; model_picker_scroll_ = 0; }
		} else if (key >= 32 && key <= 126) {
			model_picker_filter_ += (char)key;
			model_picker_selected_ = 0;
			model_picker_scroll_ = 0;
		}
		return;
	}

	// C3: Find bar input
	if (find_open_) {
		if (key == 27) { // ESC — close
			find_open_ = false;
			find_query_.clear();
			find_result_idx_ = -1;
			return;
		}
		if (key == 13) { // Enter — confirm
			if (find_mode_ == FindMode::SetId) {
				// Empty query → automatic unique ID; otherwise set the typed ID.
				std::string q = find_query_;
				while (!q.empty() && (q.front() == ' ' || q.front() == '\t')) q.erase(q.begin());
				while (!q.empty() && (q.back()  == ' ' || q.back()  == '\t')) q.pop_back();
				if (selected_object_index_ >= 0) {
					if (q.empty()) {
						AssignTaskID();
					} else {
						auto& objects = level_.GetLevelObjects().GetObjects();
						objects[selected_object_index_].taskId = q;
						objects[selected_object_index_].modified = true;
						level_.GetLevelObjects().UpdateCoordinatesInLine(objects[selected_object_index_]);
						SaveAndReloadObjects();
						auto& reloaded = level_.GetLevelObjects().GetObjects();
						if (!reloaded.empty()) selected_object_index_ = std::min(selected_object_index_, (int)reloaded.size() - 1);
						status_message_ = "Set Task ID: " + q;
					}
				}
				find_open_ = false; find_query_.clear(); find_result_idx_ = -1;
				return;
			}
			if (find_result_idx_ >= 0) {
				selected_object_index_ = find_result_idx_;
				// Expand all ancestor containers so the found item is visible in tree
				{
					auto& objects = level_.GetLevelObjects().GetObjects();
					int idx = find_result_idx_;
					while (idx >= 0 && idx < (int)objects.size()) {
						int parent = objects[idx].parentIndex;
						if (parent < 0 || parent >= (int)objects.size()) break;
						if (objects[parent].isContainer)
							objects[parent].expanded = true;
						idx = parent;
					}
				}
				// Scroll the tree to make the found item visible
				auto visibleList = GetVisibleTreeNodes();
				int current_row = -1;
				for (int i = 0; i < (int)visibleList.size(); ++i) {
					if (visibleList[i] == find_result_idx_) {
						current_row = i;
						break;
					}
				}
				if (current_row >= 0) {
					int row_h = 16;
					int start_y = 30;
					int max_rows = (window_state_.viewport_height_ - 50 - start_y) / row_h;
					if (max_rows > 0) {
						if (current_row < tree_scroll_offset_)
							tree_scroll_offset_ = current_row;
						else if (current_row >= tree_scroll_offset_ + max_rows)
							tree_scroll_offset_ = current_row - max_rows + 1;
					}
				}
			}
			find_open_ = false;
			find_query_.clear();
			find_result_idx_ = -1;
			return;
		}
		if (key == 8) { // Backspace
			if (!find_query_.empty())
				find_query_.pop_back();
		} else if (key >= 32 && key <= 126) {
			find_query_ += (char)key;
		}
		// Search (respects find_mode_). SetId is input-only — no live search.
		if (!find_query_.empty() && find_mode_ != FindMode::SetId) {
			std::string q_lower = find_query_;
			std::transform(q_lower.begin(), q_lower.end(), q_lower.begin(), [](unsigned char c){ return std::tolower(c); });
			const auto& objects = level_.GetLevelObjects().GetObjects();
			find_result_idx_ = -1;
			for (int i = 0; i < (int)objects.size(); ++i) {
				if (objects[i].deleted) continue;
				if (objects[i].type == "Task_DeclareParameters") continue;
				std::string label;
				if (find_mode_ == FindMode::TextInTask) {
					for (auto& tok : objects[i].argTokens) label += tok + " ";
				} else if (find_mode_ == FindMode::ById) {
					label = objects[i].taskId;
				} else if (find_mode_ == FindMode::ByNote) {
					label = objects[i].name;
				} else {
					label = objects[i].type + " " + objects[i].name + " " + objects[i].taskId;
				}
				std::transform(label.begin(), label.end(), label.begin(), [](unsigned char c){ return std::tolower(c); });
				if (label.find(q_lower) != std::string::npos) {
					find_result_idx_ = i;
					break;
				}
			}
		} else {
			find_result_idx_ = -1;
		}
		return;
	}

	if (task_picker_open_) {
		if (key == 27) { // ESC - Cancel and close
			task_picker_open_ = false;
			return;
		}
		if (key == 13) { // Enter - Confirm selection
			auto& objects = level_.GetLevelObjects().GetObjects();
			std::vector<int> picker_to_objects;
			
			std::string search_lower = task_picker_search_;
			std::transform(search_lower.begin(), search_lower.end(), search_lower.begin(), [](unsigned char c) { return std::tolower(c); });
			
			std::set<std::string> seen_types;
			for (int i = 0; i < (int)objects.size(); ++i) {
				if (!objects[i].deleted) {
					const auto& obj = objects[i];
					if (obj.type == "Task_DeclareParameters") continue; // picker excludes declare params
					std::string type_lower = obj.type;
					std::transform(type_lower.begin(), type_lower.end(), type_lower.begin(),
					               [](unsigned char c) { return std::tolower(c); });
					if (seen_types.count(type_lower) > 0) continue;
					seen_types.insert(type_lower);
					std::string label = obj.type + "()";

					std::string label_lower = label;
					std::transform(label_lower.begin(), label_lower.end(), label_lower.begin(), [](unsigned char c) { return std::tolower(c); });

					if (search_lower.empty() || label_lower.find(search_lower) != std::string::npos) {
						picker_to_objects.push_back(i);
					}
				}
			}

			if (task_picker_selected_idx_ >= 0 && task_picker_selected_idx_ < (int)picker_to_objects.size()) {
				if (selected_object_index_ < 0 || selected_object_index_ >= (int)objects.size()) {
					status_message_ = "Error: Must select a valid parent task first.";
					Logger::Get().Log(LogLevel::WARNING, "[App] Validation failed: Parent index is invalid.");
					task_picker_open_ = false;
					return;
				}

				int sourceIdx = picker_to_objects[task_picker_selected_idx_];
				
				std::vector<LevelObject> temp_clipboard;
				std::function<void(int, int)> copy_recurse = [&](int idx, int newParentInTemp) {
					if (idx < 0 || idx >= (int)objects.size()) return;
					
					LevelObject copy = objects[idx];
					copy.childrenIndices.clear();
					copy.parentIndex = newParentInTemp;
					copy.modified = true;
					copy.qscLine.clear();

					// Clear IDs, model names, and notes (keeping position/rotation as default)
					copy.taskId = "-1";
					copy.name = "";
					copy.original_name = "";
					copy.has_original_name = false;
					copy.modelId = "";
					copy.aiId = "";
					copy.graphId = "";
					copy.graphName = "";
					copy.primaryWeapon = "";
					copy.primaryAmmo = "";
					copy.secondaryWeapon = "";
					copy.secondaryAmmo = "";
					copy.segmentModelId = "";
					copy.secondaryModelId = "";
					copy.lensModelId = "";
					copy.splineTaskId = "";
					copy.isAttaProxy = false;
					copy.attaRecordIndex = -1;
					copy.attaParentModelId = "";

					// Synchronize cleared fields into the argTokens
					level_.GetLevelObjects().UpdateCoordinatesInLine(copy);
					
					int tempIdx = (int)temp_clipboard.size();
					temp_clipboard.push_back(copy);
					
					if (newParentInTemp != -1) {
						temp_clipboard[newParentInTemp].childrenIndices.push_back(tempIdx);
					}
					
					// Do not recursively copy children for new tasks inserted from the picker
					/*
					for (int childIdx : objects[idx].childrenIndices) {
						copy_recurse(childIdx, tempIdx);
					}
					*/
				};
				
				copy_recurse(sourceIdx, -1);

				if (!ValidateParentChildCompatibility(objects[selected_object_index_], temp_clipboard)) {
					status_message_ = "Error: Cannot add Computer to a WaterTower.";
					Logger::Get().Log(LogLevel::WARNING, "[App] Validation failed: Cannot add Computer task to WaterTower parent.");
					task_picker_open_ = false;
					return;
				}
				
				int targetParent = selected_object_index_;
				int startIdxInObjects = (int)objects.size();

				// Collect all in-use task IDs for unique ID generation
				std::set<int> usedIds;
				for (const auto& obj : objects) {
					if (obj.deleted) continue;
					if (obj.taskId.empty() || obj.taskId == "-1") continue;
					try { usedIds.insert(std::stoi(obj.taskId)); } catch (...) {}
				}

				int levelNo = level_.GetLevelNo();
				std::string aiDir = Utils::GetIGIRootPath() + "\\missions\\location0\\level" + std::to_string(levelNo) + "\\ai";

				for (size_t i = 0; i < temp_clipboard.size(); ++i) {
					LevelObject pasted = temp_clipboard[i];

					if (pasted.parentIndex == -1) {
						pasted.parentIndex = targetParent;
						if (targetParent != -1) {
							if (task_picker_insert_first_)
								objects[targetParent].childrenIndices.insert(
									objects[targetParent].childrenIndices.begin(), (int)objects.size());
							else
								objects[targetParent].childrenIndices.push_back((int)objects.size());
							objects[targetParent].modified = true;
						}
					} else {
						pasted.parentIndex += startIdxInObjects;
					}
					
					for (size_t j = 0; j < pasted.childrenIndices.size(); ++j) {
						pasted.childrenIndices[j] += startIdxInObjects;
					}
					
					if (pasted.qscFuncName == "Task_New") {
						if (pasted.type == "HumanSoldier" || pasted.type == "HumanSoldierFemale" || pasted.type == "HumanAI") {
							std::string oldId = pasted.taskId;

							// Find next available unique ID (ensuring no file conflict in AI directory)
							int newId = 1;
							while (true) {
								if (usedIds.count(newId) == 0) {
									std::string idStr = std::to_string(newId);
									std::string qvmPath = aiDir + "\\" + idStr + ".qvm";
									std::string qscPath = aiDir + "\\" + idStr + ".qsc";
									if (!std::filesystem::exists(qvmPath) && !std::filesystem::exists(qscPath)) {
										break;
									}
								}
								newId++;
							}
							usedIds.insert(newId);

							std::string newIdStr = std::to_string(newId);
							pasted.taskId = newIdStr;
							if (!pasted.argTokens.empty()) {
								pasted.argTokens[0] = newIdStr;
							}
							pasted.qscLine.clear(); // Force regeneration from argTokens on save
							pasted.modified = true;

							// For HumanAI: create and compile a new AI script (.qsc -> .qvm)
							if (pasted.type == "HumanAI") {
								std::filesystem::create_directories(aiDir);
								std::string qscPath = aiDir + "\\" + newIdStr + ".qsc";
								std::string qvmPath = aiDir + "\\" + newIdStr + ".qvm";
								std::string qscContent = 
									"if (AIFunction_GetCurrentEventType() == AIEVENT_CREATE)\n"
									"{\n"
									"  AIFunction_DefaultHandler();\n"
									"}\n"
									"else\n"
									"{\n"
									"  AIFunction_DefaultHandler();\n"
									"}";

								std::ofstream qscFile(qscPath);
								if (qscFile) {
									qscFile << qscContent;
									qscFile.close();

									// Compile QSC to QVM
									auto lexResult  = qsc::Lex(qscContent);
									auto parseResult = lexResult.ok ? qsc::Parse(lexResult.tokens) : qsc::ParseResult{};
									std::string compileErr;
									bool success = lexResult.ok && parseResult.ok &&
									               qvm::CompileToFile(*parseResult.program, qvmPath, &compileErr);
									if (success) {
										Logger::Get().Log(LogLevel::INFO, "[App] Successfully compiled new AI script: " + qvmPath);
									} else {
										std::string detail = compileErr.empty() ? "(no detail)" : compileErr;
										Logger::Get().Log(LogLevel::ERR, "[App] Failed to compile new AI script: " + detail);
										status_message_ = "Warning: Failed to compile new AI script for Task " + newIdStr + ".";
									}

									// Remove temporary qsc file
									std::error_code ec;
									std::filesystem::remove(qscPath, ec);
									if (ec) {
										Logger::Get().Log(LogLevel::WARNING, "[App] Failed to remove temp QSC: " + qscPath + " (" + ec.message() + ")");
									}
								} else {
									Logger::Get().Log(LogLevel::ERR, "[App] Failed to create temp QSC file: " + qscPath);
									status_message_ = "Warning: Failed to create temp QSC file for Task " + newIdStr + ".";
								}
							}
							Logger::Get().Log(LogLevel::INFO, "[App] Assigned unique Task ID " + newIdStr + " to newly inserted " + pasted.type);
						} else {
							pasted.taskId = "-1";
							if (!pasted.argTokens.empty()) {
								pasted.argTokens[0] = "-1";
							}
						}
					}
					
					objects.push_back(pasted);
					level_.GetLevelObjects().UpdateCoordinatesInLine(objects.back());
				}
				
				// Override position with camera location if TaskNewCameraRelative
				if (task_new_at_camera_ && startIdxInObjects < (int)objects.size()) {
					objects[startIdxInObjects].pos = glm::dvec3(viewer_.pos_);
					objects[startIdxInObjects].modified = true;
					level_.GetLevelObjects().UpdateCoordinatesInLine(objects[startIdxInObjects]);
				}
				task_picker_insert_first_ = false;
				task_new_at_camera_       = false;

				selected_object_index_ = startIdxInObjects;
				SaveAndReloadObjects();

				auto& reloaded = level_.GetLevelObjects().GetObjects();
				if (!reloaded.empty()) {
					selected_object_index_ = std::min(selected_object_index_, (int)reloaded.size() - 1);
				}
				Logger::Get().Log(LogLevel::INFO, "[App] Successfully cloned task subtree into the tree hierarchy.");
			}
			task_picker_open_ = false;
			return;
		}
		if (key == 8) { // Backspace
			if (!task_picker_search_.empty()) {
				task_picker_search_.pop_back();
				task_picker_selected_idx_ = 0;
				task_picker_scroll_offset_ = 0;
			}
			return;
		}
		if (key >= 32 && key <= 126) { // Printable characters
			if (task_picker_search_.size() < 20) {
				task_picker_search_ += (char)key;
				task_picker_selected_idx_ = 0;
				task_picker_scroll_offset_ = 0;
			}
			return;
		}
		return; // Block other keyboard input while picker is open
	}

	// C3: Ctrl+F — toggle find bar (key 6 = Ctrl+F, hardcoded for convenience)
	// Don't intercept Ctrl+Shift+F — that goes to TaskFindAgain via event bindings.
	if (key == 6 && !(glutGetModifiers() & GLUT_ACTIVE_SHIFT)) {
		find_open_       = !find_open_;
		find_mode_       = FindMode::TaskNameTypeId;
		find_query_.clear();
		find_result_idx_ = -1;
		return;
	}

	// Enter: open property editor for the selected task, same as double-click.
	// For containers also toggle expanded/collapsed.
	if (key == 13 && !(glutGetModifiers() & GLUT_ACTIVE_ALT)) {
		if (selected_object_index_ >= 0) {
			auto& objects = level_.GetLevelObjects().GetObjects();
			if (selected_object_index_ < (int)objects.size()) {
				auto& obj = objects[selected_object_index_];
				prop_editor_open_ = true; prop_panel_scroll_ = 0; prop_text_edit_field_ = -1; prop_edit_obj_index_ = -1;
				LoadAIScriptForSelected();
				if (obj.isContainer) {
					obj.expanded = !obj.expanded;
					Logger::Get().Log(LogLevel::INFO, "[App] Enter opened props + toggled expand for " + obj.type);
				} else {
					Logger::Get().Log(LogLevel::INFO, "[App] Enter opened property panel for " + obj.type);
				}
				return;
			}
		}
	}

	// Check for modifier keys - if pressed, skip movement key checks
	int modifiers = glutGetModifiers();
	bool has_modifiers = (modifiers & (GLUT_ACTIVE_CTRL | GLUT_ACTIVE_SHIFT | GLUT_ACTIVE_ALT));

	// Check movement keys (regular keyboard characters) - case insensitive
	// Only check if the config key is a regular character (ASCII < 128 and not a special key code)
	// Special key codes (arrow keys) are >= 100 and should not match regular character keys
	if (!has_modifiers) {
		// Only check movement keys if they are regular characters, not special keys
		// Special keys have codes >= 100 (GLUT_KEY_UP=101, DOWN=103, LEFT=100, RIGHT=102)
		if (config.keyMoveForward < 100 && config.keyMoveForward > 0 && toupper(key) == toupper(config.keyMoveForward)) {
			input_.keys_ |= MK_FORWARD;
			return;
		}
		if (config.keyMoveBackward < 100 && config.keyMoveBackward > 0 && toupper(key) == toupper(config.keyMoveBackward)) {
			input_.keys_ |= MK_BACKWARD;
			return;
		}
		if (config.keyMoveLeft < 100 && config.keyMoveLeft > 0 && toupper(key) == toupper(config.keyMoveLeft)) {
			input_.keys_ |= MK_LEFT;
			return;
		}
		if (config.keyMoveRight < 100 && config.keyMoveRight > 0 && toupper(key) == toupper(config.keyMoveRight)) {
			input_.keys_ |= MK_RIGHT;
			return;
		}
	}

	if (key == 27) { // ESC
		if (show_help_) { show_help_ = false; return; }
		if (prop_editor_open_) { // close the property panel first
			prop_editor_open_ = false;
			prop_field_index_ = -1;
			prop_text_edit_field_ = -1;
			return;
		}
		TogglePauseMenu();
		return;
	}


	// DEL key: delete selected task (hardcoded for standard keyboard ergonomics)
	if (key == 127) { DeleteSelectedTask(); return; }

	if (pause_mode_) {
		return;

	}


	if ((key == 13) && (glutGetModifiers() & GLUT_ACTIVE_ALT)) { // ALT + ENTER toggle full screen mode
		window_state_.full_screen_ = !window_state_.full_screen_;

		if (window_state_.full_screen_) {

			window_state_.old_viewport_width_ = window_state_.viewport_width_;
			window_state_.old_viewport_height_ = window_state_.viewport_height_;

			glutFullScreen();
		}
		else {
			glutReshapeWindow(window_state_.old_viewport_width_, window_state_.old_viewport_height_);
		}

		return;
	}

	for (int i = 0; i < count_of(MOVEMENT_KEYS); ++i) {
		const movement_key_s& mk = MOVEMENT_KEYS[i];
		if (key == mk.lower_case_ || key == mk.upper_case_) {
			input_.keys_ |= mk.key_flag_;
			return;
		}
	}

	// Object Manipulation from QED config
	if (toupper(key) == toupper(config.keyRotateAlpha)) { input_.keys_ |= MK_MANIP_A; return; }
	if (toupper(key) == toupper(config.keyRotateBeta))  { input_.keys_ |= MK_MANIP_B; return; }
	if (toupper(key) == toupper(config.keyRotateGamma)) { input_.keys_ |= MK_MANIP_G; return; }
	if (toupper(key) == toupper(config.keySnapGround))  {
        PushUndoState();
        input_.keys_ |= MK_MANIP_S;
        if (selected_object_index_ >= 0) UpdateMarkerManipulation();
        input_.keys_ &= ~MK_MANIP_S;
        return;
    }
	if (toupper(key) == toupper(config.keySnapObject))  {
        PushUndoState();
        input_.keys_ |= MK_MANIP_O;
        if (selected_object_index_ >= 0) UpdateMarkerManipulation();
        input_.keys_ &= ~MK_MANIP_O;
        return;
    }
	if (key == ' ' && !(glutGetModifiers() & GLUT_ACTIVE_ALT)) {
        PushUndoState();
        input_.keys_ |= MK_MANIP_SPACE;
        if (selected_object_index_ >= 0) UpdateMarkerManipulation();
        input_.keys_ &= ~MK_MANIP_SPACE;
        return;
    }

	if (key == 't' || key == 'T') {
		level_.TeleportToHMP(viewer_.pos_);
		viewer_.pitch_ = -30.0f; // Look down at the terrain
		UpdateViewerVectors();
		printf("Teleported to Height Map Zone\n");
		return;
	}

	// HUD toggle removed as per request



	if (key == 's' || key == 'S') {
		// Snap selected object to ground (works in any mode)
		if (selected_object_index_ >= 0) {
			auto& objects = level_.GetLevelObjects().GetObjects();
			if (selected_object_index_ < (int)objects.size()) {
				auto& obj = objects[selected_object_index_];
				if (!Utils::IsUndergroundModel(obj.name, obj.modelId)) {
					float terrainZ = 0.0f;
					if (level_.GetTerrainZ(obj.pos.x, obj.pos.y, terrainZ)) {
						float zOffset = renderer_.GetMeshZOffset(obj.modelId, obj.isBuilding);
						// Snap object bottom to terrain surface
						obj.pos.z = (double)terrainZ + (double)(zOffset * 40.96f * obj.scale);
						Logger::Get().Log(LogLevel::INFO, "[App] Snapped object to ground");
					}
				}
			}
		}
		return;
	}


	// Plain Tab cycles selection; Ctrl+I and Ctrl+Shift+I must reach DispatchEventBindings.
	if (key == '\t' && !(glutGetModifiers() & (GLUT_ACTIVE_CTRL | GLUT_ACTIVE_SHIFT | GLUT_ACTIVE_ALT))) {
		LevelObjects& lo = level_.GetLevelObjects();
		if (!lo.GetObjects().empty()) {
			selected_object_index_ = (selected_object_index_ + 1) % (int)lo.GetObjects().size();
			printf("Selected Object: %d / %d\n", selected_object_index_, (int)lo.GetObjects().size());
		}
		return;
	}

	// HandleMarkerInput(key);

	DispatchEventBindings();
}

void App::DispatchEventBindings() {
	// Guard: skip dispatch while any text-input modal is open
	if (task_picker_open_ || task_editor_open_ || find_open_ ||
	    file_dialog_mode_ != FileDialogMode::None ||
	    ac_task_picker_open_ || model_picker_open_) return;

	auto& eventBindings = Config::Get().eventBindings_;

	auto Check = [&](const std::string& name) -> bool {
		auto it = eventBindings.find(name);
		if (it == eventBindings.end()) return false;
		// Exact modifier match so e.g. Ctrl+C (TaskCopy) does not also fire while
		// Ctrl+Shift+C (TaskCopyRecursive) is pressed, F2 during Shift+F2, etc.
		return Utils::IsKeyBindingPressedExact(it->second);
	};

	// ---- Help ----
	if (Check("ToggleHelp")) {
		show_help_ = !show_help_;
		help_scroll_offset_ = 0;
		if (show_help_) LoadHelpEntries();
		return;
	}

	// ---- Camera ----
	if (Check("CameraEnable")) { /* handled by existing named binding */ }
	if (Check("CameraMoveForward")) { /* handled by existing named binding */ }
	if (Check("CameraMoveBackward")) { /* handled by existing named binding */ }
	if (Check("CameraAdjustRadius")) { /* handled by existing named binding */ }
	if (Check("CameraResetRadius")) { Logger::Get().Log(LogLevel::INFO, "[Keybind] CameraResetRadius not implemented"); }
	if (Check("CameraLookDown")) { /* handled by existing named binding */ }
	if (Check("CameraCopyPosition")) { Logger::Get().Log(LogLevel::INFO, "[Keybind] CameraCopyPosition not implemented"); }
	if (Check("CameraCopyOrientation")) { Logger::Get().Log(LogLevel::INFO, "[Keybind] CameraCopyOrientation not implemented"); }
	if (Check("CameraSnapToObject")) {
		if (selected_object_index_ >= 0) {
			auto& objects = level_.GetLevelObjects().GetObjects();
			if (selected_object_index_ < (int)objects.size()) {
				PushUndoState();
				input_.keys_ |= MK_MANIP_O;
				if (selected_object_index_ >= 0) UpdateMarkerManipulation();
				input_.keys_ &= ~MK_MANIP_O;
			}
		}
	}
	if (Check("CameraSnapToObjectWithRadius")) { Logger::Get().Log(LogLevel::INFO, "[Keybind] CameraSnapToObjectWithRadius not implemented"); }
	if (Check("CameraSnapToGround")) {
		if (selected_object_index_ >= 0) {
			auto& objects = level_.GetLevelObjects().GetObjects();
			if (selected_object_index_ < (int)objects.size()) {
				auto& obj = objects[selected_object_index_];
				if (!Utils::IsUndergroundModel(obj.name, obj.modelId)) {
					float terrainZ = 0.0f;
					if (level_.GetTerrainZ(obj.pos.x, obj.pos.y, terrainZ)) {
						float zOffset = renderer_.GetMeshZOffset(obj.modelId, obj.isBuilding);
						obj.pos.z = (double)terrainZ + (double)(zOffset * 40.96f * obj.scale);
						obj.modified = true;
						level_.GetLevelObjects().UpdateCoordinatesInLine(obj);
						Logger::Get().Log(LogLevel::INFO, "[App] CameraSnapToGround: Snapped object to ground");
					}
				}
			}
		}
	}
	if (Check("CameraStrafe")) { Logger::Get().Log(LogLevel::INFO, "[Keybind] CameraStrafe not implemented"); }
	// CameraStrafeFree is handled at the end of this function (one-shot toggle)
	// CameraStrafeLeft/Right are handled in ProcessInput (continuous per-frame movement)

	// ---- Tasks ----
	if (Check("TaskMoveUp")) {
		if (selected_object_index_ >= 0) {
			auto& objects = level_.GetLevelObjects().GetObjects();
			int par = objects[selected_object_index_].parentIndex;
			if (par >= 0) {
				auto& kids = objects[par].childrenIndices;
				auto it = std::find(kids.begin(), kids.end(), selected_object_index_);
				if (it != kids.end() && it != kids.begin()) {
					PushUndoState();
					std::swap(*it, *(it - 1));
					objects[par].modified = true;
					SaveAndReloadObjects();
				}
			}
		}
		return;
	}
	if (Check("TaskMoveDown")) {
		if (selected_object_index_ >= 0) {
			auto& objects = level_.GetLevelObjects().GetObjects();
			int par = objects[selected_object_index_].parentIndex;
			if (par >= 0) {
				auto& kids = objects[par].childrenIndices;
				auto it = std::find(kids.begin(), kids.end(), selected_object_index_);
				if (it != kids.end() && (it + 1) != kids.end()) {
					PushUndoState();
					std::swap(*it, *(it + 1));
					objects[par].modified = true;
					SaveAndReloadObjects();
				}
			}
		}
		return;
	}
	if (Check("TaskMoveHigher")) {
		if (selected_object_index_ >= 0) {
			auto& objects = level_.GetLevelObjects().GetObjects();
			int par = objects[selected_object_index_].parentIndex;
			if (par >= 0 && objects[par].parentIndex >= 0) {
				int gp = objects[par].parentIndex;
				PushUndoState();
				// Remove selected from parent's children
				auto& pKids = objects[par].childrenIndices;
				pKids.erase(std::find(pKids.begin(), pKids.end(), selected_object_index_));
				// Insert selected before parent in grandparent's children
				auto& gpKids = objects[gp].childrenIndices;
				auto gpIt = std::find(gpKids.begin(), gpKids.end(), par);
				gpKids.insert(gpIt, selected_object_index_);
				objects[selected_object_index_].parentIndex = gp;
				objects[par].modified = true;
				objects[gp].modified = true;
				objects[selected_object_index_].modified = true;
				SaveAndReloadObjects();
			}
		}
		return;
	}
	if (Check("TaskMoveLower")) {
		if (selected_object_index_ >= 0) {
			auto& objects = level_.GetLevelObjects().GetObjects();
			int par = objects[selected_object_index_].parentIndex;
			if (par >= 0) {
				auto& pKids = objects[par].childrenIndices;
				auto it = std::find(pKids.begin(), pKids.end(), selected_object_index_);
				if (it != pKids.end() && it != pKids.begin()) {
					int prevSib = *(it - 1);
					PushUndoState();
					pKids.erase(it);
					objects[prevSib].childrenIndices.push_back(selected_object_index_);
					objects[selected_object_index_].parentIndex = prevSib;
					objects[par].modified = true;
					objects[prevSib].modified = true;
					objects[selected_object_index_].modified = true;
					SaveAndReloadObjects();
				}
			}
		}
		return;
	}
	if (Check("TaskNew")) { CreateNewTask(); }
	if (Check("TaskNewFirstChild")) {
		task_picker_insert_first_ = true;
		CreateNewTask();
	}
	if (Check("TaskNewCameraRelative")) {
		task_new_at_camera_ = true;
		CreateNewTask();
	}
	if (Check("TaskCopy")) { CopySelectedTask(false); }
	if (Check("TaskCopyRecursive")) { CopySelectedTask(true); }
	if (Check("TaskPaste")) { PasteTask(); }
	if (Check("TaskSendEvent")) {
		status_message_ = "TaskSendEvent: requires live game connection";
		return;
	}
	if (Check("TaskSendEventRecursive")) {
		status_message_ = "TaskSendEventRecursive: requires live game connection";
		return;
	}
	if (Check("TaskFindTextInTask")) {
		find_mode_  = FindMode::TextInTask;
		find_open_  = true;
		find_query_.clear();
		find_result_idx_ = -1;
		return;
	}
	if (Check("TaskFindByTaskID")) {
		find_mode_  = FindMode::ById;
		find_open_  = true;
		find_query_.clear();
		find_result_idx_ = -1;
		return;
	}
	if (Check("TaskFindByTaskNote")) {
		find_mode_  = FindMode::ByNote;
		find_open_  = true;
		find_query_.clear();
		find_result_idx_ = -1;
		return;
	}
	if (Check("TaskFindAgain")) {
		if (!find_query_.empty()) {
			const auto& objects = level_.GetLevelObjects().GetObjects();
			std::string q = find_query_;
			std::transform(q.begin(), q.end(), q.begin(), [](unsigned char c){ return std::tolower(c); });
			int start = (find_result_idx_ >= 0 ? find_result_idx_ + 1 : 0);
			bool found = false;
			for (int i = 0; i < (int)objects.size(); ++i) {
				int idx = (start + i) % (int)objects.size();
				if (objects[idx].deleted || objects[idx].type == "Task_DeclareParameters") continue;
				std::string label;
				if (find_mode_ == FindMode::TextInTask) {
					for (auto& tok : objects[idx].argTokens) label += tok + " ";
				} else if (find_mode_ == FindMode::ById) {
					label = objects[idx].taskId;
				} else if (find_mode_ == FindMode::ByNote) {
					label = objects[idx].name;
				} else {
					label = objects[idx].type + " " + objects[idx].name + " " + objects[idx].taskId;
				}
				std::transform(label.begin(), label.end(), label.begin(), [](unsigned char c){ return std::tolower(c); });
				if (label.find(q) != std::string::npos) {
					find_result_idx_ = idx;
					found = true;
					// Expand ancestors and scroll to result
					{
						int anc = idx;
						while (anc >= 0 && anc < (int)objects.size()) {
							int pp = objects[anc].parentIndex;
							if (pp < 0 || pp >= (int)objects.size()) break;
							if (objects[pp].isContainer)
								const_cast<LevelObject&>(objects[pp]).expanded = true;
							anc = pp;
						}
					}
					selected_object_index_ = idx;
					// Scroll the tree to make the found item visible
					{
						auto visibleList = GetVisibleTreeNodes();
						int current_row = -1;
						for (int i = 0; i < (int)visibleList.size(); ++i) {
							if (visibleList[i] == idx) { current_row = i; break; }
						}
						if (current_row >= 0) {
							int row_h = 16;
							int start_y = 30;
							int max_rows = (window_state_.viewport_height_ - 50 - start_y) / row_h;
							if (max_rows > 0) {
								if (current_row < tree_scroll_offset_)
									tree_scroll_offset_ = current_row;
								else if (current_row >= tree_scroll_offset_ + max_rows)
									tree_scroll_offset_ = current_row - max_rows + 1;
							}
						}
					}
					break;
				}
			}
			if (!found) status_message_ = "No more matches";
		}
		return;
	}
	if (Check("TaskSetID")) {
		// Ctrl+I → input box for a Task ID. Empty + Enter = automatic unique assignment.
		if (selected_object_index_ < 0) { status_message_ = "Set Task ID: select a task first"; return; }
		find_mode_  = FindMode::SetId;
		find_open_  = true;
		find_query_.clear();
		find_result_idx_ = -1;
		return;
	}
	if (Check("TaskRebuildTree")) {
		SaveAndReloadObjects();
		status_message_ = "Tree rebuilt";
		return;
	}
	if (Check("TaskMakeTemplate")) {
		if (selected_object_index_ >= 0) {
			const auto& obj = level_.GetLevelObjects().GetObjects()[selected_object_index_];
			std::string dir = Utils::GetExeDirectory() + "\\content\\qed\\templates";
			std::filesystem::create_directories(dir);
			std::string path = dir + "\\" + obj.type + ".qsc";
			SaveTaskSubtreeToFile(selected_object_index_, path);
			status_message_ = "Template saved: " + path;
		}
		return;
	}
	if (Check("TaskSortChildren")) {
		if (selected_object_index_ >= 0) {
			PushUndoState();
			auto& objects = level_.GetLevelObjects().GetObjects();
			auto& obj = objects[selected_object_index_];
			std::sort(obj.childrenIndices.begin(), obj.childrenIndices.end(),
				[&](int a, int b){ return objects[a].type < objects[b].type; });
			obj.modified = true;
			SaveAndReloadObjects();
		}
		return;
	}
	if (Check("TaskDelete")) { DeleteSelectedTask(); }

	// ---- AnimTask ----
	if (Check("AnimTaskIncreaseKeyframeInterpolation")) { Logger::Get().Log(LogLevel::INFO, "[Keybind] AnimTaskIncreaseKeyframeInterpolation not implemented"); }
	if (Check("AnimTaskDecreaseKeyframeInterpolation")) { Logger::Get().Log(LogLevel::INFO, "[Keybind] AnimTaskDecreaseKeyframeInterpolation not implemented"); }
	if (Check("AnimTaskToggleCameraRelative")) { Logger::Get().Log(LogLevel::INFO, "[Keybind] AnimTaskToggleCameraRelative not implemented"); }
	if (Check("AnimTaskGoToCursor")) { Logger::Get().Log(LogLevel::INFO, "[Keybind] AnimTaskGoToCursor not implemented"); }
	if (Check("AnimTaskToggleWindowHidden")) { Logger::Get().Log(LogLevel::INFO, "[Keybind] AnimTaskToggleWindowHidden not implemented"); }
	if (Check("AnimTaskStartRecording")) { Logger::Get().Log(LogLevel::INFO, "[Keybind] AnimTaskStartRecording not implemented"); }
	if (Check("AnimTaskGoToTop")) { Logger::Get().Log(LogLevel::INFO, "[Keybind] AnimTaskGoToTop not implemented"); }
	if (Check("AnimTaskToggleSyncPlayback")) { Logger::Get().Log(LogLevel::INFO, "[Keybind] AnimTaskToggleSyncPlayback not implemented"); }

	// ---- Timer ----
	if (Check("TimerIncreaseSpeed")) { Logger::Get().Log(LogLevel::INFO, "[Keybind] TimerIncreaseSpeed not implemented"); }
	if (Check("TimerDecreaseSpeed")) { Logger::Get().Log(LogLevel::INFO, "[Keybind] TimerDecreaseSpeed not implemented"); }
	if (Check("TimerReset")) { Logger::Get().Log(LogLevel::INFO, "[Keybind] TimerReset not implemented"); }
	if (Check("TimerStartStop")) { Logger::Get().Log(LogLevel::INFO, "[Keybind] TimerStartStop not implemented"); }

	// ---- Manipulate ----
	if (Check("Manipulate")) { Logger::Get().Log(LogLevel::INFO, "[Keybind] Manipulate not implemented"); }
	if (Check("ManipulatePositionXY")) { Logger::Get().Log(LogLevel::INFO, "[Keybind] ManipulatePositionXY not implemented"); }
	if (Check("ManipulatePositionXZ")) { Logger::Get().Log(LogLevel::INFO, "[Keybind] ManipulatePositionXZ not implemented"); }
	if (Check("ManipulatePositionSnapToGround")) {
		if (selected_object_index_ >= 0) {
			auto& objects = level_.GetLevelObjects().GetObjects();
			if (selected_object_index_ < (int)objects.size()) {
				auto& obj = objects[selected_object_index_];
				if (!Utils::IsUndergroundModel(obj.name, obj.modelId)) {
					float terrainZ = 0.0f;
					if (level_.GetTerrainZ(obj.pos.x, obj.pos.y, terrainZ)) {
						float zOffset = renderer_.GetMeshZOffset(obj.modelId, obj.isBuilding);
						obj.pos.z = (double)terrainZ + (double)(zOffset * 40.96f * obj.scale);
						obj.modified = true;
						level_.GetLevelObjects().UpdateCoordinatesInLine(obj);
						Logger::Get().Log(LogLevel::INFO, "[App] ManipulatePositionSnapToGround: Snapped object to ground");
					}
				}
			}
		}
	}
	if (Check("ManipulatePositionSnapToObject")) {
		if (selected_object_index_ >= 0) {
			auto& objects = level_.GetLevelObjects().GetObjects();
			if (selected_object_index_ < (int)objects.size()) {
				PushUndoState();
				input_.keys_ |= MK_MANIP_O;
				if (selected_object_index_ >= 0) UpdateMarkerManipulation();
				input_.keys_ &= ~MK_MANIP_O;
			}
		}
	}
	if (Check("ManipulateOrientationAlpha")) { Logger::Get().Log(LogLevel::INFO, "[Keybind] ManipulateOrientationAlpha not implemented"); }
	if (Check("ManipulateOrientationBeta")) { Logger::Get().Log(LogLevel::INFO, "[Keybind] ManipulateOrientationBeta not implemented"); }
	if (Check("ManipulateOrientationGamma")) { Logger::Get().Log(LogLevel::INFO, "[Keybind] ManipulateOrientationGamma not implemented"); }
	if (Check("ManipulateOrientationReset")) { Logger::Get().Log(LogLevel::INFO, "[Keybind] ManipulateOrientationReset not implemented"); }

	// ---- GraphNode ----
	if (Check("ScaleGraphNode")) { Logger::Get().Log(LogLevel::INFO, "[Keybind] ScaleGraphNode not implemented"); }
	if (Check("ScaleGraphNodeHalfe")) { Logger::Get().Log(LogLevel::INFO, "[Keybind] ScaleGraphNodeHalfe not implemented"); }
	if (Check("ScaleGraphNodeDouble")) { Logger::Get().Log(LogLevel::INFO, "[Keybind] ScaleGraphNodeDouble not implemented"); }
	if (Check("CreateGraphNode")) { Logger::Get().Log(LogLevel::INFO, "[Keybind] CreateGraphNode not implemented"); }
	if (Check("DeleteGraphNode")) { Logger::Get().Log(LogLevel::INFO, "[Keybind] DeleteGraphNode not implemented"); }

	// ---- Other ----
	if (Check("ToggleDisplay")) { noclip_mode_ = !noclip_mode_; }
	if (Check("ToggleMouseInverted")) { Config::Get().invertMouse = !Config::Get().invertMouse; }
	if (Check("ToggleDebugText")) { show_debug_ = !show_debug_; }
	if (Check("ToggleTaskTypeView")) { show_task_type_ = !show_task_type_; }
	if (Check("ToggleGame")) { LaunchGame(); }
	if (Check("ToggleTaskNoteDisplay")) { Config::Get().displayTaskNote = !Config::Get().displayTaskNote; }
	if (Check("ToggleQEDRunEvent")) { Config::Get().runEvent = !Config::Get().runEvent; }
	if (Check("ToggleSaveStateOnExit")) {
		Config::Get().saveConfigOnExit = !Config::Get().saveConfigOnExit;
		status_message_ = Config::Get().saveConfigOnExit ? "Save on exit: ON" : "Save on exit: OFF";
	}
	if (Check("SaveState")) {
		SaveCurrentLevel();
		int lvl = level_.GetLevelNo();
		status_message_ = "Save level: LOCAL:missions/location" + std::to_string(lvl) +
		                  "/level" + std::to_string(lvl) + "/objects.qsc";
		return;
	}
	if (Check("SaveObjectFile")) {
		// Ctrl+S → open a path textbox and write the live objects QSC to that path.
		// (Whole-level save/compile lives in the pause menu.)
		int lvl = level_.GetLevelNo();
		file_dialog_mode_  = FileDialogMode::SaveObjectFile;
		file_dialog_path_  = "missions/location" + std::to_string(lvl) +
		                     "/level" + std::to_string(lvl) + "/objects.qsc";
		file_dialog_caret_ = (int)file_dialog_path_.size();
		return;
	}
	if (Check("SaveSubTaskObjectFile")) {
		if (selected_object_index_ >= 0) {
			file_dialog_mode_ = FileDialogMode::SaveSubTask;
			file_dialog_path_ = Config::Get().taskFileName.empty() ?
			    "content\\qed\\temp\\task.qsc" : Config::Get().taskFileName;
			file_dialog_caret_ = (int)file_dialog_path_.size();
		} else {
			status_message_ = "SaveSubTask: no task selected";
		}
		return;
	}
	if (Check("SaveSubTaskObjectFileParent")) {
		if (selected_object_index_ >= 0) {
			auto& objs = level_.GetLevelObjects().GetObjects();
			int par = objs[selected_object_index_].parentIndex;
			if (par >= 0) {
				file_dialog_mode_ = FileDialogMode::SaveSubTaskParent;
				file_dialog_path_ = Config::Get().taskFileName.empty() ?
				    "content\\qed\\temp\\task.qsc" : Config::Get().taskFileName;
				file_dialog_caret_ = (int)file_dialog_path_.size();
			} else {
				status_message_ = "SaveSubTaskParent: selected task has no parent";
			}
		} else {
			status_message_ = "SaveSubTaskParent: no task selected";
		}
		return;
	}
	if (Check("LoadSubTaskObjectFile")) {
		file_dialog_mode_ = FileDialogMode::LoadSubTask;
		file_dialog_path_ = Config::Get().taskFileName.empty() ?
		    "content\\qed\\temp\\task.qsc" : Config::Get().taskFileName;
		file_dialog_caret_ = (int)file_dialog_path_.size();
		return;
	}
	if (Check("SnapToGroundRecursive")) {
		if (selected_object_index_ >= 0) {
			PushUndoState();
			auto& objects = level_.GetLevelObjects().GetObjects();
			std::function<void(int)> snapRec = [&](int idx) {
				auto& obj = objects[idx];
				if (!Utils::IsUndergroundModel(obj.name, obj.modelId)) {
					float tz = 0.f;
					if (level_.GetTerrainZ(obj.pos.x, obj.pos.y, tz)) {
						float zOff = renderer_.GetMeshZOffset(obj.modelId, obj.isBuilding);
						obj.pos.z = (double)tz + (double)(zOff * 40.96f * obj.scale);
						obj.modified = true;
						level_.GetLevelObjects().UpdateCoordinatesInLine(obj);
					}
				}
				for (int ci : obj.childrenIndices)
					if (ci >= 0 && ci < (int)objects.size() && !objects[ci].deleted) snapRec(ci);
			};
			snapRec(selected_object_index_);
			SaveAndReloadObjects();
		}
		return;
	}
	if (Check("ToggleConsole")) { show_debug_ = !show_debug_; }
	if (Check("ConsoleIncreaseAutoActivateLevel")) {
		static const char* kLevels[] = {"DEBUG","INFO","WARNING","ERR","FATAL"};
		auto& lvl = Config::Get().consoleAutoActivate;
		lvl = std::min(4, lvl + 1);
		status_message_ = std::string("Console level: ") + kLevels[lvl];
		return;
	}
	if (Check("ConsoleDecreaseAutoActivateLevel")) {
		static const char* kLevels[] = {"DEBUG","INFO","WARNING","ERR","FATAL"};
		auto& lvl = Config::Get().consoleAutoActivate;
		lvl = std::max(0, lvl - 1);
		status_message_ = std::string("Console level: ") + kLevels[lvl];
		return;
	}
	if (Check("AutoComplete")) {
		if (prop_text_edit_field_ >= 0 && !autocomplete_keywords_.empty()) {
			int ws = prop_text_caret_;
			while (ws > 0 && (isalnum((unsigned char)prop_text_buf_[ws-1]) || prop_text_buf_[ws-1] == '_')) ws--;
			std::string prefix = prop_text_buf_.substr(ws, prop_text_caret_ - ws);
			std::string pl = prefix;
			std::transform(pl.begin(), pl.end(), pl.begin(), [](unsigned char c){ return std::tolower(c); });
			for (auto& kw : autocomplete_keywords_) {
				std::string kwl = kw;
				std::transform(kwl.begin(), kwl.end(), kwl.begin(), [](unsigned char c){ return std::tolower(c); });
				if (!pl.empty() && kwl.substr(0, pl.size()) == pl) {
					prop_text_buf_.replace(ws, prefix.size(), kw);
					prop_text_caret_ = ws + (int)kw.size();
					break;
				}
			}
		}
		return;
	}
	if (Check("AutoCompleteTaskName")) {
		if (prop_text_edit_field_ == -1) {
			status_message_ = "Click a text box first, then Ctrl+N to pick a task type";
			return;
		}
		picker_target_field_  = prop_text_edit_field_;
		picker_target_obj_    = prop_edit_obj_index_;
		picker_target_caret_  = prop_text_caret_;
		ac_task_items_ = autocomplete_keywords_;
		ac_task_picker_open_   = true;
		ac_task_selected_idx_  = 0;
		ac_task_scroll_offset_ = 0;
		ac_task_filter_.clear();
		return;
	}
	if (Check("AutoCompleteModelName")) {
		if (prop_text_edit_field_ == -1) {
			status_message_ = "Click a text box first, then Ctrl+O to pick a model";
			return;
		}
		picker_target_field_  = prop_text_edit_field_;
		picker_target_obj_    = prop_edit_obj_index_;
		picker_target_caret_  = prop_text_caret_;
		model_picker_open_    = true;
		model_picker_selected_ = 0;
		model_picker_scroll_   = 0;
		model_picker_filter_.clear();
		return;
	}
	if (Check("Undo")) { Undo(); }
	if (Check("Redo")) { Redo(); }
	if (Check("ReloadSettings")) { Config::Init(); LoadAutoCompleteKeywords(); Logger::Get().Log(LogLevel::INFO, "[App] Settings reloaded from QED config"); }
	if (Check("ToggleObjects")) { Logger::Get().Log(LogLevel::INFO, "[Keybind] ToggleObjects not implemented"); }
	if (Check("TaskMagicObjToggle")) {
		show_magic_obj_spheres_ = !show_magic_obj_spheres_;
		status_message_ = show_magic_obj_spheres_ ? "Magic objects: ON" : "Magic objects: OFF";
	}
	if (Check("AddModelToRes")) {
		int oi = selected_object_index_;
		auto& objs = level_.GetLevelObjects().GetObjects();
		if (oi >= 0 && oi < (int)objs.size() && objs[oi].modelMissingInRes) {
			if (renderer_.AddModelToLevelRes(objs[oi].modelId)) {
				level_res_models_.AddEntry("models\\" + objs[oi].modelId + ".mef");
				objs[oi].modelMissingInRes = false;
				status_message_ = "Added '" + objs[oi].modelId + "' + textures to level .res (backups .orig written).";
			} else {
				status_message_ = "Failed to add '" + objs[oi].modelId + "' to level .res (see log).";
			}
		} else {
			status_message_ = "Select an object whose model is flagged missing from the .res first.";
		}
	}
	if (Check("CameraStrafeFree")) {
		camera_strafe_free_ = !camera_strafe_free_;
		status_message_ = camera_strafe_free_ ? "Strafe lock: ON" : "Strafe lock: OFF";
	}
}


#ifdef _WIN32
#include <windows.h>
#include <tlhelp32.h>
#endif

void App::ResetLevel() {
	int levelNo = level_.GetLevelNo();

	Logger::Get().Log(LogLevel::INFO, "[App] Resetting Level " + std::to_string(levelNo));

	// Force kill any running game instance to release file locks on objects.qvm
#ifdef _WIN32
	{
		HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
		if (hSnap != INVALID_HANDLE_VALUE) {
			PROCESSENTRY32 pe;
			pe.dwSize = sizeof(pe);
			if (Process32First(hSnap, &pe)) {
				do {
					if (_wcsicmp(pe.szExeFile, L"igi.exe") == 0) {
						HANDLE hProc = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
						if (hProc) {
							TerminateProcess(hProc, 0);
							CloseHandle(hProc);
							Logger::Get().Log(LogLevel::INFO, "[App] Terminated running game instance 'igi.exe' to unlock files.");
						}
					}
				} while (Process32Next(hSnap, &pe));
			}
			CloseHandle(hSnap);
		}
	}
#endif

	if (Config::Get().enableBackup) {
		std::string gameLevelDir = Utils::GetIGIRootPath() + "\\missions\\location0\\level" + std::to_string(levelNo);
		std::string backupLevelDir = Utils::GetExeDirectory() + "\\content\\backup\\level" + std::to_string(levelNo);
		
		Logger::Get().Log(LogLevel::INFO, "[App] Restoring level from backup: " + backupLevelDir + " to " + gameLevelDir);
		
		if (std::filesystem::exists(backupLevelDir)) {
			try {
				std::filesystem::copy(backupLevelDir, gameLevelDir, std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing);
				Logger::Get().Log(LogLevel::INFO, "[App] Level reset successfully from backup.");
			} catch (const std::exception& e) {
				Logger::Get().Log(LogLevel::ERR, "[App] Failed to restore from backup: " + std::string(e.what()));
			}
		} else {
			Logger::Get().Log(LogLevel::ERR, "[App] Cannot reset level: No backup found at " + backupLevelDir);
		}
	} else {
		Logger::Get().Log(LogLevel::INFO, "[App] Reset level skipped because QEDBackup is not enabled in config.");
	}

	// Remove local objects.qsc so it recompiles fresh from QVM
	std::string exeDir = Utils::GetExeDirectory();
	std::string dstQsc = exeDir + "\\content\\qed\\temp\\objects.qsc";
	try {
		if (std::filesystem::exists(dstQsc)) {
			std::filesystem::remove(dstQsc);
		}
	}
	catch (...) {}

	// Reload level after reset
	LoadLevel(levelNo);
	// Snap objects to terrain after level reset
	SnapObjectsToTerrain();
}

void App::ResetScript() {
	int levelNo = level_.GetLevelNo();

	Logger::Get().Log(LogLevel::INFO, "[App] Resetting Script for Level " + std::to_string(levelNo) + " - restore objects.qvm from content/tools/restore to IGIPath");

	std::string toolsDir = Utils::GetExeDirectory() + "\\content\\tools";

	// Copy objects.qvm from content/tools/restore to IGIPath
	char srcQvm[1024];
	Str_SPrintf(srcQvm, 1024, "%s\\restore\\missions\\location0\\level%d\\objects.qvm", toolsDir.c_str(), levelNo);

	char dstQvm[1024];
	Str_SPrintf(dstQvm, 1024, "%s\\missions\\location0\\level%d\\objects.qvm", Utils::GetIGIRootPath().c_str(), levelNo);

	Logger::Get().Log(LogLevel::INFO, "[App] Copying objects.qvm from " + std::string(srcQvm) + " to " + std::string(dstQvm));

	try {
		if (std::filesystem::exists(srcQvm)) {
			std::filesystem::create_directories(std::filesystem::path(dstQvm).parent_path());
			// Force permissions to allow overwrite/delete
			if (std::filesystem::exists(dstQvm)) {
				std::filesystem::permissions(dstQvm, 
					std::filesystem::perms::owner_all | std::filesystem::perms::group_all | std::filesystem::perms::others_all,
					std::filesystem::perm_options::replace);
				std::filesystem::remove(dstQvm);
			}
			std::filesystem::copy_file(srcQvm, dstQvm, std::filesystem::copy_options::overwrite_existing);
			Logger::Get().Log(LogLevel::INFO, "[App] QVM copied successfully to game path.");
		}
		else {
			Logger::Get().Log(LogLevel::ERR, "[App] Error: Source QVM not found at " + std::string(srcQvm));
		}
	}
	catch (const std::exception& e) {
		Logger::Get().Log(LogLevel::ERR, "[App] ResetScript error: " + std::string(e.what()));
	}

	// Remove local objects.qsc so it recompiles fresh from QVM
	std::string exeDir = Utils::GetExeDirectory();
	std::string dstQsc = exeDir + "\\content\\qed\\temp\\objects.qsc";
	try {
		if (std::filesystem::exists(dstQsc)) {
			std::filesystem::remove(dstQsc);
		}
	}
	catch (...) {}

	// Reload the level to apply changes
	LoadLevel(levelNo);
}



void App::Input_OnKeyboardUp(unsigned char key, int x, int y) {
	auto& config = Config::Get();

	// Check for modifier keys - if pressed, skip movement key checks
	int modifiers = glutGetModifiers();
	bool has_modifiers = (modifiers & (GLUT_ACTIVE_CTRL | GLUT_ACTIVE_SHIFT | GLUT_ACTIVE_ALT));

	// Check movement keys (regular keyboard characters) - case insensitive
	// Only check if the config key is a regular character (ASCII < 128 and not a special key code)
	// Special key codes (arrow keys) are >= 100 and should not match regular character keys
	if (!has_modifiers) {
		// Only check movement keys if they are regular characters, not special keys
		// Special keys have codes >= 100 (GLUT_KEY_UP=101, DOWN=103, LEFT=100, RIGHT=102)
		if (config.keyMoveForward < 100 && config.keyMoveForward > 0 && toupper(key) == toupper(config.keyMoveForward)) {
			input_.keys_ &= ~MK_FORWARD;
			return;
		}
		if (config.keyMoveBackward < 100 && config.keyMoveBackward > 0 && toupper(key) == toupper(config.keyMoveBackward)) {
			input_.keys_ &= ~MK_BACKWARD;
			return;
		}
		if (config.keyMoveLeft < 100 && config.keyMoveLeft > 0 && toupper(key) == toupper(config.keyMoveLeft)) {
			input_.keys_ &= ~MK_LEFT;
			return;
		}
		if (config.keyMoveRight < 100 && config.keyMoveRight > 0 && toupper(key) == toupper(config.keyMoveRight)) {
			input_.keys_ &= ~MK_RIGHT;
			return;
		}
	}

	for (int i = 0; i < count_of(MOVEMENT_KEYS); ++i) {
		const movement_key_s& mk = MOVEMENT_KEYS[i];
		if (key == mk.lower_case_ || key == mk.upper_case_) {
			input_.keys_ &= ~mk.key_flag_;
			// Don't return, check manip keys too
		}
	}
	// Object Manipulation from QED config
	if (toupper(key) == toupper(config.keyRotateAlpha)) { input_.keys_ &= ~MK_MANIP_A; undo_state_pushed_for_manip_ = false; }
	if (toupper(key) == toupper(config.keyRotateBeta))  { input_.keys_ &= ~MK_MANIP_B; undo_state_pushed_for_manip_ = false; }
	if (toupper(key) == toupper(config.keyRotateGamma)) { input_.keys_ &= ~MK_MANIP_G; undo_state_pushed_for_manip_ = false; }
	if (toupper(key) == toupper(config.keySnapGround))  { input_.keys_ &= ~MK_MANIP_S; }
	if (toupper(key) == toupper(config.keySnapObject))  { input_.keys_ &= ~MK_MANIP_O; }
}

// idle
void App::OnIdle() {
	// freeglut pumps messages with a window-handle filter and misses WM_HOTKEY,
	// which is a thread message only retrievable via PeekMessage(NULL, ...).
	// Poll it here so F3 works while the game is running and editor is iconified.
	if (game_process_.running) {
		MSG msg = {};
		while (PeekMessage(&msg, NULL, WM_HOTKEY, WM_HOTKEY, PM_REMOVE)) {
			if (static_cast<int>(msg.wParam) == HOTKEY_ID_TOGGLE_GAME) {
				Logger::Get().Log(LogLevel::INFO, "[ToggleGame] Global hotkey received — stopping game");
				LaunchGame();
			}
		}
	}

	// Check game exit before the frame-rate throttle so it fires on every call,
	// even when GLUT is running slowly while the editor is iconified.
	if (game_process_.running && game_process_.hProcess) {
		bool exited = game_exited_.load(std::memory_order_acquire);
		if (!exited) {
			// Direct non-blocking poll as fallback in case monitor thread signal was missed
			DWORD waitResult = WaitForSingleObject(game_process_.hProcess, 0);
			exited = (waitResult == WAIT_OBJECT_0);
		}
		if (exited) {
			Logger::Get().Log(LogLevel::INFO, "[App] Game process exited (PID=" +
			                  std::to_string(game_process_.pid) + "), restoring editor");
			CloseHandle(game_process_.hProcess);
			CloseHandle(game_process_.hThread);
			if (game_process_.hMonitorThread) {
				WaitForSingleObject(game_process_.hMonitorThread, 1000);
				CloseHandle(game_process_.hMonitorThread);
			}
			game_exited_.store(false, std::memory_order_relaxed);
			game_process_ = {};
			prior_frame_time_ = Sys_Milliseconds();
			glutShowWindow();
			glutPostRedisplay();
			if (editor_hwnd_) {
				ShowWindow(editor_hwnd_, SW_RESTORE);
				SetForegroundWindow(editor_hwnd_);
				BringWindowToTop(editor_hwnd_);
			}
			if (editor_hwnd_) {
				KillTimer(editor_hwnd_, 1);
				UnregisterHotKey(editor_hwnd_, HOTKEY_ID_TOGGLE_GAME);
			}
			Logger::Get().Log(LogLevel::INFO, "[ToggleGame] Global hotkey unregistered — editor restored");
			return;
		}
	}

	// While the game is running the editor window is iconified.
	// glutSwapBuffers() deadlocks on minimized windows, so skip rendering entirely.
	if (game_process_.running) return;

	int64_t cur_time = Sys_Milliseconds();
	int64_t delta_time = cur_time - prior_frame_time_;
	if (delta_time < 16) {
		return;
	}

	Frame(delta_time * 0.001f);	// convert to seconds

	prior_frame_time_ = cur_time;
}

void App::Frame(float delta_seconds) {
	if (pause_mode_) {
		// Skip all updates when paused, just render
		UpdateViewDefine();
		if (mouse_state_.prior_x_ != last_pick_x_ || mouse_state_.prior_y_ != last_pick_y_) {
			hover_object_index_ = PickObjectAtScreenPos(mouse_state_.prior_x_, mouse_state_.prior_y_);
			if (hover_object_index_ >= Renderer::kAttaPickBase) hover_object_index_ = -1; // ATTA hovered (clickable; promote on click)
			last_pick_x_ = mouse_state_.prior_x_;
			last_pick_y_ = mouse_state_.prior_y_;
		}
		float ground_z = 0.0f;
		level_.GetTerrainZ(viewer_.pos_.x, viewer_.pos_.y, ground_z);
		Renderer::task_tree_view_params_s task_tree_view = {
			.show_hud_ = true,
			.status_msg_ = status_message_,
			.pause_mode_ = true,
			.show_debug_ = show_debug_,
			.show_help_ = show_help_,
			.edit_mode_ = edit_mode_,
			.terrain_edit_enabled_ = terrain_edit_enabled_,
			.selected_object_index_ = selected_object_index_,
			.hover_object_index_ = hover_object_index_,
			.hover_tree_index_ = hover_tree_index_,
			.mouse_x_ = mouse_state_.prior_x_,
			.mouse_y_ = mouse_state_.prior_y_,
			.tree_scroll_offset = tree_scroll_offset_,
			.tree_decl_expanded = tree_decl_expanded_,
			.level_objects_ = &level_.GetLevelObjects(),
			.task_picker_open_ = task_picker_open_,
			.task_picker_selected_idx_ = task_picker_selected_idx_,
			.task_picker_scroll_offset_ = task_picker_scroll_offset_,
			.task_picker_search_ = task_picker_search_,
			.enable_camera_mode_ = Utils::IsKeyBindingPressed(Config::Get().keyEnableCamera),
			.prop_editor_open_     = prop_editor_open_,
			.prop_field_index_     = prop_field_index_,
			.prop_text_edit_field_ = prop_text_edit_field_,
			.prop_edit_obj_index_  = prop_edit_obj_index_,
			.prop_drag_obj_index_  = prop_drag_obj_index_,
			.prop_text_buf_        = prop_text_buf_,
			.prop_text_caret_      = prop_text_caret_,
			.prop_panel_scroll_    = prop_panel_scroll_,
			.find_open_            = find_open_,
			.find_query_           = find_query_,
			.find_result_idx_      = find_result_idx_,
			.selected_obj_is_ai    = (selected_object_index_ >= 0 &&
				selected_object_index_ < (int)level_.GetLevelObjects().GetObjects().size() &&
				ai_model_ids_.count(level_.GetLevelObjects().GetObjects()[selected_object_index_].modelId) > 0),
			.help_scroll_offset_   = help_scroll_offset_,
			.help_entries_         = &help_entries_,
			.show_task_type_       = show_task_type_,
			.find_mode_            = (int)find_mode_,
			.file_dialog_mode_     = (int)file_dialog_mode_,
			.file_dialog_path_     = file_dialog_path_,
			.file_dialog_caret_    = file_dialog_caret_,
			.ac_task_picker_open_  = ac_task_picker_open_,
			.ac_task_selected_idx_ = ac_task_selected_idx_,
			.ac_task_scroll_offset_= ac_task_scroll_offset_,
			.ac_task_filter_       = ac_task_filter_,
			.ac_task_items_        = &ac_task_items_,
			.model_picker_open_    = model_picker_open_,
			.model_picker_selected_= model_picker_selected_,
			.model_picker_scroll_  = model_picker_scroll_,
			.model_picker_filter_  = model_picker_filter_,
			.model_ids_            = &level_model_ids_,
			.ai_script_path_       = ai_script_path_,
			.ai_script_text_       = ai_script_text_,
			.ai_script_dirty_      = ai_script_dirty_,
		};
		draw_params_.level_objects_ = &level_.GetLevelObjects();
		draw_params_.selected_object_index_ = selected_object_index_;
		draw_params_.show_magic_obj_spheres_ = show_magic_obj_spheres_;
		draw_params_.terrain_id_at_world_xy_ =
			[this](double x, double y) { return level_.GetTerrainNodeId(x, y); };
		renderer_.Draw(draw_params_, task_tree_view);

		DrawCustomCursor();
		glutSwapBuffers();
		return;
	}

	frame_++;
	frame_ %= 0xFFFFFFFF;	// reserve value 0xFFFFFFFF (-1) for INVALID_FRAME

	ProcessInput(delta_seconds);

	// Per-frame position-drag velocity: the pad / Z slider accelerate while held in
	// a direction and keep moving when the cursor is pinned at the window edge.
	if (mouse_state_.left_button_down_ && prop_field_index_ >= 0 && selected_object_index_ >= 0 &&
	    !Utils::IsKeyBindingPressed(Config::Get().keyEnableCamera)) {
		ApplyPropPositionDrag();
	}

	if (edit_mode_ && terrain_edit_enabled_ && mouse_state_.left_button_down_) {
		EditorProcessClick();
	}

	UpdateViewDefine();
	if (mouse_state_.prior_x_ != last_pick_x_ || mouse_state_.prior_y_ != last_pick_y_) {
		bool camMode    = Utils::IsKeyBindingPressed(Config::Get().keyEnableCamera);
		bool overPanel  = prop_editor_open_ &&
		                  mouse_state_.prior_x_ < (PropPanel::kLeft + PropPanel::kWidth);
		if (camMode || overPanel) {
			hover_object_index_ = -1;
		} else {
			hover_object_index_ = PickObjectAtScreenPos(mouse_state_.prior_x_, mouse_state_.prior_y_);
			if (hover_object_index_ >= Renderer::kAttaPickBase) hover_object_index_ = -1; // ATTA hovered (clickable; promote on click)
		}
		last_pick_x_ = mouse_state_.prior_x_;
		last_pick_y_ = mouse_state_.prior_y_;
	}

	vert_flat_sky_layer_s * fsl_vb = renderer_.MapFlatSkyLayersVB();
	vert_pos_a_uv_s* terrain_vb = renderer_.MapTerrainVB();
	uint32_t* terrain_ib = renderer_.MapTerrainIB();
	render_chunk_s* render_chunks = renderer_.GetTerrainRenderChunckBuffer();

	update_params_s update_params = {
		.frame_ = frame_,
		.delta_seconds_ = delta_seconds,
		.view_define_ = &view_define_,
		.flat_sky_layer_vb_ = fsl_vb,
		.terrain_mod_options_ = terrain_mod_options_,
		.terrain_vb_ = terrain_vb,
		.terrain_ib_ = terrain_ib,
		.terrain_render_chunks_ = render_chunks
	};

	level_.Update(update_params);

	if (terrain_ib) {
		renderer_.UnmapTerrainIB();
	}

	if (terrain_vb) {
		renderer_.UnmapTerrainVB();
	}

	if (fsl_vb) {
		renderer_.UnmapFlatSkyLayersVB();
	}

	draw_params_.flat_sky_layer_is_visible_ = update_params.flat_sky_layer_is_visible_;
	draw_params_.num_terrain_render_chunk_ = update_params.num_terrain_render_chunk_;
	draw_params_.level_objects_ = &level_.GetLevelObjects();
	draw_params_.selected_object_index_ = selected_object_index_;
	draw_params_.show_magic_obj_spheres_ = show_magic_obj_spheres_;
	draw_params_.terrain_id_at_world_xy_ =
		[this](double x, double y) { return level_.GetTerrainNodeId(x, y); };


	float ground_z = 0.0f;
	bridge_.SetEnabled(show_hud_);
	IGIBridge::PositionData data = bridge_.GetLatestData();
	level_.GetTerrainZ(viewer_.pos_.x, viewer_.pos_.y, ground_z);

	Renderer::task_tree_view_params_s task_tree_view = {
		.show_hud_ = show_hud_,
		.status_msg_ = status_message_,
		.pause_mode_ = pause_mode_,
		.show_debug_ = show_debug_,
		.show_help_ = show_help_,
		.edit_mode_ = edit_mode_,
		.terrain_edit_enabled_ = terrain_edit_enabled_,
		.selected_object_index_ = selected_object_index_,
		.hover_object_index_ = hover_object_index_,
		.hover_tree_index_ = hover_tree_index_,
		.mouse_x_ = mouse_state_.prior_x_,
		.mouse_y_ = mouse_state_.prior_y_,
		.tree_scroll_offset = tree_scroll_offset_,
		.tree_decl_expanded = tree_decl_expanded_,
		.level_objects_ = &level_.GetLevelObjects(),
		.task_picker_open_ = task_picker_open_,
		.task_picker_selected_idx_ = task_picker_selected_idx_,
		.task_picker_scroll_offset_ = task_picker_scroll_offset_,
		.task_picker_search_ = task_picker_search_,
		.enable_camera_mode_ = Utils::IsKeyBindingPressed(Config::Get().keyEnableCamera),
		.prop_editor_open_     = prop_editor_open_,
		.prop_field_index_     = prop_field_index_,
		.prop_text_edit_field_ = prop_text_edit_field_,
		.prop_edit_obj_index_  = prop_edit_obj_index_,
		.prop_drag_obj_index_  = prop_drag_obj_index_,
		.prop_text_buf_        = prop_text_buf_,
		.prop_text_caret_      = prop_text_caret_,
		.prop_panel_scroll_    = prop_panel_scroll_,
		.find_open_            = find_open_,
		.find_query_           = find_query_,
		.find_result_idx_      = find_result_idx_,
		.selected_obj_is_ai    = (selected_object_index_ >= 0 &&
			selected_object_index_ < (int)level_.GetLevelObjects().GetObjects().size() &&
			ai_model_ids_.count(level_.GetLevelObjects().GetObjects()[selected_object_index_].modelId) > 0),
		.help_scroll_offset_   = help_scroll_offset_,
		.help_entries_         = &help_entries_,
		.show_task_type_       = show_task_type_,
		.find_mode_            = (int)find_mode_,
		.file_dialog_mode_     = (int)file_dialog_mode_,
		.file_dialog_path_     = file_dialog_path_,
		.file_dialog_caret_    = file_dialog_caret_,
		.ac_task_picker_open_  = ac_task_picker_open_,
		.ac_task_selected_idx_ = ac_task_selected_idx_,
		.ac_task_scroll_offset_= ac_task_scroll_offset_,
		.ac_task_filter_       = ac_task_filter_,
		.ac_task_items_        = &ac_task_items_,
		.model_picker_open_    = model_picker_open_,
		.model_picker_selected_= model_picker_selected_,
		.model_picker_scroll_  = model_picker_scroll_,
		.model_picker_filter_  = model_picker_filter_,
		.model_ids_            = &level_model_ids_,
		.ai_script_path_        = ai_script_path_,
		.ai_script_text_        = ai_script_text_,
		.ai_script_dirty_       = ai_script_dirty_,
		.ai_script_vscroll_     = ai_script_vscroll_,
		.ai_script_path_hscroll_= ai_script_path_hscroll_,
	};

	renderer_.Draw(draw_params_, task_tree_view);

	DrawCustomCursor();
	glutSwapBuffers();
}

void App::ProcessInput(float delta_seconds) {
	// Safety check: ensure level is loaded before processing movement
	if (level_.GetLevelNo() == 0) {
		// Level not loaded, skip input processing
		input_.mouse_delta_x_ = 0;
		input_.mouse_delta_y_ = 0;
		return;
	}
	
	bool enableCameraMode = Utils::IsKeyBindingPressed(Config::Get().keyEnableCamera);
	
	// Update cursor — always NONE so SPR sprite cursor is the only visible cursor
	glutSetCursor(GLUT_CURSOR_NONE);

	if (!edit_mode_ || enableCameraMode) {
		if (orbit_active_) {
			// Horizontal orbit (yaw only) around selected object
			viewer_.yaw_ += -input_.mouse_delta_x_ * MOUSE_SENSITIVE;

			glm::vec3 new_forward;
			glm::vec3 dummy_right, dummy_up;
			AngleToVectors(viewer_.yaw_, viewer_.pitch_, viewer_.roll_, new_forward, dummy_right, dummy_up);

			// Recalculate position based on distance and new forward vector
			viewer_.pos_ = orbit_target_pos_ - new_forward * orbit_distance_;
		} else {
			// Standard free-look camera movement in-place
			viewer_.yaw_ += -input_.mouse_delta_x_ * MOUSE_SENSITIVE;
			viewer_.pitch_ += -input_.mouse_delta_y_ * MOUSE_SENSITIVE;
		}
	}

	input_.mouse_delta_x_ = 0;
	input_.mouse_delta_y_ = 0;

	UpdateViewerVectors();

	// Gradually increase/decrease camera movement speed based on active movement
	{
		bool is_fwd = (input_.keys_ & MK_FORWARD) || Utils::IsKeyBindingPressed(Config::Get().keyMoveCameraForward);
		bool is_bwd = (input_.keys_ & MK_BACKWARD) || Utils::IsKeyBindingPressed(Config::Get().keyMoveCameraBackward);
		bool is_left = (input_.keys_ & MK_LEFT);
		bool is_right = (input_.keys_ & MK_RIGHT);
		bool is_up = (input_.keys_ & MK_STRAIGHT_UP);
		bool is_down = (input_.keys_ & MK_STRAIGHT_DOWN);

		bool is_moving = is_fwd || is_bwd || is_left || is_right || is_up || is_down;

		if (is_moving) {
			viewer_.move_speed_ += (viewer_.move_speed_ + 8.0f * WORLD_UNITS_PER_METER) * 0.8f * delta_seconds;
			if (viewer_.move_speed_ > MAX_MOVE_SPEED) {
				viewer_.move_speed_ = MAX_MOVE_SPEED;
			}
		} else {
			viewer_.move_speed_ -= (viewer_.move_speed_ - MIN_MOVE_SPEED) * 3.0f * delta_seconds;
			if (viewer_.move_speed_ < MIN_MOVE_SPEED) {
				viewer_.move_speed_ = MIN_MOVE_SPEED;
			}
		}
	}

	if (viewer_.clip_to_z_ && !enableCameraMode) {

		float z0 = viewer_.pos_.z - VIEW_HEIGHT;
		float friction = viewer_.move_speed_ * 2.0f;

		// Check configured bindings for camera movement (which override standard keys if pressed)
		bool is_fwd = (input_.keys_ & MK_FORWARD) || Utils::IsKeyBindingPressed(Config::Get().keyMoveCameraForward);
		bool is_bwd = (input_.keys_ & MK_BACKWARD) || Utils::IsKeyBindingPressed(Config::Get().keyMoveCameraBackward);
		bool is_left = (input_.keys_ & MK_LEFT);
		bool is_right = (input_.keys_ & MK_RIGHT);

		// check movement
		if (is_fwd) {
			viewer_.velocity_.x = viewer_.forward_.x * viewer_.move_speed_;
			viewer_.velocity_.y = viewer_.forward_.y * viewer_.move_speed_;
		}

		if (is_bwd) {
			viewer_.velocity_.x = viewer_.forward_.x * -viewer_.move_speed_;
			viewer_.velocity_.y = viewer_.forward_.y * -viewer_.move_speed_;
		}

		if (is_left) {
			viewer_.velocity_.x = viewer_.right_.x * -viewer_.move_speed_;
			viewer_.velocity_.y = viewer_.right_.y * -viewer_.move_speed_;
		}

		if (is_right) {
			viewer_.velocity_.x = viewer_.right_.x * viewer_.move_speed_;
			viewer_.velocity_.y = viewer_.right_.y * viewer_.move_speed_;
		}

		if (input_.keys_ & MK_JUMP && viewer_.clip_to_z_) {
			if (viewer_.velocity_.z <= 0.0f && viewer_.on_ground_) {
				viewer_.velocity_.z = viewer_.jump_speed_;
			}
		}

		if (!input_.keys_ && viewer_.on_ground_) {

			float speed = (float)std::sqrt(viewer_.velocity_.x * viewer_.velocity_.x + viewer_.velocity_.y * viewer_.velocity_.y);
			if (speed > 1.0f) {
				float one_over_speed = 1.0f / speed;
				float dir_x = viewer_.velocity_.x * one_over_speed;
				float dir_y = viewer_.velocity_.y * one_over_speed;

				float speed_drop = friction * delta_seconds;
				speed -= speed_drop;

				if (speed <= 1.0f) {
					speed = 0.0f;
				}

				viewer_.velocity_.x = dir_x * speed;
				viewer_.velocity_.y = dir_y * speed;
			}
			else {
				viewer_.velocity_.x = 0.0f;
				viewer_.velocity_.y = 0.0f;
			}
		}

		glm::vec3 move_delta = viewer_.velocity_ * delta_seconds;
		glm::vec3 next_pos = viewer_.pos_ + move_delta;

        if (!CheckCollision(next_pos)) {
		    viewer_.pos_ = next_pos;
        } else {
            // Sliding logic (op0f;
            viewer_.velocity_.y = 0.0f;
        }

		if (viewer_.velocity_.z <= 0.0f) {

			float ret_z = 0.0f;
			bool ok = level_.GetTerrainZ(viewer_.pos_.x, viewer_.pos_.y, ret_z);

			if (ok) {
				float view_z = ret_z + VIEW_HEIGHT;

				if (viewer_.pos_.z < view_z) {
					viewer_.pos_.z = view_z;
					viewer_.velocity_.z = 0.0f;
					viewer_.on_ground_ = true;
				}
				else {
					viewer_.on_ground_ = false;
				}

			}
			else {
				viewer_.on_ground_ = false;
			}

		}
		else {
			viewer_.on_ground_ = false;	// moving upward
		}

		if (!viewer_.on_ground_) {
			viewer_.velocity_.z -= GRAVITE * delta_seconds;
		}

	}
	else {

		bool is_fwd = (input_.keys_ & MK_FORWARD) || Utils::IsKeyBindingPressed(Config::Get().keyMoveCameraForward);
		bool is_bwd = (input_.keys_ & MK_BACKWARD) || Utils::IsKeyBindingPressed(Config::Get().keyMoveCameraBackward);
		bool is_left = (input_.keys_ & MK_LEFT);
		bool is_right = (input_.keys_ & MK_RIGHT);

		// check movement - direct position update for free mode (NO collision)
		if (is_fwd) {
			viewer_.pos_ += viewer_.forward_ * viewer_.move_speed_ * delta_seconds;
		}
		if (is_bwd) {
			viewer_.pos_ -= viewer_.forward_ * viewer_.move_speed_ * delta_seconds;
		}
		if (is_left) {
			viewer_.pos_ -= viewer_.right_ * viewer_.move_speed_ * delta_seconds;
		}
		if (is_right) {
			viewer_.pos_ += viewer_.right_ * viewer_.move_speed_ * delta_seconds;
		}
		if (input_.keys_ & MK_STRAIGHT_UP && !viewer_.clip_to_z_) {
			viewer_.pos_ += VEC3_Z_DIR * viewer_.move_speed_ * delta_seconds;
		}
		if (input_.keys_ & MK_STRAIGHT_DOWN && !viewer_.clip_to_z_) {
			viewer_.pos_ -= VEC3_Z_DIR * viewer_.move_speed_ * delta_seconds;
		}
	}

	// check rotation
	bool update_orientation = false;

	if (input_.keys_ & MK_ROLL_INC) {
		update_orientation = true;
		viewer_.roll_ += 1.0f;
	}

	if (input_.keys_ & MK_ROLL_DEC) {
		update_orientation = true;
		viewer_.roll_ -= 1.0f;
	}

	if (update_orientation) {
		UpdateViewerVectors();
	}

	// CameraStrafeLeft/Right — lateral movement (held keys)
	{
		auto& ev = Config::Get().eventBindings_;
		auto CheckCont = [&](const std::string& n) {
			auto it = ev.find(n);
			return (it != ev.end()) && Utils::IsKeyBindingPressed(it->second);
		};
		if (CheckCont("CameraStrafeLeft"))
			viewer_.pos_ -= viewer_.right_ * viewer_.move_speed_ * delta_seconds;
		if (CheckCont("CameraStrafeRight"))
			viewer_.pos_ += viewer_.right_ * viewer_.move_speed_ * delta_seconds;
	}
}

void App::UpdateViewerVectors() {
	// clamp yaw, pitch & roll

	while (viewer_.yaw_ < 0.0f) {
		viewer_.yaw_ += 360.0f;
	}

	while (viewer_.yaw_ > 360.0f) {
		viewer_.yaw_ -= 360.0f;
	}

	if (viewer_.pitch_ < -89.0f) {
		viewer_.pitch_ = -89.0f;
	}

	if (viewer_.pitch_ > 89.0f) {
		viewer_.pitch_ = 89.0f;
	}

	while (viewer_.roll_ < 0.0f) {
		viewer_.roll_ += 360.0f;
	}

	while (viewer_.roll_ > 360.0f) {
		viewer_.roll_ -= 360.0f;
	}

	AngleToVectors(viewer_.yaw_, viewer_.pitch_, viewer_.roll_,
		viewer_.forward_, viewer_.right_, viewer_.up_);
}

void App::UpdateViewDefine() {
	view_define_.pos_ = viewer_.pos_;
	view_define_.forward_ = viewer_.forward_;
	view_define_.right_ = viewer_.right_;
	view_define_.up_ = viewer_.up_;
	view_define_.render_z_near_ = RENDER_Z_NEAR;

	/* rotate to coordinate:

	  Z
	  /
	 /
	/________X
	|
	|
	|
	Y

	 */

	// rotation only, with out translate
	view_define_.mat_rot_[0][0] = view_define_.right_.x;
	view_define_.mat_rot_[1][0] = view_define_.right_.y;
	view_define_.mat_rot_[2][0] = view_define_.right_.z;

	view_define_.mat_rot_[0][1] = -view_define_.up_.x;
	view_define_.mat_rot_[1][1] = -view_define_.up_.y;
	view_define_.mat_rot_[2][1] = -view_define_.up_.z;

	view_define_.mat_rot_[0][2] = view_define_.forward_.x;
	view_define_.mat_rot_[1][2] = view_define_.forward_.y;
	view_define_.mat_rot_[2][2] = view_define_.forward_.z;
}

void App::ToggleShowHUD() {
	show_hud_ = true;
}

bool App::GetShowHUD() const {
	return show_hud_;
}

void App::SetShowHUD(bool show) {
	show_hud_ = true;
}

void App::ToggleEditMode() {
    // Logic removed as requested
}

bool App::GetEditMode() const {
	return true; // Always true
}

void App::SetEditMode(bool enabled) {
    // Logic removed as requested
}

void App::SetTerrainEditEnabled(bool enabled) {
	terrain_edit_enabled_ = enabled;
}

bool App::GetTerrainEditEnabled() const {
	return terrain_edit_enabled_;
}

void App::TogglePauseMenu() {
	pause_mode_ = !pause_mode_;
	// cursor_visible_ stays TRUE always — camera lock is handled dynamically in Input_OnMotion.
	// Hiding the cursor permanently caused the "mouse stuck" bug after resuming.
	window_state_.cursor_visible_ = true;
	if (pause_mode_) {
		// Opening pause menu
		glutSetCursor(GLUT_CURSOR_NONE);
	} else {
		// Closing pause menu: reset mouse state so no stale drag occurs
		input_.mouse_delta_x_ = 0;
		input_.mouse_delta_y_ = 0;
		mouse_state_.left_button_down_ = false;
		skip_input_on_motion_once_ = false;
		glutSetCursor(GLUT_CURSOR_NONE);
	}
}

bool App::GetPauseMode() const {
	return pause_mode_;
}

void App::SetEditBrush(int brush) {
	edit_brush_ = brush;
}

int App::GetEditBrush() const {
	return edit_brush_;
}

void App::SetSelectedObjectScale(float scale) {
	if (selected_object_index_ >= 0 && selected_object_index_ < (int)level_.GetLevelObjects().GetObjects().size()) {
		level_.GetLevelObjects().GetObjects()[selected_object_index_].scale = scale;
		Logger::Get().Log(LogLevel::INFO, "[App] Scale changed to " + std::to_string(scale) + " for object " + std::to_string(selected_object_index_));
	}
}

float App::GetSelectedObjectScale() const {
	if (selected_object_index_ >= 0 && selected_object_index_ < (int)level_.GetLevelObjects().GetObjects().size()) {
		return level_.GetLevelObjects().GetObjects()[selected_object_index_].scale;
	}
	return 1.0f;
}

#include <glm/ext/matrix_projection.hpp>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>

void App::EditorProcessClick() {
	if (!window_state_.viewport_width_ || !window_state_.viewport_height_) return;

	LevelObjects& lo = level_.GetLevelObjects();
	std::vector<LevelObject>& objects = lo.GetObjects();

	if (terrain_edit_enabled_) {
		// Terrain edit mode: build ray from camera through mouse and edit terrain
		glm::dmat4 proj_matrix = glm::perspective(
			(double)view_define_.fovy_,
			(double)window_state_.viewport_width_ / (double)window_state_.viewport_height_,
			(double)view_define_.render_z_near_,
			(double)view_define_.render_z_far_
		);
		// Use actual camera position in world units, no extra scale
		glm::dmat4 mat_view = glm::lookAt(
			glm::dvec3(view_define_.pos_),
			glm::dvec3(view_define_.pos_) + glm::dvec3(view_define_.forward_),
			glm::dvec3(view_define_.up_));
		glm::dvec4 viewport(0.0, 0.0, (double)window_state_.viewport_width_, (double)window_state_.viewport_height_);

		double winX = (double)mouse_state_.prior_x_;
		double winY = (double)window_state_.viewport_height_ - (double)mouse_state_.prior_y_;

		glm::dvec3 start_pos = glm::unProject(glm::dvec3(winX, winY, 0.0), mat_view, proj_matrix, viewport);
		glm::dvec3 end_pos = glm::unProject(glm::dvec3(winX, winY, 1.0), mat_view, proj_matrix, viewport);
		glm::vec3 ray_origin = (glm::vec3)start_pos;
		glm::vec3 ray_dir = glm::normalize((glm::vec3)(end_pos - start_pos));

		printf("EditorClick: Mouse(%.0f, %.0f), RayDir(%.2f, %.2f, %.2f)\n", winX, winY, ray_dir.x, ray_dir.y, ray_dir.z);
		level_.EditorRaycastAndModify(ray_origin, ray_dir, edit_brush_);
		return;
	}

	// Object edit mode: select the object under the mouse cursor
	int pickedObject = PickObjectAtScreenPos(mouse_state_.prior_x_, mouse_state_.prior_y_);
	// Clicked a pure MEF attachment → promote it to an editable EditRigidObj task.
	if (pickedObject >= Renderer::kAttaPickBase) {
		PromoteAttaToObject(pickedObject - Renderer::kAttaPickBase);
		return;
	}
	if (pickedObject >= 0 && pickedObject < (int)objects.size()) {
		selected_object_index_ = pickedObject;
		const LevelObject& obj = objects[pickedObject];
		
		// Auto-expand tree for the selected object
		int currentIdx = pickedObject;
		while (currentIdx != -1) {
			int parentIdx = objects[currentIdx].parentIndex;
			if (parentIdx != -1) {
				objects[parentIdx].expanded = true;
			}
			currentIdx = parentIdx;
		}

		Logger::Get().Log(LogLevel::INFO, "[App] Selected object index=" + std::to_string(pickedObject) + " model=" + obj.modelId + " type=" + (obj.isBuilding ? "building" : "object"));
		printf("Selected Object [%d]: %s (%s)\n", selected_object_index_,
			objects[selected_object_index_].name.c_str(), objects[selected_object_index_].modelId.c_str());
		printf("  Pos: (%.0f, %.0f, %.0f)\n", (double)obj.pos.x, (double)obj.pos.y, (double)obj.pos.z);
		printf("  Rot (Alpha/Beta/Gamma): (%.2f, %.2f, %.2f)\n", (double)obj.rot.x, (double)obj.rot.y, (double)obj.rot.z);
		printf("  Scale: %.2f\n", obj.scale);
		marker_manip_.start_x_ = mouse_state_.prior_x_;
		marker_manip_.start_y_ = mouse_state_.prior_y_;
		marker_manip_.start_pos_ = obj.pos;
		marker_manip_.start_rot_ = obj.rot;

		// Scroll the TaskTree so the newly selected object is visible.
		{
			auto visibleList = GetVisibleTreeNodes();
			int current_row = -1;
			for (int i = 0; i < (int)visibleList.size(); ++i) {
				if (visibleList[i] == selected_object_index_) { current_row = i; break; }
			}
			if (current_row >= 0) {
				const int row_h   = 16;
				const int start_y = 30;
				int max_rows = (window_state_.viewport_height_ - 50 - start_y) / row_h;
				if (max_rows > 0) {
					if (current_row < tree_scroll_offset_)
						tree_scroll_offset_ = current_row;
					else if (current_row >= tree_scroll_offset_ + max_rows)
						tree_scroll_offset_ = current_row - max_rows + 1;
				}
			}
		}
	}
	else {
		selected_object_index_ = -1;
		marker_manip_.mode_ = ManipulationMode::None;
		Logger::Get().Log(LogLevel::INFO, "[App] Object selection cleared");
	}
}

bool App::CheckCollision(const glm::vec3& nextPos) {
    if (noclip_mode_) return false; // Bypass collision

    // Safety check: ensure level is loaded
    if (level_.GetLevelNo() == 0) {
        return false; // No collision when level not loaded
    }
    
    auto& objects = level_.GetLevelObjects().GetObjects();
    
    float playerRadius = 400.0f; 
    constexpr float BASE_SCALE = 40.96f;
    constexpr float FALLBACK_RADIUS_MODEL = 200.0f; // fallback collision radius in model units (tight)
    
    for (const auto& obj : objects) {
        float dist = glm::distance(nextPos, glm::vec3(obj.pos));
        if (dist > 150000.0f) continue;

        glm::mat4 model = glm::mat4(1.0f);
        model = glm::translate(model, glm::vec3(obj.pos.x, obj.pos.y, obj.pos.z));
        model = glm::rotate(model, (float)obj.rot.z, glm::vec3(0.0f, 0.0f, 1.0f));
        model = glm::rotate(model, (float)obj.rot.x, glm::vec3(1.0f, 0.0f, 0.0f));
        model = glm::rotate(model, (float)obj.rot.y, glm::vec3(0.0f, 1.0f, 0.0f));

        model = glm::scale(model, glm::vec3(BASE_SCALE * obj.scale));
        model = glm::rotate(model, glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f));

        glm::vec4 localPos = glm::inverse(model) * glm::vec4(nextPos, 1.0f);
        glm::vec3 extents = renderer_.GetMeshExtents(obj.modelId, obj.isBuilding);

        // Fallback: if mesh failed to load, use a minimum collision radius
        float ex = extents.x;
        float ey = extents.y;
        float ez = extents.z;
        if (ex < 1.0f && ey < 1.0f && ez < 1.0f) {
            ex = ey = ez = FALLBACK_RADIUS_MODEL;
        }

        if (std::abs(localPos.x) < (ex + playerRadius/BASE_SCALE) &&
            std::abs(localPos.y) < (ey + playerRadius/BASE_SCALE) &&
            std::abs(localPos.z) < (ez + playerRadius/BASE_SCALE)) 
        {
            static int collisionLogCount = 0;
            if (collisionLogCount < 50) {
                Logger::Get().Log(LogLevel::INFO, "[App] Collision with model=" + obj.modelId + " type=" + (obj.isBuilding ? "building" : "object"));
                collisionLogCount++;
            }
            return true;
        }
    }
    return false;
}

void App::SnapObjectsToTerrain() {
    auto& objects = level_.GetLevelObjects().GetObjects();
    Logger::Get().Log(LogLevel::INFO, "[App] Snapping " + std::to_string(objects.size()) + " objects to terrain...");

    int snapped = 0;
    int skipped = 0;
    int failed = 0;
    for (auto& obj : objects) {
        // Skip non-spatial task nodes: HumanAI, PatrolPath, ConditionalContainer, LevelFlow, etc.
        // These have no world position (stored as 0,0,0) and no model to snap.
        // IGI world coordinates are in the millions range, so (0,0) is always outside the map.
        if (obj.modelId.empty() || (obj.pos.x == 0.0 && obj.pos.y == 0.0)) {
            skipped++;
            continue;
        }
        // Skip snapping for Player Jones (000_01_1) to preserve exact QSC position
        if (obj.modelId == "000_01_1") {
            skipped++;
            continue;
        }
        // Skip snapping for Cameras, Terminals, Trains and Spline Waypoints
        // Terminals sit on interior floors at their exact QSC Z, not outdoor terrain.
        // AI soldiers (HumanSoldier/HumanSoldierFemale) fall through so they can be
        // terrain-snapped; the isIndoorChild check below handles interior AI.
        if (obj.type == "SCamera" || obj.type == "SCameraControl" || obj.type == "AlarmControl" ||
            obj.type == "AIStationaryGunHolder" || obj.type == "StationaryGun" ||
            obj.type == "Door" || obj.type == "Terminal" || obj.type == "SplineObjWaypoint" ||
            obj.type == "AmbientArea" || obj.type == "Elevator" || obj.isWire || obj.type == "Train") {
            
            // Only restore original Z if the object has NOT been moved/modified by the user
            // or by its parent building. This allows hierarchical movement to stick.
            if (!obj.modified) {
                Logger::Get().Log(LogLevel::INFO, "[App] Snapping " + obj.type + " to original QSC Z: " + obj.modelId);
                obj.pos.z = obj.original_pos.z;
            } else {
                Logger::Get().Log(LogLevel::INFO, "[App] Preserving modified Z for " + obj.type + ": " + obj.modelId);
            }
            
            obj.snap_z_offset = 0.0;
            skipped++;
            continue;
        }

        // Underground objects (Tunnels, ElevatorTunnels, UndergroundRooms) have their
        // Z defined precisely in the QSC and must go below terrain. Preserve original Z.
        // We also check parents: if a child (like a Door or Terminal) is inside an underground
        // parent, it must also skip snapping.
        bool isUnderground = Utils::IsUndergroundModel(obj.name, obj.modelId) || (obj.type == "Underground");
        
        if (!isUnderground && obj.parentIndex != -1 && obj.parentIndex < (int)objects.size()) {
            int pIdx = obj.parentIndex;
            while (pIdx != -1 && pIdx < (int)objects.size()) {
                const auto& parent = objects[pIdx];
                if (Utils::IsUndergroundModel(parent.name, parent.modelId) || (parent.type == "Underground")) {
                    isUnderground = true;
                    break;
                }
                pIdx = parent.parentIndex;
            }
        }

        if (isUnderground) {
            obj.snap_z_offset = 0.0;
            obj.pos.z = obj.original_pos.z;
            Logger::Get().Log(LogLevel::INFO, "[App] Underground context, preserving QSC Z for " + obj.modelId + " (" + obj.name + ") Z=" + std::to_string(obj.pos.z));
            skipped++;
            continue;
        }

        // Objects that are children of buildings (interior furniture, crates, AI) have
        // their QSC Z set relative to the building's pre-snap position (= terrainZ).
        // After snap, the building floor is at terrainZ + building.snap_z_offset, so
        // the child must be raised by the same amount to stay on the building floor.
        bool isIndoorChild = false;
        double buildingSnapZ = 0.0;
        if (obj.parentIndex != -1 && obj.parentIndex < (int)objects.size()) {
            int pIdx = obj.parentIndex;
            while (pIdx != -1 && pIdx < (int)objects.size()) {
                if (objects[pIdx].isBuilding) {
                    isIndoorChild = true;
                    buildingSnapZ = objects[pIdx].snap_z_offset;
                    break;
                }
                pIdx = objects[pIdx].parentIndex;
            }
        }
        if (isIndoorChild) {
            obj.snap_z_offset = 0.0;
            obj.pos.z = obj.original_pos.z + buildingSnapZ;
            skipped++;
            continue;
        }

        float terrainZ = 0.0f;
        if (level_.GetTerrainZ(obj.pos.x, obj.pos.y, terrainZ, false)) {
            bool isHuman = (obj.type == "HumanSoldier" || obj.type == "HumanSoldierFemale");
            double zDelta = obj.original_pos.z - (double)terrainZ;

            // Non-human objects always keep their QSC Z — trees, buildings, fences, crates, etc.
            // all have authoritative Z values authored by level designers. Snapping them to
            // terrain destroys heights for elevated platforms, embedded foundations, and stacked props.
            if (!isHuman) {
                obj.snap_z_offset = 0.0;
                obj.pos.z = obj.original_pos.z;
                skipped++;
                continue;
            }
            // Human soldiers: snap to terrain unless they are elevated (on a platform/building)
            // or below terrain (underground bunker).
            if (zDelta > 100.0 || zDelta < 0.0) {
                // On elevated surface or below terrain — preserve original Z.
                obj.snap_z_offset = 0.0;
                obj.pos.z = obj.original_pos.z;
                skipped++;
                continue;
            }
            // Underground/subsurface: QSC Z is far below terrain (e.g. ANYA_HQ at ~-13M).
            if (zDelta < -1000000.0) {
                obj.snap_z_offset = 0.0;
                obj.pos.z = obj.original_pos.z;
                Logger::Get().Log(LogLevel::INFO, "[App] Deep underground, preserving Z for " + obj.modelId + " (" + obj.name + ") Z=" + std::to_string(obj.pos.z));
                skipped++;
                continue;
            }
            // Human soldiers within 100 units of terrain surface: snap to terrain.
            // Apply the mesh Z-offset (pivot→feet) so feet rest on the ground, matching
            // the manual Snap-to-ground in UpdateMarkerManipulation (was: bare terrainZ,
            // which left models floating/sunk by their pivot offset).
            float zOffset = renderer_.GetMeshZOffset(obj.modelId, obj.isBuilding);
            obj.snap_z_offset = (double)(zOffset * 40.96f * obj.scale);
            obj.pos.z = (double)terrainZ + obj.snap_z_offset;
            Logger::Get().Log(LogLevel::DEBUG, "[App] Snapped human " + obj.modelId + " to Z=" + std::to_string(obj.pos.z));
            snapped++;
        } else {
            Logger::Get().Log(LogLevel::WARNING, "[App] Snap FAILED for " + obj.modelId + " at (" + std::to_string(obj.pos.x) + ", " + std::to_string(obj.pos.y) + "). Outside terrain?");
            failed++;
        }
    }
    Logger::Get().Log(LogLevel::INFO, "[App] Snap complete. snapped=" + std::to_string(snapped) + " skipped=" + std::to_string(skipped) + " failed=" + std::to_string(failed));
}
static glm::dmat3 BuildRotMatZXY(const glm::dvec3& euler) {
	glm::dmat4 m(1.0);
	m = glm::rotate(m, euler.z, glm::dvec3(0, 0, 1));
	m = glm::rotate(m, euler.x, glm::dvec3(1, 0, 0));
	m = glm::rotate(m, euler.y, glm::dvec3(0, 1, 0));
	return glm::dmat3(m);
}

// R[1][2]=sin(x); R[0][2]=-cx*sy, R[2][2]=cx*cy; R[1][0]=-sz*cx, R[1][1]=cz*cx
static glm::dvec3 ExtractEulerZXY(const glm::dmat3& R) {
	double sin_x = glm::clamp((double)R[1][2], -1.0, 1.0);
	double angle_x = std::asin(sin_x);
	double angle_y, angle_z;
	if (std::abs(std::cos(angle_x)) > 1e-6) {
		angle_y = std::atan2(-R[0][2], R[2][2]);
		angle_z = std::atan2(-R[1][0], R[1][1]);
	} else {
		angle_y = 0.0;
		angle_z = std::atan2(R[0][1], R[0][0]);
	}
	return glm::dvec3(angle_x, angle_y, angle_z);
}

void App::UpdateMarkerManipulation() {
	if (selected_object_index_ < 0) return;
	auto& objects = level_.GetLevelObjects().GetObjects();
	if (selected_object_index_ >= (int)objects.size()) return;
	LevelObject& obj = objects[selected_object_index_];

	int mods = glutGetModifiers();
	bool shift = (mods & GLUT_ACTIVE_SHIFT);
	bool ctrl  = (mods & GLUT_ACTIVE_CTRL);

	// Cumulative displacement from mouse-down origin (for translation)
	int dx = mouse_state_.prior_x_ - marker_manip_.start_x_;
	int dy = mouse_state_.prior_y_ - marker_manip_.start_y_;

	// Per-frame delta (for rotation, so it accumulates smoothly each frame)
	int fdx = input_.mouse_delta_x_;
	int fdy = input_.mouse_delta_y_;

	ManipulationMode current_mode = ManipulationMode::None;
	if (shift && ctrl) current_mode = ManipulationMode::MoveXZ;
	else if (shift)    current_mode = ManipulationMode::MoveXY;
	else if (ctrl)     current_mode = ManipulationMode::MoveXZ;
	else if (input_.keys_ & MK_MANIP_A) current_mode = ManipulationMode::RotateAlpha;
	else if (input_.keys_ & MK_MANIP_B) current_mode = ManipulationMode::RotateBeta;
	else if (input_.keys_ & MK_MANIP_G) current_mode = ManipulationMode::RotateGamma;
	else               current_mode = ManipulationMode::None;

	// Detect mid-drag transition between MoveXY and MoveXZ to prevent resetting coordinate updates in the other plane
	if (marker_manip_.mode_ != ManipulationMode::None && current_mode != ManipulationMode::None &&
	    current_mode != marker_manip_.mode_) {
		marker_manip_.start_pos_ = obj.pos;
		marker_manip_.start_x_ = mouse_state_.prior_x_;
		marker_manip_.start_y_ = mouse_state_.prior_y_;
	}

	marker_manip_.mode_ = current_mode;

	// Push undo state once at the start of each new manipulation gesture
	if (marker_manip_.mode_ != ManipulationMode::None) {
		if (!undo_state_pushed_for_manip_) {
			PushUndoState();
			undo_state_pushed_for_manip_ = true;
		}
	} else {
		undo_state_pushed_for_manip_ = false;
	}

	const float moveSensitivity = 200.0f;
	const float rotSensitivity  = 0.008f; // radians per pixel

	glm::dvec3 oldPos = obj.pos;
	glm::dvec3 oldRot = obj.rot;

	if (marker_manip_.mode_ == ManipulationMode::MoveXY) {
		glm::vec3 right   = viewer_.right_;
		glm::vec3 forward = glm::normalize(glm::vec3(viewer_.forward_.x, viewer_.forward_.y, 0.0f));
		obj.pos = marker_manip_.start_pos_ +
		          glm::dvec3(right   * (float)dx * moveSensitivity +
		                     forward * (float)-dy * moveSensitivity);
	}
	else if (marker_manip_.mode_ == ManipulationMode::MoveXZ) {
		glm::vec3 right = viewer_.right_;
		glm::vec3 up    = glm::vec3(0, 0, 1);
		obj.pos = marker_manip_.start_pos_ +
		          glm::dvec3(right * (float)dx  * moveSensitivity +
		                     up   * (float)-dy  * moveSensitivity);
	}
	// Rotation modes use per-frame delta so they accumulate correctly on obj.rot
	else if (marker_manip_.mode_ == ManipulationMode::RotateAlpha) {
		obj.rot.x += (float)fdx * rotSensitivity;
		marker_manip_.start_rot_.x = obj.rot.x; // keep start_rot in sync
	}
	else if (marker_manip_.mode_ == ManipulationMode::RotateBeta) {
		obj.rot.y += (float)fdx * rotSensitivity;
		marker_manip_.start_rot_.y = obj.rot.y;
	}
	else if (marker_manip_.mode_ == ManipulationMode::RotateGamma) {
		obj.rot.z += (float)fdx * rotSensitivity;
		marker_manip_.start_rot_.z = obj.rot.z;
	}

	// Apply snap-to-ground BEFORE computing delta so children get the full displacement
	if (input_.keys_ & MK_MANIP_S) {
		bool isUnderground = Utils::IsUndergroundModel(obj.name, obj.modelId) || (obj.type == "Underground");
		float terrainZ = 0.0f;
		if (level_.GetTerrainZ(obj.pos.x, obj.pos.y, terrainZ, isUnderground)) {
			float zOffset = renderer_.GetMeshZOffset(obj.modelId, obj.isBuilding);
			obj.snap_z_offset = isUnderground ? 0.0 : (double)(zOffset * 40.96f * obj.scale);
			obj.pos.z = (double)terrainZ + obj.snap_z_offset;
			obj.modified = true;
		}
	}

	// Apply snap-to-top-of-nearest-object: place selected on top of nearest object's upper surface
	if (input_.keys_ & MK_MANIP_O) {
		int nearestIdx = -1;
		double minDist = 1e10;
		auto& objects = level_.GetLevelObjects().GetObjects();
		for (int i = 0; i < (int)objects.size(); ++i) {
			if (i == selected_object_index_ || objects[i].deleted) continue;
			double d = glm::distance(obj.pos, objects[i].pos);
			if (d < minDist) { minDist = d; nearestIdx = i; }
		}
		if (nearestIdx >= 0) {
			const LevelObject& tgt = objects[nearestIdx];
			// Top of target in world Z: pos.z + (2*halfExtents.z - zOffset) * 40.96 * scale
			// (zOffset = -min_p.z, so top = pos.z + max_p.z * scale = pos.z + (-zOffset + 2*halfExt.z) * 40.96 * scale)
			float tgtZOff = renderer_.GetMeshZOffset(tgt.modelId, tgt.isBuilding);
			glm::vec3 tgtExt = renderer_.GetMeshExtents(tgt.modelId, tgt.isBuilding);
			double tgtTop = tgt.pos.z + (double)((-tgtZOff + 2.0f * tgtExt.z) * 40.96f * tgt.scale);
			// Place selected object so its bottom rests on that top surface
			float selZOff = renderer_.GetMeshZOffset(obj.modelId, obj.isBuilding);
			obj.snap_z_offset = (double)(selZOff * 40.96f * obj.scale);
			obj.pos.z = tgtTop + obj.snap_z_offset;
			obj.modified = true;
			Logger::Get().Log(LogLevel::INFO, "[App] Snapped object on top of: " + tgt.type);
		}
	}

	if (input_.keys_ & MK_MANIP_SPACE) {
		obj.rot = glm::vec3(0.0f);
		obj.modified = true;
	}

	// Compute full delta (includes snap displacement)
	glm::dvec3 deltaPos = obj.pos - oldPos;
	glm::dvec3 deltaRot = obj.rot - oldRot;

	bool changed = (std::abs(deltaPos.x) > 1e-6 || std::abs(deltaPos.y) > 1e-6 || std::abs(deltaPos.z) > 1e-6 ||
	                std::abs(deltaRot.x) > 1e-6 || std::abs(deltaRot.y) > 1e-6 || std::abs(deltaRot.z) > 1e-6);

	if (marker_manip_.mode_ != ManipulationMode::None) {
		char buf[128];
		if (marker_manip_.mode_ == ManipulationMode::MoveXY) {
			snprintf(buf, sizeof(buf), "Moving to XY Plane with X: %.2f Y: %.2f Z: %.2f", obj.pos.x, obj.pos.y, obj.pos.z);
			status_message_ = buf;
		} else if (marker_manip_.mode_ == ManipulationMode::MoveXZ) {
			snprintf(buf, sizeof(buf), "Moving to XZ Plane with X: %.2f Y: %.2f Z: %.2f", obj.pos.x, obj.pos.y, obj.pos.z);
			status_message_ = buf;
		} else if (marker_manip_.mode_ == ManipulationMode::RotateAlpha) {
			snprintf(buf, sizeof(buf), "Rotation Alpha: %.6f", obj.rot.x);
			status_message_ = buf;
		} else if (marker_manip_.mode_ == ManipulationMode::RotateBeta) {
			snprintf(buf, sizeof(buf), "Rotation Beta: %.6f", obj.rot.y);
			status_message_ = buf;
		} else if (marker_manip_.mode_ == ManipulationMode::RotateGamma) {
			snprintf(buf, sizeof(buf), "Rotation Gamma: %.6f", obj.rot.z);
			status_message_ = buf;
		}
	} else {
		status_message_.clear();
	}

	if (changed || (input_.keys_ & MK_MANIP_S) || (input_.keys_ & MK_MANIP_O) || (input_.keys_ & MK_MANIP_SPACE)) {
		glm::dmat3 deltaWorld = BuildRotMatZXY(obj.rot) * glm::transpose(BuildRotMatZXY(oldRot));
		PropagateTransformToChildren(selected_object_index_, deltaPos, deltaWorld, oldPos);
		obj.modified = true;
		level_.GetLevelObjects().UpdateCoordinatesInLine(obj);
		if (task_editor_open_) {
			edit_string_ = obj.qscLine;
		}
	}

	if (dx != 0 || dy != 0) {
		obj.modified = true;
	}

	if (obj.modified) {
		level_.GetLevelObjects().UpdateCoordinatesInLine(obj);
	}
}

std::string App::StripQuotes(const std::string& s) {
	if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
		return s.substr(1, s.size() - 2);
	return s;
}

// True if the field currently being text-edited is a multi-line box.
bool App::IsPropFieldMultiline(int field) const {
	if (field == PropPanel::kAIScriptTextField) return true;
	if (field < 0) return false; // note box (-2) is single-line
	int oi = (prop_edit_obj_index_ >= 0) ? prop_edit_obj_index_ : selected_object_index_;
	if (oi < 0) return false;
	const auto& objects = level_.GetLevelObjects().GetObjects();
	if (oi >= (int)objects.size()) return false;
	const TaskSchema* scp = GetSchema(objects[oi].type);
	if (!scp) return false;
	int fi = field / 3;
	if (fi < 0 || fi >= (int)scp->size()) return false;
	const std::string& tn = (*scp)[fi].typeName;
	return tn == "VarString" || tn == "String256";
}

void App::UpdateAIScriptScroll() {
	if (prop_text_edit_field_ != PropPanel::kAIScriptTextField) return;
	const int mc = AiScriptMaxChars(), box_lines = 12;
	auto starts = AiTextLineStarts(prop_text_buf_, mc);
	int cl = (int)(std::upper_bound(starts.begin(), starts.end(), prop_text_caret_) - starts.begin()) - 1;
	cl = std::max(0, std::min(cl, (int)starts.size() - 1));
	if (cl < ai_script_vscroll_)
		ai_script_vscroll_ = cl;
	else if (cl >= ai_script_vscroll_ + box_lines)
		ai_script_vscroll_ = cl - box_lines + 1;
}

void App::UpdateAIScriptPathHScroll() {
	if (prop_text_edit_field_ != PropPanel::kAIScriptPathField) return;
	const int mc = AiScriptMaxChars();
	if (prop_text_caret_ < ai_script_path_hscroll_)
		ai_script_path_hscroll_ = prop_text_caret_;
	if (prop_text_caret_ >= ai_script_path_hscroll_ + mc)
		ai_script_path_hscroll_ = prop_text_caret_ - mc + 1;
}

void App::LoadAIScriptForSelected() {
	if (ai_script_dirty_)
		status_message_ = "Warning: unsaved AI script edits discarded (save level first)";
	ai_script_path_.clear();
	ai_script_text_.clear();
	ai_script_dirty_        = false;
	ai_script_vscroll_      = 0;
	ai_script_path_hscroll_ = 0;

	if (selected_object_index_ < 0) return;
	const auto& objects = level_.GetLevelObjects().GetObjects();
	if (selected_object_index_ >= (int)objects.size()) return;
	const auto& obj = objects[selected_object_index_];

	// Only AI model types get the script section.
	if (ai_model_ids_.find(obj.modelId) == ai_model_ids_.end()) return;

	// The .qvm belongs to the HumanAI child task (not the HumanSoldier parent).
	// If this object IS the HumanAI, use its own ID; otherwise find the HumanAI child.
	const LevelObject* aiTask = nullptr;
	if (obj.type == "HumanAI") {
		aiTask = &obj;
	} else {
		for (int ci : obj.childrenIndices) {
			if (ci < 0 || ci >= (int)objects.size()) continue;
			if (objects[ci].deleted) continue;
			if (objects[ci].type == "HumanAI") { aiTask = &objects[ci]; break; }
		}
	}
	if (!aiTask || aiTask->taskId.empty()) return;

	int levelNo = level_.GetLevelNo();
	std::string aiDir = Utils::GetIGIRootPath() +
	                    "\\missions\\location0\\level" + std::to_string(levelNo) + "\\ai";
	ai_script_path_ = aiDir + "\\" + aiTask->taskId + ".qvm";

	if (!std::filesystem::exists(ai_script_path_)) {
		ai_script_text_ = "// .qvm not found: " + ai_script_path_;
		return;
	}
	QVMFile qvm = QVM_Parse(ai_script_path_);
	if (!qvm.valid) {
		ai_script_text_ = "// decompile failed (invalid QVM): " + ai_script_path_;
		return;
	}
	ai_script_text_ = QVM_DecompileToString(qvm);
}

// Commit the active text/numeric box (prop_text_buf_) back to the object and
// objects.qsc, then clear edit focus. Handles the note (-2) and any field box.
void App::CommitPropTextEdit() {
	if (prop_text_edit_field_ == -1) return;

	// AI Script Path field: update path, reload and decompile the new .qvm.
	if (prop_text_edit_field_ == PropPanel::kAIScriptPathField) {
		prop_text_edit_field_ = -1;
		ai_script_path_ = prop_text_buf_;
		if (!ai_script_path_.empty() && std::filesystem::exists(ai_script_path_)) {
			QVMFile qvm = QVM_Parse(ai_script_path_);
			ai_script_text_ = qvm.valid ? QVM_DecompileToString(qvm)
			                            : "// decompile failed: " + ai_script_path_;
		} else {
			ai_script_text_ = "// file not found: " + ai_script_path_;
		}
		ai_script_dirty_ = false;
		return;
	}

	// AI Script Text field: store edited text and mark dirty.
	if (prop_text_edit_field_ == PropPanel::kAIScriptTextField) {
		prop_text_edit_field_ = -1;
		ai_script_text_ = prop_text_buf_;
		ai_script_dirty_ = true;
		return;
	}

	int field = prop_text_edit_field_;
	prop_text_edit_field_ = -1;
	// Edits may target the selected object OR one of its child tasks (weapon/ammo).
	int oi = (prop_edit_obj_index_ >= 0) ? prop_edit_obj_index_ : selected_object_index_;
	prop_edit_obj_index_ = -1;
	if (oi < 0) return;
	auto& objects = level_.GetLevelObjects().GetObjects();
	if (oi >= (int)objects.size()) return;
	auto& obj = objects[oi];

	if (field == -2) {
		// Note edit -> obj.name (and arg[2] if present, keeping quotes).
		obj.name = prop_text_buf_;
		if (obj.argTokens.size() > 2)
			obj.argTokens[2] = "\"" + StripQuotes(prop_text_buf_) + "\"";
		obj.modified = true;
		level_.GetLevelObjects().UpdateCoordinatesInLine(obj);
		return;
	}

	const TaskSchema* scp = GetSchema(obj.type);
	if (!scp) return;
	int fi = field / 3, comp = field % 3;
	if (fi >= (int)scp->size()) return;
	const FieldDef& fd = (*scp)[fi];
	int argIdx = fd.argOffset + comp;
	if (argIdx < 0 || argIdx >= (int)obj.argTokens.size()) return;

	const std::string& tn = fd.typeName;
	bool is_str = (tn.find("String") != std::string::npos || tn == "VarString" ||
	               tn == "EnumString32" || tn == "DropDownCombo");
	bool is_int = (tn == "Int16" || tn == "Int32" || tn == "EnumInt32");
	bool is_pos = (tn == "ObjectPos"); // only sync obj.pos for actual position fields

	std::string tokenVal;
	if (is_str) {
		// Preserve quoting: strings keep surrounding quotes in the QSC.
		bool hadQuotes = obj.argTokens[argIdx].size() >= 2 &&
		                 obj.argTokens[argIdx].front() == '"';
		std::string body = StripQuotes(prop_text_buf_);
		tokenVal = (hadQuotes || tn.find("String") != std::string::npos || tn == "DropDownCombo")
		               ? ("\"" + body + "\"") : body;
	} else if (is_int) {
		long v = 0; try { v = std::lround(std::stod(prop_text_buf_)); } catch(...) {}
		char buf[64]; snprintf(buf, sizeof(buf), "%ld", v); tokenVal = buf;
	} else {
		// Real / Angle / Degrees / RangeReal32 — float formatting.
		double v = 0; try { v = std::stod(prop_text_buf_); } catch(...) {}
		char buf[64]; snprintf(buf, sizeof(buf), "%.6f", v); tokenVal = buf;
	}
	obj.argTokens[argIdx] = tokenVal;

	// Sync obj.rot after orientation field commits so the 3D marker updates.
	bool is_ori_field = (tn == "Real32x9");
	bool is_gamma_field = ((tn == "Real32" || tn == "Angle" || tn == "Degrees") && (fd.name == "Gamma" || fd.name == "Heading"));
	if (is_ori_field) {
		if (comp == 0 && fd.argOffset < (int)obj.argTokens.size())
			try { obj.rot.x = std::stod(obj.argTokens[fd.argOffset]); } catch(...) {}
		if (comp == 1 && fd.argOffset + 1 < (int)obj.argTokens.size())
			try { obj.rot.y = std::stod(obj.argTokens[fd.argOffset + 1]); } catch(...) {}
		if (comp == 2 && fd.argOffset + 2 < (int)obj.argTokens.size())
			try { obj.rot.z = std::stod(obj.argTokens[fd.argOffset + 2]); } catch(...) {}
	} else if (is_gamma_field) {
		if (fd.argOffset < (int)obj.argTokens.size())
			try { obj.rot.z = std::stod(obj.argTokens[fd.argOffset]); } catch(...) {}
	}

	// Mirror typed coords into obj.pos for ObjectPos boxes.
	if (is_pos) {
		double v = 0; try { v = std::stod(prop_text_buf_); } catch(...) {}
		if      (comp == 0) obj.pos.x = v;
		else if (comp == 1) obj.pos.y = v;
		else                obj.pos.z = v;
	}
	// Sync model field to obj.modelId so UpdateCoordinatesInLine doesn't
	// overwrite the new model with the stale obj.modelId.
	bool is_model_field = is_str && (fd.name == "Model" ||
	                                 fd.name.find("Model") != std::string::npos);
	if (is_model_field) {
		obj.modelId = StripQuotes(prop_text_buf_);
		if (!level_res_models_.Empty() && !obj.modelId.empty() &&
		    !level_res_models_.Contains(obj.modelId)) {
			obj.modelMissingInRes = true;
			status_message_ = "Model '" + obj.modelId +
				"' is not in this level's .res — it will be invisible in-game. "
				"Press Ctrl+Shift+A to add it.";
		} else {
			obj.modelMissingInRes = false;
		}
	}
	// GunPickup/AmmoPickup: the edited field is the weapon/ammo enum string, but
	// obj.modelId must hold the RESOLVED render model. Re-resolve so the viewport
	// mesh updates immediately instead of only after a reload (issue 1).
	if (obj.type == "GunPickup" || obj.type == "AmmoPickup") {
		std::string enumStr = StripQuotes(prop_text_buf_);
		if (enumStr.rfind("WEAPON_ID_", 0) == 0 || enumStr.rfind("AMMO_ID_", 0) == 0) {
			obj.modelId = level_.GetLevelObjects().ResolvePickupModelId(enumStr);
		}
	}
	obj.modified = true;
	level_.GetLevelObjects().UpdateCoordinatesInLine(obj);
}

void App::PushUndoState() {
	auto& objects = level_.GetLevelObjects().GetObjects();
	object_undo_stack_.push_back(objects);
	object_redo_stack_.clear();
	if (object_undo_stack_.size() > 20)
		object_undo_stack_.erase(object_undo_stack_.begin());
}

void App::SaveAndReloadObjects() {
	level_.SaveAndReloadObjects();
	EvaluateTrainTrackPositions();
	SnapObjectsToTerrain();
	RebuildLevelModelIds();
}

void App::RebuildLevelModelIds() {
	level_model_ids_.clear();
	for (auto& obj : level_.GetLevelObjects().GetObjects()) {
		if (obj.deleted || obj.modelId.empty()) continue;
		// Accept XXX_XX_X pattern (digits_digits_digit)
		const std::string& m = obj.modelId;
		bool ok = m.size() >= 7;
		if (ok) {
			for (char c : m) if (!isdigit(c) && c != '_') { ok = false; break; }
		}
		if (ok) level_model_ids_.insert(m);
	}
}

void App::Undo() {
	if (object_undo_stack_.empty()) { status_message_ = "Nothing to undo"; return; }
	auto& objects = level_.GetLevelObjects().GetObjects();
	object_redo_stack_.push_back(objects);
	objects = object_undo_stack_.back();
	object_undo_stack_.pop_back();
	SaveAndReloadObjects();
	status_message_ = "Undo";
}

void App::Redo() {
	if (object_redo_stack_.empty()) { status_message_ = "Nothing to redo"; return; }
	auto& objects = level_.GetLevelObjects().GetObjects();
	object_undo_stack_.push_back(objects);
	objects = object_redo_stack_.back();
	object_redo_stack_.pop_back();
	SaveAndReloadObjects();
	status_message_ = "Redo";
}

void App::PropagateTransformToChildren(int parentIdx, const glm::dvec3& deltaPos, const glm::dmat3& deltaWorld, const glm::dvec3& pivot) {
	auto& objects = level_.GetLevelObjects().GetObjects();
	std::vector<int> children = objects[parentIdx].childrenIndices;

	for (int childIdx : children) {
		LevelObject& child = objects[childIdx];

		// Rotate child's relative position around the parent's old pivot, then translate
		glm::dvec3 relPos = child.pos - pivot;
		child.pos = pivot + glm::dvec3(deltaWorld * relPos) + deltaPos;

		// Compose child's orientation with the parent's world-space rotation delta
		child.rot = ExtractEulerZXY(deltaWorld * BuildRotMatZXY(child.rot));

		if (child.type == "HumanSoldier" || child.type == "HumanSoldierFemale") {
			child.graphPos += deltaPos;
		}

		child.modified = true;
		level_.GetLevelObjects().UpdateCoordinatesInLine(child);
		PropagateTransformToChildren(childIdx, deltaPos, deltaWorld, pivot);
	}
}

int App::PickObjectAtScreenPos(int screen_x, int screen_y) {
	const auto& objects = level_.GetLevelObjects().GetObjects();
	if (objects.empty()) return -1;

	int w = window_state_.viewport_width_;
	int h = window_state_.viewport_height_;
	if (w == 0 || h == 0) return -1;

	return renderer_.PickObjectAtScreen(
		screen_x, screen_y, w, h,
		view_define_,
		objects,
		renderer_.DRAW_OBJECTS | renderer_.DRAW_BUILDINGS | renderer_.DRAW_PROPS
	);
}

void App::PromoteAttaToObject(int entry) {
	AttaPickEntry e;
	if (!renderer_.GetAttaPickEntry(entry, e)) return;

	auto& objects = level_.GetLevelObjects().GetObjects();
	PushUndoState();

	// Create a lightweight proxy LevelObject so the existing gizmo/movement system
	// can move the ATTA. The proxy is NOT serialized to QSC. When the user saves or
	// launches the game, FlushAttaProxiesToMef() converts the proxy's world position
	// back to local coordinates and patches the bytes directly in the MEF binary.
	// No renaming, no QSC tasks, no game-engine warnings.
	// Extract Euler angles (Rz * Rx * Ry order) from the captured world rotation matrix.
	// GLM column-major: m[col][row]. For Rz*Rx*Ry: sin(rx)=m[1][2], etc.
	const glm::mat3& m = e.worldRot;
	float sx = std::max(-1.0f, std::min(1.0f, m[1][2]));
	float rx = std::asin(sx);
	float ry, rz;
	if (std::fabs(std::cos(rx)) > 1e-4f) {
		ry = std::atan2(-m[0][2], m[2][2]);
		rz = std::atan2(-m[1][0], m[1][1]);
	} else {
		ry = 0.0f;
		rz = std::atan2(m[0][1], m[0][0]);
	}

	LevelObject obj;
	obj.type        = "EditRigidObj";
	obj.name        = "ATTA_PROXY:" + e.immediateParentModelId + ":" + std::to_string(e.recordIndex);
	obj.taskId      = "-1";
	obj.modelId     = e.modelId;
	obj.pos         = glm::dvec3(e.worldPos);
	obj.rot         = glm::vec3(rx, ry, rz);
	obj.scale       = (e.scale > 0.f) ? e.scale : 1.0f;
	obj.isBuilding  = false;
	obj.deleted     = false;
	obj.modified    = false;
	obj.isAttaProxy         = true;
	obj.attaRecordIndex     = e.recordIndex;
	obj.attaParentModelId   = e.immediateParentModelId;
	obj.attaIsBuilding      = false;
	obj.attaInvParentMat    = glm::inverse(e.parentWorldMat);

	int newIdx = (int)objects.size();
	objects.push_back(obj);

	// Suppress this ATTA record from being re-offered for picking.
	renderer_.MarkAttaPromotedByRecord(e.immediateParentModelId, e.recordIndex);

	selected_object_index_ = newIdx;
	marker_manip_.start_pos_ = objects[newIdx].pos;
	marker_manip_.start_rot_ = objects[newIdx].rot;
	status_message_ = "Editing ATTA '" + e.modelId + "' — move then Save to apply to .res";
	Logger::Get().Log(LogLevel::INFO,
		"[App] ATTA '" + e.modelId + "' selected for direct MEF edit"
		" (record " + std::to_string(e.recordIndex) + " in " + e.immediateParentModelId + ".mef)");
}

void App::LoadQSCForLevel(int level_no) {
	// New level: forget ATTA suppressions from the previous one (a freshly loaded
	// level's saved EditRigidObj tasks re-suppress their ATTAs via live occupancy).
	renderer_.ClearSuppressedAttas();
	try {
		namespace fs = std::filesystem;

		std::string qsc_dest = Utils::GetExeDirectory() + "\\content\\qed\\temp\\objects.qsc";
		std::string qvm_source = Utils::GetLevelQVMPath(level_no);
		
		Logger::Get().Log(LogLevel::INFO, "[App] [LoadQSCForLevel] Always reading level objects.qvm directly: " + qvm_source);
		Logger::Get().Log(LogLevel::INFO, "[App] [LoadQSCForLevel] Destination QSC: " + qsc_dest);

		// Decompile from the game QVM directly to the destination QSC
		DecompileFromGame(level_no);
		Logger::Get().Log(LogLevel::INFO, "[App] [LoadQSCForLevel] SUCCESS: Loaded/Decompiled level from QVM to: " + qsc_dest);
	}
	catch (const std::exception& e) {
		std::string errorMsg = "Error loading Level " + std::to_string(level_no) + ":\n" + std::string(e.what());
		Utils::LogAndShowError(errorMsg, "IGI Editor - Error");
		Logger::Get().Log(LogLevel::ERR, errorMsg);
	}
	catch (...) {
		std::string errorMsg = "Unknown error loading Level " + std::to_string(level_no);
		Utils::LogAndShowError(errorMsg, "IGI Editor - Error");
		Logger::Get().Log(LogLevel::ERR, errorMsg);
	}
}

void App::DecompileFromGame(int level_no) {
	try {
		namespace fs = std::filesystem;

		std::string qvm_source = Utils::GetLevelQVMPath(level_no);
		std::string qsc_dest = Utils::GetExeDirectory() + "\\content\\qed\\temp\\objects.qsc";

		if (!fs::exists(qvm_source)) {
			std::string errorMsg = "Game QVM not found at:\n" + qvm_source + "\n\nPlease check your IGI game path in qedconfig.txt";
			Utils::LogAndShowError(errorMsg, "IGI Editor - Error");
			Logger::Get().Log(LogLevel::ERR, "[App] Game QVM not found at: " + qvm_source);
			return;
		}

		QVMFile qvm = QVM_Parse(qvm_source);
		bool success = qvm.valid && QVM_Decompile(qvm, qsc_dest);
		if (!success) {
			std::string errorMsg = "Failed to decompile QVM from:\n" + qvm_source;
			Utils::LogAndShowError(errorMsg, "IGI Editor - Error");
			Logger::Get().Log(LogLevel::ERR, "[App] Failed to decompile from game QVM");
		}
	}
	catch (const std::exception& e) {
		std::string errorMsg = "Error decompiling QVM for level " + std::to_string(level_no) + ":\n" + std::string(e.what());
		Utils::LogAndShowError(errorMsg, "IGI Editor - Error");
		Logger::Get().Log(LogLevel::ERR, errorMsg);
	}
	catch (...) {
		std::string errorMsg = "Unknown error decompiling QVM for level " + std::to_string(level_no);
		Utils::LogAndShowError(errorMsg, "IGI Editor - Error");
		Logger::Get().Log(LogLevel::ERR, errorMsg);
	}
}

void App::LaunchGame() {
	if (game_process_.running) {
		// ── Toggle OFF: stop the running game ──────────────────────────────────
		Logger::Get().Log(LogLevel::INFO, "[ToggleGame] Game is running (PID=" +
		                  std::to_string(game_process_.pid) + ") — stopping...");

		// 1. Post WM_CLOSE to every window owned by the game (graceful request)
		int closedWindows = 0;
		struct CloseCtx { DWORD pid; int* count; };
		CloseCtx ctx{ game_process_.pid, &closedWindows };
		EnumWindows([](HWND hwnd, LPARAM lp) -> BOOL {
			DWORD wndPid = 0;
			GetWindowThreadProcessId(hwnd, &wndPid);
			auto* c = reinterpret_cast<CloseCtx*>(lp);
			if (wndPid == c->pid) {
				PostMessage(hwnd, WM_CLOSE, 0, 0);
				(*c->count)++;
			}
			return TRUE;
		}, reinterpret_cast<LPARAM>(&ctx));
		Logger::Get().Log(LogLevel::INFO, "[ToggleGame] WM_CLOSE posted to " +
		                  std::to_string(closedWindows) + " window(s)");

		// 2. Force-terminate immediately so we don't block the main thread.
		//    Old DirectX full-screen games rarely honour WM_CLOSE anyway.
		BOOL killed = TerminateProcess(game_process_.hProcess, 0);
		Logger::Get().Log(LogLevel::INFO, "[ToggleGame] TerminateProcess(" +
		                  std::to_string(game_process_.pid) + ") = " +
		                  (killed ? "OK" : "FAILED (err=" + std::to_string(GetLastError()) + ")"));

		// The background monitor thread will detect the process exit and set
		// game_exited_ = true; OnIdle will then clean up handles and restore the editor.
		Logger::Get().Log(LogLevel::INFO, "[ToggleGame] Waiting for OnIdle to restore editor...");
		return;
	}

	// ── Toggle ON: save level and launch the game ──────────────────────────────
	Logger::Get().Log(LogLevel::INFO, "[ToggleGame] Game is not running — launching level " +
	                  std::to_string(level_.GetLevelNo()));

	SaveCurrentLevel();
	Logger::Get().Log(LogLevel::INFO, "[ToggleGame] Level saved");

	std::string workDir = Utils::GetIGIRootPath();
	std::string cmdLine = workDir + "\\igi.exe level" + std::to_string(level_.GetLevelNo());
	Logger::Get().Log(LogLevel::INFO, "[ToggleGame] Launching: " + cmdLine);

	STARTUPINFOA si = {};
	si.cb = sizeof(si);
	PROCESS_INFORMATION pi = {};

	std::vector<char> cmdBuf(cmdLine.begin(), cmdLine.end());
	cmdBuf.push_back('\0');

	if (!CreateProcessA(nullptr, cmdBuf.data(), nullptr, nullptr, FALSE,
	                    0, nullptr, workDir.c_str(), &si, &pi)) {
		DWORD err = GetLastError();
		std::string errMsg = "Failed to launch igi.exe (error " + std::to_string(err) + ")";
		Logger::Get().Log(LogLevel::ERR, "[ToggleGame] " + errMsg);
		Utils::LogAndShowError(errMsg, "IGI Editor - Launch Error");
		return;
	}
	Logger::Get().Log(LogLevel::INFO, "[ToggleGame] CreateProcess OK — PID=" +
	                  std::to_string(pi.dwProcessId));

	// Keep our own PROCESS_ALL_ACCESS handle for TerminateProcess / WaitForSingleObject
	HANDLE hGame = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pi.dwProcessId);
	if (!hGame) {
		DWORD err = GetLastError();
		Logger::Get().Log(LogLevel::ERR, "[ToggleGame] OpenProcess failed (error=" +
		                  std::to_string(err) + ") — cannot track game");
		CloseHandle(pi.hProcess);
		CloseHandle(pi.hThread);
		return;
	}
	CloseHandle(pi.hProcess);  // release the CreateProcess copy; we use hGame

	game_process_.hProcess = hGame;
	game_process_.hThread  = pi.hThread;
	game_process_.pid      = pi.dwProcessId;
	game_process_.running  = true;

	// Spawn background monitor — WaitForSingleObject(INFINITE) on the game process.
	// Sets game_exited_ when the process exits (by any means), so OnIdle can restore.
	game_exited_.store(false, std::memory_order_relaxed);
	auto* monParam = new GameMonitorParam{hGame, &game_exited_};
	DWORD monTid = 0;
	game_process_.hMonitorThread = CreateThread(nullptr, 0, GameMonitorProc, monParam, 0, &monTid);
	Logger::Get().Log(LogLevel::INFO, "[ToggleGame] Monitor thread started (TID=" +
	                  std::to_string(monTid) + ")");

	// Register global hotkey so F3 (or whatever keyToggleGame is bound to) fires
	// even when the game has focus and the editor is iconified.
	if (editor_hwnd_) {
		const auto& kb = Config::Get().keyToggleGame;
		UINT mods = (kb.ctrl  ? MOD_CONTROL : 0)
		          | (kb.shift ? MOD_SHIFT   : 0)
		          | (kb.alt   ? MOD_ALT     : 0)
		          | MOD_NOREPEAT;
		if (RegisterHotKey(editor_hwnd_, HOTKEY_ID_TOGGLE_GAME, mods, kb.vkCode)) {
			Logger::Get().Log(LogLevel::INFO, "[ToggleGame] Global hotkey registered (VK=0x" +
			                  [&]{ std::ostringstream ss; ss << std::hex << kb.vkCode; return ss.str(); }() + ")");
		} else {
			Logger::Get().Log(LogLevel::WARNING, "[ToggleGame] RegisterHotKey failed (err=" +
			                  std::to_string(GetLastError()) + ") — F3 won't work while game runs");
		}
	}

	// Fire WM_TIMER every 100ms while iconified so freeglut's message loop keeps running
	// (without a timer it blocks in WaitMessage and OnIdle never fires).
	if (editor_hwnd_) SetTimer(editor_hwnd_, 1, 100, NULL);

	// Iconify via GLUT so its internal state stays consistent (raw ShowWindow breaks the idle loop)
	glutIconifyWindow();
	Logger::Get().Log(LogLevel::INFO, "[ToggleGame] Editor iconified — game is now active");
}

void App::SaveAndCompile() {
	namespace fs = std::filesystem;

	Logger::Get().Log(LogLevel::INFO, "[App] SaveAndCompile() starting");

	std::string qsc_source = Utils::GetExeDirectory() + "\\content\\qed\\temp\\objects.qsc";
	std::string qvm_dest = Utils::GetLevelQVMPath(level_.GetLevelNo());

	Logger::Get().Log(LogLevel::INFO, "[App] Full QSC path: " + qsc_source);
	Logger::Get().Log(LogLevel::INFO, "[App] QVM destination: " + qvm_dest);

	if (!fs::exists(qsc_source)) {
		Logger::Get().Log(LogLevel::ERR, "[App] QSC file not found at: " + qsc_source);
		return;
	}

	// Backup existing QVM before overwriting so we can revert if compile produces garbage
	std::vector<uint8_t> qvm_backup;
	{
		std::ifstream backup_in(qvm_dest, std::ios::binary);
		if (backup_in) {
			qvm_backup.assign(std::istreambuf_iterator<char>(backup_in),
			                  std::istreambuf_iterator<char>());
			Logger::Get().Log(LogLevel::INFO, "[App] Backed up existing QVM (" +
			                  std::to_string(qvm_backup.size()) + " bytes)");
		}
	}

	Logger::Get().Log(LogLevel::INFO, "[App] Compiling QSC (native)");
	std::ifstream qscFile(qsc_source);
	std::string qscSrc((std::istreambuf_iterator<char>(qscFile)), std::istreambuf_iterator<char>());
	auto lexResult  = qsc::Lex(qscSrc);
	auto parseResult = lexResult.ok ? qsc::Parse(lexResult.tokens) : qsc::ParseResult{};
	std::string compileErr;
	bool success = lexResult.ok && parseResult.ok &&
	               qvm::CompileToFile(*parseResult.program, qvm_dest, &compileErr);
	if (success) {
		// Round-trip validate: parse the QVM we just wrote to catch silent corruption
		QVMFile written_qvm = QVM_Parse(qvm_dest);
		if (!written_qvm.valid) {
			Logger::Get().Log(LogLevel::ERR, "[App] CRITICAL: Written QVM failed validation — reverting to backup");
			if (!qvm_backup.empty()) {
				std::ofstream revert(qvm_dest, std::ios::binary | std::ios::trunc);
				if (revert) {
					revert.write(reinterpret_cast<const char*>(qvm_backup.data()), qvm_backup.size());
					Logger::Get().Log(LogLevel::INFO, "[App] Backup QVM restored successfully");
				} else {
					Logger::Get().Log(LogLevel::ERR, "[App] FATAL: Could not restore backup QVM");
				}
			}
			Utils::LogAndShowError(
				"Save failed: the compiled QVM was invalid and has been reverted.\n"
				"Your edits are NOT lost — they remain in the editor.",
				"IGI Editor - Save Error");
			return;
		}
		Logger::Get().Log(LogLevel::INFO, "[App] QVM round-trip validation passed. Deployed to: " + qvm_dest);
	} else {
		std::string detail = compileErr.empty() ? "(no detail)" : compileErr;
		Logger::Get().Log(LogLevel::ERR, "[App] Failed to compile QSC. Detail: " + detail);
		Utils::LogAndShowError("Compile failed. Error: " + detail, "IGI Editor - Compile Error");
	}
}

void App::SetInitialFullscreen(int windowedW, int windowedH) {
	// Mark fullscreen as active and remember the windowed size so ALT+ENTER can
	// restore a sane window. main() calls glutFullScreen() to actually enter it.
	window_state_.full_screen_ = true;
	window_state_.old_viewport_width_  = windowedW;
	window_state_.old_viewport_height_ = windowedH;
}

void App::SetInitialDrawParts(int parts) {
	if (parts != 0) {
		draw_params_.draw_parts_ = parts;
		Logger::Get().Log(LogLevel::INFO, "[App] Set initial draw_parts to: " + std::to_string(parts));
	}
}

void App::SetInitialStickToGround(bool stick) {
	stick_to_ground_ = stick;
	if (stick) {
		SnapObjectsToTerrain();
		Logger::Get().Log(LogLevel::INFO, "[App] Enabled stick_to_ground mode");
	}
}

void App::ProcessTreeViewClick(int mx, int my) {
    if (!level_.GetLevelObjects().GetObjects().empty()) {
        auto& objects = level_.GetLevelObjects().GetObjects();
        int tree_x = 20;
        int row_h = 16;
        int start_y = 30;
        int current_row = 0;

        bool found = false;
        std::function<void(int, int)> check_node = [&](int idx, int depth) {
            if (found || idx < 0 || idx >= (int)objects.size()) return;
            const auto& obj = objects[idx];
            if (obj.deleted) return;
            
            int x = tree_x + (depth * 18);
            int y = start_y + (current_row - tree_scroll_offset_) * row_h;
            current_row++;

            if (y >= start_y && y < window_state_.viewport_height_ - 50) {
                // Check if interaction was on the node area (including [+] and label)
                if (mx >= x - 20 && mx <= x + 300 && my >= y && my <= y + row_h) {
                    found = true;
                    if (mx <= x + 5) { // Clicked on toggle area
                        if (obj.isContainer && !obj.childrenIndices.empty()) {
                            auto& nonConstObj = const_cast<LevelObject&>(obj);
                            nonConstObj.expanded = !nonConstObj.expanded;
                            Logger::Get().Log(LogLevel::INFO, "[App] Toggled tree node: " + obj.type);
                        }
                    } else { // Clicked on label area
                        selected_object_index_ = idx;
                        int currentTime = glutGet(GLUT_ELAPSED_TIME);
                        bool isDoubleClick = (idx == last_tree_click_index_ && (currentTime - last_tree_click_time_ms_ < 400));
                        last_tree_click_index_ = idx;
                        last_tree_click_time_ms_ = currentTime;

                        if (isDoubleClick) {
                            prop_editor_open_ = true; prop_panel_scroll_ = 0; prop_text_edit_field_ = -1; prop_edit_obj_index_ = -1;
                            LoadAIScriptForSelected();
                            Logger::Get().Log(LogLevel::INFO, "[App] Double clicked object from tree and opened property panel.");
                        } else {
                            Logger::Get().Log(LogLevel::INFO, "[App] Selected object from tree: " + obj.type);
                        }
                    }
                }
            }

            if (!found && obj.expanded) {
                for (int childIdx : obj.childrenIndices) {
                    check_node(childIdx, depth + 1);
                }
            }
        };

        std::vector<int> root_decls;
        std::vector<int> root_others;
        for (int i = 0; i < (int)objects.size(); ++i) {
            if (objects[i].parentIndex == -1 && !objects[i].deleted) {
                if (objects[i].type == "Task_DeclareParameters") root_decls.push_back(i);
                else root_others.push_back(i);
            }
        }

        if (!found && !root_decls.empty()) {
            int y = start_y + (current_row - tree_scroll_offset_) * row_h;
            current_row++;
            if (y >= start_y && y < window_state_.viewport_height_ - 50) {
                if (mx >= tree_x - 20 && mx <= tree_x + 300 && my >= y && my <= y + row_h) {
                    found = true;
                    if (mx <= tree_x + 5) {
                        tree_decl_expanded_ = !tree_decl_expanded_;
                        Logger::Get().Log(LogLevel::INFO, "[App] Toggled Mission Declarations");
                    } else {
                        selected_object_index_ = -2;
                    }
                }
            }
            if (!found && tree_decl_expanded_) {
                for (int idx : root_decls) check_node(idx, 1);
            }
        }

        if (!found) {
            for (int idx : root_others) {
                if (found) break;
                check_node(idx, 0);
            }
        }
    }
}

void App::ProcessTreeViewHover(int mx, int my) {
    int tree_x = 20;
    int start_y = 30;
    int row_h = 16;
    int current_row = 0;
    
    auto& objects = level_.GetLevelObjects().GetObjects();
    
    bool found = false;
    std::function<void(int, int)> check_node = [&](int idx, int depth) {
        if (found || idx < 0 || idx >= (int)objects.size()) return;
        auto& obj = objects[idx];
        if (obj.deleted) return;
        
        int x = tree_x + (depth * 18);
        int y = start_y + (current_row - tree_scroll_offset_) * row_h;
        current_row++;

        if (y >= start_y && y < window_state_.viewport_height_ - 50) {
            if (mx >= x - 20 && mx <= x + 300 && my >= y && my <= y + row_h) {
                hover_tree_index_ = idx;
                found = true;
            }
        }

        if (!found && obj.expanded) {
            for (int childIdx : obj.childrenIndices) {
                check_node(childIdx, depth + 1);
            }
        }
    };

    std::vector<int> root_decls;
    std::vector<int> root_others;
    for (int i = 0; i < (int)objects.size(); ++i) {
        if (objects[i].parentIndex == -1 && !objects[i].deleted) {
            if (objects[i].type == "Task_DeclareParameters") root_decls.push_back(i);
            else root_others.push_back(i);
        }
    }

    if (!found && !root_decls.empty()) {
        int y = start_y + (current_row - tree_scroll_offset_) * row_h;
        current_row++;
        if (y >= start_y && y < window_state_.viewport_height_ - 50) {
            if (mx >= tree_x - 20 && mx <= tree_x + 300 && my >= y && my <= y + row_h) {
                found = true;
                hover_tree_index_ = -1;
            }
        }
        if (!found && tree_decl_expanded_) {
            for (int idx : root_decls) check_node(idx, 1);
        }
    }

    if (!found) {
        for (int idx : root_others) {
            if (found) break;
            check_node(idx, 0);
        }
    }
}

void App::CreateNewTask() {
    if (task_picker_open_) return;
    PushUndoState();
    auto& objects = level_.GetLevelObjects().GetObjects();
    if (selected_object_index_ < 0 && !objects.empty()) {
        status_message_ = "Error: Must select a valid parent task first.";
        return;
    }
    if (objects.empty()) {
        LevelObject newObj;
        newObj.qscFuncName = "Task_New";
        newObj.type = "Container";
        newObj.name = "NewTask_0";
        newObj.pos = glm::dvec3(viewer_.pos_);
        newObj.rot = glm::vec3(0.0f);
        newObj.scale = 1.0f;
        newObj.isContainer = true;
        newObj.expanded = true;
        newObj.modified = true;
        newObj.taskId = "-1";
        
        objects.push_back(newObj);
        selected_object_index_ = 0;
        level_.GetLevelObjects().UpdateCoordinatesInLine(objects.back());
        SaveAndReloadObjects();
        return;
    }

    task_picker_open_ = true;
    task_picker_selected_idx_ = 0;
    task_picker_scroll_offset_ = 0;
    task_picker_search_ = "";
    Logger::Get().Log(LogLevel::INFO, "[App] Opened Task Picker overlay");
}

void App::DeleteSelectedTask() {
    if (selected_object_index_ < 0) return;
    PushUndoState();
    auto& objects = level_.GetLevelObjects().GetObjects();
    if (selected_object_index_ >= (int)objects.size()) return;
    int parentIndex = objects[selected_object_index_].parentIndex;

    std::function<void(int)> delete_recurse = [&](int idx) {
        if (idx < 0 || idx >= (int)objects.size()) return;
        objects[idx].deleted = true;
        for (int childIdx : objects[idx].childrenIndices) {
            delete_recurse(childIdx);
        }
    };

    delete_recurse(selected_object_index_);
    SaveAndReloadObjects();
    auto& reloaded = level_.GetLevelObjects().GetObjects();
    if (reloaded.empty()) selected_object_index_ = -1;
    else if (parentIndex >= 0 && parentIndex < (int)reloaded.size()) selected_object_index_ = parentIndex;
    else selected_object_index_ = std::min(selected_object_index_, (int)reloaded.size() - 1);
    Logger::Get().Log(LogLevel::INFO, "[App] Deleted task and its subtree");
}

void App::CopySelectedTask(bool includeSubtree) {
    if (selected_object_index_ < 0) return;
    auto& objects = level_.GetLevelObjects().GetObjects();
    clipboard_.clear();

    std::function<void(int, int)> copy_recurse = [&](int idx, int newParentInClipboard) {
        if (idx < 0 || idx >= (int)objects.size()) return;
        
        LevelObject copy = objects[idx];
        copy.childrenIndices.clear();
        copy.parentIndex = newParentInClipboard;
        
        int clipboardIdx = (int)clipboard_.size();
        clipboard_.push_back(copy);
        
        if (newParentInClipboard != -1) {
            clipboard_[newParentInClipboard].childrenIndices.push_back(clipboardIdx);
        }

        if (includeSubtree) {
            for (int childIdx : objects[idx].childrenIndices) {
                copy_recurse(childIdx, clipboardIdx);
            }
        }
    };

    copy_recurse(selected_object_index_, -1);
    Logger::Get().Log(LogLevel::INFO, "[App] Copied task to clipboard (subtree: " + std::string(includeSubtree ? "yes" : "no") + ")");
}

void App::PasteTask() {
    if (clipboard_.empty()) return;
    PushUndoState();
    auto& objects = level_.GetLevelObjects().GetObjects();
    if (selected_object_index_ < 0 || selected_object_index_ >= (int)objects.size()) {
        status_message_ = "Error: Must select a valid parent task first.";
        Logger::Get().Log(LogLevel::WARNING, "[App] Validation failed: Parent index is invalid for Paste operation.");
        return;
    }
    if (!ValidateParentChildCompatibility(objects[selected_object_index_], clipboard_)) {
        status_message_ = "Error: Cannot add Computer to a WaterTower.";
        Logger::Get().Log(LogLevel::WARNING, "[App] Validation failed: Cannot paste Computer task to WaterTower parent.");
        return;
    }
    int targetParent = selected_object_index_;

    int startIdxInObjects = (int)objects.size();

    // Collect all in-use task IDs for unique ID generation (same method as AssignTaskID)
    std::set<int> usedIds;
    for (const auto& obj : objects) {
        if (obj.deleted) continue;
        if (obj.taskId.empty() || obj.taskId == "-1") continue;
        try { usedIds.insert(std::stoi(obj.taskId)); } catch (...) {}
    }

    // AI folder path for QVM file copying
    int levelNo = level_.GetLevelNo();
    std::string aiDir = Utils::GetIGIRootPath() + "\\missions\\location0\\level" + std::to_string(levelNo) + "\\ai";
    
    // Copy all from clipboard to objects
    for (size_t i = 0; i < clipboard_.size(); ++i) {
        LevelObject pasted = clipboard_[i];
        
        // Update indices to point into objects_ vector
        if (pasted.parentIndex == -1) {
            pasted.parentIndex = targetParent;
            if (targetParent != -1) {
                objects[targetParent].childrenIndices.push_back((int)objects.size());
            }
        } else {
            pasted.parentIndex += startIdxInObjects;
        }

        for (size_t j = 0; j < pasted.childrenIndices.size(); ++j) {
            pasted.childrenIndices[j] += startIdxInObjects;
        }

        pasted.modified = true;

        // Generate unique task IDs for AI NPC child tasks
        if (pasted.qscFuncName == "Task_New" &&
            (pasted.type == "HumanSoldier" || pasted.type == "HumanSoldierFemale" || pasted.type == "HumanAI")) {

            std::string oldId = pasted.taskId;

            // Find next available unique ID
            int newId = 1;
            while (usedIds.count(newId)) newId++;
            usedIds.insert(newId);

            std::string newIdStr = std::to_string(newId);
            pasted.taskId = newIdStr;
            if (!pasted.argTokens.empty()) {
                pasted.argTokens[0] = newIdStr;
            }
            pasted.qscLine.clear(); // Force regeneration from argTokens on save

            // For HumanAI: copy the QVM file with the new ID
            if (pasted.type == "HumanAI" && !oldId.empty() && oldId != "-1") {
                std::string srcQvm = aiDir + "\\" + oldId + ".qvm";
                std::string dstQvm = aiDir + "\\" + newIdStr + ".qvm";
                try {
                    if (std::filesystem::exists(srcQvm)) {
                        std::filesystem::create_directories(aiDir);
                        std::filesystem::copy_file(srcQvm, dstQvm, std::filesystem::copy_options::overwrite_existing);
                        Logger::Get().Log(LogLevel::INFO, "[App] Copied AI QVM: " + srcQvm + " -> " + dstQvm);
                    } else {
                        Logger::Get().Log(LogLevel::WARNING, "[App] AI QVM not found for copy: " + srcQvm);
                    }
                } catch (const std::exception& e) {
                    Logger::Get().Log(LogLevel::ERR, "[App] Failed to copy AI QVM: " + std::string(e.what()));
                }
            }

            Logger::Get().Log(LogLevel::INFO, "[App] Assigned unique Task ID " + newIdStr + " to pasted " + pasted.type + " (was " + oldId + ")");
        }

        objects.push_back(pasted);
    }

    selected_object_index_ = startIdxInObjects;
    SaveAndReloadObjects();
    auto& reloaded = level_.GetLevelObjects().GetObjects();
    if (!reloaded.empty()) selected_object_index_ = std::min(selected_object_index_, (int)reloaded.size() - 1);
    
    Logger::Get().Log(LogLevel::INFO, "[App] Pasted task(s) from clipboard");
}

void App::AssignTaskID() {
    if (selected_object_index_ < 0) return;
    auto& objects = level_.GetLevelObjects().GetObjects();

    // Collect all in-use IDs (0..4000 range)
    std::set<int> usedIds;
    for (int i = 0; i < (int)objects.size(); ++i) {
        if (objects[i].deleted) continue;
        if (objects[i].taskId.empty() || objects[i].taskId == "-1") continue;
        try { usedIds.insert(std::stoi(objects[i].taskId)); } catch (...) {}
    }

    // Check if selected already has a valid unique ID
    const std::string& curId = objects[selected_object_index_].taskId;
    if (!curId.empty() && curId != "-1") {
        try {
            int cur = std::stoi(curId);
            int count = (int)std::count_if(objects.begin(), objects.end(), [&](const LevelObject& o){
                if (o.deleted) return false;
                try { return std::stoi(o.taskId) == cur; } catch (...) { return false; }
            });
            if (count > 1) {
                status_message_ = "Error: duplicate Task ID " + curId + " — assigning new unique ID";
            } else {
                status_message_ = "Task ID " + curId + " is already unique";
                return;
            }
        } catch (...) {}
    }

    // Find lowest positive integer not in use
    int newId = 1;
    while (usedIds.count(newId)) newId++;

    objects[selected_object_index_].taskId = std::to_string(newId);
    objects[selected_object_index_].modified = true;
    level_.GetLevelObjects().UpdateCoordinatesInLine(objects[selected_object_index_]);
    SaveAndReloadObjects();
    auto& reloaded = level_.GetLevelObjects().GetObjects();
    if (!reloaded.empty()) selected_object_index_ = std::min(selected_object_index_, (int)reloaded.size() - 1);

    status_message_ = "Assigned unique Task ID: " + std::to_string(newId);
    Logger::Get().Log(LogLevel::INFO, "[App] Assigned unique Task ID: " + std::to_string(newId));
}

void App::ModifyTaskParameters() {
	Logger::Get().Log(LogLevel::INFO, "[App] ModifyTaskParameters (Stub - parameter UI needed)");
}

void App::ClearStatusMessage() {
	status_message_.clear();
}

static int GetLookupObjectIndex(int hoverIdx, int selectedIdx) {
	if (hoverIdx >= 0) return hoverIdx;
	if (selectedIdx >= 0) return selectedIdx;
	return -1;
}

static void SetLookupStatus(std::string& status_message, const std::string& msg) {
	status_message = msg;
	Logger::Get().Log(LogLevel::INFO, msg);
	printf("%s\n", msg.c_str());
}

void App::LookupSelectedModelName() {
	auto& objects = level_.GetLevelObjects().GetObjects();
	int idx = GetLookupObjectIndex(hover_object_index_, selected_object_index_);
	if (idx < 0 || idx >= (int)objects.size()) {
		SetLookupStatus(status_message_, "[App] Model lookup: no hovered/selected object");
		return;
	}

	const auto& obj = objects[idx];
	std::string name = level_.GetLevelObjects().GetModelName(obj.modelId);
	if (name.empty()) {
		SetLookupStatus(status_message_, "[App] Model lookup: no friendly name for model ID " + obj.modelId);
		return;
	}

	SetLookupStatus(status_message_, "[App] Model lookup: " + obj.modelId + " -> " + name);
}

void App::LookupSelectedModelId() {
	auto& objects = level_.GetLevelObjects().GetObjects();
	int idx = GetLookupObjectIndex(hover_object_index_, selected_object_index_);
	if (idx < 0 || idx >= (int)objects.size()) {
		SetLookupStatus(status_message_, "[App] Model lookup: no hovered/selected object");
		return;
	}

	const auto& obj = objects[idx];
	std::string name = level_.GetLevelObjects().GetModelName(obj.modelId);
	if (name.empty()) {
		name = obj.name;
	}
	if (name.empty()) {
		SetLookupStatus(status_message_, "[App] Model lookup: object has no readable model name");
		return;
	}

	std::string modelId = level_.GetLevelObjects().GetModelId(name);
	if (modelId.empty()) {
		SetLookupStatus(status_message_, "[App] Model lookup: no model id for name \"" + name + "\"");
		return;
	}

	SetLookupStatus(status_message_, "[App] Model lookup: " + name + " -> " + modelId);
}

void App::CopySelectedModelName() {
	auto& objects = level_.GetLevelObjects().GetObjects();
	int idx = GetLookupObjectIndex(hover_object_index_, selected_object_index_);
	if (idx < 0 || idx >= (int)objects.size()) {
		SetLookupStatus(status_message_, "[App] Model copy: no hovered/selected object");
		return;
	}

	const auto& obj = objects[idx];
	std::string name = level_.GetLevelObjects().GetModelName(obj.modelId);
	if (name.empty()) {
		SetLookupStatus(status_message_, "[App] Model copy: no friendly name for model ID " + obj.modelId);
		return;
	}

	Utils::SetClipboardText(name);
	SetLookupStatus(status_message_, "[App] Copied model name: " + name);
}

void App::CopySelectedModelId() {
	auto& objects = level_.GetLevelObjects().GetObjects();
	int idx = GetLookupObjectIndex(hover_object_index_, selected_object_index_);
	if (idx < 0 || idx >= (int)objects.size()) {
		SetLookupStatus(status_message_, "[App] Model copy: no hovered/selected object");
		return;
	}

	const auto& obj = objects[idx];
	std::string name = level_.GetLevelObjects().GetModelName(obj.modelId);
	if (name.empty()) {
		name = obj.name;
	}
	if (name.empty()) {
		SetLookupStatus(status_message_, "[App] Model copy: object has no readable model name");
		return;
	}

	std::string modelId = level_.GetLevelObjects().GetModelId(name);
	if (modelId.empty()) {
		SetLookupStatus(status_message_, "[App] Model copy: no model id for name \"" + name + "\"");
		return;
	}

	Utils::SetClipboardText(modelId);
	SetLookupStatus(status_message_, "[App] Copied model id: " + modelId);
}

void App::LookupHoveredModelName() { LookupSelectedModelName(); }
void App::LookupHoveredModelId() { LookupSelectedModelId(); }

struct ModelEntry {
	std::string modelName;
	std::string modelId;
};

static std::vector<ModelEntry> LoadAllModelsFromJson() {
	std::vector<ModelEntry> entries;
	std::string jsonPath = Utils::GetExeDirectory() + "\\content\\tools\\IGIModels.json";

	if (!std::filesystem::exists(jsonPath)) {
		Logger::Get().Log(LogLevel::ERR, "[App] IGIModels.json not found in executable directory: " + jsonPath);
	} else {
		Logger::Get().Log(LogLevel::INFO, "[App] Loading model database from: " + jsonPath);
	}
	
	std::ifstream file(jsonPath, std::ios::binary);
	
	if (!file) {
		Logger::Get().Log(LogLevel::WARNING, "[App] Could not open database file: " + jsonPath);
		return entries;
	}
	
	std::stringstream ss;
	ss << file.rdbuf();
	std::string content = ss.str();
	
	size_t pos = 0;
	while ((pos = content.find("{", pos)) != std::string::npos) {
		size_t end = content.find("}", pos);
		if (end == std::string::npos) break;
		
		std::string entry = content.substr(pos, end - pos + 1);
		pos = end + 1;
		
		auto extractValue = [](const std::string& str, const std::string& key) -> std::string {
			size_t kpos = str.find("\"" + key + "\"");
			if (kpos == std::string::npos) return "";
			size_t colon = str.find(":", kpos);
			if (colon == std::string::npos) return "";
			size_t qStart = str.find("\"", colon);
			if (qStart == std::string::npos) return "";
			size_t qEnd = str.find("\"", qStart + 1);
			if (qEnd == std::string::npos) return "";
			return str.substr(qStart + 1, qEnd - qStart - 1);
		};
		
		ModelEntry item;
		item.modelName = extractValue(entry, "ModelName");
		item.modelId = extractValue(entry, "ModelId");
		
		if (!item.modelId.empty() || !item.modelName.empty()) {
			entries.push_back(item);
		}
	}
	
	return entries;
}

void App::SearchModelById() {
	auto prompt = Utils::PromptForText("Search Model by ID", "Enter Model ID to search in IGIModels.json (e.g. 419_01_1):", "");
	if (!prompt.has_value()) return;

	std::string searchId = prompt.value();
	searchId = Utils::Trim(searchId);
	if (searchId.empty()) return;

	auto entries = LoadAllModelsFromJson();
	std::vector<ModelEntry> matches;
	
	for (const auto& entry : entries) {
		if (containsIgnoreCase(entry.modelId, searchId)) {
			matches.push_back(entry);
		}
	}
	
	std::string resultMessage;
	if (matches.empty()) {
		resultMessage = "No matching models found in IGIModels.json for ID: " + searchId;
	} else {
		resultMessage = "Found " + std::to_string(matches.size()) + " matches in IGIModels.json:\n\n";
		int count = 0;
		for (const auto& match : matches) {
			if (count >= 25) {
				resultMessage += "... and " + std::to_string(matches.size() - count) + " more matches.";
				break;
			}
			resultMessage += "- ID: " + match.modelId + "  ->  Name: " + match.modelName + "\n";
			count++;
		}
	}
	
	MessageBoxA(NULL, resultMessage.c_str(), "IGIModels.json Search Results", MB_OK | MB_ICONINFORMATION);
}

void App::SearchModelByName() {
	auto prompt = Utils::PromptForText("Search Model by Name", "Enter Model Name to search in IGIModels.json (e.g. Soldier):", "");
	if (!prompt.has_value()) return;

	std::string searchName = prompt.value();
	searchName = Utils::Trim(searchName);
	if (searchName.empty()) return;

	auto entries = LoadAllModelsFromJson();
	std::vector<ModelEntry> matches;
	
	for (const auto& entry : entries) {
		if (containsIgnoreCase(entry.modelName, searchName)) {
			matches.push_back(entry);
		}
	}
	
	std::string resultMessage;
	if (matches.empty()) {
		resultMessage = "No matching models found in IGIModels.json for Name: " + searchName;
	} else {
		resultMessage = "Found " + std::to_string(matches.size()) + " matches in IGIModels.json:\n\n";
		int count = 0;
		for (const auto& match : matches) {
			if (count >= 25) {
				resultMessage += "... and " + std::to_string(matches.size() - count) + " more matches.";
				break;
			}
			resultMessage += "- Name: " + match.modelName + "  ->  ID: " + match.modelId + "\n";
			count++;
		}
	}
	
	MessageBoxA(NULL, resultMessage.c_str(), "IGIModels.json Search Results", MB_OK | MB_ICONINFORMATION);
}

std::vector<int> App::GetVisibleTreeNodes() {
    std::vector<int> visibleIndices;
    if (level_.GetLevelObjects().GetObjects().empty()) return visibleIndices;
    
    auto& objects = level_.GetLevelObjects().GetObjects();
    
    std::function<void(int)> traverse = [&](int idx) {
        if (idx < 0 || idx >= (int)objects.size()) return;
        const auto& obj = objects[idx];
        if (obj.deleted) return;
        
        visibleIndices.push_back(idx);
        
        if (obj.expanded) {
            for (int childIdx : obj.childrenIndices) {
                traverse(childIdx);
            }
        }
    };
    
    std::vector<int> root_decls;
    std::vector<int> root_others;
    for (int i = 0; i < (int)objects.size(); ++i) {
        if (objects[i].parentIndex == -1 && !objects[i].deleted) {
            if (objects[i].type == "Task_DeclareParameters") root_decls.push_back(i);
            else root_others.push_back(i);
        }
    }
    
    if (!root_decls.empty()) {
        visibleIndices.push_back(-2); // Virtual "Mission Declarations" folder
        if (tree_decl_expanded_) {
            for (int idx : root_decls) {
                traverse(idx);
            }
        }
    }
    
    for (int idx : root_others) {
        traverse(idx);
    }
    
    return visibleIndices;
}

bool App::IsComputer(const LevelObject& obj) {
	std::string name = obj.name;
	std::string type = obj.type;
	std::string modelId = obj.modelId;
	std::string qscFuncName = obj.qscFuncName;
	std::string friendlyName = level_.GetLevelObjects().GetModelName(obj.modelId);

	auto to_lower = [](std::string s) {
		std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
		return s;
	};

	name = to_lower(name);
	type = to_lower(type);
	modelId = to_lower(modelId);
	qscFuncName = to_lower(qscFuncName);
	friendlyName = to_lower(friendlyName);

	return (name.find("computer") != std::string::npos ||
			type.find("computer") != std::string::npos ||
			modelId.find("computer") != std::string::npos ||
			qscFuncName.find("computer") != std::string::npos ||
			friendlyName.find("computer") != std::string::npos);
}

bool App::IsWaterTower(const LevelObject& obj) {
	std::string name = obj.name;
	std::string type = obj.type;
	std::string modelId = obj.modelId;
	std::string qscFuncName = obj.qscFuncName;
	std::string friendlyName = level_.GetLevelObjects().GetModelName(obj.modelId);

	auto to_lower = [](std::string s) {
		std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
		return s;
	};

	name = to_lower(name);
	type = to_lower(type);
	modelId = to_lower(modelId);
	qscFuncName = to_lower(qscFuncName);
	friendlyName = to_lower(friendlyName);

	return (name.find("watertower") != std::string::npos || name.find("water_tower") != std::string::npos ||
			type.find("watertower") != std::string::npos || type.find("water_tower") != std::string::npos ||
			modelId.find("watertower") != std::string::npos || modelId.find("water_tower") != std::string::npos ||
			qscFuncName.find("watertower") != std::string::npos || qscFuncName.find("water_tower") != std::string::npos ||
			friendlyName.find("watertower") != std::string::npos || friendlyName.find("water_tower") != std::string::npos);
}

bool App::ValidateParentChildCompatibility(const LevelObject& parent, const std::vector<LevelObject>& addedSubtree) {
	if (!IsWaterTower(parent)) {
		return true;
	}
	for (const auto& obj : addedSubtree) {
		if (IsComputer(obj)) {
			return false;
		}
	}
	return true;
}

void App::EvaluateTrainTrackPositions() {
	auto& objects = level_.GetLevelObjects().GetObjects();

	std::map<std::string, int> taskToIdx;
	for (int i = 0; i < (int)objects.size(); ++i) {
		if (!objects[i].taskId.empty())
			taskToIdx[objects[i].taskId] = i;
	}

	struct SplineData {
		std::vector<glm::dvec3> pts;
		std::vector<double> cumDist;
		double totalLen = 0.0;
	};
	std::map<std::string, SplineData> splineCache;

	auto getSpline = [&](const std::string& id) -> const SplineData* {
		auto cached = splineCache.find(id);
		if (cached != splineCache.end()) return &cached->second;
		auto it = taskToIdx.find(id);
		if (it == taskToIdx.end()) return nullptr;
		const auto& spline = objects[it->second];
		if (spline.childrenIndices.size() < 2) return nullptr;
		SplineData sd;
		for (int ci : spline.childrenIndices)
			sd.pts.push_back(objects[ci].pos);
		sd.cumDist.resize(sd.pts.size(), 0.0);
		for (int i = 1; i < (int)sd.pts.size(); ++i)
			sd.cumDist[i] = sd.cumDist[i-1] + glm::length(sd.pts[i] - sd.pts[i-1]);
		sd.totalLen = sd.cumDist.back();
		splineCache[id] = sd;
		return &splineCache[id];
	};

	// Evaluate world position+rotation for a given arc distance on a spline
	auto evalOnSpline = [](const SplineData& sd, double arcLen, glm::dvec3& outPos, glm::dvec3& outRot) {
		double clamped = glm::clamp(arcLen, 0.0, sd.totalLen);
		int seg = 0;
		for (int i = 1; i < (int)sd.cumDist.size(); ++i) {
			if (sd.cumDist[i] >= clamped) { seg = i - 1; break; }
			if (i == (int)sd.cumDist.size() - 1) { seg = i - 1; break; }
		}
		int segNext = std::min(seg + 1, (int)sd.pts.size() - 1);
		double segLen = sd.cumDist[segNext] - sd.cumDist[seg];
		double t = (segLen > 0.0) ? (clamped - sd.cumDist[seg]) / segLen : 0.0;
		outPos = sd.pts[seg] + t * (sd.pts[segNext] - sd.pts[seg]);
		glm::dvec3 fwd = glm::normalize(sd.pts[segNext] - sd.pts[seg]);
		outRot.z = atan2(-fwd.y, -fwd.x); // face opposite to arc direction (cab toward trainyard)
		outRot.x = asin(glm::clamp(-fwd.z, -1.0, 1.0));
		outRot.y = 0.0;
	};

	// Save original rail positions from QSC (obj.pos.x) before modifying obj.pos
	std::vector<double> rawRailPos(objects.size(), 0.0);
	for (int i = 0; i < (int)objects.size(); ++i) {
		if (objects[i].type == "Train" && !objects[i].deleted && !objects[i].splineTaskId.empty())
			rawRailPos[i] = objects[i].pos.x;
	}

	// Computed arc distances (from spline start) per object index — filled in pass 1
	std::map<int, double> arcByObjIdx;

	int evaluated = 0;

	// Pass 1: trains with an explicit non-zero 1D position from QSC.
	// Negative position = distance from the END of the track (game convention):
	//   arcFromStart = totalLen + negativePos
	for (int i = 0; i < (int)objects.size(); ++i) {
		auto& obj = objects[i];
		if (obj.type != "Train" || obj.deleted || obj.splineTaskId.empty()) continue;
		if (rawRailPos[i] == 0.0) continue;

		const SplineData* sd = getSpline(obj.splineTaskId);
		if (!sd || sd->totalLen <= 0.0) continue;

		double rawPos = rawRailPos[i];
		double arcPos = (rawPos < 0.0) ? sd->totalLen + rawPos : rawPos;
		arcPos = glm::clamp(arcPos, 0.0, sd->totalLen);
		arcByObjIdx[i] = arcPos;

		glm::dvec3 newPos, newRot;
		evalOnSpline(*sd, arcPos, newPos, newRot);
		obj.pos = newPos;
		obj.original_pos = newPos;
		obj.rot = newRot;
		obj.original_rot = newRot;
		++evaluated;
	}

	// Pass 2: child wagons with position=0 — place them behind their parent lead car.
	// Wagon length ~52,756 game units (644 local half-extent * 2 * scale 40.96).
	const double WAGON_SPACING = 52756.0;
	std::map<int, int> wagonCountByParent;

	for (int i = 0; i < (int)objects.size(); ++i) {
		auto& obj = objects[i];
		if (obj.type != "Train" || obj.deleted || obj.splineTaskId.empty()) continue;
		if (rawRailPos[i] != 0.0) continue;
		if (obj.parentIndex < 0 || objects[obj.parentIndex].type != "Train") continue;

		auto parentArcIt = arcByObjIdx.find(obj.parentIndex);
		if (parentArcIt == arcByObjIdx.end()) continue;

		const SplineData* sd = getSpline(obj.splineTaskId);
		if (!sd || sd->totalLen <= 0.0) continue;

		int wagonIdx = wagonCountByParent[obj.parentIndex]++;
		// Negative rawRailPos on the parent means the train moves toward arc=0 (Flip=TRUE),
		// so wagons trail behind at HIGHER arc positions (where the train came from).
		double dir = (rawRailPos[obj.parentIndex] < 0.0) ? 1.0 : -1.0;
		double arcPos = glm::clamp(parentArcIt->second + dir * (wagonIdx + 1) * WAGON_SPACING, 0.0, sd->totalLen);
		arcByObjIdx[i] = arcPos;

		glm::dvec3 newPos, newRot;
		evalOnSpline(*sd, arcPos, newPos, newRot);
		obj.pos = newPos;
		obj.original_pos = newPos;
		obj.rot = newRot;
		obj.original_rot = newRot;
		++evaluated;
	}

	if (evaluated > 0)
		Logger::Get().Log(LogLevel::INFO, "[App] Evaluated track positions for " + std::to_string(evaluated) + " train objects.");
}
