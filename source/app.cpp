#include "app_internal.h"
#include "renderer/igi_transform.h"
#include "renderer/posed_draw_transform.h"
#include "runtime/door_params.h"
#include "runtime/camera_preview_policy.h"
#include "runtime/config_qvm.h"
#include "runtime/human_player_config.h"
#include "runtime/audio_system.h"
#include "runtime/gameplay_spawn.h"
#include "runtime/log_policy.h"
#include "runtime/pickup_rules.h"
#include "runtime/map_computer_camera.h"
#include "runtime/auto_save_policy.h"
#include "runtime/editor_hover_policy.h"
// The engine's "pose what is drawn" rule for the editor's viewport
// (`Human_WakeIfVisible` 0x45F140) - see the header for the citations.
#include "runtime/anim_pose_set.h"
// The F10 panel's layout, frame-step arithmetic and row text (#73), and the one
// speed ladder the F12 keys and the panel both use.
#include "runtime/anim_panel.h"
#include "runtime/editor_quick_actions.h"
#include "runtime/progress_overlay_policy.h"
#include "runtime/window_style_policy.h"
#include "runtime/viewport_size_policy.h"
#include "mission_flow_loader.h"
#include "mission_objective_loader.h"
#include "mission_state_loader.h"
#include "mission_text_resource.h"
#include <charconv>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>

// GameMonitorParam, GameMonitorProc, and HOTKEY_ID_TOGGLE_GAME live in
// app_internal.h (shared with app_editor.cpp's LaunchGame). The mutable window
// subclass globals below are used only here.

// â”€â”€ Global hotkey support â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
// We subclass GLUT's window so WM_HOTKEY messages reach our code even when
// the editor is iconified and the game has keyboard focus.
// Defined with the other IGI_PROFILE_* measurement helpers further down, and
// declared here because App::OnIdle (above them) uses it to enter Game Play for
// the gameplay animation bench.
static bool ShouldUseGameplayAnimBench();

#if defined(_WIN32)
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
#endif

// Localized mission text (StatusMessage `text`, DefineComputerObjective
// `name`) is resolved by igi::MissionTextResources, which mirrors the engine's
// one global resource list: the key is matched by folded full name across every
// mounted language archive, not against a single archive chosen here. Engine
// citations are in source/mission_text_resource.h.
//
// The resolver's only editor-side addition is a log line when no mounted archive
// defines a key (the engine renders "RM: <key>" silently); it is wired to
// Logger here.
static igi::MissionTextResources MakeMissionTextResources(const std::string& game_root) {
	return igi::MissionTextResources(
		igi::MissionTextArchivePaths(game_root),
		[](LogLevel level, const std::string& message) {
			Logger::Get().Log(level, message);
		});
}

/*
================================================================================
 App
================================================================================
*/

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
	memset(&gameplay_viewer_, 0, sizeof(gameplay_viewer_));
	gameplay_viewer_.clip_to_z_ = false;
	gameplay_viewer_.move_speed_ = MIN_MOVE_SPEED;
	gameplay_viewer_.jump_speed_ = MIN_JUMP_SPEED;
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
	igi::AudioSystem::Initialize(Utils::GetIGIRootPath());

	if (!renderer_.Init()) {
		return false;
	}

	ConfigData& cfg = Config::Get();
	// Native diagnostic sessions need their load-time evidence before the
	// initial level is opened. This changes only the in-memory developer
	// session; the authored qedconfig QSC/QVM remains untouched.
	developer_mode_ = Arg_OptionIdx(argc, argv, "--developer-mode") > -1;
	if (developer_mode_) {
		cfg.enableLogging = true;
		if (cfg.logLevelThreshold > igi::kLogLevelInfo)
			cfg.logLevelThreshold = igi::kLogLevelInfo;
	}

	renderer_.SetLightmapsEnabled(cfg.enableLightmaps);
	renderer_.SetFogEnabled(cfg.enableFog);
	renderer_.SetFogIntensity(cfg.fogIntensity);
	renderer_.SetWeatherEnabled(cfg.weatherEnabled);

	auto_save_enabled_ = cfg.auto_save_enabled;
	auto_save_interval_seconds_ = cfg.auto_save_interval_seconds;
	auto_save_last_time_ms_ = Sys_Milliseconds();

	// read options from command line
	draw_params_.overlay_wireframe_ = Arg_OptionIdx(argc, argv, "-wireframe") > 0;
	draw_params_.draw_parts_ = Arg_ReadInt(argc, argv, "-draw_parts", -1);
	draw_params_.draw_terrain_options_ = Arg_ReadInt(argc, argv, "-draw_terrain_opts", -1);
	// Apply config fog preference to terrain draw options on startup.
	if (!cfg.enableFog)
		draw_params_.draw_terrain_options_ &= ~Renderer_Terrain::DRAW_TERRAIN_OPT_FOG;
	terrain_mod_options_ = Arg_ReadInt(argc, argv, "-terrain_mod_opts", terrain_mod_options_);
	stick_to_ground_ = Arg_OptionIdx(argc, argv, "-stick_to_ground") > 0;

	// The loading overlay is drawn with the editor's viewport, and the reshape
	// callback is only registered AFTER App::Init returns (source/main.cpp), so
	// `window_state_.viewport_*` is still 0 here (the constructor memsets it) and
	// `App::DrawProgressOverlay` drops every frame of the STARTUP load on its
	// `vw <= 0 || vh <= 0` guard -- which is why the retail bar was invisible for
	// the one load every user sees.  Seed the viewport from the window GLUT has
	// already created; the later `-w`/`-h` block below still applies its own values.
	OnWindowResize((std::max)(1, glutGet(GLUT_WINDOW_WIDTH)),
	               (std::max)(1, glutGet(GLUT_WINDOW_HEIGHT)));

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

	// Re-apply the viewport from the window that main.cpp has already created.
	// This used to read -w/-h here with its own defaults (800x600) that disagreed
	// with main.cpp's (1280x720), so with no -w/-h on the command line the
	// viewport described a window size that never existed.  The window is the
	// authority; -w/-h only ever reached it through glutInitWindowSize.
	ApplyClientViewport("startup seed");

	prior_frame_time_ = Sys_Milliseconds();

	bridge_.SetEnabled(show_hud_);
	bridge_.Start();

	if (developer_mode_) {
		debug_cmd_mgr_.Start();
		Logger::Get().Log(LogLevel::INFO, "[App] Developer Mode ON via command line");
	}
	// Set initial cursor state
	LoadAllCursors();
	LoadHelpEntries();
	LoadAutoCompleteKeywords();
	glutSetCursor(GLUT_CURSOR_NONE);

#if defined(_WIN32)
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
		Logger::Get().Log(LogLevel::WARNING, "[App] editor_hwnd_ is NULL â€” global hotkey will not work");
	}
#else
	// macOS: there is no HWND to cache and no window procedure to subclass, so
	// the global toggle hotkey has no delivery path.  The in-window toggle key
	// handled by the keyboard callbacks still works.
	editor_hwnd_ = nullptr;
	Logger::Get().Log(LogLevel::INFO,
	    "[App] macOS: no Win32 HWND; global toggle hotkey disabled");
#endif

#ifdef _WIN32
	// Borderless fullscreen: strip caption/frame/box chrome and size the
	// surface to the monitor. Pause -> Quit stays the supported exit path.
	if (editor_hwnd_) {
		const LONG_PTR style = GetWindowLongPtr(editor_hwnd_, GWL_STYLE);
		SetWindowLongPtr(editor_hwnd_, GWL_STYLE,
		                 static_cast<LONG_PTR>(
		                     igi::BorderlessFullscreenWindowStyle(style)));
		MONITORINFO monitor_info = {sizeof(MONITORINFO)};
		if (GetMonitorInfo(MonitorFromWindow(editor_hwnd_, MONITOR_DEFAULTTONEAREST),
		                   &monitor_info)) {
			SetWindowPos(editor_hwnd_, HWND_TOP,
			             monitor_info.rcMonitor.left, monitor_info.rcMonitor.top,
			             monitor_info.rcMonitor.right - monitor_info.rcMonitor.left,
			             monitor_info.rcMonitor.bottom - monitor_info.rcMonitor.top,
			             SWP_NOOWNERZORDER | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
			window_state_.old_viewport_width_ = window_state_.viewport_width_;
			window_state_.old_viewport_height_ = window_state_.viewport_height_;
			window_state_.full_screen_ = true;
			// The Win32 window is now monitor-sized, but nothing has told GL about it.
			// This path resizes the window directly with SetWindowPos and GLUT does not
			// deliver a reshape for it, so the GL viewport keeps the pre-fullscreen size
			// and the uncovered area is never painted -- reported as "in fullscreen on a
			// small screen the rest of the area is black".  Re-apply the viewport from the
			// window's real client size, exactly as the reshape path would.  The monitor
			// rectangle is only the fallback seed for the (impossible on a borderless
			// popup) case where the client rectangle cannot be queried at all.
			const int fs_w = monitor_info.rcMonitor.right - monitor_info.rcMonitor.left;
			const int fs_h = monitor_info.rcMonitor.bottom - monitor_info.rcMonitor.top;
			editor_viewport_width_ = (std::max)(1, fs_w);
			editor_viewport_height_ = (std::max)(1, fs_h);
			ApplyClientViewport("borderless fullscreen");
			Logger::Get().Log(LogLevel::INFO, "[App] borderless fullscreen: viewport reapplied as " +
				std::to_string(editor_viewport_width_) + "x" + std::to_string(editor_viewport_height_));
			Logger::Get().Log(LogLevel::INFO, "[App] Editor window switched to borderless fullscreen");
		}
	}
#endif

	extern App g_app;
	static auto s_terrain_cb = [](float x, float y) -> float {
		extern App g_app;
		float z = 0.0f;
		g_app.GetLevelZ(x, y, z);
		return z;
	};
	static auto s_collision_cb = [](float x, float y, float z) -> bool {
		extern App g_app;
		return g_app.CheckCollision(glm::vec3(x, y, z));
	};
	gameplay_host_.Initialize(s_terrain_cb, s_collision_cb);
	// Authored mission conditions the expression evaluator cannot parse used to
	// be answered "false" in silence, so an objective that can never be met
	// looked exactly like one that simply has not been met yet (issue #56).
	// Report each distinct one to the log. LogAlways rather than Log: a
	// permanently inert authored condition is a content defect the user has to
	// be able to see even when the log filter is configured off.
	gameplay_host_.GetWorld().SetMissionExpressionLogSink(
		[](LogLevel level, const std::string& message) {
			Logger::Get().LogAlways(level, message);
		});
	gameplay_host_.SetGameplayInputModifier(
		[this](uint64_t, igi::PlayerInputCmd& input_command) {
			player_animation_driver_.AugmentInput(
				gameplay_host_.GetWorld(),
				input_command);
		});
	gameplay_host_.GetWorld().SetInteractionQuery(
		[this](const glm::vec3& origin, const glm::vec3& direction) {
			return HandleGameplayInteraction(origin, direction);
		});
	gameplay_host_.GetWorld().SetPickupTouchQuery(
		[this](const glm::vec3& player_position) {
			ApplyPickupTouches(player_position);
		});

	return true;
}

void App::Shutdown() {
	// main and the global App destructor can both reach shutdown. Teardown must
	// run once so renderer and gameplay resources are not invalidated twice.
	if (shutdown_started_) return;
	shutdown_started_ = true;

	if (igi::ShouldSaveBeforeEditorExit(
			!in_game_mode_, Config::Get().auto_save_enabled, level_.GetLevelNo())) {
		SaveCurrentLevel();
	}

#if defined(_WIN32)
	if (editor_hwnd_) {
		KillTimer(editor_hwnd_, 1);
		UnregisterHotKey(editor_hwnd_, HOTKEY_ID_TOGGLE_GAME);
		if (g_origEditorWndProc) {
			SetWindowLongPtr(editor_hwnd_, GWLP_WNDPROC,
			                 reinterpret_cast<LONG_PTR>(g_origEditorWndProc));
		}
	}
	g_appForHotkey = nullptr;
	g_origEditorWndProc = nullptr;
#endif

	gameplay_host_.SetGameplayInputModifier({});
	player_animation_driver_.ClearAnimationClips();
	if (in_game_mode_) {
		igi::EditorSnapshot restored_snapshot;
		gameplay_host_.CloseGameplay(restored_snapshot);
		in_game_mode_ = false;
		runtime_level_objects_.reset();
		runtime_initial_deleted_flags_.clear();
		runtime_conditionally_hidden_flags_.clear();
		runtime_guard_generator_hidden_flags_.clear();
	}
	gameplay_host_.ShutdownGameplayWindow();
#if defined(_WIN32)
	if (game_process_.running) {
		// Terminate an editor-owned game before tearing down its handles. The
		// monitor thread waits on hProcess, so it must be joined before hProcess
		// is closed or the editor can crash during quit.
		if (game_process_.hProcess) {
			TerminateProcess(game_process_.hProcess, 0);
			WaitForSingleObject(game_process_.hProcess, 2000);
		}
		if (game_process_.hMonitorThread) {
			WaitForSingleObject(game_process_.hMonitorThread, 2000);
			CloseHandle(game_process_.hMonitorThread);
		}
		if (game_process_.hProcess) CloseHandle(game_process_.hProcess);
		if (game_process_.hThread) CloseHandle(game_process_.hThread);
		game_process_ = {};
	}
#endif
	bridge_.Stop();
	StopLevelMusic();
	level_.Unload();
	level_.FreeTerrainCubeDataPools();
	animPlaybacks_.clear();
	gameplayAnimPlaybacks_.clear();
	lastGameplayAnimationTick_ = 0;
	animIdsCache_.clear();
	runtime_animation_request_serials_.clear();
	animRegistry_.Clear();
	renderer_.Shutdown();
	if (!g_isCLIMode) {
		AssetExtractor::CleanupExtractedAssets(Utils::GetExeDirectory());
	}
}

// â”€â”€ C1: Custom SPR cursor â€” multi-mode â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€


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

void App::SetFogEnabled(bool enabled) {
    renderer_.SetFogEnabled(enabled);
}

void App::SetFogIntensity(int intensity) {
    renderer_.SetFogIntensity(intensity);
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
	editor_viewport_width_ = std::max(1, width);
	editor_viewport_height_ = std::max(1, height);
	// Same-window gameplay: the editor and the gameplay surface are one GLUT
	// window (runtime/gameplay_window.cpp), so a reshape of that window is a
	// resize of BOTH.  GLUT never delivers a gameplay reshape -- the gameplay
	// callbacks App::InitializeGameplayWindow is handed are stored nowhere and
	// OnGameplayWindowResize is unreachable -- so if this left the gameplay pair
	// behind, the gameplay render would keep drawing at the old size.
	gameplay_viewport_width_ = editor_viewport_width_;
	gameplay_viewport_height_ = editor_viewport_height_;
	ApplyViewportSize(editor_viewport_width_, editor_viewport_height_, "editor reshape");
}

void App::OnGameplayWindowResize(int width, int height) {
	gameplay_viewport_width_ = std::max(1, width);
	gameplay_viewport_height_ = std::max(1, height);
	// Mirror of the above: the gameplay surface is the editor window, so a
	// gameplay resize has to move the editor pair too.
	editor_viewport_width_ = gameplay_viewport_width_;
	editor_viewport_height_ = gameplay_viewport_height_;
	ApplyViewportSize(gameplay_viewport_width_, gameplay_viewport_height_, "gameplay reshape");
}

// The single authoritative viewport update: the presenting window's CLIENT
// rectangle, applied to both viewport pairs and to window_state_.
//
// Why this exists.  There is only ONE GLUT window: GameplayWindowHost::Create
// aliases gameplay_window_id_ onto editor_window_id_ and discards the gameplay
// callbacks, so "the gameplay window" is the editor window.  There is therefore
// exactly one size that can be correct for the GL viewport, yet the code keeps
// two pairs of it and the gameplay renderer reads the GAMEPLAY pair
// (App::UpdateGameplayViewDefine copies gameplay_viewport_* into
// camera.viewport_width/height) while the pause menu, the UI hit-tests and the
// debug overlay read window_state_.viewport_*, which ApplyViewportSize sets from
// whichever pair called it last.  Any caller that applies a size the window does
// not currently have therefore paints only part of the surface and leaves the
// rest black -- the reported "half screen and half black".
//
// The client rectangle is authoritative rather than glutGet(): the borderless
// fullscreen path (App::Init) resizes the Win32 window with a direct
// SetWindowPos, which never goes through GLUT, so GLUT's own size can lag.
// igi::ResolveViewportExtent encodes the fallback order (client, then GLUT, then
// the last known-good value) and is host-tested in tests/test_editor_policy.cpp.
void App::ApplyClientViewport(const char *reason) {
	int client_w = 0;
	int client_h = 0;
#ifdef _WIN32
	if (editor_hwnd_) {
		RECT client_rect = {0, 0, 0, 0};
		if (GetClientRect(editor_hwnd_, &client_rect)) {
			client_w = client_rect.right - client_rect.left;
			client_h = client_rect.bottom - client_rect.top;
		}
	}
#endif
	const int width = igi::ResolveViewportExtent(
		client_w, glutGet(GLUT_WINDOW_WIDTH), editor_viewport_width_);
	const int height = igi::ResolveViewportExtent(
		client_h, glutGet(GLUT_WINDOW_HEIGHT), editor_viewport_height_);
	editor_viewport_width_ = width;
	editor_viewport_height_ = height;
	gameplay_viewport_width_ = width;
	gameplay_viewport_height_ = height;
	ApplyViewportSize(width, height, reason);
}

void App::ApplyViewportSize(int width, int height, const char *reason) {
	width = std::max(1, width);
	height = std::max(1, height);
	window_state_.viewport_width_ = width;
	window_state_.viewport_height_ = height;

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

	// Opt-in attribution.  Several independent callers write this one state and
	// only the last one is observable afterwards, so a stale viewport is
	// invisible without the caller's name next to the size the window really has.
	// IGI_VIEWPORT_TRACE=1 logs every application; unset (the default) it costs
	// one cached-bool test.
	static const bool trace_viewport = getenv("IGI_VIEWPORT_TRACE") != nullptr;
	if (trace_viewport) {
		std::string detail = "; glut window " +
			std::to_string(glutGet(GLUT_WINDOW_WIDTH)) + "x" +
			std::to_string(glutGet(GLUT_WINDOW_HEIGHT));
#ifdef _WIN32
		RECT client_rect = {0, 0, 0, 0};
		if (editor_hwnd_ && GetClientRect(editor_hwnd_, &client_rect)) {
			detail += "; client " +
				std::to_string(client_rect.right - client_rect.left) + "x" +
				std::to_string(client_rect.bottom - client_rect.top);
		}
#endif
		Logger::Get().Log(LogLevel::INFO,
			"[App] viewport applied " + std::to_string(width) + "x" +
			std::to_string(height) + " from " + (reason ? reason : "unlabelled") +
			detail);
	}
}

void App::RestoreEditorViewport() {
	ApplyClientViewport("restore editor viewport");
}

void App::OnDisplay() {
	// A synchronous progress overlay owns the surface while it presents its
	// frame; a scene repaint dispatched by the overlay's event pump would
	// redraw and swap over the bar before it is ever seen.
	if (igi::ShouldSuppressSceneRepaint({progress_overlay_active_, false})) return;
	// Same-window gameplay: the shared surface renders gameplay whenever the
	// runtime session is active, and the editor scene otherwise.
	if (in_game_mode_ && !rendering_editor_window_) {
		OnGameplayDisplay();
		return;
	}
	// Repaint the editor surface from its authoring state while gameplay owns
	// the other window. The render-only flag prevents this zero-delta repaint
	// from advancing physics, consuming input, or changing runtime objects.
	ApplyClientViewport("editor display");
	rendering_editor_window_ = in_game_mode_;
	Frame(0.0f);
	rendering_editor_window_ = false;
}

bool App::InitializeGameplayWindow(
	int editor_window_id,
	int width,
	int height,
	const igi::GameplayWindowCallbacks& callbacks) {
	// -w/-h is only a headless fallback: the gameplay surface is the editor
	// window, so both viewport pairs have to describe THAT window, not the size
	// the command line asked for (Init may already have gone borderless
	// fullscreen and resized it to the monitor).
	gameplay_viewport_width_ = std::max(1, width);
	gameplay_viewport_height_ = std::max(1, height);
	const bool created = gameplay_host_.InitializeGameplayWindow(
		editor_window_id, width, height, callbacks);
	ApplyClientViewport("gameplay window init");
	return created;
}

void App::ShutdownGameplayWindow() {
	gameplay_host_.ShutdownGameplayWindow();
}

void App::OnGameplayDisplay() {
	if (!in_game_mode_) return;
	// Same-window gameplay fills whatever size the shared window is now; the
	// startup -w/-h values are only a fallback for headless contexts.  The real
	// client size wins over glutGet() because the borderless-fullscreen path
	// resizes the window behind GLUT's back (App::ApplyClientViewport).
	ApplyClientViewport("gameplay display");
	Frame(0.0f);
}

void App::OnGameplayWindowClose() {
    gameplay_host_.NotifyGameplayWindowClosed();
    gameplay_window_close_requested_ = true;
}

void App::FocusGameplayWindow() {
	if (!in_game_mode_) return;

	gameplay_host_.FocusGameplayWindow();
	ApplyClientViewport("gameplay focus");
	glutSetCursor(GLUT_CURSOR_NONE);
	mouse_state_.prior_x_ = gameplay_viewport_width_ >> 1;
	mouse_state_.prior_y_ = gameplay_viewport_height_ >> 1;
	glutWarpPointer(mouse_state_.prior_x_, mouse_state_.prior_y_);
	edit_mode_ = false;
	terrain_edit_enabled_ = false;
	show_hud_ = false;
	status_message_ = "Gameplay focus active (F6 focuses editor)";
}

void App::FocusEditorWindow() {
	if (!in_game_mode_) return;

	gameplay_host_.FocusEditorWindow();
	RestoreEditorViewport();
	glutSetCursor(GLUT_CURSOR_NONE);
	edit_mode_ = true;
	terrain_edit_enabled_ = false;
	show_hud_ = true;
	if (gameplay_editor_snapshot_.has_value()) {
		selected_object_index_ = gameplay_editor_snapshot_->selected_object_id;
	}
	status_message_ = "Editor focus active; press F5 to apply changes and restart gameplay";
}

void App::CaptureEditorSnapshotForGameplayApply() {
	if (!gameplay_editor_snapshot_.has_value()) return;

	// Preserve the original editor-mode flags captured at OpenGameplay while
	// refreshing the authoring camera/selection that may have changed in the
	// editor window during this focused-edit interval.
	gameplay_editor_snapshot_->camera_pos = viewer_.pos_;
	gameplay_editor_snapshot_->camera_yaw = viewer_.yaw_;
	gameplay_editor_snapshot_->camera_pitch = viewer_.pitch_;
	gameplay_editor_snapshot_->selected_object_id = selected_object_index_;
}

// AI text editor helpers â€” must be defined before Input_OnMouse and Input_OnSpecial.

// Returns flat text offsets of each visual line start.
// Lines split on '\n'; lines longer than max_chars wrap to the next visual line.

// input
void App::OnIdle() {
	if (gameplay_window_close_requested_) {
		gameplay_window_close_requested_ = false;
		if (in_game_mode_) ToggleGamePlayMode();
	}

	// freeglut pumps messages with a window-handle filter and misses WM_HOTKEY,
	// which is a thread message only retrievable via PeekMessage(NULL, ...).
	// Poll it here so F3 works while the game is running and editor is iconified.
#if defined(_WIN32)
	if (game_process_.running) {
		MSG msg = {};
		while (PeekMessage(&msg, NULL, WM_HOTKEY, WM_HOTKEY, PM_REMOVE)) {
			if (static_cast<int>(msg.wParam) == HOTKEY_ID_TOGGLE_GAME) {
				Logger::Get().Log(LogLevel::INFO, "[ToggleGame] Global hotkey received â€” stopping game");
				LaunchGame();
			}
		}
	}
#endif

	// Check game exit before the frame-rate throttle so it fires on every call,
	// even when GLUT is running slowly while the editor is iconified.
#if defined(_WIN32)
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
			// The monitor thread is still waiting on hProcess. Join it before
			// closing that handle; closing a handle while another thread waits on
			// it is undefined and caused intermittent editor access violations.
			if (game_process_.hMonitorThread) {
				WaitForSingleObject(game_process_.hMonitorThread, 1000);
				CloseHandle(game_process_.hMonitorThread);
			}
			if (game_process_.hProcess) CloseHandle(game_process_.hProcess);
			if (game_process_.hThread) CloseHandle(game_process_.hThread);
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
			Logger::Get().Log(LogLevel::INFO, "[ToggleGame] Global hotkey unregistered â€” editor restored");
			if (Config::Get().musicEnabled) PlayLevelMusic(level_.GetLevelNo());
			return;
		}
	}
#endif

	// While the game is running the editor window is iconified.
	// glutSwapBuffers() deadlocks on minimized windows, so skip rendering entirely.
	if (game_process_.running) return;

	int64_t cur_time = Sys_Milliseconds();
	int64_t delta_time = cur_time - prior_frame_time_;
	if (delta_time < 16) {
		return;
	}

	const bool profile_frame = []() {
		static const bool enabled = getenv("IGI_PROFILE") != nullptr;
		return enabled;
	}();
	const int64_t frame_start = profile_frame ? Sys_Milliseconds() : 0;

	// IGI_PROFILE_GAMEPLAYANIMBENCH-only: see ShouldUseGameplayAnimBench. Enter
	// Game Play once a level is loaded, through the F8 handler's own function, so
	// the run measures the gameplay animation pass instead of never reaching it.
	if (ShouldUseGameplayAnimBench() && !in_game_mode_ && !gameplay_anim_bench_entered_ &&
	    level_.GetLevelNo() > 0) {
		gameplay_anim_bench_entered_ = true;
		Logger::Get().Log(LogLevel::INFO,
			"[GameplayAnimBench] entering Game Play for measurement (level " +
			std::to_string(level_.GetLevelNo()) + ")");
		ToggleGamePlayMode();
	}

	if (in_game_mode_ && IsGameplayInputFocused()) {
		gameplay_host_.GetInputRouter().SetMapComputerOpen(
			gameplay_host_.GetWorld().IsMapComputerOpen());
		gameplay_host_.MakeGameplayWindowCurrent();
		// Track the live shared-window size so gameplay always fills the
		// window (resize/maximize during a run included).
		ApplyClientViewport("gameplay idle");
		Frame(delta_time * 0.001f); // convert to seconds
		gameplay_host_.GetInputRouter().SetMapComputerOpen(
			gameplay_host_.GetWorld().IsMapComputerOpen());
	} else if (in_game_mode_) {
		// The editor may own OS focus while the runtime remains active. Advance
		// the fixed-step session explicitly, then render only the authoring
		// window so editor input and painting do not become gameplay updates.
		gameplay_host_.GetInputRouter().SetMapComputerOpen(
			gameplay_host_.GetWorld().IsMapComputerOpen());
		gameplay_host_.Update(cur_time);
		gameplay_host_.GetInputRouter().SetMapComputerOpen(
			gameplay_host_.GetWorld().IsMapComputerOpen());
		ApplyClientViewport("editor idle while gameplay active");
		rendering_editor_window_ = true;
		Frame(delta_time * 0.001f); // editor camera and authoring tools still tick
		rendering_editor_window_ = false;
	} else {
		Frame(delta_time * 0.001f); // convert to seconds
	}

	if (profile_frame) {
		static int64_t acc_ms = 0;
		static int acc_frames = 0;
		static int64_t acc_wall = 0;
		static int64_t last_report = cur_time;
		acc_ms += Sys_Milliseconds() - frame_start;
		++acc_frames;
		if (cur_time - last_report >= 1000) {
			acc_wall += cur_time - last_report;
			Logger::Get().Log(LogLevel::INFO,
				"[FrameProfile] " + std::to_string(acc_frames) + " frames in " +
				std::to_string(acc_wall) + "ms wall; busy=" +
				std::to_string(acc_ms) + "ms (" +
				std::to_string(acc_frames ? acc_ms / acc_frames : 0) + "ms/frame)");
			acc_ms = acc_frames = acc_wall = 0;
			last_report = cur_time;
		}
	}

	prior_frame_time_ = cur_time;
}

// IGI_PROFILE-only: attribute App::Frame() to the work before renderer_.Draw(),
// the Draw call itself, and everything after it (gameplay attachment draws,
// cursor, glutSwapBuffers). [FrameProfile] measures Frame() as a whole and
// [RenderProfile] measures the inside of Draw, so this closes the gap between
// them. Compiled unconditionally but only ever called when IGI_PROFILE is set.
static void LogFrameBreak(const char *tag, int64_t pre_us, int64_t draw_us,
                          int64_t post_us) {
	static int64_t acc_pre = 0, acc_draw = 0, acc_post = 0;
	static int frames = 0;
	acc_pre += pre_us;
	acc_draw += draw_us;
	acc_post += post_us;
	if (++frames >= 30) {
		Logger::Get().Log(LogLevel::INFO,
			std::string("[FrameBreak] ") + tag + " 30 frames: pre=" +
			std::to_string(acc_pre / 30000) + "ms draw=" +
			std::to_string(acc_draw / 30000) + "ms post=" +
			std::to_string(acc_post / 30000) + "ms/frame");
		frames = 0;
		acc_pre = acc_draw = acc_post = 0;
	}
}

// IGI_PROFILE-only: split the pre-render phase of App::Frame() (the part that
// [FrameBreak] reports as `pre`) into its four candidate contributors, so a
// regression can be attributed without re-instrumenting.
static void LogFrameBreakPreSplit(int64_t update_us, int64_t level_us,
                                  int64_t skinui_us, int64_t params_us) {
	static int64_t acc_update = 0, acc_level = 0, acc_skinui = 0, acc_params = 0;
	static int frames = 0;
	acc_update += update_us;
	acc_level += level_us;
	acc_skinui += skinui_us;
	acc_params += params_us;
	if (++frames >= 30) {
		Logger::Get().Log(LogLevel::INFO,
			"[FrameBreak] pre split 30 frames: update=" +
			std::to_string(acc_update / 30000) + "ms level=" +
			std::to_string(acc_level / 30000) + "ms skinui=" +
			std::to_string(acc_skinui / 30000) + "ms params=" +
			std::to_string(acc_params / 30000) + "ms/frame");
		frames = 0;
		acc_update = acc_level = acc_skinui = acc_params = 0;
	}
}

// IGI_PROFILE-only: split the `level` segment of the pre-render phase into the
// three GL buffer mappings and the engine's own Level::Update().
static void LogFrameBreakPreSplitIo(int64_t map_us, int64_t level_us,
                                    int64_t unmap_us) {
	static int64_t acc_map = 0, acc_level = 0, acc_unmap = 0;
	static int frames = 0;
	acc_map += map_us;
	acc_level += level_us;
	acc_unmap += unmap_us;
	if (++frames >= 30) {
		Logger::Get().Log(LogLevel::INFO,
			"[FrameBreak] pre io split 30 frames: map=" +
			std::to_string(acc_map / 30000) + "ms level=" +
			std::to_string(acc_level / 30000) + "ms unmap=" +
			std::to_string(acc_unmap / 30000) + "ms/frame");
		frames = 0;
		acc_map = acc_level = acc_unmap = 0;
	}
}

// IGI_PROFILE-only: the `pre` span of the PAUSED frame.  [FrameBreak] reports
// this whole span as `pre`, but the paused early-return path shares none of the
// work the editor split covers (no UpdateAnimations, no buffer maps, no
// Level::Update), so it needs its own attribution instead of being assumed to
// mirror the editor path.
static void LogFrameBreakPausedSplit(int64_t head_us, int64_t view_us,
                                     int64_t pick_us, int64_t ui_us,
                                     int64_t params_us) {
	static int64_t acc_head = 0, acc_view = 0, acc_pick = 0, acc_ui = 0, acc_params = 0;
	static int frames = 0;
	acc_head += head_us;
	acc_view += view_us;
	acc_pick += pick_us;
	acc_ui += ui_us;
	acc_params += params_us;
	if (++frames >= 30) {
		Logger::Get().Log(LogLevel::INFO,
			"[FrameBreak] paused split 30 frames: head=" +
			std::to_string(acc_head / 30000) + "ms viewdefine=" +
			std::to_string(acc_view / 30000) + "ms pick=" +
			std::to_string(acc_pick / 30000) + "ms ui=" +
			std::to_string(acc_ui / 30000) + "ms params=" +
			std::to_string(acc_params / 30000) + "ms/frame");
		frames = 0;
		acc_head = acc_view = acc_pick = acc_ui = acc_params = 0;
	}
}

// IGI_PROFILE_PICKBENCH-only: the hover pick is driven by real pointer motion,
// so a Wine smoke run issues a variable number of them -- frequently zero, and
// never the same positions twice.  That makes the pick's per-call cost
// unmeasurable by frame attribution alone (a run can sit at "calls=0/30" for a
// whole minute), and an unmeasurable change cannot be shown to be a win.  With
// IGI_PROFILE_PICKBENCH set, App::Frame issues a fixed set of picks at fixed
// screen positions every frame, so two builds are compared on identical work
// whatever the mouse did.  It is a separate flag from IGI_PROFILE on purpose:
// it adds a known constant to every frame, so it must not be on while the
// [FrameProfile]/[FrameBreak] attribution is being read.
static constexpr int kPickBenchPicks = 4;

static void LogPickBench(int64_t bench_us, const std::string& ids) {
	static int64_t acc_us = 0;
	static int frames = 0;
	acc_us += bench_us;
	if (++frames >= 30) {
		Logger::Get().Log(LogLevel::INFO,
			"[PickBench] 30 frames: " + std::to_string(kPickBenchPicks) +
			" picks/frame avg=" + std::to_string(acc_us / 30000) + "ms/frame (" +
			std::to_string(acc_us / (30 * kPickBenchPicks) / 1000) + "ms/pick) ids=" +
			ids);
		frames = 0;
		acc_us = 0;
	}
}

// IGI_PROFILE_ANIMBENCH-only: with IGI_PROFILE_ANIMBENCH set, App::Frame forces
// `selected_object_index_` to the lowest-indexed object that has an active,
// posable, skinned playback.  The editor's default posed set is now every AI the
// viewport draws (App::GetEditorDrawnAnimationIndices), so this flag's job is to
// pin down the OTHER workload for comparison - the same run with exactly one
// object selected, which the pose rule exempts from the view and LOD culls.  It
// goes through the real selection, so the frame it produces is a real editor
// frame.  Like IGI_PROFILE_PICKBENCH it adds a known constant to every frame, so
// it must not be on while the [FrameProfile]/[FrameBreak] attribution is read.
static bool ShouldUseAnimBench() {
	static const bool enabled = getenv("IGI_PROFILE_ANIMBENCH") != nullptr;
	return enabled;
}

// IGI_PROFILE_GAMEPLAYANIMBENCH-only: the plain smoke run never enters Game Play
// (F8 / the pause menu's `Game Play` row are the only ways in, and the run's
// input is a physical mouse), so `gameplayAnimPlaybacks_` stays empty and the
// gameplay animation pass - the one driven by the fixed SIMULATION clock and the
// 80 m gameplay draw filter - is completely unmeasured there.  With
// IGI_PROFILE_GAMEPLAYANIMBENCH set, App::OnIdle enters Game Play once a level is
// loaded, through the same `ToggleGamePlayMode()` the F8 handler calls
// (src/app_input_keyboard.cpp), so the frames that follow are REAL gameplay
// frames: real guards, real animation requests, real simulation ticks.  It adds
// nothing to the frame once you are in gameplay; it only automates the F8 press.
// Like IGI_PROFILE_ANIMBENCH it is a separate flag from IGI_PROFILE's own
// attribution and must not be on while [FrameProfile]/[FrameBreak] are read.
static bool ShouldUseGameplayAnimBench() {
	static const bool enabled = getenv("IGI_PROFILE_GAMEPLAYANIMBENCH") != nullptr;
	return enabled;
}

// IGI_PROFILE-only: the scene hover pick is a FULL scene colour pass into the
// pick FBO plus a synchronous glReadPixels, so its call RATE matters as much as
// its per-call cost: a pick issued once per frame costs a whole extra scene
// render. The 33 ms hover throttle cannot prevent that once the frame time
// itself exceeds 33 ms, so count the calls instead of assuming the throttle.
static void LogPickProfile(int64_t pick_us, bool called) {
	static int64_t acc_us = 0;
	static int calls = 0;
	static int frames = 0;
	acc_us += pick_us;
	if (called) ++calls;
	if (++frames >= 30) {
		Logger::Get().Log(LogLevel::INFO,
			"[FrameBreak] pick profile 30 frames: calls=" +
			std::to_string(calls) + "/30 time=" +
			std::to_string(acc_us / 30000) + "ms/frame" +
			(calls ? " (" + std::to_string(acc_us / calls / 1000) + "ms/call)" : ""));
		frames = 0;
		acc_us = 0;
		calls = 0;
	}
}

// IGI_PROFILE-only: split the `pick+maps` span of the editor path into the
// scene hover pick and each of the three per-frame GL buffer mappings, because
// the aggregate cannot say whether the cost is the pick or the maps.
static void LogFrameBreakMapSplit(int64_t pick_us, int64_t mapsky_us,
                                  int64_t mapvb_us, int64_t mapib_us) {
	static int64_t acc_pick = 0, acc_sky = 0, acc_vb = 0, acc_ib = 0;
	static int frames = 0;
	acc_pick += pick_us;
	acc_sky += mapsky_us;
	acc_vb += mapvb_us;
	acc_ib += mapib_us;
	if (++frames >= 30) {
		Logger::Get().Log(LogLevel::INFO,
			"[FrameBreak] pick/map split 30 frames: pick=" +
			std::to_string(acc_pick / 30000) + "ms mapsky=" +
			std::to_string(acc_sky / 30000) + "ms mapvb=" +
			std::to_string(acc_vb / 30000) + "ms mapib=" +
			std::to_string(acc_ib / 30000) + "ms/frame");
		frames = 0;
		acc_pick = acc_sky = acc_vb = acc_ib = 0;
	}
}

// IGI_PROFILE-only: split the editor's own per-frame work that happens before
// the renderer is called at all (animations, view define, scene hover pick).
// [FrameBreak] reports this whole span as part of `pre`; the terrain buffer
// map/upload calls that live in the same span measure 0 ms, so this is where
// the cost actually is.
static void LogFrameBreakPreSplitEditor(int64_t anim_us, int64_t view_us,
                                        int64_t pick_us, int64_t level_us) {
	static int64_t acc_anim = 0, acc_view = 0, acc_pick = 0, acc_level = 0;
	static int frames = 0;
	acc_anim += anim_us;
	acc_view += view_us;
	acc_pick += pick_us;
	acc_level += level_us;
	if (++frames >= 30) {
		Logger::Get().Log(LogLevel::INFO,
			"[FrameBreak] editor split 30 frames: anim+music=" +
			std::to_string(acc_anim / 30000) + "ms viewdefine=" +
			std::to_string(acc_view / 30000) + "ms pick+maps=" +
			std::to_string(acc_pick / 30000) + "ms levelupdate=" +
			std::to_string(acc_level / 30000) + "ms/frame");
		frames = 0;
		acc_anim = acc_view = acc_pick = acc_level = 0;
	}
}

void App::Frame(float delta_seconds) {
	const bool render_gameplay = IsGameplayRenderTarget();
	// Per-frame attribution around renderer_.Draw() (IGI_PROFILE only).
	const bool profile_frame_break = []() {
		static const bool enabled = getenv("IGI_PROFILE") != nullptr;
		return enabled;
	}();
	using FrameBreakClock = std::chrono::steady_clock;
	const auto frame_enter = profile_frame_break ? FrameBreakClock::now()
	                                             : FrameBreakClock::time_point{};
	const auto frame_us = [](FrameBreakClock::time_point a, FrameBreakClock::time_point b) {
		return std::chrono::duration_cast<std::chrono::microseconds>(b - a).count();
	};
	// IGI_PROFILE_PICKBENCH-only: see LogPickBench.  Runs once per frame, before
	// either branch, so the same fixed pick work is done whether the editor is
	// paused or not.
	if ([]() {
			static const bool enabled = getenv("IGI_PROFILE_PICKBENCH") != nullptr;
			return enabled;
		}()) {
		const int bench_w = window_state_.viewport_width_;
		const int bench_h = window_state_.viewport_height_;
		const auto bench_t0 = FrameBreakClock::now();
		std::string bench_ids;
		for (int k = 0; k < kPickBenchPicks; ++k) {
			const int px = bench_w * (k + 1) / (kPickBenchPicks + 1);
			const int hit = renderer_.PickObjectAtScreen(
				px, bench_h / 2, bench_w, bench_h, view_define_,
				level_.GetLevelObjects().GetObjects(),
				Renderer::DRAW_OBJECTS | Renderer::DRAW_BUILDINGS | Renderer::DRAW_PROPS,
				selected_object_index_);
			// The returned id is the whole contract of this pass: logging it makes
			// the benchmark a differential test of any change to the pick, not
			// just a timing.
			bench_ids += (k ? "," : "") + std::to_string(hit);
		}
		LogPickBench(frame_us(bench_t0, FrameBreakClock::now()), bench_ids);
	}
	const igi::RuntimeRenderSnapshot& render_snapshot =
		gameplay_host_.GetRenderSnapshot();
	if (developer_mode_ && (!rendering_editor_window_ || IsEditorInputActive())) {
		debug_cmd_mgr_.Update();
	}
	// Config::Init() can be requested at runtime. Keep the timer's cached
	// values synchronized so a disabled menu/config state cannot leave the old
	// enabled state running for the remainder of the session.
	ConfigData& runtime_cfg = Config::Get();
	if (auto_save_enabled_ != runtime_cfg.auto_save_enabled ||
		auto_save_interval_seconds_ != runtime_cfg.auto_save_interval_seconds) {
		auto_save_enabled_ = runtime_cfg.auto_save_enabled;
		auto_save_interval_seconds_ = runtime_cfg.auto_save_interval_seconds;
		auto_save_last_time_ms_ = Sys_Milliseconds();
	}
	// Auto-save timer
	if (igi::ShouldRunAutoSave(!in_game_mode_, runtime_cfg.auto_save_enabled,
			pause_mode_, level_.GetLevelNo())) {
		int64_t now = Sys_Milliseconds();
		if (now - auto_save_last_time_ms_ >=
				(int64_t)(std::max)(1, runtime_cfg.auto_save_interval_seconds) * 1000) {
			auto_save_last_time_ms_ = now;
			SaveCurrentLevel();
			status_message_ = "Auto-saved level " + std::to_string(level_.GetLevelNo());
		}
	}

	if (pause_mode_ && !rendering_editor_window_) {
		// IGI_PROFILE: boundary markers for the paused-path pre-render split.
		const auto paused_t_head = profile_frame_break ? FrameBreakClock::now()
		                                               : FrameBreakClock::time_point{};
		// Skip all updates when paused, just render
		if (render_gameplay) UpdateGameplayViewDefine();
		else UpdateViewDefine();
		if (render_gameplay) CaptureGameplayRenderSnapshot();
		const auto paused_t_view = profile_frame_break ? FrameBreakClock::now()
		                                               : FrameBreakClock::time_point{};
		int64_t paused_pick_us = 0;
		bool paused_pick_called = false;
		if (igi::ShouldPickSceneHover(
			mouse_state_.prior_x_ != last_pick_x_ || mouse_state_.prior_y_ != last_pick_y_,
			mouse_state_.left_button_down_, Sys_Milliseconds(), last_hover_pick_ms_, 33,
			Utils::IsKeyBindingPressed(Config::Get().keyEnableCamera))) {
			const auto paused_t_pick = profile_frame_break ? FrameBreakClock::now()
			                                               : FrameBreakClock::time_point{};
			hover_object_index_ = PickObjectAtScreenPos(mouse_state_.prior_x_, mouse_state_.prior_y_);
			if (profile_frame_break) {
				paused_pick_us = frame_us(paused_t_pick, FrameBreakClock::now());
				paused_pick_called = true;
			}
			if (hover_object_index_ >= Renderer::kAttaPickBase) hover_object_index_ = -1; // ATTA hovered (clickable; promote on click)
			last_pick_x_ = mouse_state_.prior_x_;
			last_pick_y_ = mouse_state_.prior_y_;
		}
		float ground_z = 0.0f;
		const viewer_s& active_viewer = render_gameplay ? gameplay_viewer_ : viewer_;
		level_.GetTerrainZ(active_viewer.pos_.x, active_viewer.pos_.y, ground_z);
		int propAnimBoneHierarchy; std::vector<int> propAnimIds; int propAnimActiveId; bool propAnimIsPlaying;
		ComputePropAnimUiState(propAnimBoneHierarchy, propAnimIds, propAnimActiveId, propAnimIsPlaying);
		const auto paused_t_ui = profile_frame_break ? FrameBreakClock::now()
		                                             : FrameBreakClock::time_point{};
		Renderer::task_tree_view_params_s task_tree_view = {
			.show_hud_ = show_hud_,
			.hud_overlay_visible_ = hud_overlay_visible_,
			.status_msg_ = status_message_,
			.pause_mode_ = true,
			.pause_active_input_ = pause_active_input_,
			.pause_level_input_ = pause_level_input_,
			.pause_search_input_ = pause_search_input_,
		.pause_terrain_expanded_ = pause_terrain_expanded_,
			.terrain_draw_options_ = GetTerrainDrawOptions(),
			.show_debug_ = show_debug_,
			.show_help_ = show_help_,
			.edit_mode_ = edit_mode_,
			.terrain_edit_enabled_ = terrain_edit_enabled_,
			.terrain_mod_options_ = terrain_mod_options_,
			.selected_object_index_ = selected_object_index_,
			.hover_object_index_ = hover_object_index_,
			.hover_tree_index_ = hover_tree_index_,
			.mouse_x_ = mouse_state_.prior_x_,
			.mouse_y_ = mouse_state_.prior_y_,
			.tree_scroll_offset = tree_scroll_offset_,
			.tree_decl_expanded = tree_decl_expanded_,
			.level_objects_ = &GetActiveRenderLevelObjects(),
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
			.prop_text_sel_anchor_ = prop_text_sel_anchor_,
			.prop_text_sel_focus_  = prop_text_sel_focus_,
			.prop_panel_scroll_    = prop_panel_scroll_,
			.find_open_            = find_open_,
			.find_query_           = find_query_,
			.find_result_idx_      = find_result_idx_,
			.selected_obj_is_ai    = (selected_object_index_ >= 0 &&
				selected_object_index_ < (int)level_.GetLevelObjects().GetObjects().size() &&
				(ai_model_ids_.count(level_.GetLevelObjects().GetObjects()[selected_object_index_].modelId) > 0 ||
				 [&]() {
					const std::string& t = level_.GetLevelObjects().GetObjects()[selected_object_index_].type;
					return t == "HumanSoldier" || t == "HumanSoldierFemale" ||
					       t == "HumanPlayer" || t == "HumanSoldierRPG" || t == "HumanAI";
				 }())),
			.in_game_mode_         = render_gameplay,
			.noclip_mode_          = noclip_mode_,
			.player_health_        = render_snapshot.player_health,
			.player_maximum_health_ = render_snapshot.player_maximum_health,
			.player_armor_         = render_snapshot.player_armor,
			.player_maximum_armor_ = render_snapshot.player_maximum_armor,
			.active_weapon_name_   = render_snapshot.active_weapon_name,
			.clip_ammo_            = render_snapshot.clip_ammo,
			.clip_capacity_        = render_snapshot.clip_capacity,
			.reserve_ammo_         = render_snapshot.reserve_ammo,
			.map_computer_open_    = render_snapshot.map_computer_open,
			.map_computer_objectives_ = render_snapshot.map_computer_objectives,
			.objective_text_       = render_snapshot.objective_text,
			.mission_timer_remaining_ticks_ = render_snapshot.mission_timer_remaining_ticks,
			.mission_status_messages_ = render_snapshot.mission_status_messages,
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
			.model_entries_        = &level_model_entries_,
			.ai_script_path_       = ai_script_path_,
			.ai_script_text_       = ai_script_text_,
			.ai_script_dirty_      = ai_script_dirty_,
			.terrain_brush_          = edit_brush_,
			.terrain_brush_radius_   = edit_brush_radius_,
			.terrain_brush_strength_ = edit_brush_strength_,
			.auto_save_enabled_        = auto_save_enabled_,
			.auto_save_interval_seconds_ = auto_save_interval_seconds_,
			.logging_enabled_ = Config::Get().enableLogging,
			.log_level_threshold_ = Config::Get().logLevelThreshold,
			.music_on_ = music_playing_,
			.lightmaps_on_ = Config::Get().enableLightmaps,
			.anim_status_  = draw_params_.fast_scene_preview_ ? std::string() : BuildAnimStatusString(),
			.anim_playing_ = !animPlaybacks_.empty(),
			.anim_debug_visible_ = show_anim_debug_,
			.anim_panel_playing_ = SelectedAnimPlaying(),
			.anim_panel_title_ = AnimPanelTitleText(),
			.anim_panel_has_target_ = SelectedAnimPlayback() != nullptr,
			.prop_anim_bone_hierarchy_ = propAnimBoneHierarchy,
			.prop_anim_ids_ = propAnimIds,
			.prop_anim_active_id_ = propAnimActiveId,
			.prop_anim_is_playing_ = propAnimIsPlaying,
			.pause_menu_model_ = &pause_menu_.model,
			.pause_menu_layout_ = PauseMenuLayoutNow(),
		};
		draw_params_.level_objects_ = &GetActiveRenderLevelObjects();
		draw_params_.selected_object_index_ = selected_object_index_;
		draw_params_.show_magic_obj_spheres_ = show_magic_obj_spheres_;
		// Paused: the pause menu (a 2D overlay drawn at the end of renderer_.Draw)
		// must sit in front of the scene, so do NOT draw the live skinned mesh after
		// it (that painted the character on top of the menu). Instead keep the
		// object's normal static mesh in the 3D pass â€” animation isn't advancing
		// while paused anyway â€” by not skipping it here.
		draw_params_.skip_static_draw_indices_ = nullptr;
		draw_params_.terrain_id_at_world_xy_ =
			[this](double x, double y) { return level_.GetTerrainNodeId(x, y); };
		draw_params_.terrain_z_at_world_xy_ =
			[this](double x, double y, float& z) { return level_.GetTerrainZ(x, y, z); };
		const auto frame_draw_begin = profile_frame_break ? FrameBreakClock::now()
		                                                  : FrameBreakClock::time_point{};
	renderer_.Draw(draw_params_, task_tree_view);
		const auto frame_draw_end = profile_frame_break ? FrameBreakClock::now()
		                                                : FrameBreakClock::time_point{};

	DrawCustomCursor();
	glutSwapBuffers();
	if (profile_frame_break) {
		LogFrameBreak("paused", frame_us(frame_enter, frame_draw_begin),
			frame_us(frame_draw_begin, frame_draw_end),
			frame_us(frame_draw_end, FrameBreakClock::now()));
		LogFrameBreakPausedSplit(frame_us(frame_enter, paused_t_head),
		                         frame_us(paused_t_head, paused_t_view),
		                         frame_us(paused_t_view, paused_t_ui),
		                         0,
		                         frame_us(paused_t_ui, frame_draw_begin));
		LogPickProfile(paused_pick_us, paused_pick_called);
	}
	return;
    }

	frame_++;
	frame_ %= 0xFFFFFFFF;	// reserve value 0xFFFFFFFF (-1) for INVALID_FRAME

	// Feed per-frame delta to renderer for continuous animations (rotor spin etc).
	// This is the minimal glue required so the committed rotor fix keeps working
	// after overlaying the exact fog implementation from v3.6.0-pre.
	extern float g_renderer_delta_secs;
	g_renderer_delta_secs = delta_seconds;

	// The objects whose live skinned pose the renderer submits this frame
	// (`skinnedReplacementIndices`). It must outlive renderer_.Draw(), so it is a
	// local of this Frame() call, not a temporary. In GAMEPLAY mode it is also the
	// answer to "which playbacks need a pose this frame", so it is declared here
	// and filled before the gameplay animation step - see the gameplay block below
	// and App::GetGameplayDrawnAnimationIndices.
	std::unordered_set<int> skinnedReplacementIndices;

	if (render_gameplay) {
		int64_t now_ms = Sys_Milliseconds();
		const bool profile_frames = []() {
			static const bool enabled = getenv("IGI_PROFILE") != nullptr;
			return enabled;
		}();
		const int64_t profile_frame_start = profile_frames ? Sys_Milliseconds() : 0;

		// OnIdle owns the simulation update. Display callbacks may run once per
		// window, so a zero render delta must never advance gameplay a second time.
		if (delta_seconds > 0.0f) gameplay_host_.Update(now_ms);
		const int64_t profile_after_sim = profile_frames ? Sys_Milliseconds() : 0;
		ApplyRuntimeConditionalContainerStates();
		ApplyRuntimeDoorStates();
		ApplyRuntimeExplodeObjectStates();

		const auto& player = gameplay_host_.GetWorld().GetPlayer();
		gameplay_viewer_.pos_ = player.GetEyePosition();
		gameplay_viewer_.yaw_ = player.GetYaw();
		gameplay_viewer_.pitch_ = player.GetPitch();
		UpdateGameplayViewerVectors();
		ApplyRuntimeCutSceneCamera();
		UpdateGameplayMapComputerCamera(delta_seconds);
		// Publish the simulation-owned guard transforms before synchronizing the
		// mutable render copy. Gameplay presentation must not iterate AI storage
		// while the fixed-step world can be replaced or restarted.
		CaptureGameplayRenderSnapshot();

		// Update the session-owned render copy. Authoring objects remain immutable
		// while gameplay is running and are restored by dropping this copy.
		const auto& guards = render_snapshot.guards;
		auto& objects = GetActiveRenderLevelObjects().GetObjects();
		for (const auto& guard : guards) {
			if (guard.guard_id < objects.size()) {
				const size_t guard_object_index = static_cast<size_t>(guard.guard_id);
				objects[guard_object_index].deleted =
					guard.state == igi::AiGuardState::Dead ||
					!guard.runtime_enabled;
				if (objects[guard_object_index].deleted) {
					continue;
				}
				objects[guard_object_index].pos = glm::dvec3(
					guard.position.x,
					guard.position.y,
					guard.position.z);
				objects[guard_object_index].rot.z = glm::radians(
					static_cast<double>(guard.yaw));
			}
		}
		ApplyRuntimeGuardGeneratorStates();
		// ── the gameplay pass's DRAWN SET, BEFORE the pose ──────────────────
		// The engine poses only what it draws (`Human_WakeIfVisible` 0x45F140 ->
		// `Human_Wake` 0x45F240 -> `BoneObj_Update` 0x4D38C0, reached only from
		// `Human_DrawHandler` 0x45F280 / `Human_NearClipHandler` 0x45F050), while
		// the CLOCK runs for every active AI (`Soldier_Update` 0x45D080 ->
		// `Human_FireAnimTrigger` 0x4613D0 -> `BoneObj_GetEvents` 0x4D3210).  The
		// gameplay pass's drawn set is `GetGameplayDrawnAnimationIndices()` - the
		// skinned-replacement set narrowed by this file's 80 m gameplay filter -
		// and it is built HERE because it is the very same set the draw loop below
		// iterates.  One computation, two consumers: the pose decision and the
		// draw cannot disagree.
		//
		// The ordering is load-bearing twice over:
		//   * `ApplyRuntimeAiAnimationRequests()` must run FIRST, because
		//     `ShouldUseSkinnedReplacement` (source/animation.h:295) requires
		//     `playback.playing`, so a clip the request pass starts is drawable on
		//     THIS frame only if the pass has already run.  That is why it is a
		//     separate step here rather than the first line of
		//     AdvanceGameplayAnimationClocks (where it used to live, at the same
		//     point in the frame).
		//   * the set must be complete BEFORE the animation step: a playback that
		//     is drawn must get the engine's pose+clock step THIS frame, not a
		//     clock-only step plus a draw-time wake, otherwise the drawn pose would
		//     shift by one engine frame (see AnimPlayback::Update vs
		//     AnimPlayback::AdvanceClock).
		//
		// Nothing between this point and the draw loop mutates the set's inputs
		// (the object copy was just synchronised above, the viewer position is
		// fixed for the frame, and `ShouldUseFastScenePreview` is always false when
		// `render_gameplay` is - source/runtime/camera_preview_policy.h:8-11 - so
		// the set is never gated off in gameplay mode).
		ApplyRuntimeAiAnimationRequests();
		skinnedReplacementIndices = GetGameplayDrawnAnimationIndices();
		AdvanceGameplayAnimationClocks(skinnedReplacementIndices);
		if (profile_frames) {
			static int64_t acc_total = 0, acc_sim = 0, acc_sync = 0;
			static int acc_frames = 0;
			const int64_t frame_end = Sys_Milliseconds();
			acc_sim += profile_after_sim - profile_frame_start;
			acc_sync += frame_end - profile_after_sim;
			acc_total += frame_end - profile_frame_start;
			if (++acc_frames >= 60) {
				Logger::Get().Log(LogLevel::INFO,
					"[Profile] 60 frames: sim=" + std::to_string(acc_sim) +
					"ms sync=" + std::to_string(acc_sync) +
					"ms total=" + std::to_string(acc_total) + "ms");
				acc_total = acc_sim = acc_sync = 0;
				acc_frames = 0;
			}
		}
	} else if (!in_game_mode_ || IsEditorInputActive()) {
		ProcessInput(delta_seconds);
	}
	if (in_game_mode_ && !render_gameplay) {
		// The editor window can stay focused while gameplay continues simulating.
		// Keep the gameplay-owned animation state current across that focus change.
		// NOTHING gameplay is drawn in this mode: the skinned draw loop below walks
		// `skinnedReplacementIndices`, which is the EDITOR's set here (the
		// `animPlaybacks_` lookup at render_gameplay ? gameplayAnimPlaybacks_ :
		// animPlaybacks_), so every gameplay playback advances its clock and none
		// poses - which is exactly what the engine does for an AI it does not draw.
		CaptureGameplayRenderSnapshot();
		ApplyRuntimeAiAnimationRequests();
		AdvanceGameplayAnimationClocks({});
	}
	UpdateGameplayFieldOfView();

	const bool cameraNavigating =
		Utils::IsKeyBindingPressed(Config::Get().keyEnableCamera) ||
		mouse_state_.right_button_down_ ||
		(input_.keys_ & (MK_FORWARD | MK_BACKWARD | MK_LEFT | MK_RIGHT | MK_STRAIGHT_UP | MK_STRAIGHT_DOWN)) != 0 ||
		Utils::IsKeyBindingPressed(Config::Get().keyMoveCameraForward) ||
		Utils::IsKeyBindingPressed(Config::Get().keyMoveCameraBackward) ||
		camera_mode_moved_;
	// IGI_PROFILE: boundary markers for the pre-render split reported below.
	const auto frame_pre_t1 = profile_frame_break ? FrameBreakClock::now()
	                                              : FrameBreakClock::time_point{};

	// Every AI with an active, playing clip that the editor's viewport DRAWS is
	// skinned-replaced, and the AI whose clock runs but whose pose is not drawn
	// keeps a live clock (AnimPlayback::AdvanceClock).  The old gameplay-only gate
	// left the editor's animation clocks advancing while the static meshes
	// continued to render, which made the log claim animations were playing even
	// though no animated pose was ever submitted in editor mode; the follow-up fix
	// posed the SELECTED object alone, which left every other AI looking frozen.
	//
	// This set is also the ANSWER to "which playbacks need a pose this frame", so
	// it is built BEFORE the animation update instead of after it: the engine
	// poses only what it draws (`Human_WakeIfVisible` 0x45F140 ->
	// `Human_Wake` 0x45F240 -> `BoneObj_Update` 0x4D38C0, called from
	// `Human_DrawHandler` 0x45F280 / `Human_NearClipHandler` 0x45F050), and
	// `AnimPlayback::AdvanceClock` reproduces that for everything else.  Moving
	// it here cannot change what is drawn: nothing between this point and the
	// renderer reads or writes it.
	//
	// In GAMEPLAY mode this block does not recompute the set: it was already
	// built above by `GetGameplayDrawnAnimationIndices()` and handed to
	// `AdvanceGameplayAnimationClocks`, so the draw uses that one set unchanged.
	draw_params_.fast_scene_preview_ = igi::ShouldUseFastScenePreview(
		cameraNavigating, render_gameplay,
		Utils::IsKeyBindingPressed(Config::Get().keyEnableCamera));
	if (!draw_params_.fast_scene_preview_) {
		if (!render_gameplay) {
			// IGI_PROFILE_ANIMBENCH-only: see ShouldUseAnimBench.  Force one
			// fixed animated object to be the selection, so a run can compare the
			// one-selected workload against the default one.
			if (ShouldUseAnimBench()) {
				std::unordered_set<int> bench_candidates =
					GetSkinnedReplacementObjectIndices(false);
				int bench_idx = -1;
				for (int idx : bench_candidates) {
					if (bench_idx < 0 || idx < bench_idx) bench_idx = idx;
				}
				if (bench_idx >= 0) selected_object_index_ = bench_idx;
			}
			// The posed set IS the drawn set: the AI whose pose the skinned draw
			// loop below submits.  In editor mode that is every playback the
			// viewport would draw - NOT just the selected one, which is what made
			// an unselected AI look frozen while its clock ran.  See
			// App::GetEditorDrawnAnimationIndices for the engine rule
			// (`Human_WakeIfVisible` 0x45F140) and the two tests it reuses from
			// the visual pass.
			skinnedReplacementIndices = GetEditorDrawnAnimationIndices();
		}
		// render_gameplay: already computed above, before the pose. Deliberately
		// NOT recomputed here - one set, so the posed set and the drawn set cannot
		// diverge.
	}

	// Update animation playback (auto-play for AI NPCs)
	if (!cameraNavigating && (!rendering_editor_window_ || IsEditorInputActive())) {
		if (!render_gameplay) {
			UpdateAnimations(delta_seconds, skinnedReplacementIndices);
		}
		CheckMusicLoop();
	}
	const auto frame_pre_t1anim = profile_frame_break ? FrameBreakClock::now()
	                                                  : FrameBreakClock::time_point{};

	// Per-frame position-drag velocity: the pad / Z slider accelerate while held in
	// a direction and keep moving when the cursor is pinned at the window edge.
	if (IsEditorInputActive() && mouse_state_.left_button_down_ &&
		prop_field_index_ >= 0 && selected_object_index_ >= 0 &&
	    !Utils::IsKeyBindingPressed(Config::Get().keyEnableCamera)) {
		ApplyPropPositionDrag();
	}

	if (IsEditorInputActive() && edit_mode_ && terrain_edit_enabled_ &&
		mouse_state_.left_button_down_) {
		EditorProcessClick();
	}

	if (render_gameplay) UpdateGameplayViewDefine();
	else UpdateViewDefine();
	if (render_gameplay) CaptureGameplayRenderSnapshot();
	const auto frame_pre_t1view = profile_frame_break ? FrameBreakClock::now()
	                                                  : FrameBreakClock::time_point{};
	const bool pointer_changed = mouse_state_.prior_x_ != last_pick_x_ ||
		mouse_state_.prior_y_ != last_pick_y_;
	const bool camMode = Utils::IsKeyBindingPressed(Config::Get().keyEnableCamera);
	const bool overPanel = prop_editor_open_ &&
		mouse_state_.prior_x_ < (PropPanel::kLeft + PropPanel::kWidth);
	int64_t editor_pick_us = 0;
	bool editor_pick_called = false;
	// Hover picking is deferred to a SETTLED pointer: one pick is a whole extra
	// scene pass plus a synchronous 1x1 glReadPixels
	// (renderer/renderer_objects_picking.cpp:374-412), so issuing it on the
	// motion itself pays ~23 ms for a cursor position the user has already left.
	// This is the cost that reappears the frame camera mode (the camera key,
	// `CameraEnable` = <Alt> in assets/editor/qed/qedkeybindings.qsc) is
	// released: while it is held - and while WASD moves the camera - picking is
	// suppressed, so the view moves smoothly, and only after the release does the
	// per-motion pick resume and stall the frame. The movement math itself is
	// identical in both states (App::ProcessInput; viewer_s::clip_to_z_ is never
	// set after construction), so there is no smoothing/delta term to reset here.
	if (IsEditorInputActive()) {
		const int64_t pick_now_ms = Sys_Milliseconds();
		if (camMode || cameraNavigating || overPanel) {
			hover_object_index_ = -1;
			last_pick_x_ = mouse_state_.prior_x_;
			last_pick_y_ = mouse_state_.prior_y_;
			last_hover_pick_ms_ = pick_now_ms;
		} else if (igi::ShouldPickSceneHoverOnSettle(pointer_changed,
			mouse_state_.left_button_down_, pick_now_ms, last_pointer_motion_ms_,
			last_hover_pick_ms_)) {
			const auto editor_t_pick = profile_frame_break ? FrameBreakClock::now()
			                                               : FrameBreakClock::time_point{};
			hover_object_index_ = PickObjectAtScreenPos(mouse_state_.prior_x_, mouse_state_.prior_y_);
			if (profile_frame_break) {
				editor_pick_us = frame_us(editor_t_pick, FrameBreakClock::now());
				editor_pick_called = true;
			}
			if (hover_object_index_ >= Renderer::kAttaPickBase) hover_object_index_ = -1; // ATTA hovered (clickable; promote on click)
			last_pick_x_ = mouse_state_.prior_x_;
			last_pick_y_ = mouse_state_.prior_y_;
			last_hover_pick_ms_ = pick_now_ms;
		}
	}
	const auto frame_pre_t1pick = profile_frame_break ? FrameBreakClock::now()
	                                                  : FrameBreakClock::time_point{};

	vert_flat_sky_layer_s * fsl_vb = renderer_.MapFlatSkyLayersVB();
	const auto map_t_sky = profile_frame_break ? FrameBreakClock::now()
	                                           : FrameBreakClock::time_point{};
	vert_pos_a_uv_s* terrain_vb = renderer_.MapTerrainVB();
	const auto map_t_vb = profile_frame_break ? FrameBreakClock::now()
	                                          : FrameBreakClock::time_point{};
	uint32_t* terrain_ib = renderer_.MapTerrainIB();
	const auto map_t_ib = profile_frame_break ? FrameBreakClock::now()
	                                          : FrameBreakClock::time_point{};
	render_chunk_s* render_chunks = renderer_.GetTerrainRenderChunckBuffer();
	const auto frame_pre_t1a = profile_frame_break ? FrameBreakClock::now()
	                                               : FrameBreakClock::time_point{};

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
	const auto frame_pre_t1b = profile_frame_break ? FrameBreakClock::now()
	                                               : FrameBreakClock::time_point{};

	// Publish exactly the range the engine just filled (Terrain::Update resets
	// both counters at the start of the frame and fills from index 0).
	if (terrain_ib) {
		renderer_.UnmapTerrainIB(update_params.num_terrain_idx_);
	}

	if (terrain_vb) {
		renderer_.UnmapTerrainVB(update_params.num_terrain_vert_);
	}

	if (fsl_vb) {
		renderer_.UnmapFlatSkyLayersVB();
	}
	const auto frame_pre_t2 = profile_frame_break ? FrameBreakClock::now()
	                                              : FrameBreakClock::time_point{};

	draw_params_.flat_sky_layer_is_visible_ = update_params.flat_sky_layer_is_visible_;
	draw_params_.num_terrain_render_chunk_ = update_params.num_terrain_render_chunk_;
	draw_params_.level_objects_ = &GetActiveRenderLevelObjects();
	draw_params_.selected_object_index_ = render_gameplay ? -1 : selected_object_index_;
	draw_params_.show_magic_obj_spheres_ = !cameraNavigating && !render_gameplay && show_magic_obj_spheres_;
	// `skinnedReplacementIndices` (and `fast_scene_preview_`, which decides
	// whether it is filled at all) is computed above, before UpdateAnimations,
	// because it is also the set of playbacks that need a live pose this frame.
	camera_mode_moved_ = false;
	draw_params_.skip_static_draw_indices_ = &skinnedReplacementIndices;
	draw_params_.terrain_id_at_world_xy_ =
		[this](double x, double y) { return level_.GetTerrainNodeId(x, y); };


	float ground_z = 0.0f;
	bridge_.SetEnabled(show_hud_ && !render_gameplay);
	IGIBridge::PositionData data = bridge_.GetLatestData();
	const viewer_s& render_viewer = render_gameplay ? gameplay_viewer_ : viewer_;
	level_.GetTerrainZ(render_viewer.pos_.x, render_viewer.pos_.y, ground_z);

	int propAnimBoneHierarchy; std::vector<int> propAnimIds; int propAnimActiveId; bool propAnimIsPlaying;
	ComputePropAnimUiState(propAnimBoneHierarchy, propAnimIds, propAnimActiveId, propAnimIsPlaying);
	const auto frame_pre_t3 = profile_frame_break ? FrameBreakClock::now()
	                                              : FrameBreakClock::time_point{};

	Renderer::task_tree_view_params_s task_tree_view = {
		.show_hud_ = render_gameplay ? false : show_hud_,
		.hud_overlay_visible_ = hud_overlay_visible_,
		.status_msg_ = status_message_,
		.pause_mode_ = igi::IsPauseMenuVisible(
			pause_mode_, IsGameplayInputFocused()),
		.pause_active_input_ = pause_active_input_,
		.pause_level_input_ = pause_level_input_,
		.pause_search_input_ = pause_search_input_,
		.pause_terrain_expanded_ = pause_terrain_expanded_,
		.terrain_draw_options_ = GetTerrainDrawOptions(),
		.show_debug_ = render_gameplay ? false : show_debug_,
		.show_help_ = render_gameplay ? false : show_help_,
		.edit_mode_ = render_gameplay ? false : edit_mode_,
		.terrain_edit_enabled_ = render_gameplay ? false : terrain_edit_enabled_,
		.terrain_mod_options_ = terrain_mod_options_,
		.selected_object_index_ = render_gameplay ? -1 : selected_object_index_,
		.hover_object_index_ = render_gameplay ? -1 : hover_object_index_,
		.hover_tree_index_ = render_gameplay ? -1 : hover_tree_index_,
		.mouse_x_ = mouse_state_.prior_x_,
		.mouse_y_ = mouse_state_.prior_y_,
		.tree_scroll_offset = tree_scroll_offset_,
		.tree_decl_expanded = tree_decl_expanded_,
		.level_objects_ = &GetActiveRenderLevelObjects(),
		.task_picker_open_ = render_gameplay ? false : task_picker_open_,
		.task_picker_selected_idx_ = task_picker_selected_idx_,
		.task_picker_scroll_offset_ = task_picker_scroll_offset_,
		.task_picker_search_ = task_picker_search_,
		.enable_camera_mode_ = render_gameplay ? false : Utils::IsKeyBindingPressed(Config::Get().keyEnableCamera),
		.prop_editor_open_     = render_gameplay ? false : prop_editor_open_,
		.prop_field_index_     = prop_field_index_,
		.prop_text_edit_field_ = prop_text_edit_field_,
		.prop_edit_obj_index_  = prop_edit_obj_index_,
		.prop_drag_obj_index_  = prop_drag_obj_index_,
		.prop_text_buf_        = prop_text_buf_,
		.prop_text_caret_      = prop_text_caret_,
		.prop_text_sel_anchor_ = prop_text_sel_anchor_,
		.prop_text_sel_focus_  = prop_text_sel_focus_,
		.prop_panel_scroll_    = prop_panel_scroll_,
		.find_open_            = find_open_,
		.find_query_           = find_query_,
		.find_result_idx_      = find_result_idx_,
		.selected_obj_is_ai    = (selected_object_index_ >= 0 &&
			selected_object_index_ < (int)level_.GetLevelObjects().GetObjects().size() &&
			(ai_model_ids_.count(level_.GetLevelObjects().GetObjects()[selected_object_index_].modelId) > 0 ||
			 [&]() {
				const std::string& t = level_.GetLevelObjects().GetObjects()[selected_object_index_].type;
				return t == "HumanSoldier" || t == "HumanSoldierFemale" ||
				       t == "HumanPlayer" || t == "HumanSoldierRPG" || t == "HumanAI";
			 }())),
		.in_game_mode_         = render_gameplay,
		.noclip_mode_          = noclip_mode_,
		.player_health_        = render_snapshot.player_health,
		.player_maximum_health_ = render_snapshot.player_maximum_health,
		.player_armor_         = render_snapshot.player_armor,
		.player_maximum_armor_ = render_snapshot.player_maximum_armor,
		.active_weapon_name_   = render_snapshot.active_weapon_name,
		.clip_ammo_            = render_snapshot.clip_ammo,
		.clip_capacity_        = render_snapshot.clip_capacity,
		.reserve_ammo_         = render_snapshot.reserve_ammo,
		.map_computer_open_    = render_snapshot.map_computer_open,
		.map_computer_objectives_ = render_snapshot.map_computer_objectives,
		.objective_text_       = render_snapshot.objective_text,
		.mission_timer_remaining_ticks_ = render_snapshot.mission_timer_remaining_ticks,
		.mission_status_messages_ = render_snapshot.mission_status_messages,
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
		.model_entries_        = &level_model_entries_,
		.ai_script_path_        = ai_script_path_,
		.ai_script_text_        = ai_script_text_,
		.ai_script_dirty_       = ai_script_dirty_,
		.ai_script_vscroll_     = ai_script_vscroll_,
		.ai_script_path_hscroll_= ai_script_path_hscroll_,
		.terrain_brush_          = edit_brush_,
		.terrain_brush_radius_   = edit_brush_radius_,
		.terrain_brush_strength_ = edit_brush_strength_,
		.auto_save_enabled_        = auto_save_enabled_,
		.auto_save_interval_seconds_ = auto_save_interval_seconds_,
		.logging_enabled_ = Config::Get().enableLogging,
		.log_level_threshold_ = Config::Get().logLevelThreshold,
		.music_on_ = music_playing_,
		.anim_status_  = draw_params_.fast_scene_preview_ ? std::string() : BuildAnimStatusString(),
		.anim_playing_ = !animPlaybacks_.empty(),
		.anim_debug_visible_ = show_anim_debug_,
		.anim_panel_playing_ = SelectedAnimPlaying(),
		.anim_panel_title_ = AnimPanelTitleText(),
		.anim_panel_has_target_ = SelectedAnimPlayback() != nullptr,
		.prop_anim_bone_hierarchy_ = propAnimBoneHierarchy,
		.prop_anim_ids_ = propAnimIds,
		.prop_anim_active_id_ = propAnimActiveId,
		.prop_anim_is_playing_ = propAnimIsPlaying,
		.flash_effect_strength_ = render_gameplay
			? render_snapshot.flash_effect_strength
			: 0.0f,
		.muzzle_flash_strength_ = render_gameplay
			? render_snapshot.muzzle_flash_strength
			: 0.0f,
		.player_damage_effect_strength_ = render_gameplay
			? render_snapshot.player_damage_effect_strength
			: 0.0f,
		.pause_menu_model_ = &pause_menu_.model,
		.pause_menu_layout_ = PauseMenuLayoutNow(),
	};

	const auto frame_draw_begin = profile_frame_break ? FrameBreakClock::now()
	                                                  : FrameBreakClock::time_point{};
	if (profile_frame_break) {
		LogFrameBreakPreSplit(frame_us(frame_enter, frame_pre_t1),
		                      frame_us(frame_pre_t1, frame_pre_t2),
		                      frame_us(frame_pre_t2, frame_pre_t3),
		                      frame_us(frame_pre_t3, frame_draw_begin));
		LogFrameBreakPreSplitIo(frame_us(frame_pre_t1, frame_pre_t1a),
		                        frame_us(frame_pre_t1a, frame_pre_t1b),
		                        frame_us(frame_pre_t1b, frame_pre_t2));
		LogFrameBreakPreSplitEditor(frame_us(frame_pre_t1, frame_pre_t1anim),
		                            frame_us(frame_pre_t1anim, frame_pre_t1view),
		                            frame_us(frame_pre_t1view, frame_pre_t1a),
		                            frame_us(frame_pre_t1a, frame_pre_t1b));
		LogFrameBreakMapSplit(frame_us(frame_pre_t1view, frame_pre_t1pick),
		                      frame_us(frame_pre_t1pick, map_t_sky),
		                      frame_us(map_t_sky, map_t_vb),
		                      frame_us(map_t_vb, map_t_ib));
		LogPickProfile(editor_pick_us, editor_pick_called);
	}
	renderer_.Draw(draw_params_, task_tree_view);
	const auto frame_draw_end = profile_frame_break ? FrameBreakClock::now()
	                                                : FrameBreakClock::time_point{};
	DrawGameplayProjectiles();
	DrawGameplayExplosions();
	DrawGameplayGuardMuzzleFlashes();
	DrawGameplayPlayerWeapon();

    // Find the "right hand" bone index in modelId's parsed bone list (REIH+MANB
    // names), cached per modelId. This is the same index space the engine pose's
    // bone array and the rest-pose bone list use (both come from the same shared
    // character rig), so one lookup serves both the animating and static
    // weapon-attachment paths below.
    auto findHandBoneIndex = [this](const std::string& modelId, const ParsedGeometry* geo) -> int {
        auto cached = handBoneIndexCache_.find(modelId);
        if (cached != handBoneIndexCache_.end()) return cached->second;
        int idx = -1;
        if (geo) {
            for (size_t i = 0; i < geo->bones.size(); ++i) {
                if (geo->bones[i].name == "right hand") { idx = (int)i; break; }
            }
        }
        handBoneIndexCache_[modelId] = idx;
        return idx;
    };

    // Weapon meshes are authored with the barrel along native +Y. Attached at the
    // hand bone it comes out aligned with the forearm (appears vertical). Correction,
    // applied in the weapon's own local frame (right-multiplied onto the hand matrix
    // so it still follows the hand as the arm animates):
    //   * rotate 90Â° about X  -> swings the barrel from vertical to horizontal,
    //   * rotate 180Â° about Z  -> single horizontal flip so the barrel points the
    //     correct way, then
    //   * roll 180Â° about the barrel's OWN (post-correction) direction -> flips the
    //     weapon right-side-up (was upside down) WITHOUT changing the barrel's aim,
    //     since rotating about the barrel axis leaves that axis fixed. Computed from
    //     the actual barrel direction so it's correct regardless of the gun's frame.
    const glm::mat4 kWeaponBase =
        glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f)) *
        glm::rotate(glm::mat4(1.0f), glm::radians(180.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    const glm::vec3 kBarrelDir = glm::normalize(glm::vec3(kWeaponBase * glm::vec4(0.0f, 1.0f, 0.0f, 0.0f)));
    const glm::mat4 kWeaponHandCorrection =
        glm::rotate(glm::mat4(1.0f), glm::radians(180.0f), kBarrelDir) * kWeaponBase;

    // Draw the live skinned mesh for every AI with an active, playing clip â€”
    // all of them animate and render in parallel, not just the selected object.
    // This MUST run whenever a clip is playing (not gated by F10) â€” the static
    // mesh is already skipped via skip_static_draw_indices_ above, so gating
    // this would leave those objects invisible. The bone wireframe stays scoped
    // to the selected object only (avoids clutter) and gated by 'B'.
    {
        auto& active_playbacks = render_gameplay
            ? gameplayAnimPlaybacks_ : animPlaybacks_;
        auto& objs = GetActiveRenderLevelObjects().GetObjects();
        for (int idx : skinnedReplacementIndices) {
            if (idx < 0 || idx >= (int)objs.size()) continue;
            auto playback_it = active_playbacks.find(idx);
            if (playback_it == active_playbacks.end()) continue;
            auto& pb = playback_it->second;
            // The pose comes out of the ENGINE, not out of the editor's own
            // slerp: `AnimPlayback` owns an `igi::AnimPlayer` and this is the
            // frame its last `BoneObj_Update` 0x4D38C0 produced (one glm matrix
            // per engine bone, metres, model-local). No re-pose here — see
            // `AnimPlayer::PoseFromLastStep`.
            const std::vector<glm::mat4>& worldTransforms = pb.WorldTransforms();
            if (worldTransforms.empty()) continue;

            const auto& obj = objs[idx];
            // ONE matrix for the posed body, the bone overlay and the weapon, and
            // it is the STATIC path's own composition (`IgiEulerRotationMatrix` =
            // Rz*Ry*Rx, `Matrix_SetEuler` HOOK 0x4B38E0, src/math/matrix.c) PLUS
            // the model's rig origin.
            //
            // WHY THE ORIGIN. The engine's skinned vertices are in the SKELETON's
            // frame -- `BoneMesh_Skin` 0x49B700 adds `Bone::pos`
            // (src/dxinit/d3drender.c:259-264) and the bone chain starts at the
            // skeleton's root offset, which `BoneObj_Load` 0x4D67A0 copies out of
            // the BOBJ's TLST (src/boneobj/boneobj.c:1570-1576): (0,0,0) for every
            // shipped human rig. Every other consumer of `obj.pos` -- the static
            // bind-pose bake (`SkinSkinnedVertices`, source/renderer/
            // mef_native.cpp:1122-1129), the terrain snap, the picking ray, the
            // level's authored placements -- is calibrated to a rig whose root
            // pivot is (0,0,3990.4) instead, i.e. 0.974 m above the feet. Drawing
            // the pose without that origin put every posed AI 0.974 m below the
            // static mesh, i.e. under the terrain. `PosedObjectMatrix` is the one
            // place that decision lives; tests/test_skinned_static_parity.cpp
            // asserts the two paths agree through it.
            //
            // A human the level authors INSIDE a vehicle is the one case where the
            // ENGINE's own object origin is what the authored position means: the
            // terrain snap never touches such a human, so the feet-rebased frame
            // would draw it one pivot->feet lift (3990.4 raw units, 0.974 m) above
            // its seat. See renderer/human_seat_frame.h for the citations
            // (`BoneObj_Update` 0x4D38C0, `BoneDynCubeObj_EnterHandler` 0x5124C0,
            // `Human_ProbeGround` 0x462390) and tests/test_seated_human.cpp.
            const ParsedGeometry* skinGeo =
                renderer_.GetOrLoadSkinGeometry(obj.modelId, obj.isBuilding);
            const bool seat_frame = igi::HumanSitsInVehicle(objs, idx);
            const glm::mat4 objMat = igi::PosedObjectMatrix(
                obj.pos, obj.rot, obj.scale, skinGeo != nullptr ? &skinGeo->bones : nullptr,
                seat_frame);
            // The mesh is skinned by the engine's own BoneMesh_Skin 0x49B700
            // over these same bone records (raw `Bone::matrix` + `Bone::pos`),
            // which is what `Direct3DRender_DrawBoneMesh` 0x49F700 does.
            renderer_.DrawSkinnedMesh(obj.modelId, obj.isBuilding, pb.RawBoneTransforms(), objMat);

            if (show_anim_skeleton_ && idx == selected_object_index_) {
                // boneParents is indexed by bone ID (the same engine-bone indexing
                // the pose uses), so DrawAnimSkeleton can connect each bone to its
                // real parent instead of assuming a flat chain of consecutive
                // array indices.
                std::vector<int> boneParents(worldTransforms.size(), -1);
                for (const auto& b : pb.clip->bones) {
                    if (b.index >= 0 && (size_t)b.index < boneParents.size())
                        boneParents[b.index] = b.parent;
                }
                renderer_.DrawAnimSkeleton(worldTransforms, boneParents, objMat);
            }

            if (!obj.weaponModelId.empty()) {
                int handIdx = findHandBoneIndex(obj.modelId, skinGeo);
                if (handIdx >= 0 && (size_t)handIdx < worldTransforms.size()) {
                    glm::mat4 handWorldMat = objMat * worldTransforms[handIdx] * kWeaponHandCorrection;
                    renderer_.DrawAttachedMesh(obj.weaponModelId, false, handWorldMat);
                }
            }
        }
    }

    // Static/paused AI (not currently in skinnedReplacementIndices) still hold
    // their weapon, positioned at the hand bone's REST pose instead of an
    // animated transform.
    if (!draw_params_.fast_scene_preview_) {
        auto& objs = GetActiveRenderLevelObjects().GetObjects();
        const glm::vec3 cam = render_gameplay ? gameplay_viewer_.pos_ : viewer_.pos_;
        const float maxWeaponSq = (80.0f * WORLD_UNITS_PER_METER) * (80.0f * WORLD_UNITS_PER_METER);
        for (int idx = 0; idx < (int)objs.size(); ++idx) {
            const auto& obj = objs[idx];
            if (obj.weaponModelId.empty() || obj.deleted) continue;
            if (skinnedReplacementIndices.count(idx)) continue; // already drawn above (animated)
            const glm::vec3 delta = glm::vec3(obj.pos) - cam;
            if (glm::dot(delta, delta) > maxWeaponSq) continue;

            const ParsedGeometry* geo = renderer_.GetOrLoadSkinGeometry(obj.modelId, obj.isBuilding);
            int handIdx = findHandBoneIndex(obj.modelId, geo);
            if (handIdx < 0 || !geo || (size_t)handIdx >= geo->bones.size()) continue;

            glm::vec3 restHandPos;
            auto restIt = weaponRestHandPosCache_.find(obj.modelId);
            if (restIt != weaponRestHandPosCache_.end()) {
                restHandPos = restIt->second;
            } else {
                std::vector<glm::vec3> restPositions = ComputeBoneWorldPositionsPublic(geo->bones);
                if ((size_t)handIdx >= restPositions.size()) continue;
                restHandPos = restPositions[handIdx];
                weaponRestHandPosCache_[obj.modelId] = restHandPos;
            }

            glm::mat4 objMat(1.0f);
            objMat = glm::translate(objMat, glm::vec3((float)obj.pos.x, (float)obj.pos.y, (float)obj.pos.z));
            objMat = glm::rotate(objMat, (float)obj.rot.z, glm::vec3(0, 0, 1));
            objMat = glm::rotate(objMat, (float)obj.rot.x, glm::vec3(1, 0, 0));
            objMat = glm::rotate(objMat, (float)obj.rot.y, glm::vec3(0, 1, 0));
            objMat = glm::scale(objMat, glm::vec3(40.96f * obj.scale));

            glm::mat4 handLocalMat = glm::translate(glm::mat4(1.0f), restHandPos * kMefNativeScale);
            renderer_.DrawAttachedMesh(obj.weaponModelId, false, objMat * handLocalMat * kWeaponHandCorrection);
        }
    }

	DrawCustomCursor();
	glutSwapBuffers();
	if (profile_frame_break) {
		LogFrameBreak("editor", frame_us(frame_enter, frame_draw_begin),
		              frame_us(frame_draw_begin, frame_draw_end),
		              frame_us(frame_draw_end, FrameBreakClock::now()));
	}
}

void App::UpdateGameplayFieldOfView() {
    constexpr float default_field_of_view_degrees = FOVY_IN_DEGREE;
    constexpr float zoomed_field_of_view_degrees = 40.0f;
    const igi::RuntimeCutSceneCamera& cut_scene_camera =
        gameplay_host_.GetWorld().GetActiveCutSceneCamera();
    const bool use_cut_scene_field_of_view = IsGameplayRenderTarget() &&
        cut_scene_camera.active &&
        std::isfinite(cut_scene_camera.field_of_view_y_radians) &&
        cut_scene_camera.field_of_view_y_radians > 0.0f;
    const bool map_camera_active = IsGameplayRenderTarget() &&
        gameplay_map_computer_camera_.IsRunning();
    const bool zoom_active = IsGameplayRenderTarget() &&
        gameplay_host_.GetWorld().IsZoomActive();
    const float desired_field_of_view = map_camera_active
        ? gameplay_map_computer_camera_.GetFieldOfView()
        : use_cut_scene_field_of_view
        ? cut_scene_camera.field_of_view_y_radians
        : glm::radians(
            zoom_active
                ? zoomed_field_of_view_degrees
                : default_field_of_view_degrees);
    if (std::abs(view_define_.fovy_ - desired_field_of_view) <= 0.0001f) {
        return;
    }

    view_define_.fovy_ = desired_field_of_view;
    const float viewport_aspect = static_cast<float>(
        window_state_.viewport_width_) /
        static_cast<float>(std::max(1, window_state_.viewport_height_));
    const float half_vertical_fov_tangent = std::tan(view_define_.fovy_ * 0.5f);
    view_define_.fovx_ = std::atan(
        half_vertical_fov_tangent * viewport_aspect) * 2.0f;
    view_define_.tan_half_fovx_ = std::tan(view_define_.fovx_ * 0.5f);
    view_define_.tan_half_fovy_ = half_vertical_fov_tangent;
    view_define_.half_viewport_width_div_tan_half_fovx_ =
        static_cast<float>(window_state_.viewport_width_) * 0.5f /
        view_define_.tan_half_fovx_;
    view_define_.half_viewport_height_div_tan_half_fovy_ =
        static_cast<float>(window_state_.viewport_height_) * 0.5f /
        view_define_.tan_half_fovy_;
}

void App::ApplyRuntimeCutSceneCamera() {
    if (!IsGameplayRenderTarget()) {
        return;
    }

    const igi::RuntimeCutSceneCamera& cut_scene_camera =
        gameplay_host_.GetWorld().GetActiveCutSceneCamera();
    if (!cut_scene_camera.active) {
        const igi::PlayerController& player =
            gameplay_host_.GetWorld().GetPlayer();
        gameplay_viewer_.pos_ = player.GetEyePosition();
        gameplay_viewer_.yaw_ = player.GetYaw();
        gameplay_viewer_.pitch_ = player.GetPitch();
        UpdateGameplayViewerVectors();
        return;
    }

    gameplay_viewer_.pos_ = cut_scene_camera.position;
    gameplay_viewer_.forward_ = cut_scene_camera.forward;
    gameplay_viewer_.right_ = cut_scene_camera.right;
    gameplay_viewer_.up_ = cut_scene_camera.up;
}

void App::UpdateGameplayMapComputerCamera(float delta_seconds) {
    if (!IsGameplayRenderTarget()) {
        return;
    }

    // The authored camera source does not expose a separate C++ map-camera
    // task. Keep the presentation bridge deterministic and renderer-free: the
    // The map computer owns a separate overhead view. Its center and discrete
    // field-of-view levels are fixed-step runtime state so drag/zoom input is
    // deterministic and the camera bridge only presents that state.
    constexpr float map_vantage_height_units =
        900.0f * igi::PlayerController::WORLD_METER;
    igi::RuntimeWorld& runtime_world = gameplay_host_.GetWorld();
    const igi::PlayerController& player = runtime_world.GetPlayer();
    const float player_field_of_view = glm::radians(
        gameplay_host_.GetWorld().IsZoomActive() ? 40.0f : FOVY_IN_DEGREE);
    const igi::RuntimeMapComputerPose live_eye{
        player.GetEyePosition(),
        glm::radians(player.GetYaw()),
        glm::radians(player.GetPitch())};
    const glm::vec3 player_position = player.GetPosition();
    const glm::vec3 map_center = runtime_world.GetMapComputerCenter();
    const igi::RuntimeMapComputerPose live_vantage{
        glm::vec3(map_center.x, map_center.y,
            player_position.z + map_vantage_height_units),
        0.0f,
        igi::RuntimeMapComputerCamera::kMapPitchRadians};
    const float map_field_of_view = runtime_world.GetMapComputerFieldOfView();
    const bool map_computer_open =
        gameplay_host_.GetWorld().IsMapComputerOpen();

    if (map_computer_open && !gameplay_map_computer_open_) {
        gameplay_map_computer_camera_.BeginOpen(
            live_eye,
            player_field_of_view,
            live_vantage,
            map_field_of_view);
        gameplay_map_computer_open_ = true;
    } else if (!map_computer_open && gameplay_map_computer_open_) {
        if (gameplay_map_computer_camera_.CanClose()) {
            gameplay_map_computer_camera_.BeginClose(
                live_vantage,
                map_field_of_view,
                live_eye,
                player_field_of_view);
        }
        gameplay_map_computer_open_ = false;
    }

    gameplay_map_computer_camera_.Update(
        delta_seconds,
        live_eye,
        live_vantage,
        map_field_of_view);
    if (!gameplay_map_computer_camera_.IsRunning()) {
        return;
    }

    const igi::RuntimeMapComputerPose& camera_pose =
        gameplay_map_computer_camera_.GetPose();
    gameplay_viewer_.pos_ = camera_pose.position;
    gameplay_viewer_.yaw_ = glm::degrees(camera_pose.yaw);
    gameplay_viewer_.pitch_ = glm::degrees(camera_pose.pitch);
    gameplay_viewer_.roll_ = 0.0f;
    UpdateGameplayViewerVectors();
}

void App::CaptureGameplayRenderSnapshot() {
    if (!IsGameplayRenderTarget()) {
        return;
    }

    igi::RuntimeRenderCamera camera;
    camera.position = gameplay_viewer_.pos_;
    camera.forward = gameplay_viewer_.forward_;
    camera.right = gameplay_viewer_.right_;
    camera.up = gameplay_viewer_.up_;
    camera.field_of_view_y_radians = view_define_.fovy_;
    camera.viewport_width = gameplay_viewport_width_;
    camera.viewport_height = gameplay_viewport_height_;
    gameplay_host_.Render(camera);
}

void App::DrawGameplayPlayerWeapon() {
	if (!IsGameplayRenderTarget()) return;
	// The first-person weapon mesh is part of the deferred HUD presentation:
	// keep it hidden unless the overlay is explicitly shown with Alt+H.
	if (!hud_overlay_visible_) return;
	if (gameplay_host_.GetWorld().GetActiveCutSceneCamera().active ||
		gameplay_host_.GetWorld().IsMapComputerOpen() ||
		gameplay_map_computer_camera_.IsRunning()) {
		return;
	}

	const igi::RuntimeRenderSnapshot& render_snapshot =
		gameplay_host_.GetRenderSnapshot();
	if (render_snapshot.active_weapon_model_id.empty() ||
		!render_snapshot.player_alive) {
		return;
	}

	const glm::vec3 forward = glm::normalize(render_snapshot.camera.forward);
	const glm::vec3 right = glm::normalize(render_snapshot.camera.right);
	const glm::vec3 up = glm::normalize(render_snapshot.camera.up);
	const glm::vec3 weapon_position = render_snapshot.camera.position +
		forward * (0.75f * igi::PlayerController::WORLD_METER) +
		right * (0.24f * igi::PlayerController::WORLD_METER) -
		up * (0.24f * igi::PlayerController::WORLD_METER);

	// Vanilla weapon meshes use +Y as the barrel axis. Build the camera basis
	// first, then apply the fixed-step HumanViewSway angles in rig-local space.
	// This keeps weapon transition timing in the simulation while leaving mesh
	// loading and OpenGL state in the presentation layer.
	glm::mat4 camera_basis(1.0f);
	camera_basis[0] = glm::vec4(right, 0.0f);
	camera_basis[1] = glm::vec4(forward, 0.0f);
	camera_basis[2] = glm::vec4(up, 0.0f);
	glm::mat4 local_view_sway(1.0f);
	local_view_sway = glm::rotate(
		local_view_sway,
		render_snapshot.weapon_view_pitch_radians,
		glm::vec3(1.0f, 0.0f, 0.0f));
	local_view_sway = glm::rotate(
		local_view_sway,
		render_snapshot.weapon_view_yaw_radians +
			render_snapshot.weapon_recoil_yaw_radians,
		glm::vec3(0.0f, 0.0f, 1.0f));
	local_view_sway = glm::rotate(
		local_view_sway,
		render_snapshot.weapon_recoil_pitch_radians,
		glm::vec3(1.0f, 0.0f, 0.0f));
	glm::mat4 weapon_model = glm::translate(glm::mat4(1.0f), weapon_position) *
		camera_basis * local_view_sway;
	weapon_model = glm::scale(
		weapon_model,
		glm::vec3(40.96f * 0.75f));
	renderer_.DrawAttachedMesh(
		render_snapshot.active_weapon_model_id,
		false,
		weapon_model);
}

void App::DrawGameplayProjectiles() {
	if (!IsGameplayRenderTarget()) return;

	const igi::RuntimeRenderSnapshot& render_snapshot =
		gameplay_host_.GetRenderSnapshot();
	for (const igi::RuntimeProjectileRenderState& projectile :
		render_snapshot.projectiles) {
		const char* model_id = nullptr;
		switch (projectile.type) {
			case igi::ProjectileType::FragGrenade: model_id = "135_01_1"; break;
			case igi::ProjectileType::Flashbang: model_id = "137_01_1"; break;
			case igi::ProjectileType::ProximityMine: model_id = "136_01_1"; break;
			case igi::ProjectileType::Rocket: model_id = "110_01_1"; break;
			case igi::ProjectileType::None: break;
		}
		if (model_id == nullptr) continue;

		glm::mat4 projectile_model = glm::translate(
			glm::mat4(1.0f),
			projectile.position);
		projectile_model = glm::rotate(
			projectile_model,
			glm::radians(static_cast<float>(projectile.tumble_ticks * 17U)),
			glm::vec3(0.0f, 0.0f, 1.0f));
		projectile_model = glm::scale(
			projectile_model,
			glm::vec3(40.96f * (projectile.type == igi::ProjectileType::Rocket
				? 0.65f
				: 0.35f)));
		renderer_.DrawAttachedMesh(model_id, false, projectile_model);
	}
}

void App::DrawGameplayExplosions() {
	if (!IsGameplayRenderTarget()) return;

	const igi::RuntimeRenderSnapshot& render_snapshot =
		gameplay_host_.GetRenderSnapshot();
	for (const igi::RuntimeExplosionRenderState& explosion :
		render_snapshot.explosions) {
		if (explosion.is_flashbang) continue;

		const float age_fraction = 1.0f - static_cast<float>(
			explosion.remaining_ticks) /
			static_cast<float>(
				igi::RuntimeExplosionRenderState::DISPLAY_DURATION_TICKS);
		const float radius_meters = std::max(
			0.75f,
			explosion.radius_units /
			igi::PlayerController::WORLD_METER * 0.35f);
		const float expansion = 0.85f + age_fraction * 0.35f;
		glm::mat4 explosion_model = glm::translate(
			glm::mat4(1.0f),
			explosion.position);
		explosion_model = glm::scale(
			explosion_model,
			glm::vec3(40.96f * radius_meters * expansion));
		renderer_.DrawAttachedMesh("1009_01_1", false, explosion_model);
	}
}

void App::DrawGameplayGuardMuzzleFlashes() {
	if (!IsGameplayRenderTarget()) return;

	const igi::RuntimeRenderSnapshot& render_snapshot =
		gameplay_host_.GetRenderSnapshot();
	for (const igi::RuntimeGuardMuzzleFlashState& flash :
		render_snapshot.guard_muzzle_flashes) {
		const float radius_meters = std::max(
			0.04f,
			0.12f * std::clamp(flash.strength, 0.0f, 1.0f));
		glm::mat4 flash_model = glm::translate(
			glm::mat4(1.0f),
			flash.position);
		flash_model = glm::scale(
			flash_model,
			glm::vec3(40.96f * radius_meters));
		renderer_.DrawAttachedMesh("1009_01_1", false, flash_model);
	}
}

void App::ToggleShowHUD() {
	show_hud_ = true;
}

bool App::GetShowHUD() const {
	return show_hud_;
}

void App::SetShowHUD(bool show) {
	show_hud_ = show;
}

void App::ToggleEditMode() {
	// Preserve the historical API used by editor commands while making the
	// gameplay session the single owner of the mode transition.
	ToggleGamePlayMode();
}

bool App::GetEditMode() const {
	return !in_game_mode_;
}

void App::SetEditMode(bool enabled) {
	const bool currently_in_editor = !in_game_mode_;
	if (enabled == currently_in_editor) {
		return;
	}
	ToggleGamePlayMode();
}

void App::SetTerrainEditEnabled(bool enabled) {
	if (enabled && in_game_mode_) {
		// Terrain queries are part of the live runtime collision contract. Keep
		// authoring terrain immutable until gameplay is closed or explicitly
		// rebuilt, rather than letting a brush edit alter an active session.
		terrain_edit_enabled_ = false;
		status_message_ = "Terrain editing is disabled during gameplay; close gameplay before changing terrain";
		return;
	}
	terrain_edit_enabled_ = enabled;
	if (enabled) {
		static const char* kNames[] = {"Raise","Lower","Soften","Flatten"};
		int b = (edit_brush_ >= 0 && edit_brush_ < 4) ? edit_brush_ : 0;
		status_message_ = std::string("Terrain edit ON | Brush: ") + kNames[b] +
			" | Radius: " + std::to_string((long)edit_brush_radius_) +
			" | Strength: " + std::to_string((long)edit_brush_strength_);
	}
	UpdateCursorMode(); // Force cursor update instantly
	glutPostRedisplay(); // Force instant UI refresh
}

bool App::GetTerrainEditEnabled() const {
	return terrain_edit_enabled_;
}

void App::TogglePauseMenu() {
	pause_mode_ = !pause_mode_;
	// Keep the application-level pause menu and the fixed-step scheduler on the
	// same state boundary. Frame() intentionally skips simulation while this menu
	// is visible, but the scheduler must also reject the wall-clock gap so resume
	// cannot replay stale input through its catch-up budget.
	if (in_game_mode_) {
		gameplay_host_.SetPaused(pause_mode_);
	}
	// cursor_visible_ stays TRUE always â€” camera lock is handled dynamically in Input_OnMotion.
	// Hiding the cursor permanently caused the "mouse stuck" bug after resuming.
	window_state_.cursor_visible_ = true;
	if (pause_mode_) {
		// Opening pause menu: seed level spinner with current level
		int cur = level_.GetLevelNo();
		if (cur > 0) pause_level_input_ = std::to_string(cur);
		pause_active_input_ = -1;
		// The live menu is the loaded tree (issue #74): load it on first use and
		// open the editor's own screen, so the rows on screen are the model's.
		// Opening the screen here (rather than in the loader) is what makes every
		// Escape start at the editor's own rows again, while the rows themselves
		// and their actions stay the loaded tree's.
		EnsurePauseMenu();
		igi::app_menu::OpenEditorPauseMenu(pause_menu_);
		Logger::Get().Log(LogLevel::INFO, "pause menu: opened");
		// What the menu shows, in the editor's own words: the only evidence a run on
		// this host can give (Wine maps no visible window here).
		LogPauseMenuDrawnText();
		// The menu's pointer is the GAME's, not the platform's: load
		// `LOCAL:menusystem/pointer.spr` out of the retail archive now (the GL
		// context is current here) so the very first menu frame draws it, and
		// keep the platform cursor hidden unless that sprite could not be read.
		// The game never shows the system cursor in a menu - `MenuManager_Update`
		// HOOK 0x417CF0 points its own `Cursor` task at the sprite
		// (src/menu/menumanager.c:233) and `Cursor_Draw` HOOK 0x424C40 blits it
		// (src/widget/cursor.c:38-41).
	} else {
		// Closing pause menu: reset mouse state so no stale drag occurs
		Logger::Get().Log(LogLevel::INFO, "pause menu: closed");
		input_.mouse_delta_x_ = 0;
		input_.mouse_delta_y_ = 0;
		mouse_state_.left_button_down_ = false;
		skip_input_on_motion_once_ = false;
		if (in_game_mode_) {
			// Resume first-person look: hide the OS cursor and re-center it.
			glutSetCursor(GLUT_CURSOR_NONE);
			mouse_state_.prior_x_ = gameplay_viewport_width_ >> 1;
			mouse_state_.prior_y_ = gameplay_viewport_height_ >> 1;
			glutWarpPointer(mouse_state_.prior_x_, mouse_state_.prior_y_);
		} else {
			glutSetCursor(GLUT_CURSOR_INHERIT);
		}
	}
}

bool App::GetPauseMode() const {
	return pause_mode_;
}

LevelObjects& App::GetActiveRenderLevelObjects() {
	return IsGameplayRenderTarget() && runtime_level_objects_.has_value()
		? runtime_level_objects_.value()
		: level_.GetLevelObjects();
}

void App::ToggleGamePlayMode() {
	const bool entering_gameplay = !in_game_mode_;
	if (entering_gameplay) {
		if (level_.GetLevelNo() <= 0) {
			status_message_ = "Cannot enter Game Play: load a level first";
			return;
		}

		igi::EditorSnapshot snap;
		snap.camera_pos = viewer_.pos_;
		snap.camera_yaw = viewer_.yaw_;
		snap.camera_pitch = viewer_.pitch_;
		snap.was_edit_mode = edit_mode_;
		snap.was_noclip_mode = noclip_mode_;
		snap.was_hud_visible = show_hud_;
		snap.selected_object_id = selected_object_index_;
		gameplay_editor_snapshot_ = snap;
		runtime_level_objects_ = level_.GetLevelObjects();
		runtime_initial_deleted_flags_.clear();
		runtime_initial_deleted_flags_.reserve(
			runtime_level_objects_->GetObjects().size());
		runtime_conditionally_hidden_flags_.assign(
			runtime_level_objects_->GetObjects().size(),
			0U);
		runtime_guard_generator_hidden_flags_.assign(
			runtime_level_objects_->GetObjects().size(),
			0U);
		for (const LevelObject& object : runtime_level_objects_->GetObjects()) {
			runtime_initial_deleted_flags_.push_back(object.deleted ? 1U : 0U);
		}
		noclip_mode_ = false;

		// 1. Read config.qvm profile for controls, sensitivity, sound volume
		igi::ProfileConfig profile = igi::ConfigQvmLoader::GetActiveProfile();
		gameplay_host_.GetInputRouter().SetProfile(profile);

		// 2. Read humanplayer.qvm tuning for player speed, jump impulse, health
		igi::HumanPlayerTuning tuning = igi::HumanPlayerConfigLoader::Load();
		const igi::PlayerController::Tuning player_tuning = tuning.ToControllerTuning();
		gameplay_host_.GetWorld().SetPlayerTuning(player_tuning);
		std::vector<uint32_t> configured_weapon_cycle;
		for (const int weapon_id : tuning.weapon_cycle) {
			if (weapon_id >= 0) {
				configured_weapon_cycle.push_back(static_cast<uint32_t>(weapon_id));
			}
		}
		gameplay_host_.GetWorld().SetPlayerWeaponCycle(configured_weapon_cycle);

		// 3. Spawn resolution: the editor camera is the primary spawn point so
		//    playtesting starts exactly where the level was being inspected.
		//    Authored HumanPlayer and level-start positions are fallbacks for
		//    an untouched/default camera.
		glm::vec3 spawn_pos(0.0f);
		float spawn_yaw = 0.0f;
		float spawn_pitch = 0.0f;
		bool found_spawn = false;
		bool spawned_from_camera = false;

		std::vector<igi::RuntimeSpawnCandidate> authored_spawn_candidates;
		const auto& spawn_objects = runtime_level_objects_->GetObjects();
		authored_spawn_candidates.reserve(spawn_objects.size());
		for (const LevelObject& object : spawn_objects) {
			if (object.deleted || object.type != "HumanPlayer") {
				continue;
			}

			int task_id = -1;
			const char* task_id_begin = object.taskId.data();
			const char* task_id_end = task_id_begin + object.taskId.size();
			const auto parse_result = std::from_chars(
				task_id_begin,
				task_id_end,
				task_id);
			if (parse_result.ec != std::errc() || parse_result.ptr != task_id_end) {
				task_id = -1;
			}

			float authored_yaw = glm::degrees(static_cast<float>(object.rot.z));
			while (authored_yaw < 0.0f) authored_yaw += 360.0f;
			while (authored_yaw >= 360.0f) authored_yaw -= 360.0f;
			authored_spawn_candidates.push_back({
				true,
				task_id,
				{
					glm::vec3(object.pos),
					authored_yaw,
					0.0f,
				},
			});
		}

		{
			const float kSpawnSnapEps = 1.0f;
			if (glm::length(viewer_.pos_) > kSpawnSnapEps ||
				fabsf(viewer_.yaw_) > kSpawnSnapEps) {
				spawn_pos = viewer_.pos_;
				spawn_yaw = viewer_.yaw_;
				spawn_pitch = viewer_.pitch_;
				found_spawn = true;
				spawned_from_camera = true;
				Logger::Get().Log(LogLevel::INFO, "[App] Spawn at editor camera: pos=(" +
					std::to_string(spawn_pos.x) + "," + std::to_string(spawn_pos.y) +
					"," + std::to_string(spawn_pos.z) + ") yaw=" + std::to_string(spawn_yaw));
			}
		}

		if (!found_spawn) {
			if (const std::optional<igi::RuntimeSpawnPoint> authored_spawn =
					igi::SelectAuthoredPlayerSpawn(authored_spawn_candidates)) {
				spawn_pos = authored_spawn->position;
				spawn_yaw = authored_spawn->yaw;
				spawn_pitch = authored_spawn->pitch;
				found_spawn = true;
				Logger::Get().Log(LogLevel::INFO, "[App] Authored HumanPlayer spawn: pos=(" +
					std::to_string(spawn_pos.x) + "," + std::to_string(spawn_pos.y) +
					"," + std::to_string(spawn_pos.z) + ") yaw=" + std::to_string(spawn_yaw));
			}
		}

		if (!found_spawn) {
			glm::vec3 l_start = level_.GetStartPos();
			if (l_start.z != 175000000.0f && (l_start.x != 0.0f || l_start.y != 0.0f)) {
				spawn_pos = l_start;
				spawn_yaw = glm::degrees(level_.GetStartYaw());
				while (spawn_yaw < 0.0f) spawn_yaw += 360.0f;
				while (spawn_yaw >= 360.0f) spawn_yaw -= 360.0f;
				found_spawn = true;
			}
		}

		// The editor camera stores the eye position. Runtime physics stores the
		// player's body base, so remove the verified standing eye height before
		// handing this spawn to the fixed-step controller.
		if (found_spawn && spawned_from_camera) {
			spawn_pos.z -= player_tuning.standing_eye_height_units;
		}
		gameplay_spawn_position_ = spawn_pos;
		gameplay_spawn_yaw_ = spawn_yaw;
		gameplay_spawn_pitch_ = spawn_pitch;

		if (!gameplay_host_.OpenGameplay(snap)) {
			gameplay_editor_snapshot_.reset();
			runtime_level_objects_.reset();
			runtime_initial_deleted_flags_.clear();
			runtime_conditionally_hidden_flags_.clear();
			runtime_guard_generator_hidden_flags_.clear();
			status_message_ = "Cannot enter Game Play: runtime session is already active";
			return;
		}
		if (!gameplay_host_.HasGameplayWindow()) {
			igi::EditorSnapshot ignored_snapshot;
			gameplay_host_.CloseGameplay(ignored_snapshot);
			gameplay_editor_snapshot_.reset();
			runtime_level_objects_.reset();
			runtime_initial_deleted_flags_.clear();
			runtime_conditionally_hidden_flags_.clear();
			runtime_guard_generator_hidden_flags_.clear();
			status_message_ = "Cannot enter Game Play: gameplay window is unavailable";
			return;
		}
		in_game_mode_ = true;
		gameplay_host_.FocusGameplayWindow();
		glutSetCursor(GLUT_CURSOR_NONE);
		mouse_state_.prior_x_ = gameplay_viewport_width_ >> 1;
		mouse_state_.prior_y_ = gameplay_viewport_height_ >> 1;
		glutWarpPointer(mouse_state_.prior_x_, mouse_state_.prior_y_);

		// Resolve authored gates before registering dynamic actors. Runtime
		// transforms and visibility stay in the gameplay object copy.
		SetupRuntimeMissionState();
		ApplyRuntimeConditionalContainerStates();
		SetupRuntimeDoors();

		// Register in-level enemies into the runtime AI system (patrol waypoints
		// come from each enemy's AIGraph). Runtime transforms stay in the render copy.
		SetupLevelAiGuards();
		gameplay_host_.GetWorld().RefreshAuthoredGuardGeneratorStates();
		ApplyRuntimeGuardGeneratorStates();
		SetupRuntimeLadders();
		SetupRuntimePlayerAnimation();
		gameplayAnimPlaybacks_.clear();
		lastGameplayAnimationTick_ = 0;
		SetupRuntimeInteractionState();

		// Initialize authored mission objectives for this specific level.
		InitializeGameplayMissionObjectives();
		int current_lvl = level_.GetLevelNo();
		if (current_lvl <= 0) current_lvl = 1;

		// Start authentic background music for the level
		PlayLevelMusic(current_lvl);

		gameplay_host_.GetWorld().GetPlayer().SetPosition(spawn_pos);
		gameplay_host_.GetWorld().GetPlayer().SetOrientation(spawn_yaw, spawn_pitch);
		ConfigureGameplayExtractionFallback(spawn_pos, spawn_yaw);
		gameplay_viewer_.pos_ = gameplay_host_.GetWorld().GetPlayer().GetEyePosition();
		gameplay_viewer_.yaw_ = spawn_yaw;
		gameplay_viewer_.pitch_ = spawn_pitch;
		gameplay_viewer_.roll_ = 0.0f;
		gameplay_map_computer_camera_.Reset();
		gameplay_map_computer_open_ = false;
		UpdateGameplayViewerVectors();

		// Disable all editor modes and editor tools
		edit_mode_ = false;
		terrain_edit_enabled_ = false;
		prop_editor_open_ = false;
		task_picker_open_ = false;
		ac_task_picker_open_ = false;
		model_picker_open_ = false;
		show_hud_ = false;
		selected_object_index_ = -1;
		hover_object_index_ = -1;
		status_message_ = "Game Mode Active (Profile: " + profile.name + "): WASD move, Mouse look/fire, Space jump, Right Ctrl crouch, C map, E activate, R reload, F8 editor/gameplay toggle, F5 apply/restart, F6 editor, F7 gameplay, ESC menu";
	} else {
		igi::EditorSnapshot snap;
		if (!gameplay_host_.CloseGameplay(snap)) {
			status_message_ = "Cannot leave Game Play: runtime session is not active";
			return;
		}
		gameplay_host_.HideGameplayWindow();
		RestoreEditorViewport();
		in_game_mode_ = false;
		gameplay_editor_snapshot_.reset();
		runtime_level_objects_.reset();
		runtime_initial_deleted_flags_.clear();
		runtime_conditionally_hidden_flags_.clear();
		runtime_guard_generator_hidden_flags_.clear();
		runtime_animation_request_serials_.clear();
		gameplayAnimPlaybacks_.clear();
		lastGameplayAnimationTick_ = 0;
		gameplay_map_computer_camera_.Reset();
		gameplay_map_computer_open_ = false;
		player_animation_driver_.ClearAnimationClips();
		viewer_.pos_ = snap.camera_pos;
		viewer_.yaw_ = snap.camera_yaw;
		viewer_.pitch_ = snap.camera_pitch;
		edit_mode_ = snap.was_edit_mode;
		selected_object_index_ = snap.selected_object_id;
		noclip_mode_ = snap.was_noclip_mode;
		show_hud_ = snap.was_hud_visible;
		input_.keys_ = 0;
		input_.mouse_delta_x_ = 0.0f;
		input_.mouse_delta_y_ = 0.0f;
		skip_input_on_motion_once_ = true;
		glutSetCursor(GLUT_CURSOR_INHERIT);
		UpdateViewerVectors();
		status_message_ = "Editor Mode Restored";
	}
}

void App::ApplyAndRestartGameplay() {
	if (!in_game_mode_ || !gameplay_host_.IsGameplayActive() ||
		!gameplay_editor_snapshot_.has_value()) {
		status_message_ = "Cannot apply gameplay: no active runtime session";
		return;
	}
	if (!IsGameplayInputFocused()) {
		CaptureEditorSnapshotForGameplayApply();
	}

	// Rebuild every mutable adapter from the current authoring level copy. The
	// source LevelObjects remain untouched; runtime deaths, door motion, and
	// animation requests are discarded as part of the explicit restart.
	runtime_level_objects_ = level_.GetLevelObjects();
	runtime_initial_deleted_flags_.clear();
	runtime_initial_deleted_flags_.reserve(
		runtime_level_objects_->GetObjects().size());
	runtime_conditionally_hidden_flags_.assign(
		runtime_level_objects_->GetObjects().size(),
		0U);
	runtime_guard_generator_hidden_flags_.assign(
		runtime_level_objects_->GetObjects().size(),
		0U);
	for (const LevelObject& object : runtime_level_objects_->GetObjects()) {
		runtime_initial_deleted_flags_.push_back(object.deleted ? 1U : 0U);
	}
	runtime_animation_request_serials_.clear();
	gameplayAnimPlaybacks_.clear();
	lastGameplayAnimationTick_ = 0;

	if (!gameplay_host_.ApplyAndRestartGameplay(*gameplay_editor_snapshot_)) {
		status_message_ = "Cannot apply gameplay: runtime session is not active";
		return;
	}

	SetupRuntimeMissionState();
	ApplyRuntimeConditionalContainerStates();
	SetupRuntimeDoors();
	SetupLevelAiGuards();
	gameplay_host_.GetWorld().RefreshAuthoredGuardGeneratorStates();
	ApplyRuntimeGuardGeneratorStates();
	SetupRuntimeLadders();
	SetupRuntimePlayerAnimation();
	gameplayAnimPlaybacks_.clear();
	lastGameplayAnimationTick_ = 0;
	SetupRuntimeInteractionState();
	InitializeGameplayMissionObjectives();

	// RuntimeWorld::Reset intentionally starts at the neutral origin. Restore
	// the authored spawn used when gameplay was opened so Apply+Restart is
	// deterministic and does not turn a source edit into a teleport surprise.
	gameplay_host_.GetWorld().GetPlayer().SetPosition(gameplay_spawn_position_);
	gameplay_host_.GetWorld().GetPlayer().SetOrientation(
		gameplay_spawn_yaw_, gameplay_spawn_pitch_);
	ConfigureGameplayExtractionFallback(
		gameplay_spawn_position_,
		gameplay_spawn_yaw_);
	gameplay_viewer_.pos_ = gameplay_host_.GetWorld().GetPlayer().GetEyePosition();
	gameplay_viewer_.yaw_ = gameplay_spawn_yaw_;
	gameplay_viewer_.pitch_ = gameplay_spawn_pitch_;
	gameplay_viewer_.roll_ = 0.0f;
	gameplay_map_computer_camera_.Reset();
	gameplay_map_computer_open_ = false;
	UpdateGameplayViewerVectors();
	pause_mode_ = false;
	gameplay_host_.SetPaused(false);
	FocusGameplayWindow();
	status_message_ = "Gameplay applied and restarted from the current editor snapshot";
}

void App::ConfigureGameplayExtractionFallback(
	const glm::vec3& spawn_position,
	float spawn_yaw) {
	igi::RuntimeWorld& runtime_world = gameplay_host_.GetWorld();
	if (runtime_world.GetLevelFlow().HasAuthoredMissionFlow()) {
		// Vanilla LevelFlow owns mission completion/failure. The synthetic zone
		// exists only for incomplete/editor-authored levels without that task.
		runtime_world.ClearExtractionZone();
		return;
	}

	const float spawn_yaw_radians = glm::radians(spawn_yaw);
	const glm::vec3 extraction_direction(
		-sinf(spawn_yaw_radians), cosf(spawn_yaw_radians), 0.0f);
	glm::vec3 extraction_center = spawn_position +
		extraction_direction * (40.0f * igi::PlayerController::WORLD_METER);
	float extraction_ground_height = extraction_center.z;
	if (GetLevelZ(extraction_center.x, extraction_center.y, extraction_ground_height)) {
		extraction_center.z = extraction_ground_height;
	}
	runtime_world.SetExtractionZone(
		extraction_center,
		8.0f * igi::PlayerController::WORLD_METER);
}

void App::SetupRuntimeMissionState() {
	const auto& authored_objects = runtime_level_objects_.has_value()
		? runtime_level_objects_->GetObjects()
		: level_.GetLevelObjects().GetObjects();

	const auto is_descendant_of = [&authored_objects](
		int object_index,
		int ancestor_index) {
		int parent_index = authored_objects[object_index].parentIndex;
		for (size_t depth = 0;
			 depth < authored_objects.size() &&
			 parent_index >= 0 &&
			 parent_index < static_cast<int>(authored_objects.size());
			 ++depth) {
			if (parent_index == ancestor_index) {
				return true;
			}
			parent_index = authored_objects[parent_index].parentIndex;
		}
		return false;
	};

	const auto collect_descendants = [
		&authored_objects,
		&is_descendant_of](int ancestor_index) {
		std::vector<int> descendant_indices;
		for (int object_index = 0;
			 object_index < static_cast<int>(authored_objects.size());
			 ++object_index) {
			if (object_index != ancestor_index &&
				is_descendant_of(object_index, ancestor_index)) {
				descendant_indices.push_back(object_index);
			}
		}
		return descendant_indices;
	};

	const auto is_guard_object = [](const LevelObject& object) {
		return object.type == "HumanSoldier" ||
			object.type == "HumanSoldierFemale" ||
			object.type == "HumanSoldierRPG";
	};

	const auto try_read_finite_float = [](const std::string& token, float& value) {
		if (token.empty()) {
			return false;
		}
		try {
			size_t parsed_characters = 0;
			value = std::stof(token, &parsed_characters);
			return parsed_characters == token.size() && std::isfinite(value);
		} catch (...) {
			return false;
		}
	};

	const auto try_read_boolean = [](const std::string& token, bool& value) {
		if (token == "TRUE" || token == "true" || token == "1") {
			value = true;
			return true;
		}
		if (token == "FALSE" || token == "false" || token == "0") {
			value = false;
			return true;
		}
		return false;
	};

	// The CutScene camera projection needs the authored task tree, and the walk
	// rule lives in `igi::CollectAuthoredCameraKeyTokens`
	// (source/runtime/cutscene_camera.h) so it can be tested without a level
	// archive. This is the mechanical part: three fields per object, and it is
	// only built when the level authors a CutScene at all (11 of the 14 retail
	// levels do not).
	//
	// `parentIndex` is the link the level loader builds as it parses
	// (`LoadRecursive`, source/level/level_objects.cpp:670-676, passes
	// `currentObjIdx` down as the nested task's parent), so it is exactly the
	// child chain `CutScene_CollectKeys` 0x4F4880 walks in the engine
	// (src/cutscene/cutscene.c:163-180).
	std::vector<igi::AuthoredCameraKeyObject> camera_key_objects;
	const bool has_cut_scene = std::any_of(
		authored_objects.begin(), authored_objects.end(),
		[](const LevelObject& object) { return object.type == "CutScene"; });
	if (has_cut_scene) {
		camera_key_objects.reserve(authored_objects.size());
		for (const LevelObject& authored_object : authored_objects) {
			igi::AuthoredCameraKeyObject projected;
			projected.parent_index = authored_object.parentIndex;
			projected.type = authored_object.type;
			projected.arg_tokens = authored_object.argTokens;
			projected.deleted = authored_object.deleted;
			camera_key_objects.push_back(std::move(projected));
		}
	}

	std::vector<igi::MissionStateTaskSource> task_sources;
	for (int object_index = 0;
		 object_index < static_cast<int>(authored_objects.size());
		 ++object_index) {
		const LevelObject& authored_object = authored_objects[object_index];
		// CutScene tasks ARE collected: the camera programme below the task is
		// the retail intro/end cinematic, and the runtime plays it with the
		// engine's own timeline rule (GitHub issue #80). The run/reset
		// expression gating stays with the runtime's expression state; this
		// function only carries the authored data across.
		if (authored_object.deleted ||
			(authored_object.type != "AreaActivate" &&
				authored_object.type != "EditVariable" &&
				authored_object.type != "LevelTimer" &&
				authored_object.type != "StatusMessage" &&
				authored_object.type != "ConditionalSound" &&
				authored_object.type != "ConditionalContainer" &&
				authored_object.type != "GuardGenerator" &&
				authored_object.type != "ExplodeObject" &&
				authored_object.type != "CutScene")) {
			continue;
		}

		igi::MissionStateTaskSource task_source;
		task_source.task_type = authored_object.type;
		task_source.task_id = authored_object.taskId;
		task_source.object_index = object_index;
		task_source.argument_tokens = authored_object.argTokens;
		if (authored_object.type == "CutScene") {
			task_source.edit_camera_arg_tokens =
				igi::CollectAuthoredCameraKeyTokens(camera_key_objects, object_index);
		}
		if (authored_object.type == "ConditionalContainer") {
			task_source.descendant_object_indices =
				collect_descendants(object_index);
		}
		if (authored_object.type == "GuardGenerator") {
			for (const int descendant_index : collect_descendants(object_index)) {
				if (descendant_index < 0 ||
					descendant_index >= static_cast<int>(authored_objects.size()) ||
					authored_objects[descendant_index].deleted ||
					!is_guard_object(authored_objects[descendant_index])) {
					continue;
				}
				task_source.guard_object_indices.push_back(descendant_index);
			}
		}
		task_sources.push_back(std::move(task_source));
	}

	igi::AuthoredMissionStateDefinitions definitions =
		igi::LoadAuthoredMissionStateDefinitions(task_sources);
	const std::string game_root = Utils::GetIGIRootPath();
	const igi::MissionTextResources mission_text_resources =
		MakeMissionTextResources(game_root);
	for (igi::AuthoredMissionStatusMessage& status_message :
		definitions.status_messages) {
		status_message.display_text = mission_text_resources.Resolve(
			status_message.text_resource);
	}
	const size_t area_count = definitions.area_activations.size();
	const size_t edit_variable_count = definitions.edit_variables.size();
	const size_t timer_count = definitions.level_timers.size();
	const size_t cut_scene_count = definitions.cut_scenes.size();
	const size_t conditional_sound_count = definitions.conditional_sounds.size();
	const size_t conditional_container_count =
		definitions.conditional_containers.size();
	const size_t guard_generator_count = definitions.guard_generators.size();
	const size_t explode_object_count = definitions.explode_objects.size();
	const size_t status_message_count = definitions.status_messages.size();
	gameplay_host_.GetWorld().SetAuthoredMissionState(
		std::move(definitions.area_activations),
		std::move(definitions.edit_variables),
		std::move(definitions.level_timers),
		std::move(definitions.status_messages),
		std::move(definitions.cut_scenes),
		std::move(definitions.conditional_sounds),
		std::move(definitions.explode_objects),
		std::move(definitions.conditional_containers),
		std::move(definitions.guard_generators));

	Logger::Get().Log(
		LogLevel::INFO,
		"[Gameplay] Loaded " + std::to_string(area_count) +
		" authored AreaActivate task(s) and " +
		std::to_string(edit_variable_count) +
		" authored EditVariable task(s), " +
		std::to_string(timer_count) +
		" LevelTimer task(s), " +
		std::to_string(cut_scene_count) +
		" CutScene task(s), " +
		std::to_string(conditional_sound_count) +
		" ConditionalSound task(s), " +
		std::to_string(conditional_container_count) +
		" ConditionalContainer task(s), " +
		std::to_string(guard_generator_count) +
		" GuardGenerator task(s), " +
		std::to_string(explode_object_count) +
		" ExplodeObject task(s), " +
		std::to_string(status_message_count) +
		" StatusMessage task(s)");
}

void App::SetupRuntimeDoors() {
	const auto& authored_objects = runtime_level_objects_.has_value()
		? runtime_level_objects_->GetObjects()
		: level_.GetLevelObjects().GetObjects();

	// The per-argument readers that used to live here moved into
	// `igi::ApplyDoorArgumentLayout` (runtime/door_params.h) so the layout can be
	// tested with real retail task lines; they had no other caller in this function.

	std::vector<igi::RuntimeDoorDefinition> door_definitions;
	door_definitions.reserve(authored_objects.size());
	for (int object_index = 0;
		 object_index < static_cast<int>(authored_objects.size());
		 ++object_index) {
		const LevelObject& authored_object = authored_objects[object_index];
		if (authored_object.deleted || authored_object.type != "Door") {
			continue;
		}

		igi::RuntimeDoorDefinition definition;
		definition.object_index = object_index;
		definition.task_id = authored_object.taskId;
		definition.closed_position_units = authored_object.pos;
		definition.closed_rotation_radians = static_cast<float>(authored_object.rot.z);
		// Everything that depends on the engine's argument layout lives in
		// `runtime/door_params.h`, so the mapping is a pure function that
		// `tests/test_door_params.cpp` drives with all 33 retail `Door` tasks. See
		// that header for the IGI-RE citations; in short, `slide` is two components
		// (`door.h:45`) and the three expression arguments are 17/18/19
		// (lock/open/close), which this call site previously read one slot early.
		igi::ApplyDoorArgumentLayout(
			authored_object.argTokens,
			igi::PlayerController::WORLD_METER,
			definition);
		door_definitions.push_back(std::move(definition));
	}

	gameplay_host_.GetWorld().SetAuthoredDoors(std::move(door_definitions));
}

void App::SetupRuntimeInteractionState() {
	const auto& authored_objects = runtime_level_objects_.has_value()
		? runtime_level_objects_->GetObjects()
		: level_.GetLevelObjects().GetObjects();
	const auto set_boolean = [this](
		const std::string& task_type,
		const std::string& task_id,
		const std::string& field,
		bool value) {
		if (task_id.empty() || task_id == "-1") {
			return;
		}
		gameplay_host_.GetWorld().SetMissionStateBoolean(
			task_type + "_" + task_id + "." + field,
			value);
	};

	for (const LevelObject& authored_object : authored_objects) {
		if (authored_object.deleted) {
			continue;
		}
		if (authored_object.type == "Terminal") {
			set_boolean(authored_object.type, authored_object.taskId, "isHacked", false);
			set_boolean(authored_object.type, authored_object.taskId, "isExploded", false);
		} else if (authored_object.type == "Switch") {
			set_boolean(authored_object.type, authored_object.taskId, "isPressed", false);
			set_boolean(authored_object.type, authored_object.taskId, "isLastPressed", false);
		} else if (authored_object.type == "Generator") {
			set_boolean(authored_object.type, authored_object.taskId, "isOn", false);
		} else if (authored_object.type == "Car") {
			set_boolean(authored_object.type, authored_object.taskId, "isUsed", false);
		} else if (authored_object.type == "GunPickup" ||
				authored_object.type == "AmmoPickup" ||
				authored_object.type == "GenericPickup") {
			// Only the task's own type publishes the flag: register_flag names it
			// with Task_GetName (comprun/pickup.c:202-213, param/task.c:123-135),
			// so a GunPickup owns "GunPickup_<id>.isPickedUp" and a GenericPickup
			// owns "GenericPickup_<id>.isPickedUp". Nothing registers the
			// cross-product, so the old extra "GenericPickup_" reset for every
			// pickup type cleared a symbol no level script can observe.
			set_boolean(authored_object.type, authored_object.taskId, "isPickedUp", false);
		} else if (authored_object.type == "GenericTBA") {
			set_boolean(authored_object.type, authored_object.taskId, "isFinished", false);
			set_boolean(authored_object.type, authored_object.taskId, "isFinishedThisTick", false);
		}
	}
}

void App::ApplyRuntimeDoorStates() {
	if (!runtime_level_objects_.has_value()) {
		return;
	}

	auto& objects = runtime_level_objects_->GetObjects();
	for (const igi::RuntimeDoorSnapshot& snapshot :
		gameplay_host_.GetWorld().GetDoorSnapshots()) {
		if (snapshot.object_index < 0 ||
			snapshot.object_index >= static_cast<int>(objects.size())) {
			continue;
		}

		LevelObject& object = objects[static_cast<size_t>(snapshot.object_index)];
		// The slide offset is rotated by the door's CLOSED basis, in the
		// engine's Euler order. IGI-RE `Door_Place` (HOOK 0x446290,
		// src/door/door.c:121-138) computes, with `base` the closed matrix
		// (row-major 3x3) and `m = base.m`:
		//     u = slide[0]*slidePos*kSlideScale;  v = slide[1]*slidePos*kSlideScale;
		//     x = m[1]*v + m[0]*u;  y = m[4]*v + m[3]*u;  z = m[7]*v + m[6]*u;
		// i.e. offset_world = base * (u, v, 0). `base` is the authored orientation
		// (`Param_Real32x9_Parse` -> `Matrix_SetEuler`, Rz*Ry*Rx; door.c:292 copies
		// it from the object matrix), and the opening rotation is post-multiplied
		// on top of it (`Matrix_RotateZ(&d->o.matrix, &d->base, angle)`, door.c:446),
		// which is why the closed yaw is used here and not `rot.z`.
		// This site previously composed Rz(closed)*Rx(rot.x)*Ry(rot.y) — X and Y
		// swapped — so a rotated door slid along the wrong world axis.
		const glm::dvec3 closed_rot(object.rot.x, object.rot.y,
			static_cast<double>(snapshot.closed_rotation_radians));
		const glm::vec3 world_slide_offset =
			igi::IgiEulerRotationMat3(closed_rot) * snapshot.slide_offset_units;

		object.pos = snapshot.closed_position_units +
			glm::dvec3(world_slide_offset);
		object.rot.z = static_cast<double>(snapshot.closed_rotation_radians) +
			snapshot.angle_radians;
	}
}

void App::ApplyRuntimeConditionalContainerStates() {
	if (!runtime_level_objects_.has_value()) {
		return;
	}

	auto& objects = runtime_level_objects_->GetObjects();
	if (runtime_initial_deleted_flags_.size() != objects.size()) {
		runtime_initial_deleted_flags_.clear();
		runtime_initial_deleted_flags_.reserve(objects.size());
		runtime_conditionally_hidden_flags_.assign(objects.size(), 0U);
		for (const LevelObject& object : objects) {
			runtime_initial_deleted_flags_.push_back(object.deleted ? 1U : 0U);
		}
	} else if (runtime_conditionally_hidden_flags_.size() != objects.size()) {
		runtime_conditionally_hidden_flags_.assign(objects.size(), 0U);
	}

	std::vector<uint8_t> visible_by_conditional_container(
		objects.size(),
		1U);
	for (const igi::RuntimeConditionalContainerSnapshot& snapshot :
		 gameplay_host_.GetWorld().GetConditionalContainerSnapshots()) {
		if (snapshot.is_running) {
			continue;
		}
		for (const int descendant_index : snapshot.descendant_object_indices) {
			if (descendant_index >= 0 &&
				descendant_index < static_cast<int>(objects.size())) {
				visible_by_conditional_container[
					static_cast<size_t>(descendant_index)] = 0U;
			}
		}
	}

	for (size_t object_index = 0; object_index < objects.size(); ++object_index) {
		if (runtime_initial_deleted_flags_[object_index] != 0U) {
			objects[object_index].deleted = true;
			continue;
		}

		const bool is_conditionally_hidden =
			visible_by_conditional_container[object_index] == 0U;
		if (is_conditionally_hidden) {
			objects[object_index].deleted = true;
		} else if (runtime_conditionally_hidden_flags_[object_index] != 0U) {
			// Only restore visibility that this gate hid. A pickup, dead guard,
			// or authored explosion deleted by gameplay remains deleted.
			objects[object_index].deleted = false;
		}
		runtime_conditionally_hidden_flags_[object_index] =
			is_conditionally_hidden ? 1U : 0U;
	}
}

void App::ApplyRuntimeGuardGeneratorStates() {
	if (!runtime_level_objects_.has_value()) {
		return;
	}

	auto& objects = runtime_level_objects_->GetObjects();
	if (runtime_guard_generator_hidden_flags_.size() != objects.size()) {
		runtime_guard_generator_hidden_flags_.assign(objects.size(), 0U);
	}

	std::vector<uint8_t> visible_by_guard_generator(objects.size(), 1U);
	for (const igi::RuntimeGuardGeneratorSnapshot& snapshot :
		 gameplay_host_.GetWorld().GetGuardGeneratorSnapshots()) {
		const std::unordered_set<int> active_guard_object_indices(
			snapshot.active_guard_object_indices.begin(),
			snapshot.active_guard_object_indices.end());
		for (size_t guard_index = 0;
			 guard_index < snapshot.guard_object_indices.size();
			 ++guard_index) {
			const int object_index = snapshot.guard_object_indices[guard_index];
			if (object_index < 0 ||
				object_index >= static_cast<int>(objects.size())) {
				continue;
			}
			const bool is_enabled = active_guard_object_indices.contains(object_index);
			visible_by_guard_generator[static_cast<size_t>(object_index)] =
				visible_by_guard_generator[static_cast<size_t>(object_index)] &&
				is_enabled;
		}
	}

	for (size_t object_index = 0; object_index < objects.size(); ++object_index) {
		if (runtime_initial_deleted_flags_.size() == objects.size() &&
			runtime_initial_deleted_flags_[object_index] != 0U) {
			objects[object_index].deleted = true;
			continue;
		}

		const bool is_hidden_by_guard_generator =
			visible_by_guard_generator[object_index] == 0U;
		if (is_hidden_by_guard_generator) {
			objects[object_index].deleted = true;
		} else if (runtime_guard_generator_hidden_flags_[object_index] != 0U) {
			const bool is_conditionally_hidden =
				runtime_conditionally_hidden_flags_.size() == objects.size() &&
				runtime_conditionally_hidden_flags_[object_index] != 0U;
			const igi::AiGuardEntity* guard =
				gameplay_host_.GetWorld().GetAi().FindGuard(
					static_cast<uint32_t>(object_index));
			const bool is_dead = guard != nullptr &&
				guard->state == igi::AiGuardState::Dead;
			if (!is_conditionally_hidden && !is_dead) {
				// Restore only visibility owned by this gate. Dead guards and
				// other authored gates remain deleted for the session.
				objects[object_index].deleted = false;
			}
		}
		runtime_guard_generator_hidden_flags_[object_index] =
			is_hidden_by_guard_generator ? 1U : 0U;
	}
}

void App::ApplyRuntimeExplodeObjectStates() {
	if (!runtime_level_objects_.has_value()) {
		return;
	}

	auto& objects = runtime_level_objects_->GetObjects();
	for (const igi::RuntimeExplodeObjectSnapshot& snapshot :
		gameplay_host_.GetWorld().GetExplodeObjectSnapshots()) {
		if (!snapshot.is_exploded ||
			snapshot.object_index < 0 ||
			snapshot.object_index >= static_cast<int>(objects.size())) {
			continue;
		}

		LevelObject& object = objects[static_cast<size_t>(snapshot.object_index)];
		if (snapshot.destroyed_model_name.empty()) {
			object.deleted = true;
			continue;
		}

		object.modelId = snapshot.destroyed_model_name;
	}
}

void App::InitializeGameplayMissionObjectives() {
	const int loaded_level_number = level_.GetLevelNo();
	const uint32_t mission_number = static_cast<uint32_t>(
		loaded_level_number > 0 ? loaded_level_number : 1);

	const auto& authored_objects = runtime_level_objects_.has_value()
		? runtime_level_objects_->GetObjects()
		: level_.GetLevelObjects().GetObjects();

	std::vector<igi::MissionObjectiveTaskSource> objective_task_sources;
	objective_task_sources.reserve(authored_objects.size());
	std::vector<igi::MissionFlowTaskSource> flow_task_sources;
	flow_task_sources.reserve(authored_objects.size());
	std::unordered_map<int32_t, igi::MissionObjectiveLocation> objective_locations;
	objective_locations.reserve(authored_objects.size());
	for (const LevelObject& authored_object : authored_objects) {
		if (authored_object.deleted) {
			continue;
		}
		if (!authored_object.taskId.empty() && authored_object.taskId != "-1") {
			int32_t task_id = -1;
			const char* task_id_begin = authored_object.taskId.data();
			const char* task_id_end = task_id_begin + authored_object.taskId.size();
			const auto parse_result = std::from_chars(
				task_id_begin,
				task_id_end,
				task_id);
			if (parse_result.ec == std::errc() && parse_result.ptr == task_id_end) {
				objective_locations.emplace(
					task_id,
					igi::MissionObjectiveLocation{
						authored_object.pos.x,
						authored_object.pos.y,
						authored_object.pos.z});
			}
		}
		if (authored_object.type == "DefineComputerObjective") {
			igi::MissionObjectiveTaskSource task_source;
			task_source.task_type = authored_object.type;
			task_source.argument_tokens = authored_object.argTokens;
			objective_task_sources.push_back(std::move(task_source));
			continue;
		}
		if (authored_object.type != "LevelFlow") {
			continue;
		}

		igi::MissionFlowTaskSource task_source;
		task_source.task_type = authored_object.type;
		task_source.argument_tokens = authored_object.argTokens;
		flow_task_sources.push_back(std::move(task_source));
	}

	std::vector<igi::AuthoredMissionObjectiveSet> authored_objective_sets =
		igi::LoadAuthoredMissionObjectiveDefinitions(objective_task_sources);
	const std::vector<igi::AuthoredMissionFlowDefinition> authored_flow_definitions =
		igi::LoadAuthoredMissionFlowDefinitions(flow_task_sources);
	igi::AuthoredMissionFlowDefinition authored_flow;
	if (!authored_flow_definitions.empty()) {
		// LevelFlow is normally unique. Preserve the last authored row if an
		// edited snapshot contains duplicates, matching task-update order.
		authored_flow = authored_flow_definitions.back();
	}
	if (authored_objective_sets.empty()) {
		Logger::Get().Log(
			LogLevel::WARNING,
			"[Gameplay] No authored DefineComputerObjective task found for level " +
			std::to_string(mission_number) + "; using runtime fallback objectives");
	}

	const std::string game_root = Utils::GetIGIRootPath();
	// The map computer's objective names come from the same global resource
	// list as StatusMessage text (ComputerObjectives_Draw 0x469110,
	// src/compmap/objectives.c:148-149), so both call sites share one resolver.
	// Captured by value: LevelFlow keeps the resolver past this call
	// (level_flow.cpp:37, used at :146-148), so a reference to a local would
	// dangle.
	const igi::MissionTextResources mission_text_resources =
		MakeMissionTextResources(game_root);
	igi::MissionObjectiveTextResolver text_resolver =
		[mission_text_resources](const std::string& resource_key) {
			return mission_text_resources.Resolve(resource_key);
		};
	igi::MissionObjectiveLocationResolver location_resolver =
		[objective_locations = std::move(objective_locations)](
			int32_t link_task_id,
			igi::MissionObjectiveLocation& location) {
			const auto location_iterator = objective_locations.find(link_task_id);
			if (location_iterator == objective_locations.end()) {
				return false;
			}
			location = location_iterator->second;
			return true;
		};

	gameplay_host_.GetWorld().GetLevelFlow().InitializeMission(
		mission_number,
		std::move(authored_objective_sets),
		std::move(text_resolver),
		authored_flow,
		std::move(location_resolver));

	Logger::Get().Log(
		LogLevel::INFO,
		"[Gameplay] Loaded " + std::to_string(authored_flow_definitions.size()) +
		" authored LevelFlow task(s)");
}

igi::RuntimeInteractionResult App::HandleGameplayInteraction(
	const glm::vec3& interaction_origin,
	const glm::vec3& interaction_direction) {
	constexpr float interaction_range = 2.5f * igi::PlayerController::WORLD_METER;
	constexpr float minimum_facing_dot = 0.2f;

	glm::vec3 horizontal_direction(interaction_direction.x, interaction_direction.y, 0.0f);
	const float direction_length = glm::length(horizontal_direction);
	if (direction_length <= 0.0001f) {
		return {};
	}
	horizontal_direction /= direction_length;

	const bool use_gameplay_snapshot =
		igi::ResolveRuntimeAssetTarget(
			in_game_mode_,
			runtime_level_objects_.has_value()) ==
		igi::RuntimeAssetTarget::GameplaySnapshot;
	LevelObjects& gameplay_level_objects = use_gameplay_snapshot &&
			runtime_level_objects_.has_value()
		? runtime_level_objects_.value()
		: GetActiveRenderLevelObjects();
	auto& objects = gameplay_level_objects.GetObjects();
	int nearest_index = -1;
	float nearest_distance = interaction_range;
	for (int object_index = 0; object_index < static_cast<int>(objects.size()); ++object_index) {
		const auto& object = objects[object_index];
		const bool is_interactable =
			object.type == "Door" || object.type == "Terminal" ||
			object.type == "Switch" || object.type == "Generator" ||
			object.type == "GenericTBA" ||
			object.type == "Car";
		if (object.deleted || !is_interactable) {
			continue;
		}

		const glm::vec3 offset = glm::vec3(object.pos) - interaction_origin;
		const glm::vec2 horizontal_offset(offset.x, offset.y);
		const float distance = glm::length(horizontal_offset);
		if (distance <= 0.0001f || distance > nearest_distance) {
			continue;
		}

		const glm::vec2 direction_to_object = horizontal_offset / distance;
		const float facing_dot = glm::dot(
			glm::vec2(horizontal_direction.x, horizontal_direction.y),
			direction_to_object);
		if (facing_dot < minimum_facing_dot) {
			continue;
		}

		nearest_index = object_index;
		nearest_distance = distance;
	}

	if (nearest_index < 0) {
		return {};
	}

	auto& target = objects[nearest_index];
	const auto current_objective_requires_authored_state = [this]() {
		for (const igi::MissionObjective& objective :
				 gameplay_host_.GetWorld().GetLevelFlow().GetObjectives()) {
			if (objective.is_primary && objective.state == igi::ObjectiveState::Pending) {
				return !objective.completion_expression.empty();
			}
		}
		return false;
	};

	if (target.type == "Door") {
		if (!gameplay_host_.GetWorld().ToggleDoor(nearest_index)) {
			status_message_ = "Door locked";
			return {};
		}
		status_message_ = "Door opened";
		return {true, !current_objective_requires_authored_state()};
	}

	if (target.type == "Terminal") {
		const std::string terminal_prefix = "Terminal_" + target.taskId;
		gameplay_host_.GetWorld().SetMissionStateBoolean(
			terminal_prefix + ".isHacked",
			true);
		gameplay_host_.GetWorld().SetMissionStatePulse(
			terminal_prefix + ".isHackedThisTick");
		status_message_ = "Terminal hacked";
		return {true, !current_objective_requires_authored_state()};
	}

	if (target.type == "Switch") {
		const std::string switch_prefix = "Switch_" + target.taskId;
		gameplay_host_.GetWorld().SetMissionStateBoolean(
			switch_prefix + ".isPressed",
			true);
		gameplay_host_.GetWorld().SetMissionStatePulse(
			switch_prefix + ".isLastPressed");
		status_message_ = "Switch activated";
		return {true, !current_objective_requires_authored_state()};
	}

	if (target.type == "Generator") {
		gameplay_host_.GetWorld().SetMissionStateBoolean(
			"Generator_" + target.taskId + ".isOn",
			true);
		status_message_ = "Generator started";
		return {true, !current_objective_requires_authored_state()};
	}

	// Pickups are deliberately absent from this function. The engine acquires
	// them on PROXIMITY, never on the player's use action: Human_UpdateUse
	// 0x460EE0 (src/humanbase/human.c:1269-1312) dispatches Game_GetEvent2 to
	// task list 0, while the pickups live in task list 2 and register nothing on
	// Event2. Their acquisition path is HumanPlayer_SendPosition 0x410F70
	// (src/humanplayer/humanplayer.c:1467-1484) -> Game_GetEvent1 ->
	// GenericPickup_Touch/GunPickup_Touch/AmmoPickup_Touch. See
	// App::ApplyPickupTouches.

	if (target.type == "GenericTBA") {
		const std::string generic_prefix = "GenericTBA_" + target.taskId;
		gameplay_host_.GetWorld().SetMissionStateBoolean(
			generic_prefix + ".isFinished",
			true);
		gameplay_host_.GetWorld().SetMissionStatePulse(
			generic_prefix + ".isFinishedThisTick");
		target.deleted = true;
		status_message_ = "Objective item placed";
		return {true, !current_objective_requires_authored_state()};
	}

	if (target.type == "Car") {
		gameplay_host_.GetWorld().SetMissionStateBoolean(
			"Car_" + target.taskId + ".isUsed",
			true);
		status_message_ = "Vehicle entered";
		// Vanilla M1's EditVariable_105 is advanced by the authored car/area
		// task path, which is not yet a general task-tree native. Keep the
		// existing interaction fallback so the vertical slice remains playable.
		return {true, true};
	}

	status_message_ = "Objective interaction complete";
	return {true, true};
}

void App::ApplyPickupTouches(const glm::vec3& player_position) {
	// The engine's touch broadcast, ported. HumanPlayer_SendPosition 0x410F70
	// (src/humanplayer/humanplayer.c:1467-1484) is the ONLY dispatcher of
	// Game_GetEvent1 in the whole engine, and it runs once per player update:
	//     if (Human_IsDead(h)) return;                        /* :1474-1475 */
	//     msg.h = h; memcpy(msg.pos, h->pos, sizeof(msg.pos)); /* :1476-1477 */
	//     for (i = 0; i < Game_GetTaskList2()->count; i++)
	//         TASK_HANDLER(Game_GetEvent1(), type)(task, &msg); /* :1478-1482 */
	// GenericPickup/GunPickup/AmmoPickup each register their touch handler on
	// exactly that event (Game_GetEvent1 0x4167E0 = gGameEvents[1],
	// src/game/game.c:714-718; pickup.c:264, :475, :657) and each handler
	// re-tests `touches()` before acting (pickup.c:363, :605-606, :739-740).
	// Nothing reads the player's facing; no key is pressed.
	//
	// The candidate set is exactly the placed pickups: task list 2 is populated
	// by the pickups' own create handlers (GenericPickup_Create adds itself at
	// pickup.c:293; GunPickup/AmmoPickup do it through pickup_create,
	// pickup.c:489) and no other task type registers an Event1 handler.
	//
	// Only the player is a toucher. AI humans call Human_SendPosition 0x463080
	// (src/viewcam/human.c:249-264), which dispatches Game_GetEvent0 to task
	// list 1 -- a different event and a different list, and no pickup registers
	// on either.
	//
	// `player_position` is the engine's `Toucher::pos` (comprun/comprun.h:17-21),
	// copied from `h->pos`, i.e. the body position Human_SetPosition 0x4631A0
	// moves with DynCube_Move (src/viewcam/human.c:314-326) -- not the eye.

	const bool use_gameplay_snapshot =
		igi::ResolveRuntimeAssetTarget(
			in_game_mode_,
			runtime_level_objects_.has_value()) ==
		igi::RuntimeAssetTarget::GameplaySnapshot;
	LevelObjects& gameplay_level_objects = use_gameplay_snapshot &&
			runtime_level_objects_.has_value()
		? runtime_level_objects_.value()
		: GetActiveRenderLevelObjects();

	for (LevelObject& object : gameplay_level_objects.GetObjects()) {
		if (object.deleted || !igi::IsPlacedPickupType(object.type)) {
			continue;
		}

		// radius() pickup.c:172-177 is `VirModel_GetRadius(o->model) *
		// max_scale(o)`. A placed pickup's scale is always 1.0 -- the only
		// writer of RigidDynCubeObj::scale is RigidDynCubeObj_Init 0x4C5710,
		// which sets all three factors to 1.0f (src/magicobj/magicobj.c:340-342)
		// -- so the range is the model's own radius, clamped up to 2867.2 by
		// `if (!(r >= kMinTouch)) range = kMinTouch;` (pickup.c:187-188).
		//
		// The model is the one GunPickup_Changed 0x46D050 / AmmoPickup_Changed
		// 0x46D7D0 / GenericPickup_Changed 0x46C490 resolved for the object
		// (pickup.c:580-581, :716-717, :334); the editor already maps the
		// WEAPON_ID_*/AMMO_ID_* enum to that model id in ResolvePickupModelId,
		// and a model that never resolved keeps radius kOne = 1.0f
		// (VirModel_GetRadius 0x4D0950, src/lod/mesh3d.c:381-387), which the
		// clamp then raises to the 0.7 m minimum.
		const float model_radius =
			renderer_.GetMeshHeaderRadius(object.modelId, object.isBuilding);

		const double pickup_pos[3] = {
			object.pos.x, object.pos.y, object.pos.z};
		const double player_pos[3] = {
			player_position.x, player_position.y, player_position.z};
		if (!igi::PickupTouchesPlayer(model_radius, pickup_pos, player_pos)) {
			continue;
		}

		GrantPickup(object);
	}
}

void App::GrantPickup(LevelObject& target) {
	// The three touch handlers, ported. Each sets `pickedUp` and the pickup is
	// consumed exactly once: the flag is never cleared (pickup.c:364, :612,
	// :746) and the next run tick removes the pickup -- GunPickup_Run 0x46CEA0
	// and AmmoPickup_Run 0x46D740 `Task_Delete(t)` (pickup.c:555-556, :682-683),
	// GenericPickup_Run 0x46C3C0 removes it from the sector and dynamic cube but
	// keeps the task (pickup.c:306-314). The editor's equivalent of both is
	// `deleted`, and the run tick is the same tick here.
	//
	// The flags are the task's OWN type: register_flag (pickup.c:203-213) names
	// the symbol with Task_GetName (src/param/task.c:123-135), which falls back
	// to "<TaskType name>_<id>" because no task type installs a level name
	// handler. So a GunPickup owns "GunPickup_<id>.isPickedUp" and nothing else.

	if (target.type == "GunPickup") {
		// GunPickup_Touch 0x46D230 (pickup.c:599-616): msg.id = t->weapon,
		// msg.count = t->ammo, delivered to the toucher's type handler
		// (WeaponGunPickup 0x45FFC0, src/humanbase/human.c:698-728) which adds
		// msg[1] to a carried gun of the same id or creates one with
		// Gun_New(h, 0, msg[0], msg[1], 1); then `t->pickedUp = 1` and the
		// "PICKEDUPWEAPON<name>" status message (:613-614).
		//
		// t->ammo is the weapon's clipSize, `*(int *)(w + 0x648)`
		// (GunPickup_Changed pickup.c:583; WeaponType.clipSize,
		// src/weapon/weapontypes.h:18-73). The editor's stand-in for that grant
		// is SelectWeaponByScriptId + RestoreOrInitializeAmmoState
		// (source/weapon_system.cpp:291-293).
		if (gameplay_host_.GetWorld().GetWeapons().SelectWeaponByScriptId(
				target.weaponEnumId)) {
			gameplay_host_.GetWorld().SetMissionStateBoolean(
				"GunPickup_" + target.taskId + ".isPickedUp",
				true);
			target.deleted = true;
			status_message_ = "Weapon acquired: " +
				gameplay_host_.GetWorld().GetWeapons().GetActiveWeapon().name;
		}
		return;
	}

	if (target.type == "AmmoPickup") {
		// AmmoPickup_Touch 0x46D990 (pickup.c:734-749): msg.id = t->ammo,
		// msg.count = t->count, delivered to WeaponAmmoPickup 0x45FF80
		// (src/humanbase/human.c:687-694), which does
		// `AmmoList_Add(h->ammo, msg[0], msg[1])`; then `t->pickedUp = 1` and
		// the "PICKEDUPAMMO" status message (:747-748).
		//
		// `t->count` is the authored fourth parameter, truncated to the field's
		// int16_t by ParamInt16_Parse 0x4F0110 (src/physobj/paramops.c:627-633).
		// The engine never substitutes the touching weapon's clip size, and
		// AmmoPickup_Create zeroes the field (pickup.c:676), so a pickup whose
		// count token is absent or malformed grants nothing. The truncation comes
		// from the ported rule (igi::TruncatePickupCount) so the two cannot drift.
		uint32_t rounds = 0;
		if (target.argTokens.size() > 10) {
			const std::string text = StripQuotes(target.argTokens[10]);
			char* end = nullptr;
			const long value = std::strtol(text.c_str(), &end, 10);
			if (end != text.c_str() && end != nullptr && *end == '\0') {
				rounds = static_cast<uint32_t>(
					igi::TruncatePickupCount(static_cast<int>(value)));
			}
		}
		gameplay_host_.GetWorld().GetWeapons().AddReserveAmmo(rounds);
		gameplay_host_.GetWorld().SetMissionStateBoolean(
			"AmmoPickup_" + target.taskId + ".isPickedUp",
			true);
		target.deleted = true;
		status_message_ = "Ammunition acquired";
		return;
	}

	// GenericPickup_Touch 0x46C630 (pickup.c:361-366) is the whole handler:
	//     if (touches(&t->obj, by)) t->pickedUp = 1;
	// It delivers no message and shows no status message; the pickup simply
	// disappears and publishes isPickedUp.
	gameplay_host_.GetWorld().SetMissionStateBoolean(
		"GenericPickup_" + target.taskId + ".isPickedUp",
		true);
	target.deleted = true;
	status_message_ = "Item acquired";
}

// Registers every in-level enemy (soldier family) into the runtime AiSystem so
// they patrol along their AIGraph waypoints during Game Play mode. Guard ids
// equal the level-object index, which the frame loop uses to update the
// session-owned render copy.
static bool LastTwoIntArgs(const std::string& qscLine, int& a, int& b);

void App::SetupLevelAiGuards() {
	auto& ai = gameplay_host_.GetWorld().GetAi();
	ai.Clear();
	gameplay_host_.GetWorld().ClearGuardScripts();

	auto& objects = GetActiveRenderLevelObjects().GetObjects();

	// Build a lookup of AIGraph taskId -> world pos (node coords in the .dat are
	// local to the AIGraph task's own position).
	std::map<std::string, glm::dvec3> graph_world_offsets;
	for (const auto& obj : objects) {
		if (!obj.deleted && obj.type == "AIGraph" && !obj.taskId.empty()) {
			graph_world_offsets[obj.taskId] = obj.pos;
		}
	}

	// Cache parsed nav graphs per graph id so soldiers sharing a graph (most of
	// them) don't re-parse the route table for every guard.
	std::map<int, std::shared_ptr<const GraphFile>> graph_cache;

	int level_no = (last_loaded_level_ >= 1) ? last_loaded_level_ : 1;

	for (int i = 0; i < (int)objects.size(); ++i) {
		const auto& obj = objects[i];
		if (obj.deleted) continue;
		bool isEnemy = (obj.type == "HumanSoldier" || obj.type == "HumanSoldierFemale" ||
		                obj.type == "HumanSoldierRPG");
		if (!isEnemy) continue;

		// No position filtering: each of the fourteen levels owns a different
		// world origin (observed z spans ~1.61e8-1.75e8), so no magnitude
		// heuristic can separate "unresolved" from "authored". A guard whose
		// task truly lacks a position simply stands still in play.

		igi::AiGuardEntity guard;
		guard.id = (uint32_t)i;
		guard.name = obj.name.empty() ? obj.type : obj.name;
		guard.mission_state_type = obj.type;
		guard.mission_task_id = obj.taskId;
		guard.weapon_script_id = !obj.weaponEnumId.empty()
			? obj.weaponEnumId
			: obj.primaryWeapon;
		guard.position = glm::vec3((float)obj.pos.x, (float)obj.pos.y, (float)obj.pos.z);
		guard.yaw = glm::degrees((float)obj.rot.z);
		// Declared idle clip (HumanSoldier arg@10); vanilla falls back to 2
		// when the task declares none or the set does not hold it.
		guard.stand_animation = (obj.standAnimation >= 0) ? obj.standAnimation : 2;

		// Resolve the AIGraph this enemy patrols (from its HumanAI child) and the
		// AI behavior script task id (ai/<taskId>.qvm).
		int aiGraphTaskId = -1;
		int aiTaskId = -1;
		std::string ai_script_path;
		for (int ci : obj.childrenIndices) {
			if (ci < 0 || ci >= (int)objects.size()) continue;
			if (!objects[ci].deleted && objects[ci].type == "HumanAI") {
				aiGraphTaskId = objects[ci].aiGraphTaskId;
				try { aiTaskId = std::stoi(objects[ci].taskId); }
				catch (...) { aiTaskId = -1; }
				break;
			}
		}

		if (aiGraphTaskId >= 0) {
			std::string tid = std::to_string(aiGraphTaskId);
			auto offIt = graph_world_offsets.find(tid);
			if (offIt != graph_world_offsets.end()) {
				auto cached = graph_cache.find(aiGraphTaskId);
				if (cached == graph_cache.end()) {
					std::string graphPath = Utils::GetIGIRootPath() +
						"\\missions\\location0\\level" + std::to_string(level_no) +
						"\\graphs\\graph" + tid + ".dat";
					auto g = std::make_shared<const GraphFile>(GRAPH_Parse(graphPath));
					graph_cache[aiGraphTaskId] = g;
					cached = graph_cache.find(aiGraphTaskId);
				}
				if (cached->second->valid && !cached->second->nodes.empty()) {
					guard.graph = cached->second;
					guard.graph_offset = glm::vec3((float)offIt->second.x,
					                               (float)offIt->second.y,
					                               (float)offIt->second.z);
					// Snapshot the graph nodes as waypoints too, so the Suspicious
					// investigation path keeps its existing behaviour.
					for (const auto& n : guard.graph->nodes) {
						guard.waypoints.push_back(glm::vec3(
							(float)(offIt->second.x + n.x),
							(float)(offIt->second.y + n.y),
							(float)(offIt->second.z + n.z)));
					}
					// The guard starts at the graph node nearest its spawn (local coords).
					guard.current_node = GRAPH_NearestNode(*guard.graph,
						obj.pos.x - offIt->second.x,
						obj.pos.y - offIt->second.y,
						obj.pos.z - offIt->second.z);
				}
			}
		}
		if (guard.waypoints.empty()) {
			// No usable AIGraph â€” stand guard in place (still hears/sees player).
			guard.waypoints.push_back(guard.position);
		}

		// Build the OpenIGI patrol command list from the AI script's
		// AIAction_Patrol(pathId) plus that PatrolPath task's children.
		if (aiTaskId >= 0) {
			std::string qvmPath = Utils::GetIGIRootPath() + "\\missions\\location0\\level" +
				std::to_string(level_no) + "\\ai\\" + std::to_string(aiTaskId) + ".qvm";
			ai_script_path = qvmPath;
			int pathId = FindAiScriptPatrolPathId(qvmPath);
			if (pathId >= 0) {
				// Find the PatrolPath task the script names (a child of this soldier
				// in retail data), then collect its PatrolPathCommand children in order.
				int ppIndex = -1;
				for (int ci : obj.childrenIndices) {
					if (ci < 0 || ci >= (int)objects.size()) continue;
					const auto& child = objects[ci];
					if (child.deleted || child.type != "PatrolPath") continue;
					try {
						if (std::stoi(child.taskId) == pathId) { ppIndex = ci; break; }
					} catch (...) {}
				}
				if (ppIndex < 0) {
					// Retail data does not always nest the PatrolPath under the
					// soldier; fall back to a global id search.
					for (int ci = 0; ci < (int)objects.size(); ++ci) {
						const auto& child = objects[ci];
						if (child.deleted || child.type != "PatrolPath") continue;
						try {
							if (std::stoi(child.taskId) == pathId) { ppIndex = ci; break; }
						} catch (...) {}
					}
				}
				if (ppIndex >= 0) {
					for (int pci : objects[ppIndex].childrenIndices) {
							if (pci < 0 || pci >= (int)objects.size()) continue;
							const auto& pcmd = objects[pci];
							if (pcmd.deleted || pcmd.type != "PatrolPathCommand") continue;
							int cmdCode = -1, param = -1;
							if (LastTwoIntArgs(pcmd.qscLine, cmdCode, param)) {
								guard.patrol_commands.push_back(
									igi::AiPatrolCommand{ (igi::AiPatrolCommandKind)cmdCode, param });
							}
					}
					guard.active_patrol_path_id = pathId;
					Logger::Get().Log(LogLevel::INFO,
						"[AI] Guard " + guard.name + " patrol path " +
						std::to_string(pathId) + ": " +
						std::to_string(guard.patrol_commands.size()) + " commands");
				} else {
					Logger::Get().Log(LogLevel::WARNING,
						"[AI] Guard " + guard.name + " names patrol path " +
						std::to_string(pathId) + " but no matching PatrolPath child");
				}
			}
		}

		// Keep every authored route available to AIAction_Patrol. The retail
		// script may select a different path after an event or script variable
		// changes; the runtime switches routes at the action boundary.
		for (int ci : obj.childrenIndices) {
			if (ci < 0 || ci >= (int)objects.size()) continue;
			const auto& patrol_path = objects[ci];
			if (patrol_path.deleted || patrol_path.type != "PatrolPath") continue;

			int route_id = -1;
			try { route_id = std::stoi(patrol_path.taskId); } catch (...) { continue; }
			std::vector<igi::AiPatrolCommand> route_commands;
			for (int pci : patrol_path.childrenIndices) {
				if (pci < 0 || pci >= (int)objects.size()) continue;
				const auto& command = objects[pci];
				if (command.deleted || command.type != "PatrolPathCommand") continue;
				int command_kind = -1, operand = -1;
				if (LastTwoIntArgs(command.qscLine, command_kind, operand)) {
					route_commands.push_back(igi::AiPatrolCommand{
						(igi::AiPatrolCommandKind)command_kind,
						operand});
				}
			}
			if (!route_commands.empty()) {
				guard.patrol_routes[route_id] = std::move(route_commands);
			}
		}

		ai.RegisterGuard(guard);
		if (!ai_script_path.empty()
			&& !gameplay_host_.GetWorld().AttachGuardScriptFromFile(guard.id, ai_script_path)) {
			Logger::Get().Log(LogLevel::WARNING,
				"[AI] Could not attach retail QVM " + ai_script_path);
		}
	}
	Logger::Get().Log(LogLevel::INFO, "[App] Registered " +
		std::to_string(ai.GetGuards().size()) + " enemy guards for Game Play");
}

void App::AddRuntimeLadderIfPresent(
    const std::string& model_id,
    bool is_building,
    const glm::mat4& draw_world_matrix,
    const glm::mat3& task_orientation,
    std::vector<igi::LadderPlacement>& ladder_placements) {
    if (!renderer_.IsLadderMagicObject(model_id)) {
        return;
    }

    const std::vector<glm::vec3> magic_vertices =
        renderer_.GetModelMagicVertices(model_id, is_building);
    if (magic_vertices.size() <= 3U) {
        Logger::Get().Log(LogLevel::WARNING,
            "[Gameplay] Ladder model " + model_id +
            " has no complete magic-vertex climb line");
        return;
    }

    // Mesh magic vertices are imported through the same 40.96 model-unit
    // conversion used by the renderer. The ladder contract consumes gameplay
    // units, so apply the renderer's leaf scale before world transformation.
    constexpr float model_units_to_runtime_units = 40.96f;
    const glm::mat4 magic_world_matrix = draw_world_matrix * glm::scale(
        glm::mat4(1.0f),
        glm::vec3(model_units_to_runtime_units));
    const auto transform_magic_vertex = [
        &magic_world_matrix,
        &magic_vertices](size_t index) {
        return glm::vec3(magic_world_matrix * glm::vec4(
            magic_vertices[index],
            1.0f));
    };

    ladder_placements.emplace_back(
        glm::vec3(draw_world_matrix[3]),
        transform_magic_vertex(1),
        transform_magic_vertex(2),
        transform_magic_vertex(3),
        task_orientation);
}

void App::CollectAttachedRuntimeLadders(
    const std::string& model_id,
    bool is_building,
    const glm::mat4& draw_world_matrix,
    const glm::mat3& task_orientation,
    std::unordered_set<std::string>& ancestry,
    std::vector<igi::LadderPlacement>& ladder_placements) {
    if (model_id.empty() || !ancestry.insert(model_id).second) {
        return;
    }

    AddRuntimeLadderIfPresent(
        model_id,
        is_building,
        draw_world_matrix,
        task_orientation,
        ladder_placements);

    glm::mat4 parent_rotation = draw_world_matrix;
    parent_rotation[3] = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    for (const AttachInfo& attachment : renderer_.GetModelAttachments(model_id, is_building)) {
        const glm::mat4 attachment_rotation(
            attachment.r[0], attachment.r[1], attachment.r[2], 0.0f,
            attachment.r[3], attachment.r[4], attachment.r[5], 0.0f,
            attachment.r[6], attachment.r[7], attachment.r[8], 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f);
        const glm::vec3 local_offset(
            attachment.px,
            attachment.py,
            attachment.pz);
        const glm::vec3 world_position = glm::vec3(
            draw_world_matrix * glm::vec4(local_offset, 1.0f));
        const glm::mat4 child_world_matrix = glm::translate(
            glm::mat4(1.0f),
            world_position) * parent_rotation * attachment_rotation;

        // The task frame uses the attachment transpose, while magic vertices
        // use the draw frame. This follows the reference's separate
        // childDrawOrientation/childTaskOrientation paths.
        const glm::mat3 child_task_orientation = task_orientation *
            glm::transpose(glm::mat3(attachment_rotation));
        CollectAttachedRuntimeLadders(
            attachment.modelId,
            is_building,
            child_world_matrix,
            child_task_orientation,
            ancestry,
            ladder_placements);
    }

    ancestry.erase(model_id);
}

void App::SetupRuntimeLadders() {
    std::vector<igi::LadderPlacement> ladder_placements;
    const bool use_gameplay_snapshot =
        igi::ResolveRuntimeAssetTarget(
            in_game_mode_,
            runtime_level_objects_.has_value()) ==
        igi::RuntimeAssetTarget::GameplaySnapshot;
    const LevelObjects& gameplay_level_objects = use_gameplay_snapshot &&
            runtime_level_objects_.has_value()
        ? runtime_level_objects_.value()
        : GetActiveRenderLevelObjects();
    const auto& objects = gameplay_level_objects.GetObjects();
    for (const LevelObject& object : objects) {
        if (object.deleted || object.modelId.empty()) {
            continue;
        }

        // Authored IGI rotation, in the engine's order. IGI-RE `Matrix_SetEuler`
        // (HOOK 0x4B38E0, src/math/matrix.c:99-110) composes Rz(az)*Ry(ay)*Rx(ax)
        // and `ParamReal32x9_Parse` (HOOK 0x4EFBA0) feeds the QSC triple straight
        // into it. This site previously composed Z -> X -> Y (i.e. Rz*Rx*Ry),
        // which gave attached runtime ladders wrong mount offsets and band axes
        // whenever the parent object was rotated. See renderer/igi_transform.h.
        const glm::mat4 object_rotation = igi::IgiEulerRotationMatrix(object.rot);

        const glm::mat4 object_world_matrix = glm::translate(
            glm::mat4(1.0f),
            glm::vec3(object.pos)) * object_rotation;
        std::unordered_set<std::string> ancestry;
        CollectAttachedRuntimeLadders(
            object.modelId,
            object.isBuilding,
            object_world_matrix,
            glm::mat3(object_rotation),
            ancestry,
            ladder_placements);
    }

    gameplay_host_.GetWorld().SetLadderPlacements(std::move(ladder_placements));
    Logger::Get().Log(LogLevel::INFO,
        "[Gameplay] Registered " + std::to_string(
            gameplay_host_.GetWorld().GetLadderPlacements().size()) +
        " authored ladder placements");
}

void App::SetupRuntimePlayerAnimation() {
    player_animation_driver_.ClearAnimationClips();

    const LevelObjects& gameplay_level_objects = runtime_level_objects_.has_value()
        ? runtime_level_objects_.value()
        : level_.GetLevelObjects();
    const auto& objects = gameplay_level_objects.GetObjects();

    int player_bone_hierarchy = -1;
    int player_object_index = -1;
    for (int object_index = 0;
         object_index < static_cast<int>(objects.size());
         ++object_index) {
        const LevelObject& object = objects[static_cast<size_t>(object_index)];
        if (!object.deleted && object.type == "HumanPlayer" &&
            object.boneHierarchy >= 0) {
            player_bone_hierarchy = object.boneHierarchy;
            player_object_index = object_index;
            break;
        }
    }

    if (player_bone_hierarchy < 0) {
        Logger::Get().Log(
            LogLevel::WARNING,
            "[Gameplay] No HumanPlayer animation hierarchy found; using physics movement fallback");
        return;
    }

    // HumanLocomotionStates.cs plus the three authored ladder animations. The
    // registry is already imported during level loading, so this setup only
    // injects immutable clip addresses into the fixed-step driver.
    constexpr int player_animation_ids[] = {
        2, 4, 37, 35, 64, 70, 68, 11, 10,
        57, 74, 72, 75, 77, 76, 81, 80,
        168, 169, 170,
    };
    int resolved_clip_count = 0;
    for (const int animation_id : player_animation_ids) {
        const AnimationClip* animation_clip = animRegistry_.GetClipByAnimId(
            player_bone_hierarchy,
            animation_id);
        if (animation_clip == nullptr) {
            continue;
        }
        player_animation_driver_.SetAnimationClip(animation_id, animation_clip);
        ++resolved_clip_count;
    }

    Logger::Get().Log(
        LogLevel::INFO,
        "[Gameplay] Player animation driver resolved " +
        std::to_string(resolved_clip_count) + "/19 clips for object " +
        std::to_string(player_object_index) +
        (resolved_clip_count == 0 ? "; physics movement fallback remains active" : ""));
}

void App::SetEditBrush(int brush) {
	if (brush < 0) brush = 0;
	if (brush > 3) brush = 3;
	edit_brush_ = brush;
	static const char* kNames[] = {"Raise", "Lower", "Soften", "Flatten"};
	status_message_ = std::string("Terrain brush: ") + kNames[edit_brush_] +
		"  (radius " + std::to_string((long)edit_brush_radius_) +
		", strength " + std::to_string((long)edit_brush_strength_) + ")";
	UpdateCursorMode();
	glutPostRedisplay();
}

int App::GetEditBrush() const {
	return edit_brush_;
}

bool App::TerrainPaletteClick(int x, int y) {
	if (!terrain_edit_enabled_) return false;
	int idx = TerrainPalette::HitTest(x, y, window_state_.viewport_width_, window_state_.viewport_height_);
	if (idx < 0) return false;
	switch (idx) {
	case TerrainPalette::kSelect:
		// Select/exit button: leave terrain edit, back to object editing.
		SetTerrainEditEnabled(false);
		break;
	case TerrainPalette::kRadiusDec:   AdjustBrushRadius(0.8);    break;
	case TerrainPalette::kRadiusInc:   AdjustBrushRadius(1.25);   break;
	case TerrainPalette::kStrengthDec: AdjustBrushStrength(-1.0); break;
	case TerrainPalette::kStrengthInc: AdjustBrushStrength(1.0);  break;
	default:
		SetEditBrush(TerrainPalette::BrushForIndex(idx));
		break;
	}
	return true;
}

void App::AdjustBrushRadius(double factor) {
	edit_brush_radius_ *= factor;
	if (edit_brush_radius_ < 5000.0)   edit_brush_radius_ = 5000.0;
	if (edit_brush_radius_ > 250000.0) edit_brush_radius_ = 250000.0;
	status_message_ = "Brush radius: " + std::to_string((long)edit_brush_radius_);
	glutPostRedisplay();
}

void App::AdjustBrushStrength(double delta) {
	edit_brush_strength_ += delta;
	if (edit_brush_strength_ < 1.0)   edit_brush_strength_ = 1.0;
	if (edit_brush_strength_ > 100.0) edit_brush_strength_ = 100.0;
	status_message_ = "Brush strength: " + std::to_string((long)edit_brush_strength_);
	glutPostRedisplay();
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

// â”€â”€ Animation system â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
// (Per-object auto-play init now lives in LoadLevel's parallel resolution pass;
//  see app_level.cpp. There is no single-object initializer anymore.)

void App::UpdateAnimations(float dtSec, const std::unordered_set<int>& posed_object_indices) {
    // Skip if global pause is on
    // (renderer pause check is done in the renderer)

    // Each playback drives the ENGINE's clock: `AnimPlayback::Update` hands the
    // elapsed wall-clock time to `AnimPlayer::AdvanceSeconds`, which steps whole
    // engine frames (gTicksPerFrame, src/lod/boneobj.c:86-90). The editor no
    // longer accumulates ticks itself.
    //
    // The clock follows the wall clock - a long frame steps the engine frames it
    // covered - but only the frame this draw shows is POSED.  That is the
    // engine's own budget: one pose per DRAWN object per frame
    // (`Human_WakeIfVisible` 0x45F140 -> `Human_Wake` 0x45F240 ->
    // `BoneObj_Update` 0x4D38C0, src/humanbase/human.c:262-297) against one clock
    // advance per frame (`inst->time = inst->speed + inst->time`,
    // src/boneobj/boneobj.c:884-885).  Posing every engine frame a slow frame
    // covered was N-1 discarded full poses per drawn object, and because it made
    // the frame longer it fed itself: the `[AnimProfile]` `steps=`/`poses=` pair
    // reports both halves (`poses` must stay at the drawn count while `steps`
    // follows the frame time).  See `AnimPlayer::AdvanceSeconds`.
    //
    // ONLY THE PLAYBACKS WHOSE POSE IS DRAWN GET THE POSE.  The engine splits
    // these two things itself, and the split is the reason this loop is not
    // simply "step all 30 playbacks":
    //
    //   * the CLOCK runs for every active AI, drawn or not -
    //     `Soldier_Update` 0x45D080 calls `Human_FireAnimTrigger` 0x4613D0
    //     unconditionally (src/soldier/soldier.c:877), and that is
    //     `BoneObj_GetEvents` 0x4D3210 (src/humanbase/human.c:1376), which
    //     advances `inst.time` (src/boneobj/boneobj.c:884-885).  A deactivated
    //     human stops here too - `HumanAI_Update` 0x44F410 only sends the control
    //     message that reaches `Soldier_Update` `if (Human_IsActive(...))`
    //     (src/humanai/humanai.c:387, `vulnerable` from `Human_Reset` 0x45ECD0,
    //     src/soldier/human.c:422) - so the clock must NOT be stopped by
    //     visibility;
    //   * the full bone POSE (the expensive part: `BoneObj_Update` 0x4D38C0
    //     recurses over the whole bone tree) is computed at DRAW time and only
    //     for what is drawn - `Human_WakeIfVisible` 0x45F140 returns unless
    //     `Mesh3D_Exclude` 0x4D03D0 says the bounding sphere is visible and
    //     `Mesh3D_GetBoneMeshLOD` is not -1 (too far to draw)
    //     (src/humanbase/human.c:262-287), and it is called only from
    //     `Human_DrawHandler` 0x45F280 and `Human_NearClipHandler` 0x45F050,
    //     which run over the camera's sector and the sectors the portal flood
    //     reaches (`SectorObject_Render` 0x4DD310 -> `Sector_DrawVisible`
    //     0x4DD5D0 -> `Sector_Render` 0x4DD1C0, src/sector/sector.c:928-1057).
    //
    // `posed_object_indices` is `skinnedReplacementIndices` - the objects whose
    // live skinned pose the renderer submits this frame.  In editor mode that is
    // every playback the viewport draws (App::GetEditorDrawnAnimationIndices:
    // the visual pass's view cone and the engine's own LOD cull), so an AI with a
    // playing clip is posed as soon as it is on screen instead of only while it
    // is selected.  A playback that leaves the set keeps a live clock and stops
    // posing; when it re-enters, `Update` poses it again in the same frame,
    // before the draw, at the tick the clock had reached - which is what retail
    // does when `Human_Wake` poses an object that has just come into view.
    //
    // IGI_PROFILE-only: attribute this loop, which [FrameBreak] shows is the
    // largest single block of App::Frame() outside renderer_.Draw().
    const bool profile_anim = []() {
        static const bool enabled = getenv("IGI_PROFILE") != nullptr;
        return enabled;
    }();
    const auto anim_start = profile_anim ? std::chrono::steady_clock::now()
                                         : std::chrono::steady_clock::time_point{};
    int64_t slowest_us = 0;
    int slowest_idx = -1;
    int posed_count = 0;
    // The two halves of the loop are timed apart: the whole point of the split
    // is that the pose is expensive and the clock is not, and the integer
    // ms/frame the line used to report cannot show that (both halves round to
    // 0 ms at 30 playbacks).  Microseconds per frame per half can.
    int64_t posed_us = 0;
    int64_t clock_us = 0;
    // Deterministic counters for the same loop, so the cost claim does not rest
    // on wall-clock numbers: `steps` is the engine frames the elapsed time
    // covered (the clock's work) and `poses` is the engine's full bone pose
    // builds (the expensive half). `poses` must stay at the drawn count however
    // long the frame was; before the fix it tracked `steps`.
    int64_t steps_total = 0;
    int64_t poses_total = 0;
    for (auto& [idx, pb] : animPlaybacks_) {
        const auto one_start = profile_anim ? std::chrono::steady_clock::now()
                                            : std::chrono::steady_clock::time_point{};
        const bool posed = posed_object_indices.count(idx) != 0;
        const int poses_before = pb.EnginePoseSteps();
        if (posed) {
            ++posed_count;
            steps_total += pb.Update(dtSec * 1000.f);
        } else {
            steps_total += pb.AdvanceClock(dtSec * 1000.f);
        }
        poses_total += pb.EnginePoseSteps() - poses_before;
        if (profile_anim) {
            const int64_t one_us = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - one_start).count();
            if (posed) posed_us += one_us;
            else clock_us += one_us;
            if (one_us > slowest_us) {
                slowest_us = one_us;
                slowest_idx = idx;
            }
        }
    }
    if (profile_anim) {
        static int64_t acc_us = 0;
        static int64_t acc_slowest_us = 0;
        static int64_t acc_count = 0;
        static int64_t acc_posed = 0;
        static int64_t acc_posed_us = 0;
        static int64_t acc_clock_us = 0;
        static int64_t acc_steps = 0;
        static int64_t acc_poses = 0;
        static int frames = 0;
        const auto now = std::chrono::steady_clock::now();
        acc_us += std::chrono::duration_cast<std::chrono::microseconds>(now - anim_start).count();
        acc_slowest_us += slowest_us;
        acc_count += (int64_t)animPlaybacks_.size();
        acc_posed += posed_count;
        acc_posed_us += posed_us;
        acc_clock_us += clock_us;
        acc_steps += steps_total;
        acc_poses += poses_total;
        if (++frames >= 30) {
            Logger::Get().Log(LogLevel::INFO,
                "[AnimProfile] 30 frames: playbacks=" +
                std::to_string(acc_count / 30) + " posed=" +
                std::to_string(acc_posed / 30) + " steps=" +
                std::to_string(acc_steps / 30) + " poses=" +
                std::to_string(acc_poses / 30) + " loop=" +
                std::to_string(acc_us / 30000) + "ms (" +
                std::to_string(acc_us / 30) + "us/frame: posed=" +
                std::to_string(acc_posed_us / 30) + "us clockonly=" +
                std::to_string(acc_clock_us / 30) + "us) slowest=" +
                std::to_string(acc_slowest_us / 30000) + "ms (last slowest idx=" +
                std::to_string(slowest_idx) + ")");
            acc_us = acc_slowest_us = acc_count = acc_posed = 0;
            acc_posed_us = acc_clock_us = acc_steps = acc_poses = 0;
            frames = 0;
        }
    }
}

void App::ApplyRuntimeAiAnimationRequests() {
    const auto& guards = gameplay_host_.GetWorld().GetAi().GetGuards();
    const auto& objects = GetActiveRenderLevelObjects().GetObjects();

    for (const auto& guard : guards) {
        if (guard.state == igi::AiGuardState::Dead ||
            guard.requested_animation < 0 ||
            guard.animation_request_serial == 0 ||
            guard.id >= objects.size()) {
            continue;
        }

        const auto& object = objects[guard.id];
        if (object.deleted || object.boneHierarchy < 0) {
            continue;
        }

        const auto applied_serial = runtime_animation_request_serials_.find(guard.id);
        if (applied_serial != runtime_animation_request_serials_.end() &&
            applied_serial->second == guard.animation_request_serial) {
            continue;
        }

        const AnimationClip* requested_clip = animRegistry_.GetClipByAnimId(
            object.boneHierarchy,
            guard.requested_animation);
        if (requested_clip == nullptr) {
            continue;
        }

		auto& playback = gameplayAnimPlaybacks_[static_cast<int>(guard.id)];
        if (playback.clip != requested_clip || !playback.playing) {
            playback.Start(requested_clip);
            playback.forceLoop = true;
        }
        runtime_animation_request_serials_[guard.id] = guard.animation_request_serial;
	}
}

std::unordered_set<int> App::GetGameplayDrawnAnimationIndices() {
	// `GetSkinnedReplacementObjectIndices(true)` is "every gameplay playback whose
	// live skinned pose COULD be submitted": clip playing, engine can pose it,
	// object alive, skin geometry loaded (ShouldUseSkinnedReplacement,
	// source/animation.h:295).  The gameplay pass then culls that by camera
	// DISTANCE - this is the 80 m gameplay filter the skinned draw loop has always
	// used (`maxSkin` below) - and the selected object is exempt from the cull
	// exactly as before.
	std::unordered_set<int> drawn = GetSkinnedReplacementObjectIndices(true);
	const glm::vec3 cam = gameplay_viewer_.pos_;
	const float maxSkin = 80.0f * WORLD_UNITS_PER_METER;
	const float maxSkinSq = maxSkin * maxSkin;
	const auto& objs = GetActiveRenderLevelObjects().GetObjects();
	for (auto it = drawn.begin(); it != drawn.end(); ) {
		if (*it == selected_object_index_ || *it < 0 || *it >= (int)objs.size()) {
			++it;
			continue;
		}
		const glm::vec3 delta = glm::vec3(objs[*it].pos) - cam;
		if (glm::dot(delta, delta) > maxSkinSq) it = drawn.erase(it);
		else ++it;
	}
	return drawn;
}

std::unordered_set<int> App::GetEditorDrawnAnimationIndices() {
	// The engine's rule, and the reason this function exists at all: an AI's POSE
	// is built only for what is DRAWN (`Human_WakeIfVisible` 0x45F140 ->
	// `Human_Wake` 0x45F240 -> `BoneObj_Update` 0x4D38C0, reached only from
	// `Human_DrawHandler` 0x45F280 / `Human_NearClipHandler` 0x45F050,
	// src/humanbase/human.c:235-320), while its CLOCK runs whether it is drawn or
	// not (`Soldier_Update` 0x45D080 -> `Human_FireAnimTrigger` 0x4613D0 ->
	// `BoneObj_GetEvents` 0x4D3210, src/soldier/soldier.c:877,
	// src/humanbase/human.c:1368-1387).  The editor therefore has to answer
	// "which AI does my viewport draw" to decide which playbacks get a pose - and
	// the answer must be the visual pass's own, not a second guess.
	//
	// That is what this set is.  It starts from every editor playback that COULD
	// be skinned-drawn (clip playing, engine can pose it, object alive, skin
	// geometry loaded - ShouldUseSkinnedReplacement, source/animation.h:295) and
	// then applies the visual pass's two tests through the SAME code the pass uses:
	//
	//   * `igi::PassesEditorViewCone` - the conservative view cull
	//     (`Renderer_Objects::Draw`), the editor's stand-in for the engine's
	//     `Mesh3D_Exclude` 0x4D03D0 frustum test;
	//   * `Renderer::GetObjectDrawLod` - `ModelObj_GetLOD` 0x4CFCB0 over
	//     `lod_select`, the same call the pass makes before it draws an object; a
	//     negative result is the engine's "too far to draw"
	//     (src/render/render.c:955-956) and `Mesh3D_GetBoneMeshLOD` 0x4CFF70
	//     refuses to pose those (`lod != -1`, src/humanbase/human.c:281-283).
	//
	// Before this, editor mode posed the SELECTED object alone (one entry, or
	// none), so 29 of level 1's 30 AI advanced their clocks every frame and were
	// never posed - a running animation nobody could see.  Nothing else about the
	// frame changes: this set is still the one variable that is both handed to
	// `UpdateAnimations` (pose decision) and installed as
	// `draw_params_.skip_static_draw_indices_` (the skinned draw loop iterates the
	// same set), so the posed set and the drawn set cannot diverge.
	const auto& objs = GetActiveRenderLevelObjects().GetObjects();
	std::unordered_set<int> drawn = GetSkinnedReplacementObjectIndices(false);

	const glm::vec3 cam = viewer_.pos_;
	const glm::vec3 fwd_raw = viewer_.forward_;
	const float fwd_len = glm::length(fwd_raw);
	const glm::vec3 fwd = (fwd_len > 0.01f) ? fwd_raw / fwd_len : glm::vec3(0.0f);
	const float tan_half_fovx = view_define_.tan_half_fovx_;

	for (auto it = drawn.begin(); it != drawn.end(); ) {
		const int idx = *it;
		if (idx < 0 || idx >= (int)objs.size()) {
			++it;
			continue;
		}
		const LevelObject& obj = objs[idx];
		const bool is_selected = (idx == selected_object_index_);
		// The bounding radius is the visual pass's own (`Renderer_Objects::Draw`):
		// the mesh's extent diagonal in MEF units, scaled to world units by the
		// 40.96 the model matrix uses, times the object's own scale.
		const float bound_radius =
			renderer_.GetMeshRadius(obj.modelId, obj.isBuilding) * 40.96f *
			std::max(obj.scale, 0.001f);
		const bool in_cone = igi::PassesEditorViewCone(glm::vec3(obj.pos) - cam,
		                                              bound_radius, fwd, fwd_len);
		// The LOD question is only worth asking for an object that is in view (and
		// is skipped for the selected object, which the pose rule exempts).
		const int draw_lod = (is_selected || in_cone)
			? renderer_.GetObjectDrawLod(obj, cam, fwd, tan_half_fovx)
			: -1;
		if (igi::ShouldPoseAnimationNow(is_selected, in_cone, draw_lod)) {
			++it;
		} else {
			it = drawn.erase(it);
		}
	}
	return drawn;
}

void App::AdvanceGameplayAnimationClocks(const std::unordered_set<int>& posed_object_indices) {
	// The caller has already run `ApplyRuntimeAiAnimationRequests()`: it must
	// precede the drawn-set computation, because a clip it starts is drawable on
	// the frame it starts (ShouldUseSkinnedReplacement requires
	// `playback.playing`).  See App::Frame's gameplay block.
	//
	// THE CLOCK IS THE SIMULATION'S, NOT WALL TIME.  `elapsed_ms` is derived from
	// the fixed-step simulation tick counter at `GameClock::TICK_RATE_HZ`, so the
	// animation clock and the AI/physics clock cannot drift apart.  Converting it
	// to wall time would be a fidelity bug: retail's AI animations are driven by
	// the simulation's own fixed step, not by the frame rate.
	const uint64_t simulation_tick = gameplay_host_.GetRenderSnapshot().simulation_tick;
	if (simulation_tick <= lastGameplayAnimationTick_) {
		return;
	}

	const uint64_t elapsed_ticks = simulation_tick - lastGameplayAnimationTick_;
	const float elapsed_ms = static_cast<float>(elapsed_ticks) *
		(1000.0f / static_cast<float>(igi::GameClock::TICK_RATE_HZ));

	// SAME SPLIT AS THE EDITOR PATH (App::UpdateAnimations), and the same one the
	// engine makes for a human:
	//   * the CLOCK runs for every active AI, drawn or not - `Soldier_Update`
	//     0x45D080 calls `Human_FireAnimTrigger` 0x4613D0 unconditionally
	//     (src/soldier/soldier.c:877), which is `BoneObj_GetEvents` 0x4D3210
	//     (src/humanbase/human.c:1376) advancing `inst->time`
	//     (src/boneobj/boneobj.c:884-885);
	//   * the POSE - the expensive full recursive `BoneObj_Update` 0x4D38C0 - is
	//     built only for what is drawn.  `Human_WakeIfVisible` 0x45F140 skips it on
	//     `Mesh3D_Exclude` 0x4D03D0 or a -1 `Mesh3D_GetBoneMeshLOD`
	//     (src/humanbase/human.c:262-287) and only calls `Human_Wake` 0x45F240
	//     (human.c:289-297) from `Human_DrawHandler` 0x45F280 /
	//     `Human_NearClipHandler` 0x45F050.
	// `posed_object_indices` is the gameplay pass's drawn set - the SAME set the
	// skinned draw loop iterates (App::Frame), built by
	// GetGameplayDrawnAnimationIndices() and passed straight through.  A playback
	// that leaves the 80 m filter keeps a live clock and stops posing; when it
	// re-enters, `Update` poses it at the tick the clock already reached, which is
	// bit-identical to having posed it all along (tests/test_anim_playback.cpp).
	const bool profile_anim = []() {
		static const bool enabled = getenv("IGI_PROFILE") != nullptr;
		return enabled;
	}();
	int64_t slowest_us = 0;
	int slowest_idx = -1;
	int posed_count = 0;
	int clock_only_count = 0;
	int64_t posed_us = 0;
	int64_t clock_us = 0;
	// See App::UpdateAnimations: the deterministic half of the same claim.
	int64_t steps_total = 0;
	int64_t poses_total = 0;
	for (auto& [object_index, playback] : gameplayAnimPlaybacks_) {
		const auto one_start = profile_anim ? std::chrono::steady_clock::now()
		                                    : std::chrono::steady_clock::time_point{};
		const bool posed = posed_object_indices.count(object_index) != 0;
		const int poses_before = playback.EnginePoseSteps();
		if (posed) {
			++posed_count;
			steps_total += playback.Update(elapsed_ms);
		} else {
			++clock_only_count;
			steps_total += playback.AdvanceClock(elapsed_ms);
		}
		poses_total += playback.EnginePoseSteps() - poses_before;
		if (profile_anim) {
			const int64_t one_us = std::chrono::duration_cast<std::chrono::microseconds>(
				std::chrono::steady_clock::now() - one_start).count();
			if (posed) posed_us += one_us;
			else clock_us += one_us;
			if (one_us > slowest_us) {
				slowest_us = one_us;
				slowest_idx = object_index;
			}
		}
	}
	lastGameplayAnimationTick_ = simulation_tick;

	if (profile_anim) {
		// IGI_PROFILE-only, the gameplay counterpart of [AnimProfile]: a stable
		// per-subsystem counter for a pass whose frame totals the smoke run's
		// mouse-driven input cannot reproduce.  `posed` vs `clockonly` is the whole
		// effect of the split, and the microsecond halves show which one costs.
		static int64_t acc_posed = 0;
		static int64_t acc_clock = 0;
		static int64_t acc_posed_us = 0;
		static int64_t acc_clock_us = 0;
		static int64_t acc_slowest_us = 0;
		static int64_t acc_playbacks = 0;
		static int64_t acc_elapsed_ms = 0;
		static int64_t acc_steps = 0;
		static int64_t acc_poses = 0;
		static int frames = 0;
		acc_playbacks += (int64_t)gameplayAnimPlaybacks_.size();
		acc_posed += posed_count;
		acc_clock += clock_only_count;
		acc_posed_us += posed_us;
		acc_clock_us += clock_us;
		acc_slowest_us += slowest_us;
		acc_elapsed_ms += (int64_t)elapsed_ms;
		acc_steps += steps_total;
		acc_poses += poses_total;
		if (++frames >= 30) {
			Logger::Get().Log(LogLevel::INFO,
				"[GameplayAnimProfile] 30 frames: playbacks=" +
				std::to_string(acc_playbacks / 30) + " posed=" +
				std::to_string(acc_posed / 30) + " clockonly=" +
				std::to_string(acc_clock / 30) + " steps=" +
				std::to_string(acc_steps / 30) + " poses=" +
				std::to_string(acc_poses / 30) + " loop=" +
				std::to_string(acc_posed_us / 30000 + acc_clock_us / 30000) + "ms (" +
				std::to_string((acc_posed_us + acc_clock_us) / 30) + "us/frame: posed=" +
				std::to_string(acc_posed_us / 30) + "us clockonly=" +
				std::to_string(acc_clock_us / 30) + "us) slowest=" +
				std::to_string(acc_slowest_us / 30000) + "ms (last slowest idx=" +
				std::to_string(slowest_idx) + ") sim_ms/frame=" +
				std::to_string(acc_elapsed_ms / 30));
			acc_playbacks = acc_posed = acc_clock = 0;
			acc_posed_us = acc_clock_us = acc_slowest_us = 0;
			acc_elapsed_ms = acc_steps = acc_poses = 0;
			frames = 0;
		}
	}
}

// ── The animation transport (#73) ────────────────────────────────────────────
// One implementation for the F10 panel's buttons and the QED timer keys; see
// app.h for the engine citations and `source/runtime/anim_panel.h` for the
// layout and the frame arithmetic.

AnimPlayback* App::SelectedAnimPlayback() {
    if (selected_object_index_ < 0) return nullptr;
    auto it = animPlaybacks_.find(selected_object_index_);
    if (it == animPlaybacks_.end() || it->second.clip == nullptr) return nullptr;
    return &it->second;
}

bool App::SelectedAnimPlaying() const {
    auto it = animPlaybacks_.find(selected_object_index_);
    return it != animPlaybacks_.end() && it->second.clip != nullptr && it->second.playing;
}

std::string App::AnimPanelTitleText() {
    AnimPlayback* pb = SelectedAnimPlayback();
    if (!pb) {
        return igi::FormatAnimPanelTitle(
            false, 1.0f, igi::AnimFrameStepTicks(1.0f, igi::kAnimDefaultTicksPerFrame));
    }
    // The step size is the ENGINE's frame at this speed: `inst->speed =
    // gTicksPerFrame * speed` (Anim_SetSpeed 0x4D6580, src/boneobj/anim.c:172-179),
    // which is the same value `AnimPlayback::StepFrames` steps by.
    return igi::FormatAnimPanelTitle(pb->playing, pb->speed,
                                     igi::AnimFrameStepTicks(pb->speed, pb->TicksPerFrame()));
}

void App::ToggleSelectedAnimPlayback() {
    AnimPlayback* pb = SelectedAnimPlayback();
    if (!pb) {
        status_message_ = "TimerStartStop: select an object with an animation first";
        return;
    }
    if (pb->playing) pb->Pause(); else pb->Resume();
    status_message_ = std::string("Timer ") + (pb->playing ? "running" : "stopped") +
                      ": '" + pb->clip->name + "'";
}

void App::ResetSelectedAnim() {
    AnimPlayback* pb = SelectedAnimPlayback();
    if (!pb) {
        status_message_ = "TimerReset: select an object with an animation first";
        return;
    }
    // Rewinds to the clip's first frame and stops the clock
    // (source/runtime/anim_playback.cpp:154-161).
    pb->Reset();
    status_message_ = "Timer reset to frame 0: '" + pb->clip->name + "'";
}

void App::AdjustSelectedAnimSpeed(int direction) {
    const char* action = direction > 0 ? "TimerIncreaseSpeed" : "TimerDecreaseSpeed";
    AnimPlayback* pb = SelectedAnimPlayback();
    if (!pb) {
        status_message_ = std::string(action) + ": select an object with an animation first";
        return;
    }
    const int current = igi::AnimTimerSpeedIndex(pb->speed);
    const int next = igi::AnimTimerSpeedStepIndex(current, direction);
    // "at the limit" means the step did not move: the ladder clamps at both
    // ends, and saying so is what stops the control from looking like a no-op.
    const bool at_limit = (next == current);
    pb->speed = igi::AnimTimerSpeedAt(next);
    status_message_ = "Timer speed: " + igi::AnimTimerSpeedLabel(pb->speed) +
                      (at_limit ? " (limit)" : "") + " for '" + pb->clip->name + "'";
}

void App::StepSelectedAnim(int direction) {
    AnimPlayback* pb = SelectedAnimPlayback();
    if (!pb) {
        status_message_ = "Frame step: select an object with an animation first";
        return;
    }
    if (!pb->EngineReady()) {
        status_message_ = "Frame step: the engine cannot play '" + pb->clip->name + "'";
        return;
    }
    if (pb->StepFrames(direction) == 0) {
        // Nothing moved, and the reason is the engine's own clamp rule: a
        // one-shot at the end it is stepping towards, or - for a loop - a wrap
        // that landed back on the frame it started from (`AnimStepTargetTick`,
        // mirroring `BoneObj_AnimEventsCB` 0x4D4B60, src/boneobj/boneobj.c:892-905).
        status_message_ = std::string("Frame step: '") + pb->clip->name +
                          (pb->Looping() ? "' wrapped onto the same frame"
                                         : (direction > 0 ? "' is at the end"
                                                          : "' is at the start"));
        return;
    }
    const int frame = igi::AnimFrameIndexForTick(pb->currentTimeMs, pb->TicksPerFrame());
    status_message_ = "Frame " + std::to_string(frame) + " (" +
                      igi::AnimTimerSpeedLabel(pb->speed) + "): '" + pb->clip->name + "'" +
                      (pb->playing ? "" : " (paused)");
    glutPostRedisplay();
}

bool App::AnimPanelClick(int x, int y) {
    // Same visibility gate as the draw (source/renderer/renderer_draw.cpp): the
    // panel exists while F10 is on and there is something to show.
    if (!show_anim_debug_) return false;
    if (!igi::AnimPanel::InPanel(x, y)) return false;
    if (BuildAnimStatusString().empty()) return false;

    switch (igi::AnimPanel::HitTest(x, y)) {
    case igi::AnimPanel::kPlayPause:   ToggleSelectedAnimPlayback(); break;
    case igi::AnimPanel::kStepBack:    StepSelectedAnim(-1);         break;
    case igi::AnimPanel::kStepForward: StepSelectedAnim(1);          break;
    case igi::AnimPanel::kSpeedDown:   AdjustSelectedAnimSpeed(-1);  break;
    case igi::AnimPanel::kSpeedUp:     AdjustSelectedAnimSpeed(1);   break;
    default: break;   // the panel's background: consumed, nothing to do
    }
    glutPostRedisplay();
    return true;
}

std::string App::BuildAnimStatusString() {
    if (animPlaybacks_.empty()) return {};

    std::string s;
    int count = 0;
    auto& objects = level_.GetLevelObjects().GetObjects();
    for (const auto& [idx, pb] : animPlaybacks_) {
        if (!pb.clip) continue;
        // The cap is the layout's: the panel draws at most `kMaxRows` lines, and
        // the last of those is this "... and N more" row.
        if (count >= igi::AnimPanel::kMaxRows - 1) {
            s += "... and " + std::to_string((int)animPlaybacks_.size() - count) + " more\n";
            break;
        }
        std::string name = (idx >= 0 && idx < (int)objects.size()) ? objects[idx].name : ("#" + std::to_string(idx));
        if (name.empty()) name = objects[idx].modelId;

        // One row, formatted by the same pure function the host lane pins
        // (source/runtime/anim_panel.h): the frame the engine clock is on, the
        // clip's frame count, the speed label the F12 keys also report, and the
        // transport state. Every value is engine state - `currentTimeMs` is the
        // mirror of `Anim_GetTime` 0x4D6500 and `EngineLength()` is
        // `Anim_GetLength` 0x4D64E0 - except the frame index, which is display.
        igi::AnimPanelRow row;
        row.name = name;
        row.clip = pb.clip->name;
        row.selected = (idx == selected_object_index_);
        row.playing = pb.playing;
        row.engine_ready = pb.EngineReady();
        row.looping = pb.Looping();
        row.tick = pb.currentTimeMs;
        row.length = pb.EngineLength();
        row.speed = pb.speed;
        row.ticks_per_frame = pb.TicksPerFrame();
        s += igi::FormatAnimPanelRow(row);
        s += "\n";
        count++;
    }
    if (!s.empty() && s.back() == '\n') s.pop_back();
    return s;
}

// Parses the last two comma-separated args of a flat "Task_New(...)" line,
// e.g. PatrolPathCommand's (cmdCode, param) pair in
// `Task_New(-1, "PatrolPathCommand", "Plays predefined animation 240", 0, 240)`.
static bool LastTwoIntArgs(const std::string& qscLine, int& a, int& b) {
    size_t close = qscLine.rfind(')');
    if (close == std::string::npos) return false;
    size_t open = qscLine.rfind('(', close);
    if (open == std::string::npos) return false;
    std::string inner = qscLine.substr(open + 1, close - open - 1);
    std::vector<std::string> parts;
    size_t start = 0;
    for (size_t i = 0; i <= inner.size(); ++i) {
        if (i == inner.size() || inner[i] == ',') {
            parts.push_back(inner.substr(start, i - start));
            start = i + 1;
        }
    }
    if (parts.size() < 2) return false;
    try {
        b = std::stoi(parts[parts.size() - 1]);
        a = std::stoi(parts[parts.size() - 2]);
    } catch (...) { return false; }
    return true;
}

int App::FindHumanAiTaskId(int objIndex) const {
    auto& objects = level_.GetLevelObjects().GetObjects();
    if (objIndex < 0 || objIndex >= (int)objects.size()) return -1;
    for (int ci : objects[objIndex].childrenIndices) {
        if (ci < 0 || ci >= (int)objects.size()) continue;
        if (objects[ci].deleted || objects[ci].type != "HumanAI") continue;
        try { return std::stoi(objects[ci].taskId); } catch (...) { return -1; }
    }
    return -1;
}

const std::vector<int>& App::GetOrComputeAnimationIds(int objIndex) {
    static const std::vector<int> kEmpty;
    auto cached = animIdsCache_.find(objIndex);
    if (cached != animIdsCache_.end()) return cached->second;

    auto& objects = level_.GetLevelObjects().GetObjects();
    if (objIndex < 0 || objIndex >= (int)objects.size() || objects[objIndex].boneHierarchy < 0) return kEmpty;

    return animIdsCache_[objIndex] = ComputeAnimationIdsForObject(objIndex);
}

// Pure computation, no cache reads/writes â€” safe to call concurrently from worker
// threads (level-load parallel animation resolution) as long as no other thread is
// mutating LevelObjects/AnimationRegistry at the same time (registry must already
// be fully imported â€” see LoadLevel's sequential ImportAnimations pre-pass).
std::vector<int> App::ComputeAnimationIdsForObject(int objIndex) const {
    auto& objects = level_.GetLevelObjects().GetObjects();
    if (objIndex < 0 || objIndex >= (int)objects.size()) return {};
    const auto& obj = objects[objIndex];
    if (obj.boneHierarchy < 0) return {};

    std::vector<int> ids;
    if (obj.standAnimation >= 0) ids.push_back(obj.standAnimation);

    int aiTaskId = FindHumanAiTaskId(objIndex);
    if (aiTaskId >= 0 && last_loaded_level_ >= 0) {
        std::string qvmPath = Utils::GetIGIRootPath() + "\\missions\\location0\\level" +
            std::to_string(last_loaded_level_) + "\\ai\\" + std::to_string(aiTaskId) + ".qvm";
        Logger::Get().Log(LogLevel::INFO, "[Anim] Resolving animation ids for object " +
            std::to_string(objIndex) + " via AI task " + std::to_string(aiTaskId) + " (" + qvmPath + ")");
        for (int id : FindAiScriptAnimationIds(qvmPath)) {
            if (std::find(ids.begin(), ids.end(), id) == ids.end()) ids.push_back(id);
        }
    } else {
        Logger::Get().Log(LogLevel::DEBUG, "[Anim] Object " + std::to_string(objIndex) +
            " has no HumanAI child task â€” only Stand Animation id (if any) is available");
    }

    // PatrolPath children can include "PatrolPathCommand" entries that play a
    // predefined animation (cmdCode == 0, param == animation id), independent
    // of the AI behavior script's own AIAction_PlayAnimation call.
    for (int ci : obj.childrenIndices) {
        if (ci < 0 || ci >= (int)objects.size()) continue;
        if (objects[ci].deleted || objects[ci].type != "PatrolPath") continue;
        for (int pci : objects[ci].childrenIndices) {
            if (pci < 0 || pci >= (int)objects.size()) continue;
            const auto& pcmd = objects[pci];
            if (pcmd.deleted || pcmd.type != "PatrolPathCommand") continue;
            int cmdCode = -1, param = -1;
            if (LastTwoIntArgs(pcmd.qscLine, cmdCode, param) && cmdCode == 0) {
                Logger::Get().Log(LogLevel::INFO, "[Anim] Object " + std::to_string(objIndex) +
                    " PatrolPathCommand plays predefined animation " + std::to_string(param));
                if (std::find(ids.begin(), ids.end(), param) == ids.end()) ids.push_back(param);
            }
        }
    }

    Logger::Get().Log(LogLevel::INFO, "[Anim] Object " + std::to_string(objIndex) + " (" + obj.modelId +
        ", bone hierarchy " + std::to_string(obj.boneHierarchy) + "): " + std::to_string(ids.size()) +
        " animation id(s) available");

    return ids;
}

void App::ToggleAnimationForObject(int objIndex, int animId) {
    auto& objects = level_.GetLevelObjects().GetObjects();
    if (objIndex < 0 || objIndex >= (int)objects.size()) return;
    const auto& obj = objects[objIndex];
    if (obj.boneHierarchy < 0) return;

    if (!animRegistry_.ImportAnimations(obj.boneHierarchy)) {
        Logger::Get().Log(LogLevel::WARNING, "[Anim] Could not import bone hierarchy " +
            std::to_string(obj.boneHierarchy) + " for object " + std::to_string(objIndex));
        return;
    }
    const AnimationClip* clip = animRegistry_.GetClipByAnimId(obj.boneHierarchy, animId);
    if (!clip) {
        Logger::Get().Log(LogLevel::WARNING, "[Anim] Animation id " + std::to_string(animId) +
            " not found in bone hierarchy " + std::to_string(obj.boneHierarchy));
        return;
    }

    auto& pb = animPlaybacks_[objIndex];
    if (pb.clip == clip && pb.playing) {
        pb.Pause();
        Logger::Get().Log(LogLevel::INFO, "[Anim] Paused '" + clip->name + "' for object " + std::to_string(objIndex));
    } else {
        if (pb.clip == clip) {
            pb.Resume();
            Logger::Get().Log(LogLevel::INFO, "[Anim] Resumed '" + clip->name + "' for object " + std::to_string(objIndex));
        } else {
            pb.Start(clip);
            Logger::Get().Log(LogLevel::INFO, "[Anim] Playing '" + clip->name + "' for object " + std::to_string(objIndex));
        }
    }
}

std::unordered_set<int> App::GetSkinnedReplacementObjectIndices(bool gameplay_render) {
    std::unordered_set<int> result;
    auto& objs = GetActiveRenderLevelObjects().GetObjects();
    const auto& active_playbacks = gameplay_render
        ? gameplayAnimPlaybacks_ : animPlaybacks_;
    for (const auto& [idx, pb] : active_playbacks) {
        if (idx < 0 || idx >= (int)objs.size()) continue;
        const auto& obj = objs[idx];
        // Only replace the static mesh if the skinned replacement can actually
        // render — otherwise a model whose skin geometry fails to load goes
        // permanently invisible (neither static nor skinned draw produces anything).
        if (!ShouldUseSkinnedReplacement(pb, idx, objs.size(), obj.deleted,
                                         renderer_.HasSkinGeometry(obj.modelId, obj.isBuilding))) continue;
        result.insert(idx);
    }

    if (result != animSkinnedIndicesPrev_) {
        Logger::Get().Log(LogLevel::INFO, "[Anim] Skinned/animated replacement active for " +
            std::to_string(result.size()) + " AI object(s) in parallel: " +
            [&]() {
                std::string s;
                for (int idx : result) s += std::to_string(idx) + " ";
                return s;
            }());
        animSkinnedIndicesPrev_ = result;
    }
    return result;
}

void App::ComputePropAnimUiState(int& boneHierarchy, std::vector<int>& ids, int& activeId, bool& isPlaying) {
    boneHierarchy = -1;
    ids.clear();
    activeId = -1;
    isPlaying = false;

    if (!prop_editor_open_ || selected_object_index_ < 0) return;
    auto& objects = level_.GetLevelObjects().GetObjects();
    if (selected_object_index_ >= (int)objects.size()) return;
    const auto& obj = objects[selected_object_index_];
    if (obj.boneHierarchy < 0) return;

    boneHierarchy = obj.boneHierarchy;
    ids = GetOrComputeAnimationIds(selected_object_index_);
    auto pbIt = animPlaybacks_.find(selected_object_index_);
    if (pbIt != animPlaybacks_.end() && pbIt->second.clip) {
        activeId = pbIt->second.clip->animId;
        isPlaying = pbIt->second.playing;
    }
}

void App::ToggleAutoSave() {
	auto_save_enabled_ = !auto_save_enabled_;
	auto_save_last_time_ms_ = Sys_Milliseconds();
	Config::Get().auto_save_enabled = auto_save_enabled_;
	Config::Save();
	status_message_ = auto_save_enabled_ ? "Auto-save: ON" : "Auto-save: OFF";
}

void App::AdjustAutoSaveInterval(int delta_seconds) {
	auto_save_interval_seconds_ += delta_seconds;
	if (auto_save_interval_seconds_ < 10)   auto_save_interval_seconds_ = 10;
	if (auto_save_interval_seconds_ > 3600) auto_save_interval_seconds_ = 3600;
	auto_save_last_time_ms_ = Sys_Milliseconds();
	Config::Get().auto_save_interval_seconds = auto_save_interval_seconds_;
	Config::Save();
	status_message_ = "Auto-save interval: " + std::to_string(auto_save_interval_seconds_) + "s";
}
