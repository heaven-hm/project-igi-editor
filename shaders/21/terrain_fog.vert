/******************************************************************************
 * @file    terrain_fog.frag
 * @brief   vertex shader to apply fog
 *****************************************************************************/

#version 120

// GLSL 1.20 port of shaders/41/terrain_fog.vert
// The ub_mats uniform block is replaced by three plain file-scope uniforms of
// the same name/type/order (no UBO support on a legacy 2.1 context). The Lead
// looks these up by name with glGetUniformLocation.

uniform mat4 g_mvp_mat_follow_view;
uniform mat4 g_mvp_flat_sky_layer;
uniform mat4 g_mvp_objects;

// Attribute locations are bound from C++ with glBindAttribLocation (1.20 has
// no `layout (location = N)`); the original index is kept:
attribute vec3  vs_in_pos_local;	// location = 0

varying vec3 vs_out_pos_in_view;

void main() {
	vec4 v4 = vec4(vs_in_pos_local, 1.0);

	gl_Position = g_mvp_objects * v4;
	vs_out_pos_in_view = (g_mvp_objects * v4).xyz;
}
