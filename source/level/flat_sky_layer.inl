/******************************************************************************
 * @file    flat_sky_layer.inl
 * @brief   flat sky layer
 *****************************************************************************/

constexpr float MAX_ALPHA = 0.999f;
constexpr float ONE_OVER_12 = 1.0f / 12.0f;

// vec3_in: 
static void Vec3ToScreenVec2(const view_define_s& vd,
	const glm::vec3& vec3_in, glm::vec2& vec2_out)
{
	/*
	   vec3_in defined in coordinate (left-handled), vec3_in.z >= WORLD_Z_NEAR:

					Z
				   /
				  /
		X________/
				 |
				 |
				 |
				 Y

	 */

	float one_over_z = 1.0f / vec3_in.z;

	vec2_out.x = vec3_in.x * one_over_z * vd.half_viewport_width_div_tan_half_fovx_ + vd.viewport_width_ * 0.5f;
	/*

	 NOTE: we view vec3_in from origin


	  vec3_in
	 \_|__  ---- vec3_in.z * tan(fovx * 0.5) occupy half viewport
	  \   |
	   \  | ____ angle: fovx * 0.5
		\ |/
		 \|
		viewer (at origin)

	 equal to:

	 1):
	   (vec3_in.x / vec3_in.z) * ((vd.viewport_width_ * 0.5) / tan(fovx * 0.5)) + vd.viewport_width_ * 0.5f

	 2):
	   (vec3_in.x * (vd.viewport_width_ * 0.5)) / (vec3_in.z * tan(fovx * 0.5)) + vd.viewport_width_ * 0.5f

	 3):
	   (  vec3_in.x / (vec3_in.z * tan(fovx * 0.5))  ) * (vd.viewport_width_ * 0.5) + vd.viewport_width_ * 0.5f

	   NOTE: (vec3_in.z * tan(fovx * 0.5)) is the X value that occupy half the viewport

	   so: (  vec3_in.x / (vec3_in.z * tan(fovx * 0.5))  ) is the portion vec3_in.x occupy half the viewport (signed)

	   finally we add:   + vd.viewport_width_ * 0.5f
	   convert vec2_out.x
		 from range (-vd.viewport_width_ * 0.5f, vd.viewport_width_ * 0.5f)
		 to   range (0.0, vd.viewport_width_)

	 */

	vec2_out.y = vec3_in.y * one_over_z * vd.half_viewport_height_div_tan_half_fovy_ + vd.viewport_height_ * 0.5f;

	/* same as calculate vec2_out.x

	   because Y axis is downward, same as viewport coordinate,
	   we do not need to reverse vec3_in.y

	 */
}
