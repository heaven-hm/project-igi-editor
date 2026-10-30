/******************************************************************************
 * @file    app_view.cpp
 * @brief   App: camera/view update, collision, terrain snap, marker manipulation
 *          Split from app.cpp; shares app_internal.h.
 *****************************************************************************/
#include "app_internal.h"
#include "renderer/igi_transform.h"
#include "runtime/editor_camera_start.h"
#include "level/ai_graph_ground.h"
#include "level/collision_drop_rules.h"
#include "level/collision_world.h"
#include "level/terrain_snap_rules.h"
#include <unordered_map>
#include <unordered_set>

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
			igi::ObjectOrbitCamera orbit;
			orbit.target = orbit_target_pos_;
			orbit.distance = orbit_distance_;
			orbit.yaw_degrees = viewer_.yaw_;
			orbit.pitch_degrees = viewer_.pitch_;
			orbit = igi::StepObjectOrbit(
				orbit,
				-input_.mouse_delta_x_ * MOUSE_SENSITIVE,
				-input_.mouse_delta_y_ * MOUSE_SENSITIVE);
			viewer_.yaw_ = orbit.yaw_degrees;
			viewer_.pitch_ = orbit.pitch_degrees;
			orbit_distance_ = orbit.distance;
			viewer_.pos_ = igi::ObjectOrbitCameraPosition(orbit);
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
	UpdateViewerVectors(viewer_);
}

void App::UpdateGameplayViewerVectors() {
	UpdateViewerVectors(gameplay_viewer_);
}

void App::UpdateViewerVectors(viewer_s& viewer) {
	while (viewer.yaw_ < 0.0f) {
		viewer.yaw_ += 360.0f;
	}

	while (viewer.yaw_ > 360.0f) {
		viewer.yaw_ -= 360.0f;
	}

	viewer.pitch_ = std::clamp(viewer.pitch_, -89.0f, 89.0f);

	while (viewer.roll_ < 0.0f) {
		viewer.roll_ += 360.0f;
	}

	while (viewer.roll_ > 360.0f) {
		viewer.roll_ -= 360.0f;
	}

	AngleToVectors(
		viewer.yaw_,
		viewer.pitch_,
		viewer.roll_,
		viewer.forward_,
		viewer.right_,
		viewer.up_);
}

void App::UpdateViewDefine() {
	UpdateViewDefine(viewer_, view_define_);
}

void App::UpdateGameplayViewDefine() {
	UpdateViewDefine(gameplay_viewer_, view_define_);
}

void App::UpdateViewDefine(const viewer_s& viewer, view_define_s& view_define) {
	view_define.pos_ = viewer.pos_;
	view_define.forward_ = viewer.forward_;
	view_define.right_ = viewer.right_;
	view_define.up_ = viewer.up_;
	view_define.render_z_near_ = RENDER_Z_NEAR;

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
	view_define.mat_rot_[0][0] = view_define.right_.x;
	view_define.mat_rot_[1][0] = view_define.right_.y;
	view_define.mat_rot_[2][0] = view_define.right_.z;

	view_define.mat_rot_[0][1] = -view_define.up_.x;
	view_define.mat_rot_[1][1] = -view_define.up_.y;
	view_define.mat_rot_[2][1] = -view_define.up_.z;

	view_define.mat_rot_[0][2] = view_define.forward_.x;
	view_define.mat_rot_[1][2] = view_define.forward_.y;
	view_define.mat_rot_[2][2] = view_define.forward_.z;
}

void App::EditorProcessClick() {
	if (!window_state_.viewport_width_ || !window_state_.viewport_height_) return;

	LevelObjects& lo = level_.GetLevelObjects();
	std::vector<LevelObject>& objects = lo.GetObjects();

	if (terrain_edit_enabled_) {
		// Push undo state once per click-drag sequence for terrain edits
		if (!undo_state_pushed_for_manip_) {
			PushUndoState();
			undo_state_pushed_for_manip_ = true;
		}
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
		level_.EditorRaycastAndModify(ray_origin, ray_dir, edit_brush_, edit_brush_radius_, edit_brush_strength_);
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
				const int row_h   = CurrentUiRowHeight();
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
    if (noclip_mode_) return false;
    if (level_.GetLevelNo() == 0) return false;

    const bool use_gameplay_snapshot =
        igi::ResolveRuntimeAssetTarget(
            in_game_mode_,
            runtime_level_objects_.has_value()) ==
        igi::RuntimeAssetTarget::GameplaySnapshot;
    const LevelObjects& collision_level_objects = use_gameplay_snapshot &&
            runtime_level_objects_.has_value()
        ? runtime_level_objects_.value()
        : level_.GetLevelObjects();
    const auto& objects = collision_level_objects.GetObjects();
    constexpr float BASE_SCALE = 40.96f;
    constexpr float EDITOR_CAMERA_RADIUS = 300.0f; // ~0.075m
    const float collision_radius = in_game_mode_
        ? igi::PlayerController::BODY_RADIUS
        : EDITOR_CAMERA_RADIUS;

	for (int object_index = 0; object_index < static_cast<int>(objects.size()); ++object_index) {
		const auto& obj = objects[object_index];
		if (obj.deleted) continue;
		if (in_game_mode_ && obj.type == "Door" &&
			gameplay_host_.GetWorld().IsDoorFullyOpen(object_index)) {
			continue;
		}
        if (in_game_mode_ &&
            (obj.type == "HumanSoldier" || obj.type == "HumanSoldierFemale" ||
             obj.type == "HumanSoldierRPG" || obj.type == "HumanPlayer" ||
             obj.type == "HumanAI")) {
            // Runtime AI is simulated as dynamic entities. The injected
            // collision callback must represent static level geometry only;
            // guards are handled by RuntimeWorld's obstacle and hit queries.
            continue;
        }
        std::string mId = !obj.modelId.empty() ? obj.modelId : obj.segmentModelId;
        if (mId.empty()) continue;

        float dist = glm::distance(nextPos, glm::vec3(obj.pos));
        if (dist > 150000.0f) continue;

        glm::mat4 model = glm::mat4(1.0f);
        model = glm::translate(model, glm::vec3(obj.pos.x, obj.pos.y, obj.pos.z));
        // Authored IGI rotation: Rz * Ry * Rx, like the game (renderer/igi_transform.h).
        model = model * igi::IgiEulerRotationMatrix(obj.rot);
        float s = (obj.scale > 0.0f) ? obj.scale : 1.0f;
        model = glm::scale(model, glm::vec3(BASE_SCALE * s));
        model = glm::rotate(model, glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f));

        glm::vec4 localPos = glm::inverse(model) * glm::vec4(nextPos, 1.0f);
        glm::vec3 extents = renderer_.GetMeshExtents(mId, obj.isBuilding);
        glm::vec3 center = renderer_.GetMeshCenter(mId, obj.isBuilding);

        if (extents.x < 1.0f && extents.y < 1.0f && extents.z < 1.0f) continue;

        // Gameplay uses the same 0.4 m body envelope as the fixed-step
        // HumanWallProbe port; the editor camera keeps its historical narrow
        // pick/collision envelope so authoring navigation remains precise.
        float pr = collision_radius / BASE_SCALE;
        glm::vec3 rel = glm::vec3(localPos) - center;
        if (std::abs(rel.x) < (extents.x + pr) &&
            std::abs(rel.y) < (extents.y + pr) &&
            std::abs(rel.z) < (extents.z + pr))
        {
            return true;
        }
    }
    return false;
}

namespace {

// The HumanAI controller under a human placement, and the AIGraph task id it
// was authored with.
//
// The engine creates the controller as a CHILD of the human: HumanAI_Create
// HOOK 0x44F060 calls Human_SetAI(ai->task.parent, ai)
// (src/humanai/humanai.c:62-70, :69), and Human_SetAI HOOK 0x460C70 stores the
// pointer in Human::ai (src/humanbase/human.c:1157-1161).  The graph id is the
// HumanAI's second declared parameter, "Graph ID" / Int32
// (src/aiaction/aiaction.c:204-207), i.e. QSC argument 4 -- the editor already
// stores it as LevelObject::aiGraphTaskId.
struct SoldierAI {
    bool has_controller = false;
    int graph_task_id = -1;
};

SoldierAI FindSoldierAI(const std::vector<LevelObject>& objects, int soldier_index) {
    SoldierAI result;
    if (soldier_index < 0 || soldier_index >= (int)objects.size()) return result;
    std::vector<int> pending = objects[soldier_index].childrenIndices;
    int budget = 64;  // a malformed/cyclic tree cannot hang a level load
    while (!pending.empty() && budget-- > 0) {
        const int index = pending.back();
        pending.pop_back();
        if (index < 0 || index >= (int)objects.size() || objects[index].deleted) continue;
        if (objects[index].type == "HumanAI") {
            result.has_controller = true;
            result.graph_task_id = objects[index].aiGraphTaskId;
            return result;
        }
        for (int child : objects[index].childrenIndices) pending.push_back(child);
    }
    return result;
}

const LevelObject* FindAiGraphTask(const std::vector<LevelObject>& objects, int graph_task_id) {
    if (graph_task_id < 0) return nullptr;
    const std::string id = std::to_string(graph_task_id);
    for (const LevelObject& o : objects) {
        if (!o.deleted && o.type == "AIGraph" && o.taskId == id) return &o;
    }
    return nullptr;
}

}  // namespace

void App::SnapObjectsToTerrain() {
    auto& objects = level_.GetLevelObjects().GetObjects();
    Logger::Get().Log(LogLevel::INFO, "[App] Snapping " + std::to_string(objects.size()) + " objects to terrain...");

    // ── Build the OBJECT half of the engine's ground query ────────────────────
    // The engine's drop is not terrain-only.  Coll_Drop HOOK 0x4CABF0 body
    // (src/coll/coll.c:575-614) runs Coll_LinesEx over the same vertical segment
    // (:577) and then lets the object hit and the terrain hit compete on
    // |z - probe| (:593-594); the object hit wins a tie.  Coll_LinesEx
    // HOOK 0x4CDB20 (src/model/coll.c:497-505) walks dynamic-cube trees 0, 1, 4
    // and 5 -- the rigid objects and the sector objects (buildings) -- and every
    // one answers with RigidDynCubeObj_LineHit HOOK 0x4C1840
    // (src/qasm/rigidhit.c:104-470), i.e. the model's HSMC collision set
    // (Direct3DRender_LoadModel HOOK 0x49FB10, src/bonemesh/d3drender.c:245-260).
    //
    // The human probe asks for collision set 1: Human_ProbeGround passes 1 as
    // Coll_DropPointsEx's last argument (src/humanbase/human.c:1759,1771), which
    // Coll_Drop forwards to Coll_LinesEx as `q->mesh` (src/coll/coll.c:577,
    // src/model/coll.c:515).  Set 1 is the walkable/human set: the invisible
    // colbox volumes carry their box only in set 1 (measured on the shipped
    // COMMON/MODELS/models/colbox*.mef), while a building's set 1 is the coarse
    // one-surface mesh and set 0 is the multi-material bullet/LOS mesh.
    //
    // Bone objects (humans) live in trees 2/3, which Coll_LinesEx skips, so they
    // are not part of this world; their models also carry no set-1 collision.
    igi::CollisionWorld collision_world;
    {
        // Only humans WITHOUT an AI controller are dropped by this pass, and only
        // they query the object world, so a level whose humans all have a HumanAI
        // pays nothing here.  (The engine builds its cubes either way; this is a
        // load-time shortcut with no effect on the answer.)
        bool any_human = false;
        for (std::size_t i = 0; i < objects.size(); ++i) {
            const LevelObject& o = objects[i];
            if (o.deleted || (o.type != "HumanSoldier" && o.type != "HumanSoldierFemale")) continue;
            if (!FindSoldierAI(objects, (int)i).has_controller) {
                any_human = true;
                break;
            }
        }
        std::unordered_map<std::string, const igi::CollisionMesh*> mesh_by_model;
        std::vector<uint8_t> mef_bytes;
        if (any_human) {
            for (std::size_t i = 0; i < objects.size(); ++i) {
                const LevelObject& obj = objects[i];
                if (obj.deleted || obj.modelId.empty()) continue;
                // An ATTA proxy is a sub-record of its parent model: the parent's
                // instance already covers the world-space geometry.
                if (obj.isAttaProxy) continue;
                // BoneDynCubeObj (human) collision is not in the queried trees.
                if (obj.type == "HumanSoldier" || obj.type == "HumanSoldierFemale" ||
                    obj.type == "HumanPlayer") {
                    continue;
                }
                // Wires and spline objects are NOT drawn with
                // T(pos) * R(rot) * S(scale): a wire is stretched between its two
                // anchors (Wire_ParamsChanged 0x43F3D0, src/props/wire.c:160-191)
                // and a spline draws per-span matrices.  An instance at the
                // authored pose would be a wrong surface, so leave them out
                // rather than invent one.
                if (obj.isWire || obj.isSplineContainer || obj.isSplineWaypoint) continue;
                const igi::CollisionMesh* mesh = nullptr;
                auto cached = mesh_by_model.find(obj.modelId);
                if (cached != mesh_by_model.end()) {
                    mesh = cached->second;
                } else {
                    if (renderer_.GetModelMefBytes(obj.modelId, obj.isBuilding, mef_bytes)) {
                        igi::CollisionMesh parsed;
                        if (igi::ParseCollisionMeshSet(mef_bytes.data(), mef_bytes.size(), 1, parsed)) {
                            mesh = collision_world.InternMesh(obj.modelId, std::move(parsed));
                        }
                    }
                    mesh_by_model.emplace(obj.modelId, mesh);
                }
                if (mesh == nullptr) continue;  // no set-1 collision: the engine has none either
                float matrix[9];
                igi::CollisionMatrixFromEuler(obj.rot.x, obj.rot.y, obj.rot.z, matrix);
                const double pos[3] = {obj.pos.x, obj.pos.y, obj.pos.z};
                collision_world.AddInstance((int)i, mesh, pos, matrix, obj.scale);
            }
        }
        Logger::Get().Log(LogLevel::INFO, "[App] Collision world: " +
            std::to_string(collision_world.instance_count()) + " objects, " +
            std::to_string(collision_world.mesh_count()) + " meshes, " +
            std::to_string(collision_world.triangle_count()) + " triangles, " +
            std::to_string(collision_world.byte_size()) + " bytes");
    }

    int snapped = 0;
    int skipped = 0;
    int no_terrain = 0;
    // AIGraph data for the AI humans, parsed once per AIGraph task per pass
    // (GRAPH_Parse = GraphOpen HOOK 0x4F9FF0, src/graphnode/stream.c:196-237).
    std::unordered_map<std::string, GraphFile> graph_cache;
    for (auto& obj : objects) {
        // Skip non-spatial task nodes: HumanAI, PatrolPath, ConditionalContainer, LevelFlow, etc.
        // These have no world position (stored as 0,0,0) and no model to snap.
        // IGI world coordinates are in the millions range, so (0,0) is always outside the map.
        if (obj.modelId.empty() || (obj.pos.x == 0.0 && obj.pos.y == 0.0)) {
            skipped++;
            continue;
        }
        // Collision proxies (colbox / colbox2 / colbox3 / ...) are engine collision
        // volumes, not drawable geometry: the engine never resolves a mesh for them
        // (Renderer_Objects::IsSkippedModelId -> igi::IsCollisionProxyModelId,
        // level/terrain_snap_rules.h; source/renderer/mef_native.cpp:677-684 — a colbox
        // MEF's only geometry is the collision mesh) and never drops them to the ground.
        // This pass used to fall through to the terrain query for them and log
        // "Snap FAILED" whenever it missed: colbox3 is co-located with the EkksHQLift /
        // WinchHouseLift buildings and colbox2 with the Barracks, and the query misses
        // under those interiors because the terrain there is a designed DiscardTerrain
        // hole (src/terredit/discard.c:10-12), so every load produced bogus warnings.
        // Leave them entirely alone.
        if (Renderer_Objects::IsSkippedModelId(obj.modelId)) {
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
        // AI soldiers do NOT fall through to the terrain drop any more: their Z
        // comes from their AI graph below (Human_ProbeGround's AI branch, HOOK
        // 0x462390, src/humanbase/human.c:1698-1751, never runs the collision
        // query).
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

        // ── A human WITH an AI controller: the Z comes from the AI GRAPH ──────
        // Human_ProbeGround HOOK 0x462390 splits on h->ai at
        // src/humanbase/human.c:1697 and NEVER runs the collision query when it
        // is set (the drop block at :1752-1773 is the h->ai == NULL branch).
        // With a graph and a node and no queued move, the probe's Z is the
        // node's world position (`Graph_GetNodePos(d, node); hits[0].pos.z =
        // d[2];`, :1714-1717).  With no graph it leaves the pose untouched
        // (:1701-1702 -> :1806-1813).  The move-queue interpolation
        // (:1719-1745) is the AI's runtime patrol progress and cannot exist in
        // a static placement, so the load-time answer is the node's Z.
        //
        // The node is the nearest one to the AUTHORED placement:
        // HumanAI_ParamsChanged HOOK 0x4505D0 (src/humanai/humanai.c:724-765)
        // runs from Level_Load's whole-level Param_GetChangedEvent broadcast
        // (HOOK 0x4F0D40, src/level/level.c:364), sets
        // ai->graph = Graph_Find(ai->graphId) (:739-740) and
        // ai->node = AIGraph_FindNearest(ai->graph, authored pos) (:746-756).
        //
        // So an AI human either follows its graph node or keeps the authored
        // QSC Z -- it is never dropped onto the terrain or onto an object.
        const bool isHuman = (obj.type == "HumanSoldier" || obj.type == "HumanSoldierFemale");
        if (isHuman) {
            const int obj_index = static_cast<int>(&obj - objects.data());
            const SoldierAI ai = FindSoldierAI(objects, obj_index);
            if (ai.has_controller) {
                const LevelObject* graph_task = FindAiGraphTask(objects, ai.graph_task_id);
                std::string parent_type;
                if (graph_task != nullptr && graph_task->parentIndex >= 0 &&
                    graph_task->parentIndex < (int)objects.size()) {
                    parent_type = objects[graph_task->parentIndex].type;
                }

                igi::AiGraphGround ground;
                bool have_node = false;
                std::string reason;
                if (graph_task == nullptr) {
                    reason = "no AIGraph task " + std::to_string(ai.graph_task_id) +
                             " for its HumanAI (Graph_Find returns NULL; "
                             "src/humanbase/human.c:1701-1702 runs no probe)";
                } else if (!igi::GraphParentIsContainer(parent_type)) {
                    // Graph_GetNodePos 0x4F9770's attached branch (graph.c:345-362)
                    // uses the parent's matrix and position: the parent's POSE.
                    reason = "AIGraph " + graph_task->taskId + " is attached to its parent (" +
                             (parent_type.empty() ? std::string("unknown") : parent_type) +
                             "); its node world Z is the parent's runtime pose";
                } else {
                    auto cached = graph_cache.find(graph_task->taskId);
                    if (cached == graph_cache.end()) {
                        const std::string path = Utils::GetIGIRootPath() +
                            "\\missions\\location0\\level" + std::to_string(last_loaded_level_) +
                            "\\graphs\\graph" + graph_task->taskId + ".dat";
                        cached = graph_cache.emplace(graph_task->taskId, GRAPH_Parse(path)).first;
                    }
                    const GraphFile& graph = cached->second;
                    if (!graph.valid) {
                        reason = "graph " + graph_task->taskId + " data did not parse (" +
                                 graph.error + ")";
                    } else {
                        const double origin[3] = {graph_task->pos.x, graph_task->pos.y,
                                                  graph_task->pos.z};
                        // At level load obj.pos is the authored QSC position, i.e.
                        // exactly the value HumanAI_ParamsChanged hands to
                        // AIGraph_FindNearest (src/humanai/humanai.c:746-748); a
                        // later re-snap searches from the placement's current
                        // position (the same params-changed rebind the engine does
                        // when a placement is edited).
                        const double human[3] = {obj.pos.x, obj.pos.y, obj.pos.z};
                        ground = igi::AiGraphGroundZ(graph, origin, human);
                        have_node = ground.found;
                        if (!have_node) {
                            reason = "no usable node in graph " + graph_task->taskId +
                                     " (AIGraph_FindNearest skips VIEW nodes, "
                                     "src/sysmisc/aigraph.c:311)";
                        }
                    }
                }

                obj.snap_z_offset = 0.0;
                if (have_node) {
                    obj.pos.z = ground.world_z;
                    snapped++;
                    // INFO with the ARITHMETIC, so a soldier that still stands
                    // wrong is decided from the log rather than guessed at.
                    Logger::Get().Log(LogLevel::INFO,
                        "[App] Snapped AI human " + obj.modelId + " to AIGraph " +
                        graph_task->taskId + " node " + std::to_string(ground.node_id) +
                        " graphOriginZ=" + std::to_string(graph_task->pos.z) +
                        " nodeLocalZ=" + std::to_string(ground.node_local_z) +
                        " -> Z=" + std::to_string(ground.world_z) +
                        " authoredZ=" + std::to_string(obj.original_pos.z) +
                        " (Human_ProbeGround AI branch, no collision query)");
                } else {
                    obj.pos.z = obj.original_pos.z;
                    skipped++;
                    Logger::Get().Log(LogLevel::INFO,
                        "[App] AI human " + obj.modelId + " keeps authored Z " +
                        std::to_string(obj.original_pos.z) + ": " + reason);
                }
                continue;
            }
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
        const bool terrainFound = level_.GetTerrainZ(obj.pos.x, obj.pos.y, terrainZ, false);
        // Reaching here means the human has NO AI controller (h->ai == NULL), so
        // Human_ProbeGround's collision branch is the engine's rule for it;
        // `isHuman` was established above, before the AI graph branch.
        const double zDelta = obj.original_pos.z - (double)terrainZ;

        // ── The engine's ground query, both halves ────────────────────────────
        // Coll_Drop HOOK 0x4CABF0 body (src/coll/coll.c:575-614) asks the objects
        // (Coll_LinesEx over the vertical segment, :577) AND the terrain
        // (Coll_VerticalHit, :591), then keeps whichever hit is nearest to the
        // probe point (:593-594); the object hit wins a tie.  It probes DOWN with
        // the caller's dz first, and Human_ProbeGround (HOOK 0x462390,
        // src/humanbase/human.c:1754-1772) repeats with +kFarUp only when that
        // whole query found nothing.
        //
        // The probe starts at the AUTHORED QSC Z (obj.original_pos.z), not at
        // obj.pos.z: obj.pos.z may already hold a previous snap, and a previous
        // snap to the terrain below a building would hide the building floor
        // (which is above the probe) from a DOWN probe.
        igi::DropHit object_hit;
        igi::DropHit terrain_hit;
        igi::DropHit chosen_hit;
        if (isHuman) {
            const double probe_z = obj.original_pos.z;
            const igi::CollisionProbeHit object_down = collision_world.ProbeVertical(
                obj.pos.x, obj.pos.y, probe_z, /*up=*/false, igi::kHumanProbeDistance);
            if (object_down.found) {
                object_hit.found = true;
                object_hit.z = object_down.z;
            }
            if (terrainFound) {
                terrain_hit = igi::TerrainDropCandidate(probe_z, (double)terrainZ,
                                                        -igi::kHumanProbeDistance);
            }
            chosen_hit = igi::ChooseNearestDropHit(probe_z, object_hit, terrain_hit);
            if (!chosen_hit.found) {
                const igi::CollisionProbeHit object_up = collision_world.ProbeVertical(
                    obj.pos.x, obj.pos.y, probe_z, /*up=*/true, igi::kHumanProbeDistance);
                igi::DropHit up_object;
                if (object_up.found) {
                    up_object.found = true;
                    up_object.z = object_up.z;
                }
                igi::DropHit up_terrain;
                if (terrainFound) {
                    up_terrain = igi::TerrainDropCandidate(probe_z, (double)terrainZ,
                                                           igi::kHumanProbeDistance);
                }
                chosen_hit = igi::ChooseNearestDropHit(probe_z, up_object, up_terrain);
            }
        }

        // The rule itself is igi::DecideCollisionSnap (level/terrain_snap_rules.h),
        // shared with its host-lane test.  Only a human with NO AI controller
        // reaches this point (AI humans were handled above): props always keep
        // their authored QSC Z and a query miss never moves anything, but such a
        // human WITH a hit always snaps -- the engine has no distance band:
        // Human_ProbeGround reaches 1000 m each way (src/humanbase/human.c:192-193)
        // and Game_DropToGround 400 m (src/game/misc.c:82).
        if (igi::DecideCollisionSnap(chosen_hit.found, isHuman) ==
            igi::TerrainSnapDecision::SnapToTerrain) {
            // The engine puts the human's FOOT on the hit, not the mesh's lowest
            // vertex: Human_ProbeGround HOOK 0x462390 ends with
            // `to[2] = foot[2] + hits[0].pos.z` (src/humanbase/human.c:1816).
            // The editor's human mesh is baked into a FEET frame -- the bind-pose
            // bake sums the rig's root pivot into every vertex
            // (SkinSkinnedVertices, source/renderer/mef_native.cpp), which is
            // why the static and the posed draw both put the soles at obj.pos
            // (source/renderer/posed_draw_transform.h:24-30) -- so the engine's
            // rule here is exactly obj.pos.z = hit z.
            //
            // The old `-min_p.z` lift (source/renderer/model.cpp:311) measured
            // LEG geometry, not the sole.  Measured on the shipped human models,
            // the lowest vertex is upper-leg/arm geometry 0.27..0.67 m below the
            // origin, while the foot/toe vertices sit within 0.02 m of it
            // (tests/test_seated_human.cpp, TheGroundedFrameSoleIsAtTheModelOrigin).
            // Lifting by `-min_p.z` therefore floated every soldier by that
            // amount; the human's placement has no such offset.
            obj.snap_z_offset = 0.0;
            obj.pos.z = chosen_hit.z;
            // INFO, with the ARITHMETIC, not just the result: a soldier that
            // still renders off the ground is either a wrong hit or a snap that
            // never ran, and printing the terms makes the next run decide instead
            // of guess.
            Logger::Get().Log(LogLevel::INFO, "[App] Snapped human " + obj.modelId +
                " authoredZ=" + std::to_string(obj.original_pos.z) +
                " objectHitZ=" + std::to_string(object_hit.found ? object_hit.z : 0.0) +
                " objectHit=" + (object_hit.found ? "yes" : "no") +
                " terrainZ=" + std::to_string(terrainZ) +
                " terrainCandidate=" + (terrain_hit.found ? "yes" : "no") +
                " zDelta=" + std::to_string(zDelta) +
                " -> Z=" + std::to_string(obj.pos.z) +
                " (foot on the nearest collision hit, no pivot->feet lift)");
            snapped++;
        } else {
            // Keep the authored QSC Z.  This is the engine's own answer to every case
            // here: when a drop query misses, Game_DropToGround (HOOK 0x4149B0,
            // src/game/misc.c:75-97) probes down then up and then `return`s with `pos`
            // untouched — there is no clamp and no default Z — and lightmap placement
            // moves p.z only `if (hit.found)` (src/graphnode/lightmap.c:325-333,
            // Coll_DropPointsMat HOOK 0x4CB9F0).  Restoring original_pos.z and clearing
            // the offset also drops any offset a previous snap left on this object, so
            // the value written back to the QSC is the authored one.
            obj.snap_z_offset = 0.0;
            obj.pos.z = obj.original_pos.z;
            if (!terrainFound) {
                // No terrain triangle under (x, y).  Either the point is outside the
                // terrain octree or its leaf is a DiscardTerrain hole — the engine's own
                // editor box that "tells the qcube walk to drop the terrain inside it,
                // and takes collision points that fall inside it away from the terrain"
                // (src/terredit/discard.c:10-12; DiscardTerrain_Visit HOOK 0x52C0B0 sets
                // gQCubeSkipCells, :113-117; DiscardTerrain_CollQuery HOOK 0x52BF00,
                // :75-87).  Terrain::RecursiveGetZ honours the same flag
                // (source/level/terrain_query.cpp:315).  Normal for interiors,
                // helipads and lift shafts, so this is information, not a warning.
                no_terrain++;
                Logger::Get().Log(LogLevel::INFO, "[App] No terrain under " + obj.modelId +
                    (isHuman ? std::string(" and no object collision either")
                             : std::string(" (not a human; objects are not queried for it)")) +
                    " at (" + std::to_string(obj.pos.x) + ", " + std::to_string(obj.pos.y) +
                    "); keeping authored Z " + std::to_string(obj.original_pos.z));
            } else {
                // A non-human whose authored Z is authoritative.  A HUMAN can no
                // longer reach this branch: with any hit it always snaps above,
                // and a miss was handled by the branch above.
                skipped++;
            }
        }
    }
    Logger::Get().Log(LogLevel::INFO, "[App] Snap complete. snapped=" + std::to_string(snapped) + " skipped=" + std::to_string(skipped) + " no_terrain=" + std::to_string(no_terrain));
}

void App::UpdateGraphNodeManipulation(int x, int y) {
	const int id = renderer_.GraphSelected();
	if (id < 0) return;

	const int mods = glutGetModifiers();
	const bool ctrl = (mods & GLUT_ACTIVE_CTRL);

	// Cumulative pixel displacement from drag start.
	const int dx = x - graph_node_manip_.start_x_;
	const int dy = y - graph_node_manip_.start_y_;
	const float moveSensitivity = 200.0f;  // same mapping as object manipulation

	const glm::vec3 right = viewer_.right_;
	glm::dvec3 np;
	if (ctrl) {
		// Vertical edit: horizontal via screen-right, height (Z) via -dy.
		const glm::vec3 up(0.0f, 0.0f, 1.0f);
		np = graph_node_manip_.start_pos_ +
		     glm::dvec3(right * (float)dx * moveSensitivity +
		                up    * (float)-dy * moveSensitivity);
	} else {
		// Default: move in the horizontal H/V plane (camera-relative).
		const glm::vec3 fwd = glm::normalize(glm::vec3(viewer_.forward_.x, viewer_.forward_.y, 0.0f));
		np = graph_node_manip_.start_pos_ +
		     glm::dvec3(right * (float)dx * moveSensitivity +
		                fwd   * (float)-dy * moveSensitivity);
	}

	renderer_.SetGraphNodePos(id, np);

	// Live telemetry near the cursor (reuses the object-move status line).
	const glm::dvec3 d = np - graph_node_manip_.start_pos_;
	char buf[192];
	snprintf(buf, sizeof(buf),
	         "Node %d  H: %.1f  V: %.1f  Z: %.1f   dH: %.1f  dV: %.1f  dZ: %.1f",
	         id, np.x, np.y, np.z, d.x, d.y, d.z);
	status_message_ = buf;
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

	// task-11 item 5: a plain LMB drag (no modifier, no manipulation key) is a
	// deliberate no-op — it selects/keeps the object and moves nothing, and no undo
	// state is pushed because there is nothing to undo. The message names the mode
	// that did nothing ("Manipulate (None)") and then lists the modifiers that do
	// work, so the user is never left with an unexplained dead drag. This branch used
	// to be silent: the object did not move and nothing on screen said why.
	// The gesture stays a no-op on purpose rather than inventing a drag plane: the
	// two planes this editor defines are already taken by Shift (view plane) and
	// Ctrl (vertical plane), so a default plane would need a design decision.
	{
		const bool no_manip_key = !(input_.keys_ & (MK_MANIP_A | MK_MANIP_B | MK_MANIP_G |
		                                            MK_MANIP_S | MK_MANIP_O | MK_MANIP_SPACE));
		if (current_mode == ManipulationMode::None && no_manip_key) {
			status_message_ = "Manipulate (None): drag moves nothing — Shift = view "
			                  "plane, Ctrl = vertical, A/B/G = rotate, Space = reset "
			                  "orientation, S/O = snap to ground / nearest object";
		}
	}

	// Push undo state once at the start of each new manipulation gesture
	if (marker_manip_.mode_ != ManipulationMode::None || terrain_edit_enabled_) {
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
