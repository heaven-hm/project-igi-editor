#include "app_internal.h"

// GameMonitorParam, GameMonitorProc, and HOTKEY_ID_TOGGLE_GAME live in
// app_internal.h (shared with app_editor.cpp's LaunchGame). The mutable window
// subclass globals below are used only here.

// ── Global hotkey support ────────────────────────────────────────────────────
// We subclass GLUT's window so WM_HOTKEY messages reach our code even when
// the editor is iconified and the game has keyboard focus.
static WNDPROC g_origEditorWndProc = nullptr;
static App*    g_appForHotkey      = nullptr;
float g_renderer_delta_secs = 0.0f;  // frame delta for renderer continuous animations (rotor spin)

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

	renderer_.SetLightmapsEnabled(cfg.enableLightmaps);
	renderer_.SetFogEnabled(cfg.enableFog);

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

	if (Arg_OptionIdx(argc, argv, "--developer-mode") > -1) {
		developer_mode_ = true;
		debug_cmd_mgr_.Start();
		Logger::Get().Log(LogLevel::INFO, "[App] Developer Mode ON via command line");
	}
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
	StopLevelMusic();
	level_.Unload();
	level_.FreeTerrainCubeDataPools();
	animPlaybacks_.clear();
	animIdsCache_.clear();
	animRegistry_.Clear();
	renderer_.Shutdown();
	if (!g_isCLIMode) {
		AssetExtractor::CleanupExtractedAssets(Utils::GetExeDirectory());
	}
}

// ── C1: Custom SPR cursor — multi-mode ────────────────────────────────────────


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

// AI text editor helpers — must be defined before Input_OnMouse and Input_OnSpecial.

// Returns flat text offsets of each visual line start.
// Lines split on '\n'; lines longer than max_chars wrap to the next visual line.

// input
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
			if (Config::Get().musicEnabled) PlayLevelMusic(level_.GetLevelNo());
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
	if (developer_mode_) {
		debug_cmd_mgr_.Update();
	}
	// Auto-save timer
	if (auto_save_enabled_ && !pause_mode_ && level_.GetLevelNo() > 0) {
		int64_t now = Sys_Milliseconds();
		if (now - auto_save_last_time_ms_ >= (int64_t)auto_save_interval_seconds_ * 1000) {
			auto_save_last_time_ms_ = now;
			SaveCurrentLevel();
			status_message_ = "Auto-saved level " + std::to_string(level_.GetLevelNo());
		}
	}

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
		int propAnimBoneHierarchy; std::vector<int> propAnimIds; int propAnimActiveId; bool propAnimIsPlaying;
		ComputePropAnimUiState(propAnimBoneHierarchy, propAnimIds, propAnimActiveId, propAnimIsPlaying);
		Renderer::task_tree_view_params_s task_tree_view = {
			.show_hud_ = show_hud_,
			.status_msg_ = status_message_,
			.pause_mode_ = true,
			.pause_active_input_ = pause_active_input_,
			.pause_level_input_ = pause_level_input_,
			.pause_search_input_ = pause_search_input_,
			.pause_terrain_expanded_ = pause_terrain_expanded_,
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
			.terrain_brush_          = edit_brush_,
			.terrain_brush_radius_   = edit_brush_radius_,
			.terrain_brush_strength_ = edit_brush_strength_,
			.auto_save_enabled_        = auto_save_enabled_,
			.auto_save_interval_seconds_ = auto_save_interval_seconds_,
			.music_on_ = music_playing_,
			.lightmaps_on_ = Config::Get().enableLightmaps,
			.anim_status_  = BuildAnimStatusString(),
			.anim_playing_ = !animPlaybacks_.empty(),
			.anim_debug_visible_ = show_anim_debug_,
			.prop_anim_bone_hierarchy_ = propAnimBoneHierarchy,
			.prop_anim_ids_ = propAnimIds,
			.prop_anim_active_id_ = propAnimActiveId,
			.prop_anim_is_playing_ = propAnimIsPlaying,
		};
		draw_params_.level_objects_ = &level_.GetLevelObjects();
		draw_params_.selected_object_index_ = selected_object_index_;
		draw_params_.show_magic_obj_spheres_ = show_magic_obj_spheres_;
		// Paused: the pause menu (a 2D overlay drawn at the end of renderer_.Draw)
		// must sit in front of the scene, so do NOT draw the live skinned mesh after
		// it (that painted the character on top of the menu). Instead keep the
		// object's normal static mesh in the 3D pass — animation isn't advancing
		// while paused anyway — by not skipping it here.
		draw_params_.skip_static_draw_indices_ = nullptr;
		draw_params_.terrain_id_at_world_xy_ =
			[this](double x, double y) { return level_.GetTerrainNodeId(x, y); };
		draw_params_.terrain_z_at_world_xy_ =
			[this](double x, double y, float& z) { return level_.GetTerrainZ(x, y, z); };
	renderer_.Draw(draw_params_, task_tree_view);

	DrawCustomCursor();
	glutSwapBuffers();
	return;
    }

    frame_++;
	frame_ %= 0xFFFFFFFF;	// reserve value 0xFFFFFFFF (-1) for INVALID_FRAME

	ProcessInput(delta_seconds);

	// Update animation playback (auto-play for AI NPCs)
	g_renderer_delta_secs = delta_seconds;
	UpdateAnimations(delta_seconds);
	CheckMusicLoop();

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
	// All AI with an active, playing clip are skinned-replaced simultaneously
	// (skinnedReplacementIndices must outlive renderer_.Draw() below, so it's a
	// local in this Frame() call, not a temporary).
	std::unordered_set<int> skinnedReplacementIndices = GetSkinnedReplacementObjectIndices();
	draw_params_.skip_static_draw_indices_ = &skinnedReplacementIndices;
	draw_params_.terrain_id_at_world_xy_ =
		[this](double x, double y) { return level_.GetTerrainNodeId(x, y); };


	float ground_z = 0.0f;
	bridge_.SetEnabled(show_hud_);
	IGIBridge::PositionData data = bridge_.GetLatestData();
	level_.GetTerrainZ(viewer_.pos_.x, viewer_.pos_.y, ground_z);

	int propAnimBoneHierarchy; std::vector<int> propAnimIds; int propAnimActiveId; bool propAnimIsPlaying;
	ComputePropAnimUiState(propAnimBoneHierarchy, propAnimIds, propAnimActiveId, propAnimIsPlaying);

	Renderer::task_tree_view_params_s task_tree_view = {
		.show_hud_ = show_hud_,
		.status_msg_ = status_message_,
		.pause_mode_ = pause_mode_,
		.pause_active_input_ = pause_active_input_,
		.pause_level_input_ = pause_level_input_,
		.pause_search_input_ = pause_search_input_,
		.pause_terrain_expanded_ = pause_terrain_expanded_,
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
		.terrain_brush_          = edit_brush_,
		.terrain_brush_radius_   = edit_brush_radius_,
		.terrain_brush_strength_ = edit_brush_strength_,
		.auto_save_enabled_        = auto_save_enabled_,
		.auto_save_interval_seconds_ = auto_save_interval_seconds_,
		.music_on_ = music_playing_,
		.anim_status_  = BuildAnimStatusString(),
		.anim_playing_ = !animPlaybacks_.empty(),
		.anim_debug_visible_ = show_anim_debug_,
		.prop_anim_bone_hierarchy_ = propAnimBoneHierarchy,
		.prop_anim_ids_ = propAnimIds,
		.prop_anim_active_id_ = propAnimActiveId,
		.prop_anim_is_playing_ = propAnimIsPlaying,
	};

	renderer_.Draw(draw_params_, task_tree_view);

    // Find the "right hand" bone index in modelId's parsed bone list (REIH+MANB
    // names), cached per modelId. This is the same index space EvaluateWorld's
    // worldTransforms and the rest-pose bone list use (both come from the same
    // shared character rig), so one lookup serves both the animating and static
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
    //   * rotate 90° about X  -> swings the barrel from vertical to horizontal,
    //   * rotate 180° about Z  -> single horizontal flip so the barrel points the
    //     correct way, then
    //   * roll 180° about the barrel's OWN (post-correction) direction -> flips the
    //     weapon right-side-up (was upside down) WITHOUT changing the barrel's aim,
    //     since rotating about the barrel axis leaves that axis fixed. Computed from
    //     the actual barrel direction so it's correct regardless of the gun's frame.
    const glm::mat4 kWeaponBase =
        glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f)) *
        glm::rotate(glm::mat4(1.0f), glm::radians(180.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    const glm::vec3 kBarrelDir = glm::normalize(glm::vec3(kWeaponBase * glm::vec4(0.0f, 1.0f, 0.0f, 0.0f)));
    const glm::mat4 kWeaponHandCorrection =
        glm::rotate(glm::mat4(1.0f), glm::radians(180.0f), kBarrelDir) * kWeaponBase;

    // Draw the live skinned mesh for every AI with an active, playing clip —
    // all of them animate and render in parallel, not just the selected object.
    // This MUST run whenever a clip is playing (not gated by F10) — the static
    // mesh is already skipped via skip_static_draw_indices_ above, so gating
    // this would leave those objects invisible. The bone wireframe stays scoped
    // to the selected object only (avoids clutter) and gated by 'B'.
    {
        auto& objs = level_.GetLevelObjects().GetObjects();
        for (int idx : skinnedReplacementIndices) {
            if (idx < 0 || idx >= (int)objs.size()) continue;
            auto& pb = animPlaybacks_[idx];
            std::vector<glm::mat4> worldTransforms;
            animRegistry_.EvaluateWorld(pb.clip, pb.currentTimeMs, worldTransforms);
            if (worldTransforms.empty()) continue;

            const auto& obj = objs[idx];
            glm::mat4 objMat(1.0f);
            objMat = glm::translate(objMat, glm::vec3((float)obj.pos.x, (float)obj.pos.y, (float)obj.pos.z));
            objMat = glm::rotate(objMat, (float)obj.rot.z, glm::vec3(0, 0, 1));
            objMat = glm::rotate(objMat, (float)obj.rot.x, glm::vec3(1, 0, 0));
            objMat = glm::rotate(objMat, (float)obj.rot.y, glm::vec3(0, 1, 0));
            objMat = glm::scale(objMat, glm::vec3(40.96f * obj.scale));
            renderer_.DrawSkinnedMesh(obj.modelId, obj.isBuilding, worldTransforms, objMat);

            if (show_anim_skeleton_ && idx == selected_object_index_) {
                // boneParents is indexed by bone ID (same indexing EvaluateWorld uses for
                // worldTransforms), so DrawAnimSkeleton can connect each bone to its real
                // parent instead of assuming a flat chain of consecutive array indices.
                std::vector<int> boneParents(worldTransforms.size(), -1);
                for (const auto& b : pb.clip->bones) {
                    if (b.index >= 0 && (size_t)b.index < boneParents.size())
                        boneParents[b.index] = b.parent;
                }
                renderer_.DrawAnimSkeleton(worldTransforms, boneParents, objMat);
            }

            if (!obj.weaponModelId.empty()) {
                const ParsedGeometry* geo = renderer_.GetOrLoadSkinGeometry(obj.modelId, obj.isBuilding);
                int handIdx = findHandBoneIndex(obj.modelId, geo);
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
    {
        auto& objs = level_.GetLevelObjects().GetObjects();
        for (int idx = 0; idx < (int)objs.size(); ++idx) {
            const auto& obj = objs[idx];
            if (obj.weaponModelId.empty() || obj.deleted) continue;
            if (skinnedReplacementIndices.count(idx)) continue; // already drawn above (animated)

            const ParsedGeometry* geo = renderer_.GetOrLoadSkinGeometry(obj.modelId, obj.isBuilding);
            int handIdx = findHandBoneIndex(obj.modelId, geo);
            if (handIdx < 0 || !geo || (size_t)handIdx >= geo->bones.size()) continue;

            std::vector<glm::vec3> restPositions = ComputeBoneWorldPositionsPublic(geo->bones);
            if ((size_t)handIdx >= restPositions.size()) continue;

            glm::mat4 objMat(1.0f);
            objMat = glm::translate(objMat, glm::vec3((float)obj.pos.x, (float)obj.pos.y, (float)obj.pos.z));
            objMat = glm::rotate(objMat, (float)obj.rot.z, glm::vec3(0, 0, 1));
            objMat = glm::rotate(objMat, (float)obj.rot.x, glm::vec3(1, 0, 0));
            objMat = glm::rotate(objMat, (float)obj.rot.y, glm::vec3(0, 1, 0));
            objMat = glm::scale(objMat, glm::vec3(40.96f * obj.scale));

            glm::mat4 handLocalMat = glm::translate(glm::mat4(1.0f), restPositions[handIdx] * kMefNativeScale);
            renderer_.DrawAttachedMesh(obj.weaponModelId, false, objMat * handLocalMat * kWeaponHandCorrection);
        }
    }

	DrawCustomCursor();
	glutSwapBuffers();
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
	// cursor_visible_ stays TRUE always — camera lock is handled dynamically in Input_OnMotion.
	// Hiding the cursor permanently caused the "mouse stuck" bug after resuming.
	window_state_.cursor_visible_ = true;
	if (pause_mode_) {
		// Opening pause menu: seed level spinner with current level
		int cur = level_.GetLevelNo();
		if (cur > 0) pause_level_input_ = std::to_string(cur);
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
	if (brush < 0) brush = 0;
	if (brush > 3) brush = 3;
	edit_brush_ = brush;
	static const char* kNames[] = {"Raise", "Lower", "Soften", "Flatten"};
	status_message_ = std::string("Terrain brush: ") + kNames[edit_brush_] +
		"  (radius " + std::to_string((long)edit_brush_radius_) +
		", strength " + std::to_string((long)edit_brush_strength_) + ")";
}

int App::GetEditBrush() const {
	return edit_brush_;
}

bool App::TerrainPaletteClick(int x, int y) {
	if (!edit_mode_ || !terrain_edit_enabled_) return false;
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
}

void App::AdjustBrushStrength(double delta) {
	edit_brush_strength_ += delta;
	if (edit_brush_strength_ < 1.0)   edit_brush_strength_ = 1.0;
	if (edit_brush_strength_ > 100.0) edit_brush_strength_ = 100.0;
	status_message_ = "Brush strength: " + std::to_string((long)edit_brush_strength_);
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

// ── Animation system ─────────────────────────────────────────────────────────
// (Per-object auto-play init now lives in LoadLevel's parallel resolution pass;
//  see app_level.cpp. There is no single-object initializer anymore.)

void App::UpdateAnimations(float dtSec) {
    // Skip if global pause is on
    // (renderer pause check is done in the renderer)

    // For each active playback, update time
    for (auto& [idx, pb] : animPlaybacks_) {
        pb.Update(dtSec * 1000.f);
    }
}

std::string App::BuildAnimStatusString() {
    if (animPlaybacks_.empty()) return {};

    std::string s;
    int count = 0;
    auto& objects = level_.GetLevelObjects().GetObjects();
    for (const auto& [idx, pb] : animPlaybacks_) {
        if (!pb.clip) continue;
        if (count >= 5) { // cap at 5 lines
            s += "... and " + std::to_string((int)animPlaybacks_.size() - count) + " more\n";
            break;
        }
        std::string name = (idx >= 0 && idx < (int)objects.size()) ? objects[idx].name : ("#" + std::to_string(idx));
        if (name.empty()) name = objects[idx].modelId;
        s += name + ": " + pb.clip->name;
        if (pb.playing) {
            int ms = (int)pb.currentTimeMs;
            int d = pb.clip->duration_ms();
            s += " [" + std::to_string(ms) + "/" + std::to_string(d) + "ms]";
        } else {
            s += " [paused]";
        }
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

// Pure computation, no cache reads/writes — safe to call concurrently from worker
// threads (level-load parallel animation resolution) as long as no other thread is
// mutating LevelObjects/AnimationRegistry at the same time (registry must already
// be fully imported — see LoadLevel's sequential ImportAnimations pre-pass).
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
            " has no HumanAI child task — only Stand Animation id (if any) is available");
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

std::unordered_set<int> App::GetSkinnedReplacementObjectIndices() {
    std::unordered_set<int> result;
    auto& objs = level_.GetLevelObjects().GetObjects();
    for (const auto& [idx, pb] : animPlaybacks_) {
        // Paused (animation toggled off for this AI) -> show its normal static
        // mesh, not a frozen skinned pose. Only objects actively playing get
        // replaced with the live skinned draw.
        if (!pb.clip || !pb.playing) continue;
        if (idx < 0 || idx >= (int)objs.size()) continue;
        const auto& obj = objs[idx];
        // Only skip the static draw if the skinned replacement can actually render —
        // otherwise a model whose skin geometry fails to load goes permanently
        // invisible (neither the static nor the skinned draw ever produces anything).
        if (!renderer_.HasSkinGeometry(obj.modelId, obj.isBuilding)) continue;
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


