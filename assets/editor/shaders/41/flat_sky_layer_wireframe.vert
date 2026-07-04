/******************************************************************************
 * @file    flat_sky_layer_wireframe.vert
 * @brief   vertex shader of flat sky layer in wireframe mode
 *****************************************************************************/

#version 410 core

uniform ub_mats {
	mat4			g_mvp_mat_follow_view;
	mat4			g_mvp_flat_sky_layer;
	mat4			g_mvp_objects;
};

layout (location = 0) in  vec4 vs_in_pos;

void main() {
	gl_Position = g_mvp_flat_sky_layer * vec4(vs_in_pos.x, vs_in_pos.y, 0.0, 1.0);
}
