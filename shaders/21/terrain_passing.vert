/******************************************************************************
 * @file    terrain_passing.vert
 * @brief   common vertex shader for terrain
 *****************************************************************************/

#version 120

// GLSL 1.20 port of shaders/41/terrain_passing.vert
// The ub_mats uniform block is replaced by three plain file-scope uniforms of
// the same name/type/order (no UBO support on a legacy 2.1 context). The Lead
// looks these up by name with glGetUniformLocation.

uniform mat4 g_mvp_mat_follow_view;
uniform mat4 g_mvp_flat_sky_layer;
uniform mat4 g_mvp_objects;

// Attribute locations are bound from C++ with glBindAttribLocation (1.20 has
// no `layout (location = N)`); the original indices are kept:
attribute vec4 vs_in_pos_and_alpha;	// location = 0
attribute vec2 vs_in_uv;		// location = 1

varying vec3  vs_out_pos_in_view;
varying float vs_out_tex_alpha;
varying vec2  vs_out_uv;

void main() {
	vec4 v4 = vec4(vs_in_pos_and_alpha.xyz, 1.0);

	gl_Position = g_mvp_objects * v4;
	vs_out_pos_in_view = (g_mvp_objects * v4).xyz;

	vs_out_tex_alpha = vs_in_pos_and_alpha.w;
	vs_out_uv.x = vs_in_uv.x;
	// flip texture y coordinate for GL
	vs_out_uv.y = 1.0 - vs_in_uv.y;
}
