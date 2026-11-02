/******************************************************************************
 * @file    skydome.vert
 * @brief   vertex shader of skydome
 *****************************************************************************/

#version 120

// GLSL 1.20 port of shaders/41/skydome.vert
// The ub_mats uniform block is replaced by three plain file-scope uniforms of
// the same name/type/order (no UBO support on a legacy 2.1 context). The Lead
// looks these up by name with glGetUniformLocation.

uniform mat4 g_mvp_mat_follow_view;
uniform mat4 g_mvp_flat_sky_layer;
uniform mat4 g_mvp_objects;

// Attribute locations are bound from C++ with glBindAttribLocation (1.20 has
// no `layout (location = N)`); the original indices are kept:
attribute vec3 vs_in_pos;	// location = 0
attribute vec3 vs_in_clr;	// location = 1

varying vec3 vs_out_clr;

void main() {
	gl_Position = g_mvp_mat_follow_view * vec4(vs_in_pos, 1.0);
	vs_out_clr = vs_in_clr;
}
