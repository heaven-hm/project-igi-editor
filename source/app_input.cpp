/******************************************************************************
 * @file    app_input.cpp
 * @brief   App input: event-binding dispatch + level/script reset
 *          Split from app_input.cpp; shares app_internal.h.
 *****************************************************************************/
#include "app_internal.h"

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
	if (Check("ShowGraphNodes")) {
		// Show/hide the navigation graph of the selected AIGraph task. The graph
		// file is graph<taskId>.dat in the current level's graphs/ folder.
		const auto& objs = level_.GetLevelObjects().GetObjects();
		if (selected_object_index_ >= 0 && selected_object_index_ < (int)objs.size() &&
		    objs[selected_object_index_].type == "AIGraph") {
			if (renderer_.IsGraphOverlayVisible()) {
				renderer_.ToggleGraphOverlay();  // hide
			} else {
				const std::string path = Utils::GetIGIRootPath() +
					"\\missions\\location0\\level" + std::to_string(last_loaded_level_) +
					"\\graphs\\graph" + objs[selected_object_index_].taskId + ".dat";
				if (renderer_.LoadGraphOverlayFile(path)) renderer_.ToggleGraphOverlay();  // show
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
	if (Check("SaveState")) {
		SaveCurrentLevel();
		int lvl = level_.GetLevelNo();
		status_message_ = "Save level: LOCAL:missions/location" + std::to_string(lvl) +
		                  "/level" + std::to_string(lvl) + "/objects.qsc";
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
		file_dialog_path_  = "missions/location" + std::to_string(lvl) +
		                     "/level" + std::to_string(lvl) + "/objects.qsc";
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
	if (Check("ToggleObjects")) { Logger::Get().Log(LogLevel::INFO, "[Keybind] ToggleObjects not implemented"); }
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
	if (Check("CameraStrafeFree")) {
		camera_strafe_free_ = !camera_strafe_free_;
		status_message_ = camera_strafe_free_ ? "Strafe lock: ON" : "Strafe lock: OFF";
	}
	if (Check("TerrainBrushCycle")) {
		edit_brush_ = (edit_brush_ + 1) % 4;
		static const char* kNames[] = {"Raise","Lower","Soften","Flatten"};
		status_message_ = std::string("Terrain brush: ") + kNames[edit_brush_];
	}
	if (Check("TerrainBrushRadiusDec"))   AdjustBrushRadius(0.8);
	if (Check("TerrainBrushRadiusInc"))   AdjustBrushRadius(1.25);
	if (Check("TerrainBrushStrengthDec")) AdjustBrushStrength(-1.0);
	if (Check("TerrainBrushStrengthInc")) AdjustBrushStrength(1.0);
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
		std::string backupLevelDir = Utils::GetExeDirectory() + "\\editor\\backup\\level" + std::to_string(levelNo);
		
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
	std::string dstQsc = exeDir + "\\editor\\qed\\temp\\objects.qsc";
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
	LoadLevel(levelNo);
}



