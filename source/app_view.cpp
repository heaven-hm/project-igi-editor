/******************************************************************************
 * @file    app_view.cpp
 * @brief   App: camera/view update, collision, terrain snap, marker manipulation
 *          Split from app.cpp; shares app_internal.h.
 *****************************************************************************/
#include "app_internal.h"

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

