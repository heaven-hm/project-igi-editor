/******************************************************************************
 * @file    flat_sky_layer_wireframe.vert
 * @brief   vertex shader of flat sky layer in wireframe mode
 *****************************************************************************/

#version 120

// GLSL 1.20 port of shaders/41/flat_sky_layer_wireframe.vert
// The ub_mats uniform block is replaced by three plain file-scope uniforms of
// the same name/type/order (no UBO support on a legacy 2.1 context). The Lead
// looks these up by name with glGetUniformLocation.

uniform mat4 g_mvp_mat_follow_view;
uniform mat4 g_mvp_flat_sky_layer;
uniform mat4 g_mvp_objects;

// Attribute locations are bound from C++ with glBindAttribLocation (1.20 has
// no `layout (location = N)`); the original index is kept:
attribute vec4 vs_in_pos;	// location = 0

void main() {
	gl_Position = g_mvp_flat_sky_layer * vec4(vs_in_pos.x, vs_in_pos.y, 0.0, 1.0);
}
