/******************************************************************************
 * @file    app_ui.cpp
 * @brief   App: cursor/help/autocomplete loading, file dialog, custom cursor, progress overlay
 *          Split from app.cpp; shares app_internal.h.
 *****************************************************************************/
#include "app_internal.h"

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
		"editor\\qed\\TerrainEditIcon_Pointer.spr",     // 0 Default
		"editor\\qed\\highlighttool.spr",               // 1 Hover
		"editor\\qed\\activetool.spr",                  // 2 Selected
		"editor\\qed\\TerrainEditIcon_Lift.spr",        // 3 TerrainLift
		"editor\\qed\\TerrainEditIcon_Lower.spr",       // 4 TerrainLower
		"editor\\qed\\TerrainEditIcon_Flatten.spr",     // 5 TerrainFlatten
		"editor\\qed\\TerrainEditIcon_FlattenLine.spr", // 6 TerrainFlattenLine
		"editor\\qed\\TerrainEditIcon_Drop.spr",        // 7 TerrainDrop
		"editor\\qed\\TerrainEditIcon_Soften.spr",      // 8 TerrainSoften
		"editor\\qed\\inactivetool.spr",                // 9 Inactive
		"editor\\qed\\editor_camera.spr",               // 10 Camera (ALT held)
		"editor\\qed\\editor_move.spr",                 // 11 Move (ALT held + moving)
	};
	cursor_loaded_count_ = 0;
	for (int i = 0; i < NUM_CURSORS; ++i) {
		cursor_tex_ids_[i] = LoadOneSpr(paths[i], cursor_tex_ws_[i], cursor_tex_hs_[i]);
		if (cursor_tex_ids_[i]) cursor_loaded_count_++;
	}

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

void App::DrawProgressOverlay(const char* title, int pct, const char* stage) {
	pct = std::max(0, std::min(100, pct));
	int vw = window_state_.viewport_width_;
	int vh = window_state_.viewport_height_;
	if (vw <= 0 || vh <= 0) return;
	glViewport(0, 0, vw, vh);
	glDisable(GL_SCISSOR_TEST); // prevent ImGui from clipping the overlay
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
	int bx = px+20, by = py+18, bw = pw-40, bh = 12;
	glColor3f(0.0f, 0.3f, 0.0f);
	glBegin(GL_QUADS);
	glVertex2i(bx, by); glVertex2i(bx+bw, by);
	glVertex2i(bx+bw, by+bh); glVertex2i(bx, by+bh);
	glEnd();
	int fw = bw * pct / 100;
	glColor3f(0.1f, 0.95f, 0.1f);
	glBegin(GL_QUADS);
	glVertex2i(bx, by); glVertex2i(bx+fw, by);
	glVertex2i(bx+fw, by+bh); glVertex2i(bx, by+bh);
	glEnd();
	glDisable(GL_BLEND);

	char msg[160];
	snprintf(msg, sizeof(msg), "%s  -  %d%%  (%s)", title ? title : "", pct, stage ? stage : "");
	glColor3f(0.0f, 0.9f, 0.0f);
	glRasterPos2i(px + 20, py + ph - 18);
	for (const char* c = msg; *c; ++c)
		glutBitmapCharacter(GLUT_BITMAP_HELVETICA_12, *c);

	glMatrixMode(GL_PROJECTION); glPopMatrix();
	glMatrixMode(GL_MODELVIEW);  glPopMatrix();
	glEnable(GL_DEPTH_TEST);
	glutMainLoopEvent();
	glutSwapBuffers();
}

