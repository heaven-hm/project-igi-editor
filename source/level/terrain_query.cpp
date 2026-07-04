/******************************************************************************
 * @file    terrain_query.cpp
 * @brief   Terrain implementation: height queries + editor raycast/modify
 *          Split from terrain.cpp; shares terrain_internal.h.
 *****************************************************************************/
#include "terrain_internal.h"

bool Terrain::GetFirstHMPCenter(glm::vec3& out_pos) const {
	if (num_height_map_ > 0) {
		const height_map_s* hmp = height_maps_ + qtask_height_maps_[0].idx_in_array_;
		int hmp_dim = 1 << hmp->height_map_line_width_shift_;
		double width = hmp_dim / hmp->local_pos_to_hmp_pos_;
		out_pos.x = (float)(hmp->cube_min_x_ + width * 0.5);
		out_pos.y = (float)(hmp->cube_min_y_ + width * 0.5);
		out_pos.z = 175000000.0f; // High up
		return true;
	}
	return false;
}

bool Terrain::GetZ(const dyn_cube_s* root_dyn_cube, double x, double y, float & ret_z, bool ignore_discard) {
	// Safety check: ensure terrain data is loaded
	if (!root_dyn_cube) {
		Logger::Get().Log(LogLevel::WARNING, "[Terrain] GetZ called with null root_dyn_cube");
		return false;
	}
	if (!ctr_) {
		Logger::Get().Log(LogLevel::WARNING, "[Terrain] GetZ called with null ctr_ (terrain not loaded)");
		return false;
	}
	
	last_getz_leaf_node_ = nullptr;

	get_z_pos_.x = x;
	get_z_pos_.y = y;
	get_z_pos_.z = 0.0;

	glm::ivec3 cube_pos(0);
	return RecursiveGetZ(root_dyn_cube, ctr_ + 1, cube_pos, 0, ROOT_CUBE_HALF_SIZE, 0, ret_z, ignore_discard);
}

double Terrain::CalcHMPDeltaZ(const height_map_s* height_map, double vert_x, double vert_y) {
	const uint8_t * height_map_item = (const uint8_t*)height_map->height_map_item_;

	double xf = (vert_x - height_map->cube_min_x_) * height_map->local_pos_to_hmp_pos_;
	double yf = (vert_y - height_map->cube_min_y_) * height_map->local_pos_to_hmp_pos_;

	int int_xf = (int)xf;
	int int_yf = (int)yf;

	double decimal_xf = xf - (double)int_xf;
	double decimal_yf = yf - (double)int_yf;

	int shifts = height_map->height_map_line_width_shift_;

	// height map size is  2^x + 1

	double h0, h1, h2, h3;

	int hmp_dim = 1 << shifts;
	int hmp_row = hmp_dim + 1;

	int iy0 = int_yf * hmp_row;
	int iy1 = (int_yf + 1) * hmp_row;
	if (int_yf >= hmp_dim) {
		iy1 = iy0;	// clamp
	}

	int ix0 = int_xf;
	int ix1 = int_xf + 1;
	if (int_xf >= hmp_dim) {
		ix1 = ix0;	// clamp
	}

	// get samples
	h0 = (double)*(height_map_item + iy0 + ix0);
	h1 = (double)*(height_map_item + iy0 + ix1);
	h2 = (double)*(height_map_item + iy1 + ix0);
	h3 = (double)*(height_map_item + iy1 + ix1);

	// Bilinear interpolcation

	// first interplate along x direction
	double x_dir_interpolated1 = (h1 - h0) * decimal_xf + h0;
	double x_dir_interpolated2 = (h3 - h2) * decimal_xf + h2;

	// then interplate along y direction
	double y_dir_interpolated = (x_dir_interpolated2 - x_dir_interpolated1) * decimal_yf + x_dir_interpolated1;

	// y_dir_interpolated range: [-128.0, 127]
	// (y_dir_interpolated - 64.0) range: [-192, 63]
	// delta_z range: [-49152, 16128]

	double delta_z = (y_dir_interpolated - 64.0) * 256.0;

	return delta_z;
}

bool Terrain::RecursiveGetZ(const dyn_cube_s* dyn_cube, const ctr_node_s* cube_node,
	glm::ivec3 & cube_pos, int cube_level, int cube_dim, uint8_t trans_flag, float & ret_z, bool ignore_discard)
{
	if (dyn_cube) {
		DynCube_RunQTaskPushFunc(dyn_cube);
	}

	bool found = false;

	if (cube_level == LEAF_CUBE_LOD_LEVEL) {

		const height_map_s * hmpc = nullptr;
		if (height_map_stack_size_ && (cur_mod_options_ & TERRAIN_HEIGHT_MOD)) {
			uint32_t hmp_idx = height_map_stack_[height_map_stack_size_ - 1];
			hmpc = height_maps_ + hmp_idx - 1;
		}

		glm::dvec3 v_buf[64];
		uint32_t v_cnt = 0;

		const cmd_item_s* cmd = cube_node->cmd_;

		double scale = (double)(1 << (27 - cube_level));

		const uint32_t* triangles = (const uint32_t*)(cmd + 1);
		const uint32_t* vertices = triangles + cmd->num_triangle_;

		uint16_t grp1_vert_cnt = cmd->num_parent_vertex_;
		uint16_t grp2_vert_cnt = cmd->num_child_vertex_;

		uint16_t vert_cnt = grp1_vert_cnt + grp2_vert_cnt;

		for (uint16_t j = 0; j < vert_cnt; ++j) {
			uint32_t v = vertices[j];

			int shifted_x = (v >> 26) & 0x3F;
			int shifted_y = (v >> 20) & 0x3F;
			int shifted_z = (v >> 14) & 0x3F;

			int x = shifted_x - 24;
			int y = shifted_y - 24;
			int z = shifted_z - 24;

			if (trans_flag & 4) {	// case: 4, 5, 6, 7: mirror and rotation
				// bit2 is 1

				// center (0, 0)
				// x flipped

				x = 24 - shifted_x;
			}
			// other case: rotation

			if (trans_flag & 1) {
				// bit0 is 1

				if (trans_flag & 2) {
					// bit1 is 1
					x = -x;
				}
				else {
					// bit1 is 0
					y = 24 - shifted_y;
				}

				// swap x, y
				int temp = x;
				x = y;
				y = temp;
			}
			else {
				// bit0 is 0

				if (trans_flag & 2) {
					// bit1 is 1
					x = -x;
					y = 24 - shifted_y;
				}
				// else: bit1 is 0: do nothing
			}

			glm::dvec3 * vert = v_buf + v_cnt++;

			double vert_x = (double)x * scale * ONE_OVER_3 + cube_pos[0];
			double vert_y = (double)y * scale * ONE_OVER_3 + cube_pos[1];
			double vert_z = (double)z * scale * ONE_OVER_3 + cube_pos[2];

			if (hmpc) {
				double delta_z = CalcHMPDeltaZ(hmpc, vert_x, vert_y);
				vert_z += delta_z;
			}

			vert->x = vert_x;
			vert->y = vert_y;
			vert->z = vert_z;
		}

		// check vertices
		if (get_z_is_int_pos_) {
			for (uint32_t j = 0; j < v_cnt; ++j) {
				const glm::dvec3 & v0 = v_buf[j];

				if ((int)v0.x == get_z_ix_ && (int)v0.y == get_z_iy_) {
					ret_z = (float)v0.z;

					last_getz_leaf_node_ = cube_node;
					found = true;
					goto DONE;
				}
			}
		}

		for (uint16_t j = 0; j < cmd->num_triangle_; ++j) {
			uint32_t tri = triangles[j];

			uint8_t indices[3];

			if (trans_flag & 4) {
				// mirrored
				indices[0] = (uint8_t)((tri & 0xff00) >> 8);
				indices[1] = (uint8_t)(tri & 0xff);
				indices[2] = (uint8_t)((tri & 0xff0000) >> 16);
			}
			else {
				indices[0] = (uint8_t)((tri & 0xff0000) >> 16);
				indices[1] = (uint8_t)(tri & 0xff);
				indices[2] = (uint8_t)((tri & 0xff00) >> 8);
			}

			bool inside_cur_tri = true;

			for (int k = 0; k < 3; ++k) {
				int idx0 = indices[k];
				int idx1 = indices[(k + 1) % 3];

				glm::dvec3 v_1_0 = v_buf[idx1] - v_buf[idx0];
				v_1_0.z = 0.0;

				glm::dvec3 v_pos_0;

				v_pos_0.x = get_z_pos_.x - v_buf[idx0].x;
				v_pos_0.y = get_z_pos_.y - v_buf[idx0].y;
				v_pos_0.z = 0.0;

				glm::dvec3 v_c = glm::cross(v_1_0, v_pos_0);

				if (v_c.z <= 0.0) {
					inside_cur_tri = false;
					break;
				}
			}

			if (inside_cur_tri) {
				glm::dvec3 v10 = v_buf[indices[1]] - v_buf[indices[0]];
				glm::dvec3 v20 = v_buf[indices[2]] - v_buf[indices[0]];

				glm::dvec3 n = glm::normalize( glm::cross(v10, v20) );

				double d = -glm::dot(v_buf[indices[2]], n);

				ret_z = (float)(-(n.x * get_z_pos_.x + n.y * get_z_pos_.y + d) / n.z);

				last_getz_leaf_node_ = cube_node;
				found = true;
				goto DONE;
			}

		}

		goto DONE;
	}
	else {
		uint8_t children_mask = cube_node->children_mask_;

		if (children_mask) {

			// check down to next level
			const uint8_t* children_access_order = CUBE_IDX_TABLE + trans_flag * 8;
			const uint8_t* trans_lines = CUBE_TRANS_TABLE + trans_flag * 8;

			int sub_cube_level = cube_level + 1;
			int sub_cube_dim = cube_dim >> 1;

			constexpr int CHILD_ACCESS_ORDER[] = { 4, 0, 5, 1, 6, 2, 7, 3 };

			for (int i = 0; i < 8; ++i) {

				int access_idx = CHILD_ACCESS_ORDER[i];

				uint8_t child_idx = children_access_order[access_idx];

				if (children_mask & (1 << child_idx)) {

					glm::ivec3 sub_cube_pos;
					float min_xyz[3];
					float max_xyz[3];

					const int* dims = dim_table_ + access_idx * 3;

					for (int j = 0; j < 3; ++j) {
						sub_cube_pos[j] = cube_pos[j] + (dims[j] >> sub_cube_level);

						min_xyz[j] = (float)(sub_cube_pos[j] - sub_cube_dim);
						max_xyz[j] = (float)(sub_cube_pos[j] + sub_cube_dim);
					}

					// check x & y range
					if (get_z_pos_.x >= min_xyz[0] && get_z_pos_.x <= max_xyz[0] &&
						get_z_pos_.y >= min_xyz[1] && get_z_pos_.y <= max_xyz[1])
					{

						const dyn_cube_s* sub_dyn_cube = nullptr;
						if (dyn_cube && dyn_cube->children_mask_ & (1 << child_idx)) {
							sub_dyn_cube = dyn_cube->children_[child_idx];
						}

						if (!sub_dyn_cube || ignore_discard || !(cur_mod_options_ & TERRAIN_DISCARD_MOD) || !(sub_dyn_cube->flags_ & CUBE_FLAG_DISCARD_TERRAIN)) {

							found = RecursiveGetZ(
								sub_dyn_cube,
								ctr_ + cube_node->children_[child_idx],
								sub_cube_pos,
								sub_cube_level,
								sub_cube_dim,
								trans_lines[cube_node->cmd_transform_[child_idx]],
								ret_z,
								ignore_discard
							);

							if (found) {
								goto DONE;
							}


						}

					}
				}

			}

			ret_z = get_z_pos_.z;

			goto DONE;	// not found, return old value
		}
		else {
			ret_z = get_z_pos_.z;
			goto DONE;	// not found, return old value
		}
	}

DONE:

	if (dyn_cube) {
		DynCube_RunQTaskPopFunc(dyn_cube);
	}

	return found;
}

void Terrain::EditorRaycastAndModify(const dyn_cube_s* root_dyn_cube, const glm::vec3& ray_origin, const glm::vec3& ray_dir, int brush_type, double radius, double strength) {
	// Basic Raymarch
	glm::vec3 current_pos = ray_origin;
	float step_size = 500.0f; // 500 units per step
	bool hit = false;
	
	for (int i = 0; i < 10000; ++i) { // max 5,000,000 units distance
		current_pos += ray_dir * step_size;
		
		float terrain_z = 0.0f;
		if (GetZ(root_dyn_cube, current_pos.x, current_pos.y, terrain_z)) {
			if (current_pos.z <= terrain_z + 100.0f) { // Added a tiny tolerance to prevent slipping through cracks
				hit = true;
				break;
			}
		}
	}
	
	if (!hit) {
		printf("Raymarch miss\n");
		return;
	}

	printf("Raymarch HIT at (%.2f, %.2f, %.2f)\n", current_pos.x, current_pos.y, current_pos.z);

	// We have an approximate hit point on the terrain (current_pos.x, current_pos.y)
	// Now we need to modify the height map at this location.
	// Iterate over all active height maps to find which one covers this X/Y area.
	
	double max_strength = strength; // Max height change per frame at center (from caller)

	int changed_count = 0;

	// For the FLATTEN brush, capture the target height at the hit center (once per call).
	// Use the first hmp whose bbox contains the hit point. If none, flatten becomes a no-op.
	int target_val = -1; // -1 == not captured
	if (brush_type == 3) {
		for (int i = 0; i < num_height_map_; ++i) {
			height_map_s* hmp = height_maps_ + qtask_height_maps_[i].idx_in_array_;
			int hmp_dim = 1 << hmp->height_map_line_width_shift_;
			double min_x = hmp->cube_min_x_;
			double min_y = hmp->cube_min_y_;
			double max_x = min_x + (hmp_dim / hmp->local_pos_to_hmp_pos_);
			double max_y = min_y + (hmp_dim / hmp->local_pos_to_hmp_pos_);
			if (current_pos.x < min_x || current_pos.x > max_x ||
				current_pos.y < min_y || current_pos.y > max_y) {
				continue;
			}
			int tx = (int)(((double)current_pos.x - min_x) * hmp->local_pos_to_hmp_pos_ + 0.5);
			int ty = (int)(((double)current_pos.y - min_y) * hmp->local_pos_to_hmp_pos_ + 0.5);
			if (tx < 0) tx = 0; if (tx > hmp_dim) tx = hmp_dim;
			if (ty < 0) ty = 0; if (ty > hmp_dim) ty = hmp_dim;
			target_val = (uint8_t)hmp->height_map_item_[ty * (hmp_dim + 1) + tx];
			break;
		}
		if (target_val < 0) {
			printf("Flatten: no hmp contains hit center, skipping stroke\n");
			return;
		}
	}

	for (int i = 0; i < num_height_map_; ++i) {
		height_map_s* hmp = height_maps_ + qtask_height_maps_[i].idx_in_array_;
		
		int hmp_dim = 1 << hmp->height_map_line_width_shift_;
		
		// Find world coordinate bounds for this height map
		double min_x = hmp->cube_min_x_;
		double min_y = hmp->cube_min_y_;
		double max_x = min_x + (hmp_dim / hmp->local_pos_to_hmp_pos_);
		double max_y = min_y + (hmp_dim / hmp->local_pos_to_hmp_pos_);
		
		// Does the brush intersect this height map bounding box?
		if (current_pos.x + radius < min_x || current_pos.x - radius > max_x ||
			current_pos.y + radius < min_y || current_pos.y - radius > max_y) {
			continue;
		}
		
		// Modify pixels in the height map
		for (int y = 0; y <= hmp_dim; ++y) {
			for (int x = 0; x <= hmp_dim; ++x) {
				double world_x = min_x + x / hmp->local_pos_to_hmp_pos_;
				double world_y = min_y + y / hmp->local_pos_to_hmp_pos_;
				
				double dx = (double)current_pos.x - world_x;
				double dy = (double)current_pos.y - world_y;
				double dist_sq = dx*dx + dy*dy;
				
				double rad_sq = radius * radius;
				if (dist_sq <= rad_sq) {
					int idx = y * (hmp_dim + 1) + x;
					// Read as uint8_t because CalcHMPDeltaZ casts it to uint8_t!
					int current_val = (uint8_t)hmp->height_map_item_[idx];
					
					// Smooth falloff based on distance
					double falloff = 1.0 - (dist_sq / rad_sq);

					if (brush_type == 0 || brush_type == 1) { // RAISE / LOWER
						int applied_strength = (int)(max_strength * falloff);
						if (applied_strength < 1) applied_strength = 1;
						if (brush_type == 0) {
							current_val += applied_strength;
						} else {
							current_val -= applied_strength;
						}
					} else if (brush_type == 2) { // SOFTEN: move toward neighbor average
						// neighbor indices in the same array, clamped to [0, hmp_dim]
						int xl = (x > 0) ? x - 1 : x;
						int xr = (x < hmp_dim) ? x + 1 : x;
						int yu = (y < hmp_dim) ? y + 1 : y;
						int yd = (y > 0) ? y - 1 : y;
						int n_left  = (uint8_t)hmp->height_map_item_[y  * (hmp_dim + 1) + xl];
						int n_right = (uint8_t)hmp->height_map_item_[y  * (hmp_dim + 1) + xr];
						int n_up    = (uint8_t)hmp->height_map_item_[yu * (hmp_dim + 1) + x];
						int n_down  = (uint8_t)hmp->height_map_item_[yd * (hmp_dim + 1) + x];
						int avg = (n_up + n_down + n_left + n_right) / 4;
						double k = falloff * (max_strength / 15.0);
						current_val += (int)((avg - current_val) * k);
					} else { // FLATTEN: move toward captured target height
						double k = falloff * (max_strength / 15.0);
						current_val += (int)((target_val - current_val) * k);
					}

					// Clamp to uint8 bounds (0 to 255)
					if (current_val > 255) current_val = 255;
					if (current_val < 0) current_val = 0;
					
					hmp->height_map_item_[idx] = (int8_t)current_val;
					changed_count++;
				}
			}
		}
	}
	
	printf("Modified %d pixels across height maps\n", changed_count);

	// Force the terrain engine to rebuild the chunks since data changed
	ClearCubeDataHash();
}

