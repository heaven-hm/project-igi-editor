/******************************************************************************
 * @file    terrain_lod.cpp
 * @brief   Terrain implementation: per-frame update + LOD render-cube selection
 *          Split from terrain.cpp; shares terrain_internal.h.
 *****************************************************************************/
#include "terrain_internal.h"

void Terrain::Update(update_params_s& params, const dyn_cube_s* root_dyn_cube) {
	tan_half_fovx_ = params.view_define_->tan_half_fovx_;
	tan_half_fovy_ = params.view_define_->tan_half_fovy_;

	pre_frame_ = cur_frame_;
	cur_frame_ = params.frame_;

	pre_mod_options_ = cur_mod_options_;
	cur_mod_options_ = params.terrain_mod_options_;
	if (cur_mod_options_ != pre_mod_options_) {
		// modification options changed, need reset cube data hash table
		ClearCubeDataHash();
	}

	vertices_ = params.terrain_vb_;
	indices_ = params.terrain_ib_;
	render_chunks_ = params.terrain_render_chunks_;
	num_vertex_ = 0;
	num_index_ = 0;
	num_render_chunk_ = 0;

	cur_int_view_pos_[0] = (int)params.view_define_->pos_.x;
	cur_int_view_pos_[1] = (int)params.view_define_->pos_.y;
	cur_int_view_pos_[2] = (int)params.view_define_->pos_.z;

	double cur_dbl_view_pos[3];

	cur_dbl_view_pos[0] = cur_int_view_pos_[0];
	cur_dbl_view_pos[1] = cur_int_view_pos_[1];
	cur_dbl_view_pos[2] = cur_int_view_pos_[2];

	double trans_out[3];
	TransformDoubleVec3(params.view_define_->mat_rot_, cur_dbl_view_pos, trans_out);

	cur_int_view_pos_rotated_[0] = (int)trans_out[0];
	cur_int_view_pos_rotated_[1] = (int)trans_out[1];
	cur_int_view_pos_rotated_[2] = (int)trans_out[2];

	// init dim_table_rotated_
	const int* t = dim_table_;
	int* t_rot = dim_table_rotated_;

	for (int z = -1; z <= 1; z += 2) {
		for (int y = -1; y <= 1; y += 2) {
			for (int x = -1; x <= 1; x += 2) {

				double temp[3] = { (double)t[0], (double)t[1], (double)t[2] };
				double temp_out[3];

				TransformDoubleVec3(params.view_define_->mat_rot_, temp, temp_out);

				t_rot[0] = (int)temp_out[0];
				t_rot[1] = (int)temp_out[1];
				t_rot[2] = (int)temp_out[2];

				t += 3;
				t_rot += 3;
			}
		}
	}

	num_render_cube_ = 0;
	num_cube_data_ = 0;

	const double UPDATE_ROOT_CUBE_HALF_SIZE = 1073741800.0;	// slightly less than 2^30
	const double UPDATE_ROOT_CUBE_RADIUS = UPDATE_ROOT_CUBE_HALF_SIZE * SQRT_3;

	float root_cube_ctr_horizontal_dist_to_clip_plane = (float)(UPDATE_ROOT_CUBE_RADIUS / std::cos(params.view_define_->fovx_ * 0.5));
	float root_cube_ctr_vertical_dist_to_clip_plane   = (float)(UPDATE_ROOT_CUBE_RADIUS / std::cos(params.view_define_->fovy_ * 0.5));

	GenerateRenderCube(root_dyn_cube, ctr_ + 1, root_cube_ctr_horizontal_dist_to_clip_plane, root_cube_ctr_vertical_dist_to_clip_plane);

	// clear old hash data
	for (int i = 0; i < CUBE_DATA_HASH_ITEM_COUNT; ++i) {
		cube_data_hash_s* cube_data_hash = cube_data_hash_ + i;
		if (cube_data_hash->frame_ != cur_frame_) {
			cube_data_hash->frame_ = INVALID_FRAME;	// mark not used
			if (cube_data_hash->cube_data_) {
				FreeCubeData(
					cube_data_hash->cube_data_,
					cube_data_hash->buf_type_);

				cube_data_hash->cube_data_ = nullptr;
			}
		}
	}

	GenerateCubeData();

	for (int i = 0; i < num_cube_data_; ++i) {
		GenerateCubeMesh(cube_data_array_[i]);
	}

	params.num_terrain_vert_ = num_vertex_;
	params.num_terrain_idx_ = num_index_;
	params.num_terrain_render_chunk_ = num_render_chunk_;
}

void Terrain::GenerateRenderCube(const dyn_cube_s* root_dyn_cube,
	const ctr_node_s* root_ctr_cube, float root_cube_ctr_horizontal_dist_to_clip_plane, float root_cube_ctr_vertical_dist_to_clip_plane)
{
	struct cube_info_s {
		bool				clipped_checked_;
		bool				clipped_;
		bool				added_to_render_cube_;
		bool				lod_checked_;
		const dyn_cube_s*	dyn_cube_;
		const ctr_node_s*	ctr_node_;
		uint32_t			lod_level_;
		glm::ivec3			cube_pos_;
		glm::ivec3			cube_pos_rotated_;
		float				cube_ctr_horizontal_dist_to_clip_plane_;	// decrease by half when down to child cube
		float				cube_ctr_vertical_dist_to_clip_plane_;		// decrease by half when down to child cube
		uint32_t			child_no_;
		uint32_t			trans_flag_;
	};

	cube_info_s stack[17];	// LOD level: 0~16
	int stack_size = 0;

	auto PushToStack = [this, &stack, &stack_size](
		const dyn_cube_s* dyn_cube,
		const ctr_node_s* ctr_node,
		uint32_t lod_level,
		const glm::ivec3& cube_pos,
		const glm::ivec3& cube_pos_rotated,
		float cube_ctr_horizontal_dist_to_clip_plane,
		float cube_ctr_vertical_dist_to_clip_plane,
		uint32_t child_no,
		uint32_t trans_flag
		)
		{
			if (stack_size > 16) {
				Log(log_type_t::LOG_FATAL, __FILE__, __LINE__, "stack overflow\n");
				return;
			}

			cube_info_s* ci = stack + stack_size;

			ci->clipped_checked_ = false;
			ci->clipped_ = false;
			ci->added_to_render_cube_ = false;
			ci->lod_checked_ = false;
			ci->dyn_cube_ = dyn_cube;
			ci->ctr_node_ = ctr_node;
			ci->lod_level_ = lod_level;
			ci->cube_pos_ = cube_pos;
			ci->cube_pos_rotated_ = cube_pos_rotated;
			ci->cube_ctr_horizontal_dist_to_clip_plane_ = cube_ctr_horizontal_dist_to_clip_plane;
			ci->cube_ctr_vertical_dist_to_clip_plane_ = cube_ctr_vertical_dist_to_clip_plane;
			ci->child_no_ = child_no;
			ci->trans_flag_ = trans_flag;

			stack_size++;

			DynCube_RunQTaskPushFunc(dyn_cube);
		};

	auto PopFromStack = [this, &stack, &stack_size]()
		{
			if (stack_size <= 0) {
				Log(log_type_t::LOG_FATAL, __FILE__, __LINE__, "stack underflow\n");
				return;
			}

			DynCube_RunQTaskPopFunc(stack[stack_size - 1].dyn_cube_);

			stack_size--;
		};

	// push root node to stack
	glm::ivec3 cube_pos(0);
	glm::ivec3 cube_pos_rotated(0);

	PushToStack(root_dyn_cube, root_ctr_cube, 0, cube_pos, cube_pos_rotated,
		root_cube_ctr_horizontal_dist_to_clip_plane, 
		root_cube_ctr_vertical_dist_to_clip_plane, 0, 0);

	// start process
	while (stack_size) {	// if stack is not empty
		cube_info_s& ci = stack[stack_size - 1];	// fetch item from stack top

		if (ci.added_to_render_cube_ || ci.clipped_ || ci.child_no_ > 7 /* loop children finished */) {
			PopFromStack();
		}
		else {
			int cube_half_size = ROOT_CUBE_HALF_SIZE >> ci.lod_level_;

			// check clipping
			bool clipped = false;

			if (ci.clipped_checked_) {
				clipped = ci.clipped_;
			}
			else {
				glm::ivec3 cube_pos_rotated_sub_view_pos_rotated =
					ci.cube_pos_rotated_ - cur_int_view_pos_rotated_;

				if ((64.0f - ci.cube_ctr_vertical_dist_to_clip_plane_) >= cube_pos_rotated_sub_view_pos_rotated.z) {
					clipped = true;
				}

				/*
				
				   horizontal cull

				     the demo cube just touch the left frustum plane

				        \               /
	  left frustum plane \             / right frustum plane
				          \           /
				     ______\ forward (+Z)
				    |      |\   |   /
					| cube_|_\D1|  /
					|___|__| |\ | /
					    |    | \|/
					    |	 | view
				        |-D2-|


					D1: horizontal_half_width_at_z
					D2: cube_radius / cos(fovx * 0.5)
				  
				 
				 */

				if (!clipped) {
					double x_test = cube_pos_rotated_sub_view_pos_rotated.z * tan_half_fovx_ + ci.cube_ctr_horizontal_dist_to_clip_plane_;
					if (cube_pos_rotated_sub_view_pos_rotated.x <= -x_test || cube_pos_rotated_sub_view_pos_rotated.x >= x_test) {
						// clipped by left or right frustum plane
						clipped = true;
					}
				}

				if (!clipped) {
					double y_test = cube_pos_rotated_sub_view_pos_rotated.z * tan_half_fovy_ + ci.cube_ctr_vertical_dist_to_clip_plane_;
					if (cube_pos_rotated_sub_view_pos_rotated.y <= -y_test || cube_pos_rotated_sub_view_pos_rotated.y >= y_test) {
						// clipped by top or bottom frustum plane
						clipped = true;
					}
				}

				if (!clipped) {
					if ((ci.cube_pos_.z + cube_half_size) < -LEVEL1_CUBE_HALF_SIZE) {
						clipped = true;
					}
				}

				ci.clipped_checked_ = true;
				ci.clipped_ = clipped;
			}

			if (!clipped) {

				bool draw_cur_cube = false;

				if (!ci.lod_checked_) {

					// check detail level
					glm::ivec3 cube_pos_sub_view_pos = ci.cube_pos_ - cur_int_view_pos_;

					int max_axis_aligned_dist_to_view = cube_pos_sub_view_pos.x > 0 ? cube_pos_sub_view_pos.x : -cube_pos_sub_view_pos.x;

					int temp = cube_pos_sub_view_pos.y > 0 ? cube_pos_sub_view_pos.y : -cube_pos_sub_view_pos.y;
					if (temp > max_axis_aligned_dist_to_view) {
						max_axis_aligned_dist_to_view = temp;
					}

					temp = cube_pos_sub_view_pos.z > 0 ? cube_pos_sub_view_pos.z : -cube_pos_sub_view_pos.z;
					if (temp > max_axis_aligned_dist_to_view) {
						max_axis_aligned_dist_to_view = temp;
					}

					// max_axis_aligned_dist_to_view is positive
					// but delta might be negtiave if view_pos inside current cube
					//   if delta > 0 then delta is the distance from view to cube's face
					//   along that axis

					int delta = max_axis_aligned_dist_to_view - cube_half_size;

					// div 1/32 cube_half_size

					/*
					    ______
					   |      |
					   | cube |
					   |______|
					       |\
						   | \ 
						   |  face
						   |
						   | distance_to_face
						   |
                        viewpos


					  if distance_to_face >= 5.09375 times cube_half_size then
					  the cube add to render queue

					    // 5.09375 = 1 / 32

					  render cube as coarser as possible

					  i.e. if a cube is far enough then that cube can be renderer

					 */

					// v: delta div (cube_haf_size / 32)
					int v = delta >> (25 - ci.lod_level_);

					// if view_pos inside current test_cube,
					//    delta must be negative 
					//    and then v must be negative
					//    the v >= CUBE_ACTIVE_COMPARE_INT test will fail so we will
					//    continue to check its children.
					// delta 

					// v: be directly  proportional to delta
					//    be inversely proportional to (25 - ci.lod_level_)
					//    be directly  proportional to ci.lod_level_

					//    if delta to test_cube more small or cube_half_size more larger
					//    nore less likely to draw test_cube

					draw_cur_cube = v >= CUBE_ACTIVE_COMPARE_INT || ci.lod_level_ >= LEAF_CUBE_LOD_LEVEL;

					ci.lod_checked_ = true;
				}

				if (draw_cur_cube) {

					// reach max level or this cube is active
					TryAddRenderCube(ci.ctr_node_, ci.cube_pos_, ci.lod_level_, ci.trans_flag_, ci.dyn_cube_);
					ci.added_to_render_cube_ = true;

				}
				else {
					const uint8_t* children_access_order = CUBE_IDX_TABLE + ci.trans_flag_ * 8;
					const uint8_t* trans_lines = CUBE_TRANS_TABLE + ci.trans_flag_ * 8;

					uint8_t children_mask = ci.ctr_node_->children_mask_;
					uint8_t child_idx = children_access_order[ci.child_no_];

					if (children_mask & (1 << child_idx)) {

						int sub_cube_lod_level = ci.lod_level_ + 1;

						glm::ivec3 sub_cube_pos;
						glm::ivec3 sub_cube_pos_rotated;

						const int* dims = dim_table_ + ci.child_no_ * 3;
						const int* rot_dims = dim_table_rotated_ + ci.child_no_ * 3;

						for (int j = 0; j < 3; ++j) {
							sub_cube_pos[j] = ci.cube_pos_[j] + (dims[j] >> sub_cube_lod_level);
							sub_cube_pos_rotated[j] = ci.cube_pos_rotated_[j] + (rot_dims[j] >> sub_cube_lod_level);
						}

						const ctr_node_s* sub_ctr_node = ctr_ + ci.ctr_node_->children_[child_idx];

						const dyn_cube_s* sub_dyn_cube = nullptr;
						if (ci.dyn_cube_ && ci.dyn_cube_->children_mask_ & (1 << child_idx)) {
							sub_dyn_cube = ci.dyn_cube_->children_[child_idx];
						}

						if (!sub_dyn_cube || !(cur_mod_options_ & TERRAIN_DISCARD_MOD) || !(sub_dyn_cube->flags_ & CUBE_FLAG_DISCARD_TERRAIN)) {
							PushToStack(sub_dyn_cube, sub_ctr_node,
								sub_cube_lod_level,
								sub_cube_pos, sub_cube_pos_rotated,
								ci.cube_ctr_horizontal_dist_to_clip_plane_ * 0.5f, 
								ci.cube_ctr_vertical_dist_to_clip_plane_ * 0.5f,
								0,
								trans_lines[ci.ctr_node_->cmd_transform_[child_idx]]);
						}
						// else: discarded
					}
				}
			}

			ci.child_no_ += 1;	// process next child
		}
	}
}

void Terrain::DynCube_RunQTaskPushFunc(const dyn_cube_s* dyn_cube) {
	if (dyn_cube) {

		const qtask_s* qtask = dyn_cube->qtask_link_chain_;
		while (qtask) {

			if (qtask->task_type_ == TASK_TYPE_TERRAIN_TEXTURE_MODIFIER) {
				const qtask_texture_modifier_s* qtask_texture_modifier = (const qtask_texture_modifier_s*)qtask;

				// +1: make it > 0
				//     0 means that texture modifier does not exists
				uint8_t idx_add_one = (uint8_t)qtask_texture_modifier->idx_in_array_ + 1;

				material_indices_stack_[material_indices_stack_size_++] = idx_add_one;
				packed_material_indices_ <<= 8;
				packed_material_indices_ |= idx_add_one;

			}
			else if (qtask->task_type_ == TASK_TYPE_TERRAIN_LIGHT_MAP) {
				const qtask_terrain_light_map_s * qtask_terrain_light_map = (const qtask_terrain_light_map_s*)qtask;

				uint8_t push_idx = qtask_terrain_light_map->idx_in_array_;
				light_map_stack_[light_map_stack_size_++] = push_idx;
			}
			else if (qtask->task_type_ == TASK_TYPE_TERRAIN_HEIGHT_MAP) {
				const qtask_height_map_s * qtask_height_map = (const qtask_height_map_s*)qtask;

				uint32_t push_idx = qtask_height_map->idx_in_array_;
				height_map_stack_[height_map_stack_size_++] = push_idx + 1;
			}

			qtask = qtask->next_;
		}
	}
}

void Terrain::DynCube_RunQTaskPopFunc(const dyn_cube_s* dyn_cube) {
	if (dyn_cube) {
		const qtask_s* qtask = dyn_cube->qtask_link_chain_;
		while (qtask) {

			if (qtask->task_type_ == TASK_TYPE_TERRAIN_TEXTURE_MODIFIER) {
				if (material_indices_stack_size_) {
					material_indices_stack_size_--;

					packed_material_indices_ >>= 8;

					if (material_indices_stack_size_ >= 4) {
						uint8_t material_idx = material_indices_stack_[material_indices_stack_size_ - 4];
						packed_material_indices_ |= (material_idx << 24);
					}
				}
			}
			else if (qtask->task_type_ == TASK_TYPE_TERRAIN_LIGHT_MAP) {
				light_map_stack_size_--;
			}
			else if (qtask->task_type_ == TASK_TYPE_TERRAIN_HEIGHT_MAP) {
				height_map_stack_size_--;
			}

			qtask = qtask->next_;
		}
	}
}

void Terrain::TryAddRenderCube(const ctr_node_s* ctr_node, const glm::ivec3& cube_pos, int cube_level,
	uint32_t trans_flag, const dyn_cube_s* dyn_cube)
{
	if (cube_level >= RENDER_CUBE_MIN_LOD_LEVEL && 
		(!dyn_cube || !(cur_mod_options_ & TERRAIN_DISCARD_MOD) || !(dyn_cube->flags_ & CUBE_FLAG_DISCARD_TERRAIN)))
	{
		// generate a position based hash value
		int cube_data_search_value = (
			((cube_pos[0] ^ cube_pos[1] ^ cube_pos[2]) & 0x1FFC0)
			^
			(
				(
					(cube_pos[1] & 0x3FF800)
					^
					(
						(cube_pos[0] & 0x1FFC000)
						^
						(cube_pos[2] >> 2) & 0x1FFC000
						) >> 3
					)
				>> 5
				)
			) >> 6;

		for (uint32_t hash_frame = cube_data_hash_[cube_data_search_value].frame_;
			hash_frame != INVALID_FRAME;
			hash_frame = cube_data_hash_[cube_data_search_value].frame_)
		{
			cube_data_hash_s* cur_cube_data_hash = cube_data_hash_ + cube_data_search_value;

			if (pre_frame_ == hash_frame && cur_cube_data_hash->pos_ == cube_pos) {
				break;
			}

			cube_data_search_value = (cube_data_search_value + 1) & 0x7FF /* 2047 */;
		}

		cube_data_hash_s* cube_data_hash = cube_data_hash_ + cube_data_search_value;

		if (pre_frame_ == cube_data_hash->frame_) {

			if (!cube_data_hash->cube_data_) {
				Log(log_type_t::LOG_FATAL, __FILE__, __LINE__, "Should have allocated data from cube exiting previous frame\n");
				return;
			}

			cube_data_array_[num_cube_data_++] = cube_data_hash->cube_data_;
			cube_data_hash->frame_ = cur_frame_;
		}
		else {

			if (cube_data_hash->cube_data_) {
				Log(log_type_t::LOG_FATAL, __FILE__, __LINE__, "Should not have allocated hash data\n");
				return;
			}

			if (num_render_cube_ < MAX_RENDER_CUBE) {

				cube_data_hash->pos_ = cube_pos;
				cube_data_hash->frame_ = cur_frame_;

				render_cube_s* render_cube = render_cubes_ + num_render_cube_++;

				render_cube->pos_[0] = (double)cube_pos[0];
				render_cube->pos_[1] = (double)cube_pos[1];
				render_cube->pos_[2] = (double)cube_pos[2];
				render_cube->lod_level_ = cube_level;
				render_cube->trans_flag_ = trans_flag;
				render_cube->cmd_ = ctr_node->cmd_;

				render_cube->packed_hmp_idx_lmp_idx_ = 0;

				uint16_t hmp_idx = 0;
				if ((cur_mod_options_ & TERRAIN_HEIGHT_MOD) && height_map_stack_size_) {
					hmp_idx = height_map_stack_[height_map_stack_size_ - 1];
				}

				uint16_t lmp_idx = 0xFFFF;
				if (light_map_stack_size_) {
					lmp_idx = light_map_stack_[light_map_stack_size_ - 1];
				}

				render_cube->packed_hmp_idx_lmp_idx_ = (hmp_idx << 16) | lmp_idx;
				render_cube->packed_tex_modifier_indices_ = cur_mod_options_ & TERRAIN_TEXTURE_MOD ? packed_material_indices_ : 0;

				render_cube->cube_data_hash_ = cube_data_hash;
			}
		}
	}
}

