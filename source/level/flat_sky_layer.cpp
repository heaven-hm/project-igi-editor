/******************************************************************************
 * @file    flat_sky_layer.cpp
 * @brief   flat sky layer
 *****************************************************************************/

#include "pch.h"
#include "flat_sky_layer.inl"

/*
================================================================================
 FlatSkyLayer
================================================================================
*/
FlatSkyLayer::FlatSkyLayer():
	valid_(false),
	color_(1.0f),
	scale_(1.0f),
	x_speed_(0.0f),
	y_speed_(0.0f),
	cur_x_(0.0f),
	cur_y_(0.0f)
{
	// do nothing
}

FlatSkyLayer::~FlatSkyLayer() {
	// do nothing
}

void FlatSkyLayer::Setup(int layer_no, IRenderResLoader * render_res_loader, const char* tex_file, const glm::vec4& color, float scale, float x_speed, float y_speed)
{
	valid_ = true;
	color_ = color;
	scale_ = scale;
	x_speed_ = x_speed;
	y_speed_ = y_speed;

	char tex_filename_only[1024];
	Str_ExtractFileName(tex_file, tex_filename_only, 1024);

	char full_tex_filename[1024];
	Str_SPrintf(full_tex_filename, 1024,
		"%s/missions/location0/common/textures/%s",
		g_folders.res_folder_, tex_filename_only);

	pics_s pics = {};
	if (Tex_Load(full_tex_filename, pics)) {
		render_res_loader->LoadFlatSkyLayerTex(layer_no, pics.pics_);

		// debug
		/*
		char filename[1024];
		Str_SPrintf(filename, 1024, "d:/%03d.bmp", layer_no);
		Pic_SaveToBMP(pics.pics_, filename);
		//*/

		Pic_FreePics(pics);
	}
}

void FlatSkyLayer::Reset() {
	valid_ = false;
}

bool FlatSkyLayer::Update(const fsl_update_params_s& params) {
	bool visible = false;

	vert_flat_sky_layer_s* dst_vert = params.vb_ + params.layer_no_ * NUM_VERTEX_PER_FLAT_SKY_LAYER;

	// clamp cur_x_, cur_y_ into range [0.0, 1.0]
	cur_x_ += x_speed_;

	while (cur_x_ > 1.0f) {
		cur_x_ -= 1.0f;
	}

	while (cur_x_ < 0.0f) {
		cur_x_ += 1.0f;
	}

	cur_y_ += y_speed_;

	while (cur_y_ > 1.0f) {
		cur_y_ -= 1.0f;
	}

	while (cur_y_ < 0.0f) {
		cur_y_ += 1.0f;
	}

	float z_far_div_z_far_sub_z_near =
		params.vd_->render_z_far_ / (params.vd_->render_z_far_ - params.vd_->render_z_near_);

	float flat_sky_z_pos_in_world = params.flat_sky_z_pos_ * WORLD_UNITS_PER_METER;
	float flat_sky_distance_in_world = params.flat_sky_distance_ * WORLD_UNITS_PER_METER;

	// flat_sky_fog_amount: [0.0, 1.0]
	/* fog_start:

	   side view:

	viewer

	   |<-------------------- flat_sky_distance_in_world --------------------->|
	   |
	   |<- WORLD_Z_NEAR ->|<----- to calculate flat_sky_fog_amount part ------>|
	   |<- fog start ->|<--                                                 -->|
	                                                   
		e.g.
		  flat_sky_distance_in_world: 163840.0
		  WORLD_Z_NEAR: 409.599976
		  flat_sky_fog_amount: 0.906666994


		  fog_start = 163840.0 - (163840.0 - 409.599976) * 0.906666994
		            = 163840.0 - 148176.949517977607856
					= 15663.050482022392144

	 */

	float fog_start = flat_sky_distance_in_world - (flat_sky_distance_in_world - WORLD_Z_NEAR) * params.flat_sky_fog_amount_;
	float one_over_flat_sky_distance_sub_remain_dist = 1.0f / (flat_sky_distance_in_world - fog_start);

	glm::vec3 forward_dir_in_horizontal;

	forward_dir_in_horizontal.x = params.vd_->mat_rot_[0][2];	// view_define_.forward_.x
	forward_dir_in_horizontal.y = params.vd_->mat_rot_[1][2];	// view_define_.forward_.y
	forward_dir_in_horizontal.z = 0.0f;

	if (forward_dir_in_horizontal.x == 0.0f && forward_dir_in_horizontal.y == 0.0f) {
		forward_dir_in_horizontal.x = 1.0f;	// avoid zero vector
	}

	// normalize x y
	glm::vec2 temp = glm::normalize(forward_dir_in_horizontal.xy());
	forward_dir_in_horizontal.x = temp.x;
	forward_dir_in_horizontal.y = temp.y;

	// forward_in_horizontal & left_in_horizontal are orthogonal on XOY plane
	glm::vec3 left_dir_in_horizontal(-forward_dir_in_horizontal.y, forward_dir_in_horizontal.x, 0.0f);

	/*

	  forward_in_horizontal, left_in_horizontal defined in coordinate:

		Z   Y
		|  /
		| /
		|/________X

		aligned in XOY plane (i.e.  z component is zero)

	 */

	// when viewing forward_dir & left_dir from mat_rot_ defined coordinate

	//  get 2 vectors forward_dir_in_horizontal & left_dir_in_horizontal affected by pitch & roll respectively.
	glm::vec3 forward_dir_in_horizontal_rotated = params.vd_->mat_rot_ * forward_dir_in_horizontal;
	glm::vec3 left_dir_in_horizontal_rotated = params.vd_->mat_rot_ * left_dir_in_horizontal;

	/* forward_dir_in_horizontal_rotated, left_dir_in_horizontal_rotated defined in coordinate:
	
	   after transformed by params.vd_->mat_rot_, they might not in horizontal plane
	   in new coordinates

		   Z
		  /
		 /
		/________X
		|
		|
		|
		Y

	 */


	/* flat_sky_far_right, flat_sky_far_left defined in coordinate:
	   
		Z   Y
		|  /
		| /
		|/________X

	 */
	glm::vec3 flat_sky_far_right, flat_sky_far_left;

	flat_sky_far_right.x = params.flat_sky_distance_ * WORLD_UNITS_PER_METER;
	flat_sky_far_right.y = params.flat_sky_distance_ * WORLD_UNITS_PER_METER;
	flat_sky_far_right.z = params.flat_sky_z_pos_ * WORLD_UNITS_PER_METER;

	flat_sky_far_left.x = params.flat_sky_distance_ * -WORLD_UNITS_PER_METER;
	flat_sky_far_left.y = params.flat_sky_distance_ * WORLD_UNITS_PER_METER;
	flat_sky_far_left.z = params.flat_sky_z_pos_ * WORLD_UNITS_PER_METER;

	/* forward_dir_in_horizontal_rotated, left_dir_in_horizontal_rotated defined in coordinate:
	   
	   both forward_dir_in_horizontal_rotated & left_dir_in_horizontal_rotated are normalized

										    Z  forward_dir_in_horizontal_rotated
										   /
										  /
		rot_space_horizontal_left________/________X
										 |
										 |
										 |
										 Y

	 */

	// mat_to_left_handed is a orthogonal matrix
	// but not a rotation matrix, it changed handedness of coordinate
	glm::mat3 mat_to_left_handed;

	/*  
	    the result converted to mat_rot_ defined coordinates
	    [ left_dir_in_horizontal_rotated    ]
	    [ forward_dir_in_horizontal_rotated ]
		[ up_dir_in_horizontal_rotated      ]

		form a matrix M
		mat_to_left_handed = transpose(M)
		
		equal to: M', i.e.  inverse(M)

		mat_to_left_handed: trans back to mat_rot_ defined coordinate

		flat_sky_far_lef & flat_sky_far_right are defined
		in transpose(M) flat_sky_plane space

	 */

	mat_to_left_handed[0].x = left_dir_in_horizontal_rotated.x;
	mat_to_left_handed[1].x = forward_dir_in_horizontal_rotated.x;
	mat_to_left_handed[2].x = params.vd_->mat_rot_[2][0];

	mat_to_left_handed[0].y = left_dir_in_horizontal_rotated.y;
	mat_to_left_handed[1].y = forward_dir_in_horizontal_rotated.y;
	mat_to_left_handed[2].y = params.vd_->mat_rot_[2][1];

	mat_to_left_handed[0].z = left_dir_in_horizontal_rotated.z;
	mat_to_left_handed[1].z = forward_dir_in_horizontal_rotated.z;
	mat_to_left_handed[2].z = params.vd_->mat_rot_[2][2];

	/*
	  from coordinate (right-handed):
	  
		Z   Y
		|  /
		| /
		|/________X

	  to coordinate (left-handed, D3D7 fixed function view space):
	        
			    Z
	           /
	          /
    X________/
	         |
	         |
	         |
		     Y

	 */

	/*
	 mat_to_left_handled is a linear transformation

	 e.g.

						Z   Y
						|  /
	flat_sky_far_left	| /		flat_sky_far_right
						|/________X

	 // flat_sky_far_right -> 
	 (  163840.0,   163840.0,    24576.0) -> ( -163840.0,   -24576.0,   163840.0)
	 
	 // flat_sky_far_left  ->
	 ( -163840.0,   163840.0,    24576.0) -> (  163840.0,   -24576.0,   163840.0)

	 z: changed to forward distance

				Z
			   /
			  /
	X________/
			 |
			 |
			 |
			 Y

	 */


	// flat_sky_far_right, flat_sky_far_left defined in flat_sky_plane space
	//  * mat_to_left_handed transform back into view (mat_rot defined) space

	glm::vec3 left_handed_flat_sky_far_right = mat_to_left_handed * flat_sky_far_right;
	glm::vec3 left_handed_flat_sky_far_left  = mat_to_left_handed * flat_sky_far_left;

	if (left_handed_flat_sky_far_right.z >= WORLD_Z_NEAR && left_handed_flat_sky_far_left.z >= WORLD_Z_NEAR) {

		// insure not clipped by near plane
		// insured z is positive

		glm::vec2 vec2_flat_sky_far_right, vec2_flat_sky_far_left;

		/*
		
						Z
					   /
					  /
			X________/
					 |
					 |
					 |
					 Y

			->

		   X________
					|
					|
					|
					Y


			e.g.

			( -163840.0,   -24576.0,   163840.0) -> ( -7.111099,  58.933334)
			(  163840.0,   -24576.0,   163840.0) -> (207.111099,  58.933334)

		 */

		Vec3ToScreenVec2(*params.vd_, left_handed_flat_sky_far_right, vec2_flat_sky_far_right);
		Vec3ToScreenVec2(*params.vd_, left_handed_flat_sky_far_left,  vec2_flat_sky_far_left);

		/*
		      vec2_vertical_dir
		  X<---------------------|
		          /|\            |
		           |             |	
		   <-------              | vec2_flat_sky_far_left - vec2_flat_sky_far_right
								 |
								 |  viewport space (Y downward)
								 |
								 Y


			=> flip horizontal

						  vec2_vertical_dir
		    ---------------------X
			|	  /|\            |
			|	   |             |
		    |       ------->     | vec2_flat_sky_far_left - vec2_flat_sky_far_right
			|					 |
			|					 | viewport space (Y downward)
			|					 |
			|					 
			Y

		 */

		glm::vec2 vec2_horizontal_dir = glm::normalize(vec2_flat_sky_far_left - vec2_flat_sky_far_right);

		// vector orthognal to vec2_horizontal_dir
		glm::vec2 vec2_vertical_dir(vec2_horizontal_dir[1], -vec2_horizontal_dir[0]);

		// half point
		//  e.g.  lf: (0, 200), rt: (800, 200)
		//      = ((0, 200) - (800, 200)) * 0.5 + (800, 200)
		//      = (-800, 0) * 0.5 + (800, 200)
		//      = (-400, 0) + (800, 200)
		//      = (400, 200)
		glm::vec2 vec2_horizontal_ctr = (vec2_flat_sky_far_left - vec2_flat_sky_far_right) * 0.5f + vec2_flat_sky_far_right;

		float max_proj_length_along_vec2_vertical_dir = -1.0f;	// init to a negative value
		float prj_min_x_in_viewport =  100000.0f;
		float prj_max_x_in_viewport = -100000.0f;

		/*
		
		  vec2_viewport_corner

		    idx 0       idx 1
			(0,0)      (w,0)
			   ____________________ X
			  |          |
			  |          |
			  |__________|
			(0,h)      (w,h)
		    idx 2       idx 3
			  |
			  |
			  Y

		 */

		glm::vec2 vec2_viewport_corner[4] = {
			glm::vec2(0.0f),
			glm::vec2((float)params.vd_->viewport_width_, 0.0f),
			glm::vec2(0.0f, (float)params.vd_->viewport_height_),
			glm::vec2((float)params.vd_->viewport_width_, (float)params.vd_->viewport_height_)
		};

		for (int i = 0; i < 4; ++i) {
			// 4 corner of screen
			glm::vec2 vec2_viewport_corner_to_flat_sky_far_viewspace_ctr = vec2_viewport_corner[i] - vec2_horizontal_ctr;

			/*  e.g.
			
			    c0 ~ c3: 4 corners of viewport
				the cross point: flat sky layer far line center in screen space
			

			      /|\  vec2_vertical_dir
			       |
			    c0____c1
			    |\    /|
				| \  / |
				|__\/__| ____ flat sky layer far line in screen space
				|  /\  |
				| /  \ |
			    |/____\|
			    c2     c3

				if flat sky layer far line outside the viewport


				 _____        flat sky layer far line in screen space (but outside visible area)
				  /|\  vec2_vertical_dir
				   |
				c0____c1
				|\    /|
				| \  / |
				|  \/  |
				|  /\  |
				| /  \ |
				|/____\|
				c2     c3

				all vector vec2_viewport_corner_to_flat_sky_far_viewspace_ctr
				projected onto vec2_vertical_dir will be negative
			
			 */

			float proj_length_to_vertical_dir_in_viewport = glm::dot(vec2_viewport_corner_to_flat_sky_far_viewspace_ctr, vec2_vertical_dir);
			if (max_proj_length_along_vec2_vertical_dir <= proj_length_to_vertical_dir_in_viewport) {
				max_proj_length_along_vec2_vertical_dir = proj_length_to_vertical_dir_in_viewport;
			}

			// vec2_viewport_corner_to_flat_sky_far_viewspace_ctr signed projected length to vec2_horizontal_dir
			float proj_length_to_horizontal_dir_in_viewport = glm::dot(vec2_viewport_corner_to_flat_sky_far_viewspace_ctr, vec2_horizontal_dir);
			if (prj_min_x_in_viewport >= proj_length_to_horizontal_dir_in_viewport)
				prj_min_x_in_viewport = proj_length_to_horizontal_dir_in_viewport;

			if (prj_max_x_in_viewport <= proj_length_to_horizontal_dir_in_viewport) {
				prj_max_x_in_viewport = proj_length_to_horizontal_dir_in_viewport;
			}
		}

		if (max_proj_length_along_vec2_vertical_dir >= 0.0f) {
			// flat sky layer vertical projected length is positve (in viewport area) 

			visible = true;

			vert_flat_sky_layer_s temp_vert[26];

			glm::vec2 vec2_flat_sky_layer_block_delta = vec2_vertical_dir * (max_proj_length_along_vec2_vertical_dir * ONE_OVER_12);
			glm::vec2 vec2_flat_sky_layer_block_left  = vec2_horizontal_ctr + vec2_horizontal_dir * (prj_min_x_in_viewport * 1.001f);
			glm::vec2 vec2_flat_sky_layer_block_right = vec2_horizontal_ctr + vec2_horizontal_dir * (prj_max_x_in_viewport * 1.001f);

			for (int i = 0; i < 13; ++i) {
				// x,y: viewport space coordinate
				temp_vert[i * 2].pos_.x = vec2_flat_sky_layer_block_left.x;
				temp_vert[i * 2].pos_.y = vec2_flat_sky_layer_block_left.y;

				// x,y: viewport space coordinate
				temp_vert[i * 2 + 1].pos_.x = vec2_flat_sky_layer_block_right.x;
				temp_vert[i * 2 + 1].pos_.y = vec2_flat_sky_layer_block_right.y;

				vec2_flat_sky_layer_block_left  += vec2_flat_sky_layer_block_delta;
				vec2_flat_sky_layer_block_right += vec2_flat_sky_layer_block_delta;
			}

			float viewport_ctr_x = params.vd_->viewport_width_  * 0.5f;
			float viewport_ctr_y = params.vd_->viewport_height_ * 0.5f;

			// calculate uv, rgba & RHW
			for (int i = 0; i < 26; ++i) {
				glm::vec3 vert_in_proj_plane;

				/*
				
				  (temp_vert[i].pos_.x - viewport_ctr_x):
				     convert from range [0, viewport_width] to [-viewport_width * 0.5f, viewport_width * 0.5f]

				 */
				vert_in_proj_plane.x = (temp_vert[i].pos_.x - viewport_ctr_x) / params.vd_->half_viewport_width_div_tan_half_fovx_;

				// calculate uv in world space
				vert_in_proj_plane.y = (temp_vert[i].pos_.y - viewport_ctr_y) / params.vd_->half_viewport_height_div_tan_half_fovy_;
				/* same like calculate vert_in_proj_plane.x 
				  
				 */
				
				vert_in_proj_plane.z = 1.0f;	// lie at far frustum plane

				// note: right multiply, apply inverse transform
				glm::vec3 vert_world = vert_in_proj_plane * params.vd_->mat_rot_;

				float scale_back = flat_sky_z_pos_in_world / vert_world.z;

				glm::vec3 vert_world_scaled_back = vert_world * scale_back;

				// scale_ defined in objects.qsc
				temp_vert[i].uv_.s = vert_world_scaled_back.x * scale_ + cur_x_;
				temp_vert[i].uv_.t = vert_world_scaled_back.y * scale_ + cur_y_;

				// pos_.z: distance in rotated coordinate

				/*
				  
					  Z
					  /
					 /
					/________X
					|
					|
					|
					Y

				 */

				// convert z to rot coordinate
				temp_vert[i].pos_.z = params.vd_->mat_rot_[0][2] * vert_world_scaled_back.x +
					                  params.vd_->mat_rot_[1][2] * vert_world_scaled_back.y +
					                  params.vd_->mat_rot_[2][2] * vert_world_scaled_back.z;

				temp_vert[i].clr_ = color_;

				// z >= fog_start
				// fog_start = flat_sky_distance_in_world - (flat_sky_distance_in_world - WORLD_Z_NEAR) * flat_sky_fog_amount
				if (temp_vert[i].pos_.z >= fog_start) {

					float alpha = (flat_sky_distance_in_world / temp_vert[i].pos_.z * fog_start  - fog_start) * one_over_flat_sky_distance_sub_remain_dist;

					/*
					 equal to:

					 1):
					   (flat_sky_distance_in_world / temp_vert[i].pos_.z * fog_start  - fog_start) * one_over_flat_sky_distance_sub_remain_dist

					   (fog_start / temp_vert[i].pos_.z) * ( (flat_sky_distance_in_world - temp_vert[i].pos_.z) * one_over_flat_sky_distance_sub_remain_dist )

					 */

					alpha = (fog_start / temp_vert[i].pos_.z) * ((flat_sky_distance_in_world - temp_vert[i].pos_.z) * one_over_flat_sky_distance_sub_remain_dist);

					// (fog_start / temp_vert[i].pos_.z) as an adjust factor

					// if (i % 2) {
					//	printf("%02d: alpha = %f\n", i, alpha);
					//}

					temp_vert[i].clr_.a = alpha;

					// clamp
					if (alpha >= 0.0f) {
						if (alpha > MAX_ALPHA) {
							temp_vert[i].clr_.a = MAX_ALPHA;
						}
					}
					else {
						// alpha < 0.0
						temp_vert[i].clr_.a = 0.0f;
					}

				}
				else {
					// < fog_start, no fog, set full color
					temp_vert[i].clr_.a = 1.0f;
				}

				temp_vert[i].clr_.a *= color_.a;	// multiply by color_.a, make it transparency
				temp_vert[i].pos_.z *= 16.0f;		// scale up by 16


				if (temp_vert[i].pos_.z > 0.0f) {
					float render_cur_z_clamped = params.vd_->render_z_near_ /* 0.4096f */ * 1.01f;
					float render_cur_z = temp_vert[i].pos_.z * RENDERER_MODEL_SCALE_DOWN;

					// clamp z to range [NEAR_PLANE, FAR_PLANE]
					//  which is [0.4096f, 409600.03f]
					if (render_cur_z_clamped <= render_cur_z) {
						// vd.render_z_near_  * 1.01f <= render_cur_z

						if (params.vd_->render_z_far_ >= render_cur_z) {
							// render_cur_z in between vd.render_z_near_ * 1.01f and vd.render_z_far_

							render_cur_z_clamped = render_cur_z;
						}
						else {
							// vd.render_z_far_ < render_cur_z, clamp to vd.render_z_far_
							render_cur_z_clamped = params.vd_->render_z_far_;	// 409600.03125f
						}
					}
					// else: vd.render_z_near_  * 1.01f > render_cur_z, use vd.render_z_near_  * 1.01f

					/* 
					   3d point (x,y,z) after perspective projection
					   output 4d point (x',y',z',w)

					   for right-handed perspective projection matrix:
					     the w == -z (z value before apply perspective projection)

					   for left-handed perspective projection matrix:
					     the w == z  (z value before apply perspective projection)
					
					 */

					// The "reciprocal" of a number just means 1 divided by that number, 
					//   so RHW = 1/W.

					// D3D7: RHW = 1.0f /  render_cur_z_clamped
					// GL:   RHW = 1.0f / -render_cur_z_clamped
					float RHW = 1.0f / -render_cur_z_clamped;

					temp_vert[i].pos_.w = RHW;

					float interpolation = (1.0f - RHW * params.vd_->render_z_near_) * z_far_div_z_far_sub_z_near;

					// z store the depth value
					//    interpolation factor between MIN_DEPTH_VALUE & MAX_DEPTH_VALUE
					float depth = (params.vd_->render_max_depth_ - params.vd_->render_min_depth_) * interpolation + params.vd_->render_min_depth_;
					temp_vert[i].pos_.z = depth;
				}
				else {
					// temp_vert[i].pos_.z <= 0.0f
					temp_vert[i].pos_.z = 0.5f;
					temp_vert[i].pos_.w = 1.0f / ((params.vd_->render_z_far_ - params.vd_->render_z_near_) * 0.5f);
				}
			}

			// temp
			for (int i = 0; i < 12; ++i) {
				// 24 ---- 25
				// 22 ---- 23
				// ...
				//  4 ----  5
				//  2 ----  3
				//  0 ----  1

				int temp_vert_idx0 = i * 2;
				int temp_vert_idx1 = temp_vert_idx0 + 1;
				int temp_vert_idx2 = temp_vert_idx0 + 2;
				int temp_vert_idx3 = temp_vert_idx2 + 1;

				vert_flat_sky_layer_s * dst = dst_vert + i * 9;

				dst[0] = temp_vert[temp_vert_idx0];
				dst[1] = temp_vert[temp_vert_idx1];
				dst[2] = temp_vert[temp_vert_idx2];

				dst[3] = temp_vert[temp_vert_idx3];
				dst[4] = temp_vert[temp_vert_idx2];
				dst[5] = temp_vert[temp_vert_idx1];

				dst[6] = temp_vert[temp_vert_idx1];
				dst[7] = temp_vert[temp_vert_idx2];
				dst[8] = temp_vert[temp_vert_idx2];
			}

			// need reverse y: GL diff to D3D
			for (int i = 0; i < NUM_VERTEX_PER_FLAT_SKY_LAYER; ++i) {
				dst_vert[i].pos_.y = params.vd_->viewport_height_ - dst_vert[i].pos_.y;
				dst_vert[i].uv_.t *= -1.0f;
			}
		}

	}

	return visible;
}
