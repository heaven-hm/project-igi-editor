/******************************************************************************
 * @file    app_input.cpp
 * @brief   App input: event-binding dispatch + level/script reset
 *          Split from app_input.cpp; shares app_internal.h.
 *****************************************************************************/
#include "app_internal.h"
#include "runtime/editor_camera_start.h"
#include "runtime/editor_quick_actions.h"
#include "runtime/loading_bar_style.h"
#include <cstdio>
#include <fstream>

// ---------------------------------------------------------------------------
// Editor camera radius (task-8 CameraResetRadius; used by task-11 item 1 notes).
// EDITOR-ONLY: this value describes the editor's own orbit camera
// (igi::ObjectOrbitCamera, the only camera radius this editor has). IGI-RE
// decompiles IGI.exe and contains no QED editor code, so there is no game-side
// behaviour to match and none is claimed.
// Units: world units, 4096 wu = 1 m (the editor's 40.96 cm->wu scale convention,
// see docs/igi-re-parity/PROPS_WIRE_INTERACTABLES.md).
// ---------------------------------------------------------------------------
static constexpr float kDefaultOrbitRadius = 40960.0f;   // 10 m

static std::string FormatWorldUnitsAsMetres(float world_units) {
	char buf[32];
	std::snprintf(buf, sizeof(buf), "%.3g m", world_units / 4096.0f);
	return std::string(buf);
}

// Look up the human-readable Area name for an AIGraph from
// <exe>\editor\tools\QGraphs\graph_level<N>.json (array of {Graph, Area}).
// Parsed manually to match the project's other JSON readers. "" if not found.
static std::string LookupGraphArea(int level, const std::string& graphId) {
    if (graphId.empty()) return "";
    const int want = atoi(graphId.c_str());
    const std::string path = Utils::GetExeDirectory() +
        "\\editor\\tools\\QGraphs\\graph_level" + std::to_string(level) + ".json";
    std::ifstream f(path, std::ios::binary);
    if (!f) return "";
    const std::string d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    size_t pos = 0;
    while ((pos = d.find("\"Graph\"", pos)) != std::string::npos) {
        const size_t colon = d.find(':', pos + 7);
        if (colon == std::string::npos) break;
        const int g = atoi(d.c_str() + colon + 1);
        const size_t ap = d.find("\"Area\"", colon);
        if (ap == std::string::npos) break;
        const size_t ac = d.find(':', ap + 6);
        const size_t q1 = (ac == std::string::npos) ? std::string::npos : d.find('"', ac + 1);
        const size_t q2 = (q1 == std::string::npos) ? std::string::npos : d.find('"', q1 + 1);
        const std::string area = (q1 != std::string::npos && q2 != std::string::npos)
                                 ? d.substr(q1 + 1, q2 - q1 - 1) : "";
        if (g == want) return area;
        pos = (q2 != std::string::npos) ? q2 : (ap + 6);
    }
    return "";
}

void App::ApplyOrbitRadius(float radius) {
	// Re-derive the eye position from the stored orbit target so the camera keeps
	// looking at the same object (source/runtime/editor_camera_start.h:94-99).
	igi::ObjectOrbitCamera orbit;
	orbit.target = orbit_target_pos_;
	orbit.distance = radius;
	orbit.yaw_degrees = viewer_.yaw_;
	orbit.pitch_degrees = viewer_.pitch_;
	orbit_distance_ = orbit.distance;
	viewer_.pos_ = igi::ObjectOrbitCameraPosition(orbit);
	UpdateViewerVectors();
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

	// Bindings removed for GitHub issue #48 ("implement or remove the 25 stubbed
	// keybindings"). They are gone from the shipped
	// assets/editor/qed/qedkeybindings.qsc, but an already-deployed copy of that
	// file in a user's QED folder can still bind them — so rather than silently
	// doing nothing, say why. Every name here has a row in
	// docs/igi-re-parity/KEYBINDINGS.md.
	auto Removed = [&](const char* name) -> bool {
		if (!Check(name)) return false;
		// "no editor handler": accurate for every name below, including
		// CameraSnapToObjectWithRadius — that one's behaviour does exist, but it
		// belongs to the F11 special-key handler's Shift path
		// (source/app_input_keyboard.cpp:466-471), not to this event name.
		status_message_ = std::string(name) +
			" was removed (#48): no editor handler and no IGI-RE counterpart — "
			"see docs/igi-re-parity/KEYBINDINGS.md";
		return true;
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
	if (Check("CameraAdjustRadius")) {
		// task-11 item 1: REMOVED, not implemented. The editor has no radius
		// adjuster anywhere (nothing reads the old keyAdjustCameraRadius field, and
		// the only radius, igi::ObjectOrbitCamera::distance, is derived by
		// BeginObjectOrbit and only ever reset by CameraResetRadius). Any rate,
		// direction or preset ladder would have been invented, so the binding is
		// gone from assets/editor/qed/qedkeybindings.qsc and a stale user config
		// gets this message instead. Editor-only: IGI-RE decompiles IGI.exe and
		// contains no QED editor code, so there is no game behaviour to match.
		if (Removed("CameraAdjustRadius")) return;
	}
	if (Check("CameraResetRadius")) {
		// The editor's only camera radius is the object-orbit distance
		// (igi::ObjectOrbitCamera::distance, source/runtime/editor_camera_start.h:39-44).
		// Reset it to the editor's documented default radius and re-derive the eye
		// position from the stored orbit target, so the camera stays aimed at the
		// same object. NOTE: the struct's own initialiser is 1.0 world unit, which
		// BeginObjectOrbit (source/runtime/editor_camera_start.h:64-84) only uses as
		// a degenerate fallback when the camera is already on top of the target —
		// 1 wu is 0.24 mm, not a usable viewing distance, so the reset uses
		// kDefaultOrbitRadius (10 m) instead.
		// IGI-RE has no counterpart to cite: it decompiles IGI.exe only, and its
		// editor-camera module attaches the camera to a target with no orbit radius
		// at all (src/editcam/editcamera.c:212 EditCamera_AttachRotation,
		// :273 EditCamera_AttachPosition). Editor-only feature; see
		// docs/igi-re-parity/KEYBINDINGS.md.
		if (orbit_active_) {
			ApplyOrbitRadius(kDefaultOrbitRadius);
			status_message_ = "Camera radius reset to " +
			                  FormatWorldUnitsAsMetres(kDefaultOrbitRadius);
		} else {
			status_message_ = "CameraResetRadius: no orbit target (F11 snaps the camera to an object first)";
		}
		return;
	}
	if (Check("CameraLookDown")) {
		// task-11 item 2: REMOVED, not implemented. Nothing in the editor reads the
		// old keyLookDown field and no camera-look control exists to wire it to
		// (the mouse owns pitch, source/app_view.cpp:43); any step size or rate
		// would have been invented, so the binding is gone from
		// assets/editor/qed/qedkeybindings.qsc and a stale user config gets this
		// message instead. Editor-only: IGI-RE decompiles IGI.exe and contains no
		// QED editor code, so there is no game behaviour to match.
		if (Removed("CameraLookDown")) return;
	}
	if (Check("CameraCopyPosition")) {
		// Issue #48: implemented. Retail QED.doc, "General keystrokes" —
		// "Ctrl P  Copy camera position to clipboard". The editor already owns a
		// clipboard (Utils::SetClipboardText, source/utils.cpp:445 — the same call
		// source/app_editor.cpp:136 uses for text edits) and its live camera is
		// viewer_.pos_, so this copies that value and nothing else: no new state
		// and no second clipboard path. Editor-only: IGI-RE decompiles IGI.exe and
		// contains no QED code, so there is no game-side behaviour to match.
		const std::string text = igi::FormatCameraTripleForClipboard(
			viewer_.pos_.x, viewer_.pos_.y, viewer_.pos_.z);
		Utils::SetClipboardText(text);
		status_message_ = "Camera position copied to clipboard: " + text;
		return;
	}
	if (Check("CameraCopyOrientation")) {
		// Issue #48: implemented — the same command on the camera's angles.
		// viewer_.yaw_/pitch_/roll_ are in degrees (source/app.h:618-620) and are
		// the editor's own camera orientation (persisted as
		// QEDSetCameraOrientation, source/config.cpp:505-508). QED.doc's table
		// documents the Ctrl P position copy; this is its <Ctrl><Shift>O sibling
		// from the shipped default bindings.
		const std::string text = igi::FormatCameraTripleForClipboard(
			viewer_.yaw_, viewer_.pitch_, viewer_.roll_);
		Utils::SetClipboardText(text);
		status_message_ = "Camera orientation copied to clipboard: " + text;
		return;
	}
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
	if (Removed("CameraSnapToObjectWithRadius")) return;
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
	if (Removed("CameraStrafe")) return;
	// CameraStrafeLeft/Right are handled in ProcessInput (continuous per-frame movement)
	// CameraStrafeFree was removed in task-11 item 3: it toggled camera_strafe_free_,
	// which nothing read, so the status bar announced a state change that never
	// happened. Strafing itself (source/app_view.cpp:225-236) is unconditional, so
	// there was no lock for it to release.
	if (Removed("CameraStrafeFree")) return;

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
	if (Check("TaskFind")) {
		// task-11 (found by the new "every advertised binding does something"
		// test, not one of the five items): <Ctrl><F> was advertised in
		// assets/editor/qed/qedkeybindings.qsc:44 with no handler anywhere, so it
		// was a silent no-op next to its three working siblings below. The find bar
		// state already exists (find_open_/find_mode_/find_query_, source/app.h:365-369)
		// and its default mode is FindMode::TaskNameTypeId, so the generic find opens
		// that mode — the same four lines as its siblings, no new state and no new
		// behaviour invented. Editor-only: IGI-RE decompiles IGI.exe and has no QED
		// task-tree find bar, so no game-side claim is made.
		find_mode_  = FindMode::TaskNameTypeId;
		find_open_  = true;
		find_query_.clear();
		find_result_idx_ = -1;
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
		if (find_query_.empty()) {
			// No previous query — open the search bar so the user can type one.
			find_open_ = true;
			find_mode_ = FindMode::TaskNameTypeId;
			find_result_idx_ = -1;
			return;
		}
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
				// ById requires exact match — task IDs are pixel-perfect, so
				// typing "7" must not also match "73", "700", etc. All other
				// find modes keep substring matching.
				const bool is_match = (find_mode_ == FindMode::ById)
					? (label == q)
					: (label.find(q) != std::string::npos);
				if (is_match) {
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
							int row_h = CurrentUiRowHeight();
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
			std::string dir = Utils::GetExeDirectory() + "\\editor\\qed\\templates";
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
	if (Removed("AnimTaskIncreaseKeyframeInterpolation")) return;
	if (Removed("AnimTaskDecreaseKeyframeInterpolation")) return;
	if (Removed("AnimTaskToggleCameraRelative")) return;
	if (Removed("AnimTaskGoToCursor")) return;
	if (Removed("AnimTaskToggleWindowHidden")) return;
	if (Removed("AnimTaskStartRecording")) return;
	if (Removed("AnimTaskGoToTop")) return;
	if (Removed("AnimTaskToggleSyncPlayback")) return;

	// ---- Timer (the animation playback clock) ----
	// Issue #48: implemented. Retail QED.doc, "General keystrokes" — "Ctrl R
	// Reset timer", "Ctrl T  Timer Start / Stop", "F12 / Shift F12  Increment /
	// Decrement AnimTask speed". The editor's ONE playback clock is the selected
	// object's igi::AnimPlayback (source/animation.h, "the editor's ONE playback
	// path"): `currentTimeMs` is the timer, `playing` is start/stop, `Reset()`
	// rewinds and stops, and `speed` is the multiplier the engine is given by
	// AnimPlayback::SyncEngineFlags (source/runtime/anim_playback.cpp:147-151,
	// Anim_SetSpeed 0x4D6580: `inst->speed = gTicksPerFrame * speed`). The
	// playbacks are created by the level load (source/app_level.cpp:664-676
	// auto-plays one per eligible AI NPC) and by the property editor's animation
	// list (App::ToggleAnimationForObject).
	//
	// Issue #73: the four handlers below are now the SAME App methods the F10
	// panel's buttons call (`App::ToggleSelectedAnimPlayback`, `ResetSelectedAnim`,
	// `AdjustSelectedAnimSpeed` in source/app.cpp), so the keys and the panel
	// cannot drift apart. The key names are still what the status bar reports.
	// Editor-only: IGI-RE contains no QED code, so no game-side counterpart is
	// claimed for the keys themselves; the engine call the speed reaches
	// (Anim_SetSpeed 0x4D6580) is cited above.
	if (Check("TimerStartStop")) {
		ToggleSelectedAnimPlayback();
		return;
	}
	if (Check("TimerReset")) {
		ResetSelectedAnim();
		return;
	}
	if (Check("TimerIncreaseSpeed") || Check("TimerDecreaseSpeed")) {
		const bool faster = Check("TimerIncreaseSpeed");
		AdjustSelectedAnimSpeed(faster ? 1 : -1);
		return;
	}

	// ---- Manipulate ----
	if (Removed("Manipulate")) return;
	if (Removed("ManipulatePositionXY")) return;
	if (Removed("ManipulatePositionXZ")) return;
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
	if (Removed("ManipulateOrientationAlpha")) return;
	if (Removed("ManipulateOrientationBeta")) return;
	if (Removed("ManipulateOrientationGamma")) return;
	if (Removed("ManipulateOrientationReset")) return;

	// ---- GraphNode ----
	// Graph node edit controls (only while the overlay is shown).
	if (renderer_.IsGraphOverlayVisible()) {
		if (Check("ScaleGraphNode"))       { PushUndoState(); renderer_.ScaleSelectedGraphNode(0.0f); return; }  // reset radius
		if (Check("ScaleGraphNodeHalfe"))  { PushUndoState(); renderer_.ScaleSelectedGraphNode(0.5f); return; }
		if (Check("ScaleGraphNodeDouble")) { PushUndoState(); renderer_.ScaleSelectedGraphNode(2.0f); return; }
		if (Check("CreateGraphNode"))      { PushUndoState(); renderer_.CreateGraphNode();           return; }
		if (Check("DeleteGraphNode"))      { PushUndoState(); renderer_.DeleteSelectedGraphNode();    return; }
		if (Check("AddGraphLink"))         { PushUndoState(); status_message_ = renderer_.AddGraphLinkStep();        return; }
		if (Check("RemoveGraphLink"))      { PushUndoState(); status_message_ = renderer_.RemoveGraphLinkStep();     return; }
		if (Check("ToggleGraphNodeLabels")) {
			renderer_.ToggleGraphLabels();
			status_message_ = renderer_.GraphLabelsVisible() ? "Graph labels: ON" : "Graph labels: OFF";
			return;
		}
	}
	if (Check("ShowGraphNodes")) {
		// Show/hide the navigation graph of the selected AIGraph task. The graph
		// file is graph<taskId>.dat in the current level's graphs/ folder.
		const auto& objs = level_.GetLevelObjects().GetObjects();
		if (selected_object_index_ >= 0 && selected_object_index_ < (int)objs.size() &&
		    objs[selected_object_index_].type == "AIGraph") {
			if (renderer_.IsGraphOverlayVisible()) {
				renderer_.ToggleGraphOverlay();  // hide
			} else {
				const LevelObject& g = objs[selected_object_index_];
				const std::string path = Utils::GetIGIRootPath() +
					"\\missions\\location0\\level" + std::to_string(last_loaded_level_) +
					"\\graphs\\graph" + g.taskId + ".dat";
				// Node coords are local to the AIGraph task's graph origin (its world pos).
				const std::string area = LookupGraphArea(last_loaded_level_, g.taskId);
				Logger::Get().Log(LogLevel::INFO, "[App] Graph task " + g.taskId +
					" (" + area + ") world offset=(" + std::to_string(g.pos.x) + ", " +
					std::to_string(g.pos.y) + ", " + std::to_string(g.pos.z) + ")");
				if (renderer_.LoadGraphOverlayFile(path, g.pos)) {
					renderer_.SetGraphOverlayMeta(g.taskId, area);
					renderer_.ToggleGraphOverlay();  // show
				}
			}
		} else {
			Logger::Get().Log(LogLevel::INFO, "[App] ShowGraphNodes: select an AIGraph task first");
		}
		return;
	}

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
	if (Check("ToggleAutoSave")) { ToggleAutoSave(); return; }
	if (Check("AutoSaveIntervalUp")) { AdjustAutoSaveInterval(10); return; }
	if (Check("AutoSaveIntervalDown")) { AdjustAutoSaveInterval(-10); return; }
	if (Check("SaveState")) {
		SaveCurrentLevel();
		int lvl = level_.GetLevelNo();
		status_message_ = "Save level: LOCAL:missions/location0/level" +
		                  std::to_string(lvl) + "/objects.qsc";
		return;
	}
	if (Check("SaveObjectFile")) {
		// While editing an AIGraph overlay, Ctrl+S writes the edited node
		// positions straight back to graph<taskId>.dat in the game.
		if (renderer_.IsGraphOverlayVisible() && renderer_.GraphOverlayDirty()) {
			status_message_ = renderer_.SaveGraphOverlay() ? "Graph saved." : "Graph save FAILED.";
			return;
		}
		// Ctrl+S → open a path textbox and write the live objects QSC to that path.
		// (Whole-level save/compile lives in the pause menu.)
		int lvl = level_.GetLevelNo();
		file_dialog_mode_  = FileDialogMode::SaveObjectFile;
		file_dialog_path_  = "missions/location0/level" +
		                     std::to_string(lvl) + "/objects.qsc";
		file_dialog_caret_ = (int)file_dialog_path_.size();
		return;
	}
	if (Check("SaveSubTaskObjectFile")) {
		if (selected_object_index_ >= 0) {
			file_dialog_mode_ = FileDialogMode::SaveSubTask;
			file_dialog_path_ = Config::Get().taskFileName.empty() ?
			    "editor\\qed\\temp\\task.qsc" : Config::Get().taskFileName;
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
				    "editor\\qed\\temp\\task.qsc" : Config::Get().taskFileName;
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
		    "editor\\qed\\temp\\task.qsc" : Config::Get().taskFileName;
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
	if (Check("ToggleObjects")) {
		// "Toggle objects in editor": the same three-way object gate the View menu
		// uses (source/main.cpp:439-450, MENU_SHOW_ALL). Renderer::DRAW_OBJECTS
		// short-circuits the whole object pass (renderer_objects.cpp:760-766);
		// BUILDINGS/PROPS are the per-kind fallbacks consulted once it is cleared.
		// This is an editor viewport option: IGI-RE decompiles IGI.exe only and has
		// no equivalent, so the evidence is the editor's own draw-part system
		// (main.cpp:342-346, debug_command_manager.cpp:104).
		const int kAllObjects = Renderer::DRAW_OBJECTS | Renderer::DRAW_BUILDINGS |
		                        Renderer::DRAW_PROPS;
		const bool objects_on = (GetDrawParts() & Renderer::DRAW_OBJECTS) != 0 &&
		                        (GetDrawParts() & Renderer::DRAW_BUILDINGS) != 0;
		SetDrawParts(objects_on ? (GetDrawParts() & ~kAllObjects)
		                        : (GetDrawParts() | kAllObjects));
		status_message_ = objects_on ? "Objects: OFF" : "Objects: ON";
		return;
	}
	if (Check("TaskMagicObjToggle")) {
		show_magic_obj_spheres_ = !show_magic_obj_spheres_;
		status_message_ = show_magic_obj_spheres_ ? "Magic objects: ON" : "Magic objects: OFF";
	}
	if (Check("AddModelToRes")) {
		int oi = selected_object_index_;
		auto& objs = level_.GetLevelObjects().GetObjects();
		if (oi >= 0 && oi < (int)objs.size() && objs[oi].modelMissingInRes) {
			std::string addId = objs[oi].modelId;
			DrawProgressOverlay("Adding model to .res", 0, "starting");
			auto progressCb = [this, addId](size_t done, size_t total) {
				int pct = total ? (int)(done * 100 / total) : 0;
				DrawProgressOverlay(("Adding '" + addId + "' to .res").c_str(), pct, "packing textures");
			};
			if (renderer_.AddModelToLevelRes(addId, progressCb)) {
				level_res_models_.AddEntry("models\\" + objs[oi].modelId + ".mef");
				objs[oi].modelMissingInRes = false;
				std::string fam = addId.substr(0, addId.find('_'));
				status_message_ = "Added model family '" + fam + "' (+textures) to .res/.dat/.mtp (backups written).";
			} else {
				status_message_ = "Failed to add '" + objs[oi].modelId + "' to level .res (see log).";
			}
		} else {
			status_message_ = "Select an object whose model is flagged missing from the .res first.";
		}
	}
	// CameraStrafeFree removed in task-11 item 3 — see the guard near the Camera
	// section above; the flag and the status message that announced it are gone.
	if (terrain_edit_enabled_) {
		if (Check("TerrainBrushCycle")) {
			SetEditBrush((edit_brush_ + 1) % 4);
		}
		if (Check("TerrainBrushRadiusDec"))   AdjustBrushRadius(0.8);
		if (Check("TerrainBrushRadiusInc"))   AdjustBrushRadius(1.25);
		if (Check("TerrainBrushStrengthDec")) AdjustBrushStrength(-1.0);
		if (Check("TerrainBrushStrengthInc")) AdjustBrushStrength(1.0);
	}
}


#ifdef _WIN32
#include <windows.h>
#endif

void App::ResetLevel() {
	if (in_game_mode_) {
		status_message_ = "Close gameplay before resetting the level";
		return;
	}

	int levelNo = level_.GetLevelNo();

	Logger::Get().Log(LogLevel::INFO, "[App] Resetting Level " + std::to_string(levelNo));

	// The restore is a whole-folder copy and can take seconds: it reports through
	// the same bar as a level load (source/runtime/loading_bar_style.h section 5).
	// This ladder stops at 20% because the reload that follows runs the level-load
	// ladder, which carries the bar to 100.
	DrawProgressOverlay("Resetting Level", igi::kLevelResetPreparing.percent,
	                    igi::kLevelResetPreparing.stage);

	// Only stop the game process launched and tracked by this editor instance.
	// Scanning by executable name used to terminate unrelated retail sessions.
#ifdef _WIN32
	{
		if (game_process_.running && game_process_.hProcess) {
			TerminateProcess(game_process_.hProcess, 0);
			WaitForSingleObject(game_process_.hProcess, 2000);
			game_exited_.store(true, std::memory_order_release);
			if (game_process_.hMonitorThread) {
				WaitForSingleObject(game_process_.hMonitorThread, 1000);
				CloseHandle(game_process_.hMonitorThread);
			}
			CloseHandle(game_process_.hProcess);
			if (game_process_.hThread) CloseHandle(game_process_.hThread);
			game_process_ = {};
			game_exited_.store(false, std::memory_order_release);
			Logger::Get().Log(LogLevel::INFO, "[App] Terminated the editor-owned game process to unlock files.");
		}
	}
#endif

	// Always restore from backup — backup is created on every level load so it
	// captures the pristine state of the full level folder (objects, ai, graphs,
	// models, textures, terrain). A full folder replacement (remove + copy)
	// ensures deleted graph nodes, removed AI scripts, etc. are all reverted.
	DrawProgressOverlay("Resetting Level", igi::kLevelResetRestoring.percent,
	                    igi::kLevelResetRestoring.stage);
	bool restored = false;
	{
		std::string gameLevelDir = Utils::GetIGIRootPath() + "\\missions\\location0\\level" + std::to_string(levelNo);
		std::string backupLevelDir = Utils::GetExeDirectory() + "\\editor\\backup\\level" + std::to_string(levelNo);
		std::string stagingLevelDir = gameLevelDir + ".reset_tmp";
		std::string oldLevelDir = gameLevelDir + ".reset_old";

		Logger::Get().Log(LogLevel::INFO, "[App] Restoring level from backup: " + backupLevelDir + " to " + gameLevelDir);

		if (std::filesystem::exists(backupLevelDir)) {
			try {
				// Stage the complete backup first. If staging or replacement fails,
				// leave the user's level untouched and do not reload a false reset.
				std::error_code ec;
				std::filesystem::remove_all(stagingLevelDir, ec);
				ec.clear();
				std::filesystem::copy(backupLevelDir, stagingLevelDir,
					std::filesystem::copy_options::recursive);
				std::filesystem::remove_all(oldLevelDir, ec);
				ec.clear();
				bool movedOld = false;
				if (std::filesystem::exists(gameLevelDir)) {
					std::filesystem::rename(gameLevelDir, oldLevelDir);
					movedOld = true;
				}
				try {
					std::filesystem::rename(stagingLevelDir, gameLevelDir);
				} catch (...) {
					if (movedOld && !std::filesystem::exists(gameLevelDir))
						std::filesystem::rename(oldLevelDir, gameLevelDir);
					throw;
				}
				if (movedOld) std::filesystem::remove_all(oldLevelDir, ec);
				restored = true;
				Logger::Get().Log(LogLevel::INFO, "[App] Level reset successfully from backup.");
			} catch (const std::exception& e) {
				Logger::Get().Log(LogLevel::ERR, "[App] Failed to restore from backup: " + std::string(e.what()));
			}
		} else {
			Logger::Get().Log(LogLevel::ERR, "[App] Cannot reset level: No backup found at " + backupLevelDir);
		}
	}
	if (!restored) {
		status_message_ = "Reset failed: pristine level backup is unavailable";
		return;
	}

	// Remove local objects.qsc so it recompiles fresh from QVM
	std::string exeDir = Utils::GetExeDirectory();
	std::string dstQsc = exeDir + "\\editor\\qed\\temp\\objects.qsc";
	try {
		if (std::filesystem::exists(dstQsc)) {
			std::filesystem::remove(dstQsc);
		}
	}
	catch (...) {}

	// Reload level after reset
	DrawProgressOverlay("Resetting Level", igi::kLevelResetReloading.percent,
	                    igi::kLevelResetReloading.stage);
	last_loaded_level_ = -1;
	Config::Get().cameraPosX = 0.0f;
	Config::Get().cameraPosY = 0.0f;
	Config::Get().cameraPosZ = 0.0f;
	Config::Get().cameraOriX = 0.0f;
	Config::Get().cameraOriY = 0.0f;
	Config::Get().cameraOriZ = 0.0f;
	LoadLevel(levelNo);
	// Snap objects to terrain after level reset
	SnapObjectsToTerrain();
}

void App::ResetScript() {
	int levelNo = level_.GetLevelNo();

	Logger::Get().Log(LogLevel::INFO, "[App] Resetting Script for Level " + std::to_string(levelNo) + " - restore objects.qvm from editor/tools/restore to IGIPath");

	std::string toolsDir = Utils::GetExeDirectory() + "\\editor\\tools";

	// Copy objects.qvm from editor/tools/restore to IGIPath
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
	std::string dstQsc = exeDir + "\\editor\\qed\\temp\\objects.qsc";
	try {
		if (std::filesystem::exists(dstQsc)) {
			std::filesystem::remove(dstQsc);
		}
	}
	catch (...) {}

	// Reload the level to apply changes
	last_loaded_level_ = -1;
	Config::Get().cameraPosX = 0.0f;
	Config::Get().cameraPosY = 0.0f;
	Config::Get().cameraPosZ = 0.0f;
	Config::Get().cameraOriX = 0.0f;
	Config::Get().cameraOriY = 0.0f;
	Config::Get().cameraOriZ = 0.0f;
	LoadLevel(levelNo);
}


