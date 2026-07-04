/******************************************************************************
 * @file    terrain_mesh.cpp
 * @brief   Terrain implementation: cube data + mesh generation
 *          Split from terrain.cpp; shares terrain_internal.h.
 *****************************************************************************/
#include "terrain_internal.h"

void Terrain::GenerateCubeData() {
	// tex mod

	struct tex_mod_info_s {
		int			bitmap_line_width_shift_;
		float		local_pos_to_bitmap_pos_;
		float		cube_min_x_;
		float		cube_min_y_;
		const uint8_t* bitmap_item_;
		int			material_idx_;
	} tex_mod_info_buf[4];	// at most blend 4 material textures

	// clear tex_mod_info_buf
	memset(&tex_mod_info_buf, 0, sizeof(tex_mod_info_buf));

	uint32_t pri_tex_mod_flags = 0;

	for (int i = 0; i < num_render_cube_; ++i) {
		render_cube_s* render_cube = render_cubes_ + i;

		if (render_cube->packed_tex_modifier_indices_ != pri_tex_mod_flags) {
			// need update tex_mod_info_buf
			pri_tex_mod_flags = render_cube->packed_tex_modifier_indices_;

			uint32_t p = render_cube->packed_tex_modifier_indices_;

			for (int j = 0; j < 4; ++j) {
				// check 4 bytes one by one
				uint8_t tm_idx_add_one = (uint8_t)p;

				if (tm_idx_add_one) {
					const texture_modifier_s* texture_modifier = texture_modifiers_ + (tm_idx_add_one - 1);

					tex_mod_info_buf[j].bitmap_line_width_shift_ = texture_modifier->bitmap_line_width_shift_;
					tex_mod_info_buf[j].local_pos_to_bitmap_pos_ = texture_modifier->local_pos_to_bitmap_pos_;
					tex_mod_info_buf[j].cube_min_x_ = (float)texture_modifier->cube_min_x_;
					tex_mod_info_buf[j].cube_min_y_ = (float)texture_modifier->cube_min_y_;
					tex_mod_info_buf[j].bitmap_item_ = texture_modifier->bitmap_item_;
					tex_mod_info_buf[j].material_idx_ = texture_modifier->material_idx_;
				}
				else {
					tex_mod_info_buf[j].bitmap_line_width_shift_ = 0;	// no modifier
				}

				p >>= 8;
			}
		}

		uint32_t buf_type = CUBE_DATA_BUF_TYPE_1600;
		cube_data_s* cube_data = AllocCubeData(0, buf_type);
		if (!cube_data) {
			Log(log_type_t::LOG_FATAL, __FILE__, __LINE__, "cube_data  overflow (pre-alloc)");
			return;
		}

		render_cube->cube_data_hash_->cube_data_ = cube_data;
		render_cube->cube_data_hash_->buf_type_ = buf_type;

		memcpy(cube_data->cube_ctr_pos_, render_cube->pos_, sizeof(double) * 3);

		const cmd_item_s* cmd = render_cube->cmd_;

		uint16_t parent_vert_cnt = cmd->num_parent_vertex_;
		uint16_t child_vert_cnt = cmd->num_child_vertex_;
		uint16_t total_vert_cnt = parent_vert_cnt + child_vert_cnt;

		const uint32_t* cmd_triangles = (const uint32_t*)(cmd + 1);
		const uint32_t* cmd_vertices = cmd_triangles + cmd->num_triangle_;

		cube_data->total_vertex_count_ = total_vert_cnt;
		cube_data->parent_vertex_count_ = parent_vert_cnt;
		cube_data->lod_level_ = render_cube->lod_level_;

		uint32_t trans_flag = render_cube->trans_flag_;

		const height_map_s* height_map = nullptr;
		uint16_t hmp_idx = render_cube->packed_hmp_idx_lmp_idx_ >> 16;
		if (hmp_idx) {
			height_map = height_maps_ + hmp_idx - 1;
		}

		//	lod level,	scale
		//	16			2048
		//	15			4096
		//	14			8192
		//	...
		double scale = (double)(1 << (27 - render_cube->lod_level_));

		cube_data_vert_pos_s* cube_data_vert_pos = (cube_data_vert_pos_s*)(cube_data + 1);

		cube_data_vert_pos_s* vert_pos = cube_data_vert_pos;
		for (uint16_t i = 0; i < total_vert_cnt; ++i) {
			uint32_t cmd_vert = cmd_vertices[i];

			// raw x, y, z range: 0~63
			int shifted_x = (cmd_vert >> 26) & 0x3F;
			int shifted_y = (cmd_vert >> 20) & 0x3F;
			int shifted_z = (cmd_vert >> 14) & 0x3F;

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

			double vert_x = (double)x * scale * ONE_OVER_3 + render_cube->pos_[0];
			double vert_y = (double)y * scale * ONE_OVER_3 + render_cube->pos_[1];
			double vert_z = (double)z * scale * ONE_OVER_3 + render_cube->pos_[2];

			float delta_z = 0.0f;

			if (height_map && render_cube->lod_level_ >= MIN_CUBE_LOD_LEVEL_TO_APPLY_HMP) {
				float dz = (float)CalcHMPDeltaZ(height_map, vert_x, vert_y);
				if (i >= parent_vert_cnt) {
					// children
					vert_z += dz;
				}
				else {
					// parent
					if (render_cube->lod_level_ > MIN_CUBE_LOD_LEVEL_TO_APPLY_HMP) {
						vert_z += dz;
					}
					else {
						// MIN_CUBE_LOD_LEVEL_TO_APPLY_HMP
						delta_z = dz;
					}
				}
			}

			vert_pos->pos_[0] = (float)vert_x;
			vert_pos->pos_[1] = (float)vert_y;
			vert_pos->pos_[2] = (float)vert_z;

			if (i >= parent_vert_cnt) {
				// child vertex
				vert_pos->parent_idx_ = cmd_vert & 0x3F;
			}
			else {
				// parent vertex
				vert_pos->delta_z_ = delta_z;
			}

			vert_pos++;
		}

		struct vert_info_s {
			int			ref_vert_idx_;
			float		vert_x_sub_cube_ctr_x_;
			float		vert_y_sub_cube_ctr_y_;
			float		vert_z_sub_cube_ctr_z_;
			float		unk1_;
			uint32_t	packed_flags_;   // high 16 bits: cur vertex mat_idx, lower 16 bits: parent vertex mat_idx
			float		unk2_;
			float		u_;
			float		v_;
		} vert_info_buf[64];	// atmost 64 vertices per cube

		if (total_vert_cnt > 64) {
			Log(log_type_t::LOG_FATAL, __FILE__, __LINE__, "total_vert_cnt exceeds vert_info_buf capacity");
			return;
		}

		uint32_t tex_mods = 0;

		for (uint16_t i = 0; i < total_vert_cnt; ++i) {
			uint32_t cmd_vert = cmd_vertices[i];
			vert_info_s* vi = vert_info_buf + i;

			uint32_t mat_of_vert = 0;

			vi->vert_x_sub_cube_ctr_x_ = cube_data_vert_pos[i].pos_[0] - (float)render_cube->pos_[0];
			vi->vert_y_sub_cube_ctr_y_ = cube_data_vert_pos[i].pos_[1] - (float)render_cube->pos_[1];
			vi->vert_z_sub_cube_ctr_z_ = cube_data_vert_pos[i].pos_[2] - (float)render_cube->pos_[2];

			for (int j = 0; j < 4; ++j) {

				if (tex_mod_info_buf[j].bitmap_line_width_shift_) {

					float local_pos_to_bitmap_pos = tex_mod_info_buf[j].local_pos_to_bitmap_pos_;

					uint32_t bitmap_x = (uint32_t)((cube_data_vert_pos[i].pos_[0] - tex_mod_info_buf[j].cube_min_x_) * local_pos_to_bitmap_pos);
					uint32_t bitmap_y = (uint32_t)((cube_data_vert_pos[i].pos_[1] - tex_mod_info_buf[j].cube_min_y_) * local_pos_to_bitmap_pos);

					uint8_t bits = *(tex_mod_info_buf[j].bitmap_item_ + (bitmap_y << tex_mod_info_buf[j].bitmap_line_width_shift_) + (bitmap_x >> 3));

					if ((bits >> (bitmap_x & 7)) & 1) {
						// that bit is 1
						mat_of_vert = tex_mod_info_buf[j].material_idx_;
						break;
					}
				}
				else {
					break;
				}
			}

			tex_mods |= (1 << mat_of_vert);

			if (i < parent_vert_cnt) {
				vi->ref_vert_idx_ = i;

				vi->packed_flags_ = (mat_of_vert << 16) | 255;
			}
			else {
				vi->ref_vert_idx_ = cmd_vert & 0x3F;

				vert_info_s* ref_vi = vert_info_buf + vi->ref_vert_idx_;

				uint16_t mat_of_ref_vert = (ref_vi->packed_flags_ >> 16);

				if (mat_of_ref_vert == mat_of_vert) {
					vi->packed_flags_ = (mat_of_vert << 16) | 255;
				}
				else {
					vi->packed_flags_ = (mat_of_vert << 16) | mat_of_ref_vert;
				}

			}
		}

		int tex_mod_indices[32];
		int tex_mod_indices_cnt = 0;

		tex_mod_indices[0] = 0;

		int check_bit = 0;
		while (tex_mods) {

			if (tex_mods & 1) {
				tex_mod_indices[tex_mod_indices_cnt++] = check_bit;
			}

			check_bit++;
			tex_mods >>= 1;
		}

		uint32_t size_before_tri_and_tex = (uint32_t)(sizeof(cube_data_s)
			+ sizeof(cube_data_vert_pos_s) * total_vert_cnt);

		uint32_t cube_data_one_tex_parent_vert_uv_size = (uint32_t)sizeof(cube_data_parent_vert_uv_s) * parent_vert_cnt;
		uint32_t cube_data_one_tex_child_vert_uv_size = (uint32_t)sizeof(cube_data_child_vert_uv_s) * child_vert_cnt;
		uint32_t cube_data_one_tex_vert_uv_size =
			cube_data_one_tex_parent_vert_uv_size
			+ cube_data_one_tex_child_vert_uv_size
			+ sizeof(uint32_t) /* tex */;

		uint32_t total_cube_data_size = (uint32_t)(size_before_tri_and_tex
			+ sizeof(cube_data_tri_and_tex_s)
			+ 2 * tex_mod_indices_cnt * cube_data_one_tex_vert_uv_size	// tile
			+ cube_data_one_tex_vert_uv_size);	// lmp

		if (total_cube_data_size > 8000) {
			Log(log_type_t::LOG_FATAL, __FILE__, __LINE__, "total_cube_data_size %u > 8000", total_cube_data_size);
			FreeCubeData(cube_data, buf_type);
			return;
		}

		if ((total_cube_data_size > 3200 && buf_type < CUBE_DATA_BUF_TYPE_8000)
			|| (total_cube_data_size > 1600 && buf_type < CUBE_DATA_BUF_TYPE_3200)) {

			cube_data_s* old_cube_data = cube_data;
			uint32_t old_buf_type = buf_type;

			cube_data = AllocCubeData(total_cube_data_size, buf_type);
			if (cube_data) {
				memcpy(cube_data, old_cube_data, size_before_tri_and_tex);

				render_cube->cube_data_hash_->cube_data_ = cube_data;
				render_cube->cube_data_hash_->buf_type_ = buf_type;
			}

			FreeCubeData(old_cube_data, old_buf_type);

			if (!cube_data) {
				Log(log_type_t::LOG_FATAL, __FILE__, __LINE__, "cube_data overflow, need size: %u", total_cube_data_size);
				return;
			}
		}

		cube_data_tri_and_tex_s* cube_data_tri_and_tex = (cube_data_tri_and_tex_s*)((char*)cube_data + size_before_tri_and_tex);

		cube_data_tri_and_tex->trans_flag_ = trans_flag;
		cube_data_tri_and_tex->triangle_indices_ = (uint32_t*)(cmd + 1);
		cube_data_tri_and_tex->tile_count_ = tex_mod_indices_cnt;
		cube_data_tri_and_tex->triangle_count_ = cmd->num_triangle_;

		int cube_half_size = ROOT_CUBE_HALF_SIZE >> render_cube->lod_level_;

		char* tile_uv_start = (char*)(cube_data_tri_and_tex + 1);

		for (int i = 0; i < tex_mod_indices_cnt; ++i) {

			uint16_t cur_mat_idx = (uint16_t)tex_mod_indices[i];

			float* curr_uv_ptr = (float*)(tile_uv_start + i * 2 * cube_data_one_tex_vert_uv_size);
			float* next_uv_ptr = (float*)(tile_uv_start + i * 2 * cube_data_one_tex_vert_uv_size + cube_data_one_tex_vert_uv_size);

			const material_array_s* tma = materials_.material_array_map_ + cur_mat_idx;
			const material_s* cur_tm = tma->material_array_ + render_cube->lod_level_;

			tile_map_s* tile_map_tex1 = tile_maps_ + cur_tm->tex1_tile_map_group_idx_;
			tile_map_s* tile_map_tex2 = tile_maps_ + cur_tm->tex2_tile_map_group_idx_;

			int shift_for_tex1 = 22 - render_cube->lod_level_;

			uint32_t y_off = (((int)(int64_t)(cur_tm->f_tex1_ * render_cube->pos_[1]) >> shift_for_tex1) & 0x7FF) >> 8;
			uint32_t x_off = (((int)(int64_t)(cur_tm->f_tex1_ * render_cube->pos_[0]) >> shift_for_tex1) & 0x7FF) >> 8;

			uint32_t tex1 = tile_map_tex1->tex_idx_in_terrain_tex_[8 * y_off + x_off];

			*(uint32_t*)curr_uv_ptr++ = tex1;

			int shift_for_tex2 = 23 - render_cube->lod_level_;

			y_off = (((int)(int64_t)(cur_tm->f_tex2_ * render_cube->pos_[1]) >> shift_for_tex2) & 0x7FF) >> 8;
			x_off = (((int)(int64_t)(cur_tm->f_tex2_ * render_cube->pos_[0]) >> shift_for_tex2) & 0x7FF) >> 8;

			uint32_t tex2 = tile_map_tex2->tex_idx_in_terrain_tex_[8 * y_off + x_off];

			*(uint32_t*)next_uv_ptr++ = tex2;

			// tex1
			float uv1_x = ((uint8_t)((int)(cur_tm->f_tex1_ * render_cube->pos_[0]) >> shift_for_tex1) + 1.0f) * ONE_OVER_256;
			float uv1_y = ((uint8_t)((int)(cur_tm->f_tex1_ * render_cube->pos_[1]) >> shift_for_tex1) + 1.0f) * ONE_OVER_256;
			float uv1_z = ((uint8_t)((int)(cur_tm->f_tex1_ * render_cube->pos_[2]) >> shift_for_tex1) + 1.0f) * ONE_OVER_256;
			float uv1_f = cur_tm->f_tex1_ / cube_half_size;

			// tex2
			float uv2_x = ((uint8_t)((int)(cur_tm->f_tex2_ * render_cube->pos_[0]) >> shift_for_tex2) + 1.0f) * ONE_OVER_256;
			float uv2_y = ((uint8_t)((int)(cur_tm->f_tex2_ * render_cube->pos_[1]) >> shift_for_tex2) + 1.0f) * ONE_OVER_256;
			float uv2_z = ((uint8_t)((int)(cur_tm->f_tex2_ * render_cube->pos_[2]) >> shift_for_tex2) + 1.0f) * ONE_OVER_256;
			float uv2_f = cur_tm->f_tex2_ * 0.5f / cube_half_size;

			for (uint16_t j = 0; j < total_vert_cnt; ++j) {
				uint32_t cmd_vert = cmd_vertices[j];

				uint16_t parent_vert_idx  =  cmd_vert & 0x3F;
				uint32_t cmd_vert_bit_8_9 = (cmd_vert >> 8) & 3;
				uint32_t cmd_vert_bit_6_7 = (cmd_vert >> 6) & 3;

				cube_data_vert_pos_s* cur_vert = cube_data_vert_pos + j;
				cube_data_vert_pos_s* ref_vert = nullptr;

				if (j < parent_vert_cnt) {
					ref_vert = cur_vert;
				}
				else {
					ref_vert = cube_data_vert_pos + parent_vert_idx;
				}

				float cur_vert_x_sub_pos_x = (float)(cur_vert->pos_[0] - render_cube->pos_[0]);
				float cur_vert_y_sub_pos_y = (float)(cur_vert->pos_[1] - render_cube->pos_[1]);
				float cur_vert_z_sub_pos_z = (float)(cur_vert->pos_[2] - render_cube->pos_[2]);

				float ref_vert_x_sub_pos_x = (float)(ref_vert->pos_[0] - render_cube->pos_[0]);
				float ref_vert_y_sub_pos_y = (float)(ref_vert->pos_[1] - render_cube->pos_[1]);
				float ref_vert_z_sub_pos_z = (float)(ref_vert->pos_[2] - render_cube->pos_[2]);

				// tex1
				float u = 0.0f, v = 0.0f, du = 0.0f, dv = 0.0f;

				if (cmd_vert_bit_8_9) {

					if (((trans_flag ^ (uint8_t)cmd_vert_bit_8_9) & 1) != 0) {
						u = cur_vert_y_sub_pos_y * uv1_f + uv1_y;

						float ref_u = ref_vert_y_sub_pos_y * uv1_f + uv1_y;
						du = ref_u - u;
					}
					else {
						u = cur_vert_x_sub_pos_x * uv1_f + uv1_x;

						float ref_u = ref_vert_x_sub_pos_x * uv1_f + uv1_x;
						du = ref_u - u;
					}

					v = cur_vert_z_sub_pos_z * uv1_f + uv1_z;

					dv = 0.0f;

				}
				else {
					u = cur_vert_x_sub_pos_x * uv1_f + uv1_x;
					v = cur_vert_y_sub_pos_y * uv1_f + uv1_y;

					float ref_u = ref_vert_x_sub_pos_x * uv1_f + uv1_x;
					float ref_v = ref_vert_y_sub_pos_y * uv1_f + uv1_y;

					du = ref_u - u;
					dv = ref_v - v;
				}

				curr_uv_ptr[1] = u;
				curr_uv_ptr[2] = v;

				auto calc_uv_flags = [](uint16_t parent_vert_idx, uint16_t cur_mat_idx, uint32_t vert_packed_flags) -> uint32_t {

					uint32_t diffuse_byte_offset = 0;

					uint16_t ref_vert_mat_idx = (uint16_t)vert_packed_flags;
					uint16_t cur_vert_mat_idx = (uint16_t)(vert_packed_flags >> 16);

					if (cur_mat_idx == ref_vert_mat_idx) {
						diffuse_byte_offset = 8;
					}
					else if (cur_mat_idx == cur_vert_mat_idx) {
						diffuse_byte_offset = ref_vert_mat_idx != 255 ? 16 : 24 /* ref_vert_mat_idx == 255: cur == ref mat idx */;
					}

					// 32: 8 float diffuse values: sizeof(float) * 8 = 32 bytes
					//     each parent's morphing factors are different
					// diffuse_byte_offset as row offset
					return (parent_vert_idx << 16) | (diffuse_byte_offset + 32 * parent_vert_idx);
				};

				uint32_t tex1_packed_flags = 0;

				if (j >= parent_vert_cnt) {
					// child vertex

					tex1_packed_flags = calc_uv_flags(parent_vert_idx, cur_mat_idx, vert_info_buf[j].packed_flags_);
					*(uint32_t*)curr_uv_ptr = tex1_packed_flags;

					curr_uv_ptr[3] = du;
					curr_uv_ptr[4] = dv;

					// +5: cube_data_child_vert_uv_s
					curr_uv_ptr += 5;
				}
				else {
					// parent vertex

					tex1_packed_flags = calc_uv_flags(j, cur_mat_idx, vert_info_buf[j].packed_flags_);
					*(uint32_t*)curr_uv_ptr = tex1_packed_flags;

					// +3: cube_data_parent_vert_uv_s
					curr_uv_ptr += 3;
				}

				// tex2
				if (cmd_vert_bit_6_7) {

					if (((trans_flag ^ (uint8_t)cmd_vert_bit_6_7) & 1) != 0) {
						u = cur_vert_y_sub_pos_y * uv2_f + uv2_y;

						float ref_u = ref_vert_y_sub_pos_y * uv2_f + uv2_y;

						du = ref_u - u;
					}
					else {
						u = cur_vert_x_sub_pos_x * uv2_f + uv2_x;

						float ref_u = ref_vert_x_sub_pos_x * uv2_f + uv2_x;

						du = ref_u - u;
					}

					v = cur_vert_z_sub_pos_z * uv2_f + uv2_z;
					float ref_v = ref_vert_z_sub_pos_z * uv2_f + uv2_z;

					dv = ref_v - v;
				}
				else {
					u = cur_vert_x_sub_pos_x * uv2_f + uv2_x;
					v = cur_vert_y_sub_pos_y * uv2_f + uv2_y;

					float ref_u = ref_vert_x_sub_pos_x * uv2_f + uv2_x;
					float ref_v = ref_vert_y_sub_pos_y * uv2_f + uv2_y;

					du = ref_u - u;
					dv = ref_v - v;
				}

				*(uint32_t*)next_uv_ptr = tex1_packed_flags + 4; /* + 4: byte offset plus 4, point to next diffuse color for texture blending */
				next_uv_ptr[1] = u;
				next_uv_ptr[2] = v;

				if (j >= parent_vert_cnt) {
					// child
					next_uv_ptr[3] = du;
					next_uv_ptr[4] = dv;

					// +5: cube_data_child_vert_uv_s
					next_uv_ptr += 5;
				}
				else {
					// parent

					// +3: cube_data_parent_vert_uv_s
					next_uv_ptr += 3;
				}
			}

		}

		// lmp
		float* lmp_uv_ptr = (float*)(tile_uv_start + 2 * tex_mod_indices_cnt * cube_data_one_tex_vert_uv_size);

		if ((uint16_t)render_cube->packed_hmp_idx_lmp_idx_ != 0xFFFF) {

			const terrain_light_map_s * terrain_light_map = terrain_light_maps_ + (uint16_t)render_cube->packed_hmp_idx_lmp_idx_;
			*(uint32_t*)lmp_uv_ptr++ = terrain_light_map->tex_idx_ + 1;

			float one_over_cube_size = terrain_light_map->one_over_cube_size_;
			float half_pixel_uv = terrain_light_map->half_pixel_uv_;

			for (uint16_t j = 0; j < total_vert_cnt; ++j) {
				uint32_t cmd_vert = cmd_vertices[j];

				uint16_t parent_vert_idx = cmd_vert & 0x3F;

				cube_data_vert_pos_s* cur_vert = cube_data_vert_pos + j;
				cube_data_vert_pos_s* ref_vert = nullptr;

				if (j < parent_vert_cnt) {
					ref_vert = cube_data_vert_pos + j;
				}
				else {
					ref_vert = cube_data_vert_pos + parent_vert_idx;
				}

				float u = (cur_vert->pos_[0] - (float)terrain_light_map->cube_min_x_) * one_over_cube_size + half_pixel_uv;
				float v = (cur_vert->pos_[1] - (float)terrain_light_map->cube_min_y_) * one_over_cube_size + half_pixel_uv;

				float ref_u = (ref_vert->pos_[0] - (float)terrain_light_map->cube_min_x_) * one_over_cube_size + half_pixel_uv;
				float ref_v = (ref_vert->pos_[1] - (float)terrain_light_map->cube_min_y_) * one_over_cube_size + half_pixel_uv;

				lmp_uv_ptr[1] = u;
				lmp_uv_ptr[2] = v;

				if (j >= parent_vert_cnt) {
					// child

					uint16_t parent_vert_idx = cmd_vert & 0x3F;

					*(uint32_t*)lmp_uv_ptr = (parent_vert_idx << 16) | (parent_vert_idx * 32);

					// delta uv, uv interpolation of child vertex
					lmp_uv_ptr[3] = ref_u - u;
					lmp_uv_ptr[4] = ref_v - v;

					// +5: cube_data_child_vert_uv_s
					lmp_uv_ptr += 5;
				}
				else {
					// parent

					*(uint32_t*)lmp_uv_ptr = (j << 16) | (j * 32);

					// +3: cube_data_parent_vert_uv_s
					lmp_uv_ptr += 3;
				}
			}

		}
		else {
			*(uint32_t*)lmp_uv_ptr = 0;
		}

		cube_data_array_[num_cube_data_++] = cube_data;
	}
}

void Terrain::GenerateCubeMesh(const cube_data_s* cube_data) {
	float inter_factors[64];	// each parent vertices

	float diffuse_table[64 * 8] = {};

	uint16_t parent_vert_count = cube_data->parent_vertex_count_;
	uint16_t total_vert_count = cube_data->total_vertex_count_;

	const cube_data_vert_pos_s* vert_pos = (const cube_data_vert_pos_s*)(cube_data + 1);
	for (uint16_t i = 0; i < parent_vert_count; ++i) {
		float delta_x = vert_pos[i].pos_[0] - cur_int_view_pos_[0];
		float delta_y = vert_pos[i].pos_[1] - cur_int_view_pos_[1];
		float delta_z = vert_pos[i].pos_[2] - cur_int_view_pos_[2];

		// max abslute axis distance
		float max_delta = delta_x;
		if (max_delta < 0.0f) {
			max_delta = -delta_x;
		}

		if (delta_y < 0.0f) {
			delta_y = -delta_y;
		}

		if (delta_y > max_delta) {
			max_delta = delta_y;
		}

		if (delta_z < 0.0f) {
			delta_z = -delta_z;
		}

		if (delta_z > max_delta) {
			max_delta = delta_z;
		}

		/* lod_level:	1 << (25 - lod_level)
		   7			262144
		   8			131072
		   9			65536
		   10			32768
		   11			16384
		   12			8192
		   13			4096
		   14			2048
		   15			1024
		   16			512
		 */

		float factor = max_delta / ((1 << (25 - cube_data->lod_level_)) * CUBE_ACTIVE_COMPARE_SUB_CUBE_START_MERGE_DELTA)
			- CUBE_MORPHING_FACTOR_COMPARE;

		if (factor >= 0.0f) {
			if (factor > 1.0f) {
				factor = 1.0f;
			}
		}
		else {
			factor = 0.0f;
		}

		inter_factors[i] = factor;

		// calc diffuse
		float one_minus_inter_factor = 1.0f - factor;

		float diffuse = one_minus_inter_factor;
		float diffuse2 = one_minus_inter_factor * one_minus_inter_factor;

		float reverse_diffuse = factor;

		float* cur_parent_vert_diffuse_row = diffuse_table + 8 * i;

		cur_parent_vert_diffuse_row[0] = 0.0f;
		cur_parent_vert_diffuse_row[1] = 0.0f;

		cur_parent_vert_diffuse_row[2] = diffuse - diffuse2;
		cur_parent_vert_diffuse_row[3] = diffuse2 + reverse_diffuse - diffuse; // diffuse2 + reverse_diffuse - diffuse;

		cur_parent_vert_diffuse_row[4] = diffuse2;
		cur_parent_vert_diffuse_row[5] = diffuse - diffuse2;

		cur_parent_vert_diffuse_row[6] = one_minus_inter_factor;
		cur_parent_vert_diffuse_row[7] = factor;
	}

	glm::vec3 temp_vert3[64];

	for (int i = 0; i < cube_data->parent_vertex_count_; ++i) {
		// parent vertices

		float factor = inter_factors[i];
		float one_minus_factor = 1.0f - factor;

		temp_vert3[i].x = vert_pos[i].pos_[0];
		temp_vert3[i].y = vert_pos[i].pos_[1];

		// factor is 0: +delta_z_
		// factor is 1: +0.0
		temp_vert3[i].z = vert_pos[i].pos_[2] + vert_pos[i].delta_z_ * one_minus_factor;
	}

	for (int i = cube_data->parent_vertex_count_; i < cube_data->total_vertex_count_; ++i) {
		// child vertices
		
		const glm::vec3* ref_vert_pos = temp_vert3 + vert_pos[i].parent_idx_;

		
		float factor = inter_factors[vert_pos[i].parent_idx_];
		float one_minus_factor = 1.0f - factor;

		// factor is 0: completely splitted
		// factor is 1: completely merged
		temp_vert3[i].x = ref_vert_pos->x * factor + vert_pos[i].pos_[0] * one_minus_factor;
		temp_vert3[i].y = ref_vert_pos->y * factor + vert_pos[i].pos_[1] * one_minus_factor;
		temp_vert3[i].z = ref_vert_pos->z * factor + vert_pos[i].pos_[2] * one_minus_factor;
	}

	const cube_data_tri_and_tex_s* tri_and_tex = (const cube_data_tri_and_tex_s*)(vert_pos + cube_data->total_vertex_count_);

	uint32_t indices_count = tri_and_tex->triangle_count_ * 3;
	int tex_group_cnt = tri_and_tex->tile_count_;

	const float* src_uv = (const float*)(tri_and_tex + 1);

	uint32_t temp_indices[256];

	if (indices_count > 256) {
		Log(log_type_t::LOG_FATAL, __FILE__, __LINE__, "too many triangles per cube");
		return;
	}

	const uint32_t* triangles = tri_and_tex->triangle_indices_;

	const uint32_t* src_tri_ptr = triangles;
	uint32_t* dst_idx_of_temp_indices = temp_indices;

	for (uint16_t i = 0; i < tri_and_tex->triangle_count_; ++i) {
		uint32_t src_tri = *src_tri_ptr;

		if (tri_and_tex->trans_flag_ & 4) {
			// mirrored
			dst_idx_of_temp_indices[0] = (uint8_t)((src_tri & 0xff00) >> 8);
			dst_idx_of_temp_indices[1] = (uint8_t)(src_tri & 0xff);
			dst_idx_of_temp_indices[2] = (uint8_t)((src_tri & 0xff0000) >> 16);
		}
		else {
			dst_idx_of_temp_indices[0] = (uint8_t)((src_tri & 0xff0000) >> 16);
			dst_idx_of_temp_indices[1] = (uint8_t)(src_tri & 0xff);
			dst_idx_of_temp_indices[2] = (uint8_t)((src_tri & 0xff00) >> 8);
		}

		src_tri_ptr++;
		dst_idx_of_temp_indices += 3;
	}

	// tile
	for (int i = 0; i < tex_group_cnt * 2; ++i) {
		uint32_t tex = *(uint32_t*)src_uv++;

		render_chunk_s* rc = nullptr;

		if (i == 0) {
			rc = GetRenderChunkByTex(0, tex);
		}
		else {
			rc = GetRenderChunkByTex(1, tex);
		}

		if (!rc) {
			Log(log_type_t::LOG_FATAL, __FILE__, __LINE__, "render chunk overflow");
			break;
		}

		if (rc->num_render_triangle_ >= MAX_TRIANGLE_LIST_PER_CHUNK) {
			Log(log_type_t::LOG_FATAL, __FILE__, __LINE__, "rc->num_render_triangle_ > %d\n", MAX_TRIANGLE_LIST_PER_CHUNK);
			break;
		}

		// put long loop as inner loop
		int free_vert_count = MAX_TERRAIN_VERTICES - num_vertex_;
		if (free_vert_count < total_vert_count) {
			Log(log_type_t::LOG_FATAL, __FILE__, __LINE__, "vertex overflow");
			break;
		}

		int free_idx_count = MAX_TERRAIN_INDICES - num_index_;
		if (free_idx_count < (int)indices_count) {
			Log(log_type_t::LOG_FATAL, __FILE__, __LINE__, "index overflow");
			break;
		}

		vert_pos_a_uv_s* dst_vert = vertices_ + num_vertex_;

		for (int j = 0; j < parent_vert_count; ++j) {

			dst_vert->pos_ = temp_vert3[j];

			dst_vert->uv_[0] = src_uv[1];
			dst_vert->uv_[1] = src_uv[2];

			uint32_t flags = *(uint32_t*)&src_uv[0];
			uint16_t diffuse_byte_offset = (uint16_t)flags;

			dst_vert->tex_alpha_ = diffuse_table[diffuse_byte_offset >> 2];

			src_uv += 3;	// cube_data_parent_vert_uv_s
			dst_vert++;
		}

		for (int j = parent_vert_count; j < total_vert_count; ++j) {
			float factor = inter_factors[vert_pos[j].parent_idx_];

			dst_vert->pos_ = temp_vert3[j];

			dst_vert->uv_[0] = src_uv[1] + src_uv[3] * factor;
			dst_vert->uv_[1] = src_uv[2] + src_uv[4] * factor;

			uint32_t flags = *(uint32_t*)&src_uv[0];
			uint16_t diffuse_byte_offset = (uint16_t)flags;

			dst_vert->tex_alpha_ = diffuse_table[diffuse_byte_offset >> 2];

			src_uv += 5;	// cube_data_child_vert_uv_s
			dst_vert++;
		}

		uint32_t* dst_idx = indices_ + num_index_;

		for (uint32_t j = 0; j < indices_count; ++j) {
			dst_idx[j] = temp_indices[j] + num_vertex_ /* as offset */;
		}

		render_triangles_s* rt = rc->render_triangles_ + rc->num_render_triangle_++;
		rt->first_vert_idx_ = num_index_;
		rt->num_triangle_ = tri_and_tex->triangle_count_;

		num_vertex_ += total_vert_count;
		num_index_ += indices_count;
	}

	// lmp
	uint32_t light_map = *(uint32_t*)src_uv++;
	if (light_map) {
		// has lmp
		uint32_t tex = light_map - 1;

		render_chunk_s* rc = GetRenderChunkByTex(2, tex);
		if (!rc) {
			Log(log_type_t::LOG_FATAL, __FILE__, __LINE__, "render chunk overflow");
			return;
		}

		if (rc->num_render_triangle_ >= MAX_TRIANGLE_LIST_PER_CHUNK) {
			Log(log_type_t::LOG_FATAL, __FILE__, __LINE__, "rc->num_render_triangle_ > %d\n", MAX_TRIANGLE_LIST_PER_CHUNK);
			return;
		}

		int free_vert_count = MAX_TERRAIN_VERTICES - num_vertex_;
		if (free_vert_count < total_vert_count) {
			Log(log_type_t::LOG_FATAL, __FILE__, __LINE__, "vertex overflow\n");
			return;
		}

		int free_idx_count = MAX_TERRAIN_INDICES - num_index_;
		if (free_idx_count < (int)indices_count) {
			Log(log_type_t::LOG_FATAL, __FILE__, __LINE__, "index overflow");
			return;
		}

		vert_pos_a_uv_s* dst_vert = vertices_ + num_vertex_;

		for (int j = 0; j < parent_vert_count; ++j) {

			dst_vert->pos_ = temp_vert3[j];

			dst_vert->uv_[0] = src_uv[1];
			dst_vert->uv_[1] = src_uv[2];

			dst_vert->tex_alpha_ = 0.0f;

			src_uv += 3;
			dst_vert++;
		}

		for (int j = parent_vert_count; j < total_vert_count; ++j) {
			float factor = inter_factors[vert_pos[j].parent_idx_];

			dst_vert->pos_ = temp_vert3[j];

			dst_vert->uv_[0] = src_uv[1] + src_uv[3] * factor;
			dst_vert->uv_[1] = src_uv[2] + src_uv[4] * factor;

			dst_vert->tex_alpha_ = 0.0f;

			src_uv += 5;
			dst_vert++;
		}

		uint32_t* dst_idx = indices_ + num_index_;

		for (uint32_t j = 0; j < indices_count; ++j) {
			dst_idx[j] = temp_indices[j] + num_vertex_;
		}

		render_triangles_s* rt = rc->render_triangles_ + rc->num_render_triangle_++;
		rt->first_vert_idx_ = num_index_;
		rt->num_triangle_ = tri_and_tex->triangle_count_;

		num_vertex_ += total_vert_count;
		num_index_ += indices_count;
	}
}

