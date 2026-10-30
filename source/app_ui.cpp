/******************************************************************************
 * @file    app_ui.cpp
 * @brief   App: cursor/help/autocomplete loading, file dialog, custom cursor, progress overlay
 *          Split from app.cpp; shares app_internal.h.
 *****************************************************************************/
#include "app_internal.h"
#include "runtime/loading_bar_style.h"
#include "runtime/pause_menu_cursor.h"
#include "runtime/progress_overlay_policy.h"
#include "runtime/retail_pointer_sprite.h"

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
	// Build absolute paths from IGI root — SPR files live in editor\qed\ relative to game install.
	const std::string qed = Utils::GetIGIRootPath() + "\\editor\\qed\\";
	const std::string absPaths[NUM_CURSORS] = {
		qed + "TerrainEditIcon_Pointer.spr",  // 0 Default
		qed + "highlighttool.spr",             // 1 Hover
		qed + "activetool.spr",                // 2 Selected
		qed + "TerrainEditIcon_Lift.spr",      // 3 TerrainLift
		qed + "TerrainEditIcon_Lower.spr",     // 4 TerrainLower
		qed + "TerrainEditIcon_Flatten.spr",   // 5 TerrainFlatten
		qed + "TerrainEditIcon_FlattenLine.spr",// 6 TerrainFlattenLine
		qed + "TerrainEditIcon_Drop.spr",      // 7 TerrainDrop
		qed + "TerrainEditIcon_Soften.spr",    // 8 TerrainSoften
		qed + "inactivetool.spr",              // 9 Inactive
		qed + "editor_camera.spr",             // 10 Camera (ALT held)
		qed + "editor_move.spr",               // 11 Move (ALT held + moving)
	};
	cursor_loaded_count_ = 0;
	for (int i = 0; i < NUM_CURSORS; ++i) {
		cursor_tex_ids_[i] = LoadOneSpr(absPaths[i].c_str(), cursor_tex_ws_[i], cursor_tex_hs_[i]);
		if (cursor_tex_ids_[i]) cursor_loaded_count_++;
		else Logger::Get().Log(LogLevel::WARNING, "[Cursor] Failed to load: " + absPaths[i]);
	}
	Logger::Get().Log(LogLevel::INFO, "[Cursor] Loaded " + std::to_string(cursor_loaded_count_) +
		"/" + std::to_string(NUM_CURSORS) + " cursor sprites");

	// Cache AITYPE_ model IDs from IGIModels.json
	ai_model_ids_.clear();
	{
		std::string jsonPath = Utils::GetExeDirectory() + "\\editor\\tools\\IGIModels.json";
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
		Utils::GetExeDirectory() + "\\editor\\qed\\qedkeybindings.qsc",
		Utils::GetExeDirectory() + "\\qed\\qedkeybindings.qsc",
		Utils::GetIGIRootPath() + "\\editor\\qed\\qedkeybindings.qsc",
		Utils::GetIGIRootPath() + "\\qed\\qedkeybindings.qsc",
		"editor\\qed\\qedkeybindings.qsc",
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
	std::string path = Utils::GetExeDirectory() + "\\editor\\tools\\IGIAutoComplete.txt";
	std::ifstream f(path);
	if (!f.is_open()) {
		// try IGI root fallback
		path = Utils::GetIGIRootPath() + "\\editor\\tools\\IGIAutoComplete.txt";
		f.open(path);
	}
	if (!f.is_open()) { Logger::Get().Log(LogLevel::WARNING, "[App] IGIAutoComplete.txt not found"); return; }
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
		std::string tempQsc = Utils::GetExeDirectory() + "\\editor\\qed\\temp\\objects.qsc";
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
	bool enableCameraMode = Utils::IsKeyBindingPressed(Config::Get().keyEnableCamera);
	if (enableCameraMode)
		camera_mode_moved_ = false;
	current_cursor_mode_ = igi::ResolveCursorMode(
		enableCameraMode,
		mouse_state_.left_button_down_,
		terrain_edit_enabled_,
		edit_brush_,
		selected_object_index_,
		hover_object_index_);
}

// The GAME's own pointer, `LOCAL:menusystem/pointer.spr` out of
// `menusystem/ingamemenu.res` - the sprite `MenuManager_Update` HOOK 0x417CF0
// hands to `Cursor_SetSprite` (src/menu/menumanager.c:233) and `Cursor_Draw`
// HOOK 0x424C40 then blits at the pointer unscaled and at full intensity
// (src/widget/cursor.c:38-41; the 128.0f it passes is 1.0 after the engine's
// 1/128 `COLOR_SCALE`, src/math/draw.c:12).  The editor draws that sprite
// instead of the platform cursor while a menu is open, so the menu's pointer is
// the game's and not the system's.  Loaded once, lazily, on the first menu
// frame; a missing install leaves it unloaded and the caller falls back to the
// platform cursor rather than drawing nothing.
void App::EnsureRetailPointerCursor() {
	if (retail_pointer_tried_) return;
	retail_pointer_tried_ = true;

	const igi::RetailPointerImage image =
		igi::LoadRetailPointerSprite(Utils::GetIGIRootPath());
	if (!image.ok) {
		Logger::Get().Log(LogLevel::WARNING,
			"[Cursor] retail menu pointer unavailable: " + image.reason);
		return;
	}
	pic_s pic;
	pic.width_  = image.width;
	pic.height_ = image.height;
	pic.pixels_ = const_cast<uint8_t*>(image.rgba.data());
	// NEAREST: the pointer is a 20x22 pixel bitmap drawn 1:1, so filtering could
	// only blur it.  The engine draws it with the sprite blit, which does not
	// filter either.
	retail_pointer_tex_ = GL_RegisterTexture(&pic, GL_CLAMP_TO_EDGE, GL_NEAREST,
	                                         GL_NEAREST, false);
	retail_pointer_w_ = image.width;
	retail_pointer_h_ = image.height;
	Logger::Get().Log(LogLevel::INFO,
		"[Cursor] retail menu pointer " + std::string(igi::kRetailPointerResourceName) +
		" " + std::to_string(image.width) + "x" + std::to_string(image.height) +
		" from " + std::string(igi::kRetailPointerArchive));
}

void App::DrawRetailPointerCursor() {
	EnsureRetailPointerCursor();
	if (!retail_pointer_tex_) return;

	const int mx = mouse_state_.prior_x_ + igi::kRetailPointerHotspotX;
	const int my = mouse_state_.prior_y_ + igi::kRetailPointerHotspotY;
	const int vw = window_state_.viewport_width_;
	const int vh = window_state_.viewport_height_;
	const int w  = retail_pointer_w_;
	const int h  = retail_pointer_h_;

	// Same fixed-function HUD state as DrawCustomCursor: a shader/VAO left bound
	// by the 3D pass makes immediate-mode textured quads draw nothing.
	glUseProgram(0);
	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	for (int i = 0; i < 4; ++i) glDisableVertexAttribArray(i);
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
	glBindTexture(GL_TEXTURE_2D, retail_pointer_tex_);
	// 128/128 = full intensity, exactly what Cursor_Draw passes.
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
}

void App::DrawCustomCursor() {
	// While the pause menu is up the pointer is the GAME's own sprite, not the
	// editor's tool cursor and not the platform cursor.
	if (igi::ShouldDrawCustomCursor(pause_mode_)) {
		if (retail_pointer_tex_) {
			DrawRetailPointerCursor();
			return;
		}
		// The retail sprite could not be read: fall back to the platform cursor
		// rather than drawing nothing.  EnsureRetailPointerCursor logs why.
		EnsureRetailPointerCursor();
		return;
	}
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
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	for (int i = 0; i < 4; ++i) glDisableVertexAttribArray(i);
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

void App::DrawProgressOverlay(const char* title, int pct, const char* stage) {
	pct = std::max(0, std::min(100, pct));
	int vw = window_state_.viewport_width_;
	int vh = window_state_.viewport_height_;
	// No viewport, nothing to draw into: the frame is dropped (see
	// `ProgressOverlayCanPaint`, runtime/progress_overlay_policy.h, and why
	// `App::Init` has to seed the viewport before the startup level load).
	if (!igi::ProgressOverlayCanPaint(vw, vh)) return;

	// The overlay owns the surface for this pass: present it BEFORE pumping
	// events and suppress any scene repaint the pump dispatches (see
	// runtime/progress_overlay_policy.h), or the bar is never seen.
	struct OverlayGuard {
		bool& flag;
		~OverlayGuard() { flag = false; }
	} guard{progress_overlay_active_};
	progress_overlay_active_ = true;

	glViewport(0, 0, vw, vh);
	glDisable(GL_SCISSOR_TEST); // prevent ImGui from clipping the overlay
	glUseProgram(0);
	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	for (int i = 0; i < 4; ++i) glDisableVertexAttribArray(i);
	glBindBufferBase(GL_UNIFORM_BUFFER, 0, 0);
	// The retail loading screen clears to black when it tears the bar down:
	//   LoadingScreen_Delete (HOOK 0x48A5D0, src/loadscreen/loadscreen.c:100-108)
	//       Display_SetClearColor(0, 0, 0);
	//       Display_Clear();
	// The editor has no loading picture, so its overlay backdrop is that black.
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glDisable(GL_DEPTH_TEST);
	glMatrixMode(GL_PROJECTION);
	glPushMatrix(); glLoadIdentity();
	glOrtho(0, vw, 0, vh, -1, 1);
	glMatrixMode(GL_MODELVIEW);
	glPushMatrix(); glLoadIdentity();

	// ── the retail loading bar ──────────────────────────────────────────────
	// Geometry, colours and the fill rule all come from
	// source/runtime/loading_bar_style.h, which cites
	//   LoadingScreen_New  (HOOK 0x48A440, src/loadscreen/loadscreen.c:63-96)
	//   LoadingScreen_Step (HOOK 0x48A620, src/loadscreen/loadscreen.c:110-124)
	// The bar is a flat 560x10 two-tone green bitmap inside a screen-centred
	// 640x480 band (:74-76).  It has NO border, NO panel and NO gradient -- and
	// it is NOT the pause menu's MenuScreen_DrawFrame (HOOK 0x421CD0,
	// src/menuscreen/menuscreen.c:168-229), which is a different widget.
	const igi::LoadingBarLayout bar = igi::BuildLoadingBarLayout(vw, vh);
	// The layout is top-down (the engine blits with a top-left origin) while
	// this projection has y=0 at the bottom, so flip the y.
	const int barY = vh - (bar.y + bar.h);

	// One DEBUG line per presented frame: the overlay is the one UI surface whose
	// absence cannot be told apart from "nothing to report" (the user's own report
	// was exactly that -- the bar was invisible for the STARTUP load, see
	// runtime/progress_overlay_policy.h).  DEBUG keeps it out of a normal run's log
	// (a bulk lightmap bake presents one frame per object) while making a
	// diagnostic run say which frames were actually painted, at which viewport and
	// with which retail rect.
	Logger::Get().Log(LogLevel::DEBUG,
	                  std::string("progress overlay: ") + (title != nullptr ? title : "") +
	                      " " + std::to_string(pct) + "% (" +
	                      (stage != nullptr ? stage : "") + ") viewport " +
	                      std::to_string(vw) + "x" + std::to_string(vh) + " bar " +
	                      std::to_string(bar.x) + "," + std::to_string(bar.y) + " " +
	                      std::to_string(bar.w) + "x" + std::to_string(bar.h) + " filled " +
	                      std::to_string(igi::LoadingBarFillWidthForPercent(bar.w, pct)));

	glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	// Track: every pixel starts at 0x7F00 (:77-78).  The low 24 bits are
	// literal 8-bit RGB -- Direct3D_Blit takes them verbatim and ORs in the
	// alpha (src/d3d/direct3d.c:1111-1114) -- so the green is 0x7F / 255 and
	// NOT the menus' 1/128 domain (gColorScale DATA 0x534AB0,
	// src/res/qvertex.c:189).  Dividing by 128 here would draw the track at
	// 0.99 and erase the contrast against the fill.
	glColor4f(igi::kRetailLoadingBarRed, igi::kRetailLoadingTrackGreen,
	          igi::kRetailLoadingBarBlue, 1.0f);
	glBegin(GL_QUADS);
	glVertex2i(bar.x, barY); glVertex2i(bar.x + bar.w, barY);
	glVertex2i(bar.x + bar.w, barY + bar.h); glVertex2i(bar.x, barY + bar.h);
	glEnd();

	// Fill: the first `filled` pixels of each row are overwritten with 0xFF00
	// (:119-121), i.e. 8-bit green 0xFF = 1.0.  The retail rule is
	//   filled = ls->bar.w * ls->done / ls->total;   (:115)
	// an integer fraction of `done`/`total`; the editor's driver is a
	// percentage, which is the same rule with total == 100.
	const int filled = igi::LoadingBarFillWidthForPercent(bar.w, pct);
	if (filled > 0) {
		glColor4f(igi::kRetailLoadingBarRed, igi::kRetailLoadingFillGreen,
		          igi::kRetailLoadingBarBlue, 1.0f);
		glBegin(GL_QUADS);
		glVertex2i(bar.x, barY); glVertex2i(bar.x + filled, barY);
		glVertex2i(bar.x + filled, barY + bar.h); glVertex2i(bar.x, barY + bar.h);
		glEnd();
	}
	glDisable(GL_BLEND);

	// ── the editor's own label ──────────────────────────────────────────────
	// NO ENGINE ORACLE for this text.  The retail loading bar draws none:
	// LoadingScreen_New loads only the loading picture (`loading.pic`, a sprite
	// via Text_Load HOOK 0x4B6DB0, src/res/text.c:12-35) and never a .fnt, so
	// there is no retail loading-bar font to follow.  The editor therefore
	// keeps its OWN strings and its OWN font, drawn just above the bar.
	char msg[160];
	snprintf(msg, sizeof(msg), "%s  -  %d%%  (%s)", title ? title : "", pct, stage ? stage : "");
	glColor3f(0.0f, igi::kRetailLoadingFillGreen, 0.0f);
	void* overlayFont = (Config::Get().systemFontSize >= 15) ? GLUT_BITMAP_HELVETICA_18
	                  : (Config::Get().systemFontSize <= 11) ? GLUT_BITMAP_HELVETICA_10
	                                                         : GLUT_BITMAP_HELVETICA_12;
	glRasterPos2i(bar.x, barY + bar.h + 6);
	for (const char* c = msg; *c; ++c)
		glutBitmapCharacter(overlayFont, *c);

	glMatrixMode(GL_PROJECTION); glPopMatrix();
	glMatrixMode(GL_MODELVIEW);  glPopMatrix();
	glEnable(GL_DEPTH_TEST);

	const igi::ProgressOverlayState overlay_state{progress_overlay_active_, false};
	if (igi::ShouldPresentOverlayBeforePumpingEvents(overlay_state))
		glutSwapBuffers();
	glutMainLoopEvent();
}

