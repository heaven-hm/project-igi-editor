/******************************************************************************
 * @file    app_editor.cpp
 * @brief   App editing/workflow: property + AI-script editing, undo/redo, task
 *          tree ops, QSC load/compile/decompile, attach promotion, game launch.
 *          Split from app.cpp; shares app_internal.h.
 *****************************************************************************/
#include "app_internal.h"

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

	// Only AI model types get the script section. Check both the modelId
	// (against the AITYPE_ set from IGIModels.json) AND the object type —
	// HumanSoldier/HumanSoldierFemale/HumanPlayer/HumanSoldierRPG are always
	// AI containers even if their modelId isn't tagged AITYPE_ in the JSON.
	bool isAiType = (obj.type == "HumanSoldier" || obj.type == "HumanSoldierFemale" ||
	                 obj.type == "HumanPlayer" || obj.type == "HumanSoldierRPG" ||
	                 obj.type == "HumanAI");
	bool isAiModel = (ai_model_ids_.find(obj.modelId) != ai_model_ids_.end());
	if (!isAiType && !isAiModel) return;

	// The .qvm belongs to the HumanAI child task (not the HumanSoldier parent).
	// If this object IS the HumanAI, use its own ID; otherwise walk the children
	// recursively to find a HumanAI — the AI task can be nested several levels
	// deep (e.g. HumanSoldier → Gun → HumanAI). Limit the search depth so a
	// malformed tree doesn't hang the editor.
	const LevelObject* aiTask = nullptr;
	if (obj.type == "HumanAI") {
		aiTask = &obj;
	} else {
		const int kMaxAIDepth = 15;
		std::vector<int> frontier = obj.childrenIndices;
		int depth = 0;
		while (!frontier.empty() && depth < kMaxAIDepth) {
			std::vector<int> next;
			for (int ci : frontier) {
				if (ci < 0 || ci >= (int)objects.size()) continue;
				if (objects[ci].deleted) continue;
				if (objects[ci].type == "HumanAI") {
					aiTask = &objects[ci];
					frontier.clear();
					break;
				}
				for (int gc : objects[ci].childrenIndices) next.push_back(gc);
			}
			if (aiTask) break;
			frontier = std::move(next);
			++depth;
		}
		if (!aiTask) {
			Logger::Get().Log(LogLevel::WARNING,
				"[App] HumanAI not found under HumanSoldier '" + obj.name +
				"' (taskId=" + obj.taskId + ") within " + std::to_string(kMaxAIDepth) +
				" levels of children — AI script not loaded");
		}
	}
	if (!aiTask || aiTask->taskId.empty()) {
		if (aiTask)
			Logger::Get().Log(LogLevel::WARNING,
				"[App] HumanAI found but taskId is empty for '" + obj.name +
				"' (taskId=" + obj.taskId + ") — AI script not loaded");
		return;
	}

	int levelNo = level_.GetLevelNo();
	std::string aiDir = Utils::GetIGIRootPath() +
	                    "\\missions\\location0\\level" + std::to_string(levelNo) + "\\ai";
	ai_script_path_ = aiDir + "\\" + aiTask->taskId + ".qvm";
	Logger::Get().Log(LogLevel::INFO,
		"[App] AI script loaded for " + obj.type + " '" + obj.name +
		"' (taskId=" + obj.taskId + ") -> HumanAI taskId=" + aiTask->taskId +
		" path=" + ai_script_path_);

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
	PushUndoState();

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
	}

	// GunPickup/AmmoPickup: the edited field is the weapon/ammo enum string, but
	// obj.modelId must hold the RESOLVED render model. Re-resolve so the viewport
	// mesh updates immediately instead of only after a reload (issue 1).
	if (obj.type == "GunPickup" || obj.type == "AmmoPickup") {
		std::string enumStr = StripQuotes(prop_text_buf_);
		if (enumStr.rfind("WEAPON_ID_", 0) == 0 || enumStr.rfind("AMMO_ID_", 0) == 0) {
			obj.modelId = level_.GetLevelObjects().ResolvePickupModelId(enumStr);
			is_model_field = true; // treat it as a model field to trigger packing and preload
		}
	}

	if (is_model_field && !obj.modelId.empty()) {
		if (!level_res_models_.Empty() && !level_res_models_.Contains(obj.modelId)) {
			obj.modelMissingInRes = true;
			// Auto-add the foreign model immediately — no extra keypress needed.
			std::string addId = obj.modelId;
			DrawProgressOverlay(("Adding '" + addId + "' to .res").c_str(), 0, "starting");
			auto progressCb = [this, addId](size_t done, size_t total) {
				int pct = total ? (int)(done * 100 / total) : 0;
				DrawProgressOverlay(("Adding '" + addId + "' to .res").c_str(), pct, "packing textures");
			};
			if (renderer_.AddModelToLevelRes(addId, progressCb)) {
				level_res_models_.AddEntry("models\\" + addId + ".mef");
				obj.modelMissingInRes = false;
				std::string fam = addId.substr(0, addId.find('_'));
				status_message_ = "Added model family '" + fam + "' (+textures) to .res/.dat/.mtp.";
			} else {
				status_message_ = "Failed to add '" + addId + "' to level .res (see log).";
			}
		} else {
			obj.modelMissingInRes = false;
		}

		// Eagerly load the (possibly new) model now, with a progress overlay, so a heavy
		// model with many textures doesn't appear to freeze the editor on the next frame
		// (the load is otherwise lazy in Draw → looks like a hang). (user feedback)
		DrawProgressOverlay(("Loading model '" + obj.modelId + "'").c_str(), 40, "mesh & textures");
		renderer_.PreloadModel(obj.modelId, obj.isBuilding);
	}

	obj.modified = true;
	level_.GetLevelObjects().UpdateCoordinatesInLine(obj);

	// Live-sync the graph overlay offset when the user moves the AIGraph
	// task via the property panel — otherwise the 3D nodes/edges stay at the
	// stale position while F7 is showing the graph.
	SyncGraphOverlayOffsetFromAIGraph();
}

void App::PushUndoState() {
	UndoState state;
	state.objects = level_.GetLevelObjects().GetObjects();
	state.ai_script_path = ai_script_path_;
	state.ai_script_text = ai_script_text_;
	state.ai_script_dirty = ai_script_dirty_;
	state.terrain_mod_options = terrain_mod_options_;
	state.terrain_hmp = level_.SnapshotTerrainHMP();
	if (renderer_.IsGraphOverlayVisible()) {
		state.graph_overlay = renderer_.GetGraphOverlaySnapshot();
		state.graph_overlay_visible = true;
	}
	undo_stack_.push_back(std::move(state));
	redo_stack_.clear();
	if (undo_stack_.size() > 20)
		undo_stack_.erase(undo_stack_.begin());
}

void App::SaveAndReloadObjects() {
	level_.SaveAndReloadObjects();
	EvaluateTrainTrackPositions();
	SnapObjectsToTerrain();
	RebuildLevelModelIds();
	// Reloading objects recreates them from the QSC, so the AIGraph task's pos
	// is the source of truth again — re-apply it to the graph overlay.
	SyncGraphOverlayOffsetFromAIGraph();
}

void App::SyncGraphOverlayOffsetFromAIGraph() {
	// No-op unless the user has pressed F7 to show the graph (the offset is
	// only consumed by the overlay draw path; the on-disk graph<id>.dat never
	// changes here, only the displayed world position does).
	if (!renderer_.IsGraphOverlayVisible()) return;
	const std::string& tid = renderer_.GraphOverlayTaskId();
	if (tid.empty()) return;
	for (const auto& o : level_.GetLevelObjects().GetObjects()) {
		if (o.type == "AIGraph" && o.taskId == tid) {
			const glm::dvec3& current = renderer_.GraphOverlayOffset();
			if (current != o.pos) {
				renderer_.SetGraphOverlayOffset(o.pos);
				Logger::Get().Log(LogLevel::INFO,
					"[App] Graph overlay offset live-synced to AIGraph " + tid +
					" pos=(" + std::to_string(o.pos.x) + ", " +
					std::to_string(o.pos.y) + ", " + std::to_string(o.pos.z) + ")");
			}
			return;
		}
	}
}

void App::RebuildLevelModelIds() {
	level_model_ids_.clear();
	level_.GetLevelObjects().LoadModelNames();
	for (const auto& pair : level_.GetLevelObjects().GetModelNamesMap()) {
		const std::string& m = pair.first;
		bool ok = m.size() >= 7;
		if (ok) {
			for (char c : m) if (!isdigit(c) && c != '_') { ok = false; break; }
		}
		if (ok) level_model_ids_.insert(m);
	}
}

void App::Undo() {
	if (undo_stack_.empty()) { status_message_ = "Nothing to undo"; return; }
	// Save current state to redo stack
	UndoState current;
	current.objects = level_.GetLevelObjects().GetObjects();
	current.ai_script_path = ai_script_path_;
	current.ai_script_text = ai_script_text_;
	current.ai_script_dirty = ai_script_dirty_;
	current.terrain_mod_options = terrain_mod_options_;
	current.terrain_hmp = level_.SnapshotTerrainHMP();
	if (renderer_.IsGraphOverlayVisible()) {
		current.graph_overlay = renderer_.GetGraphOverlaySnapshot();
		current.graph_overlay_visible = true;
	}
	redo_stack_.push_back(std::move(current));

	// Restore from undo stack
	const UndoState& s = undo_stack_.back();
	level_.GetLevelObjects().GetObjects() = s.objects;
	ai_script_path_ = s.ai_script_path;
	ai_script_text_ = s.ai_script_text;
	ai_script_dirty_ = s.ai_script_dirty;
	terrain_mod_options_ = s.terrain_mod_options;
	level_.RestoreTerrainHMP(s.terrain_hmp);

	if (s.graph_overlay_visible) {
		renderer_.RestoreGraphOverlay(s.graph_overlay);
		if (!renderer_.IsGraphOverlayVisible())
			renderer_.SetGraphOverlayVisible(true);
	} else {
		if (renderer_.IsGraphOverlayVisible())
			renderer_.SetGraphOverlayVisible(false);
	}

	undo_stack_.pop_back();
	// Mark ATTA proxies as modified so FlushAttaProxiesToMef() rewrites their
	// local positions back into the MEF binary. Without this, undoing a building
	// ATTA move left the MEF at the post-edit position while the proxy's world
	// pos reverted — the 3D view and the saved level disagreed.
	for (auto& o : level_.GetLevelObjects().GetObjects())
		if (o.isAttaProxy) o.modified = true;
	FlushAttaProxiesToMef();
	SaveAndReloadObjects();
	// The objects vector was just replaced with the snapshot; if a visible
	// graph overlay's task id is present in the restored list, the AIGraph
	// task's pos is now the canonical one and the overlay must follow it.
	SyncGraphOverlayOffsetFromAIGraph();
	status_message_ = "Undo";
}

void App::Redo() {
	if (redo_stack_.empty()) { status_message_ = "Nothing to redo"; return; }
	// Save current state to undo stack
	UndoState current;
	current.objects = level_.GetLevelObjects().GetObjects();
	current.ai_script_path = ai_script_path_;
	current.ai_script_text = ai_script_text_;
	current.ai_script_dirty = ai_script_dirty_;
	current.terrain_mod_options = terrain_mod_options_;
	current.terrain_hmp = level_.SnapshotTerrainHMP();
	if (renderer_.IsGraphOverlayVisible()) {
		current.graph_overlay = renderer_.GetGraphOverlaySnapshot();
		current.graph_overlay_visible = true;
	}
	undo_stack_.push_back(std::move(current));

	// Restore from redo stack
	const UndoState& s = redo_stack_.back();
	level_.GetLevelObjects().GetObjects() = s.objects;
	ai_script_path_ = s.ai_script_path;
	ai_script_text_ = s.ai_script_text;
	ai_script_dirty_ = s.ai_script_dirty;
	terrain_mod_options_ = s.terrain_mod_options;
	level_.RestoreTerrainHMP(s.terrain_hmp);

	if (s.graph_overlay_visible) {
		renderer_.RestoreGraphOverlay(s.graph_overlay);
		if (!renderer_.IsGraphOverlayVisible())
			renderer_.SetGraphOverlayVisible(true);
	} else {
		if (renderer_.IsGraphOverlayVisible())
			renderer_.SetGraphOverlayVisible(false);
	}

	redo_stack_.pop_back();
	// Same ATTA-proxy resync as Undo — see comment there.
	for (auto& o : level_.GetLevelObjects().GetObjects())
		if (o.isAttaProxy) o.modified = true;
	FlushAttaProxiesToMef();
	SaveAndReloadObjects();
	// Same graph-overlay live-sync as Undo — see comment there.
	SyncGraphOverlayOffsetFromAIGraph();
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

		std::string qsc_dest = Utils::GetExeDirectory() + "\\editor\\qed\\temp\\objects.qsc";
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
		std::string qsc_dest = Utils::GetExeDirectory() + "\\editor\\qed\\temp\\objects.qsc";

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

	std::string qsc_source = Utils::GetExeDirectory() + "\\editor\\qed\\temp\\objects.qsc";
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


