/******************************************************************************
 * @file    terrain_passing.vert
 * @brief   common vertex shader for terrain (GL 3.3 Core - Wine/Mac/Linux compatible)
 *****************************************************************************/

#version 330 core

uniform ub_mats {
	mat4			g_mvp_mat_follow_view;
	mat4			g_mvp_flat_sky_layer;
	mat4			g_mvp_objects;
};

layout (location = 0) in  vec4  vs_in_pos_and_alpha;
layout (location = 1) in  vec2  vs_in_uv;

layout (location = 0) out vec3  vs_out_pos_in_view;
layout (location = 1) out float vs_out_tex_alpha;
layout (location = 2) out vec2  vs_out_uv;

void main() {
	vec4 v4 = vec4(vs_in_pos_and_alpha.xyz, 1.0);

	gl_Position = g_mvp_objects * v4;
	vs_out_pos_in_view = (g_mvp_objects * v4).xyz;

	vs_out_tex_alpha = vs_in_pos_and_alpha.w;
	vs_out_uv.x = vs_in_uv.x;
	// flip texture y coordinate for GL
	vs_out_uv.y = 1.0 - vs_in_uv.y;
}
