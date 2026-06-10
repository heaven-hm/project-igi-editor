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

// BuildRotMatZXY, ExtractEulerZXY → camera/camera.cpp (non-static, global linkage)
// Forward-declare so app.cpp (and object_editor.cpp) can call them.
glm::dmat3 BuildRotMatZXY(const glm::dvec3& euler);
glm::dvec3 ExtractEulerZXY(const glm::dmat3& M);

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
			char title[32];
			snprintf(title, sizeof(title), "Loading Level %d", level_no);
			DrawProgressOverlay(title, pct, stage);
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
	Frame(0.0f);
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

// ProcessInput, UpdateViewerVectors, UpdateViewDefine → camera/camera.cpp

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

// SetTerrainEditEnabled, GetTerrainEditEnabled → editor/terrain_editor.cpp

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

// SetEditBrush, GetEditBrush, TerrainPaletteClick, AdjustBrushRadius,
// AdjustBrushStrength, SetSelectedObjectScale, GetSelectedObjectScale → editor/terrain_editor.cpp

// EditorProcessClick → editor/object_editor.cpp

// CheckCollision → camera/camera.cpp

// SnapObjectsToTerrain → editor/object_editor.cpp
// BuildRotMatZXY, ExtractEulerZXY → camera/camera.cpp (non-static, global linkage)

// UpdateMarkerManipulation, StripQuotes, IsPropFieldMultiline,
// UpdateAIScriptScroll, UpdateAIScriptPathHScroll, LoadAIScriptForSelected,
// CommitPropTextEdit, PushUndoState, SaveAndReloadObjects, RebuildLevelModelIds,
// Undo, Redo, PropagateTransformToChildren, PickObjectAtScreenPos,
// PromoteAttaToObject → editor/object_editor.cpp

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

// GetLookupObjectIndex, SetLookupStatus, LookupSelectedModelName → editor/object_editor.cpp

// LookupSelectedModelId → editor/object_editor.cpp
// LookupSelectedModelId, CopySelectedModelName, CopySelectedModelId,
// LookupHoveredModelName, LookupHoveredModelId, SearchModelById, SearchModelByName,
// GetVisibleTreeNodes, IsComputer, IsWaterTower, ValidateParentChildCompatibility → editor/object_editor.cpp

// EvaluateTrainTrackPositions → camera/camera.cpp
