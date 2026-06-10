/******************************************************************************
 * @file    skydome.vert
 * @brief   vertex shader of skydome (GL 3.3 Core - Wine/Mac/Linux compatible)
 *****************************************************************************/

#version 330 core

uniform ub_mats {
	mat4			g_mvp_mat_follow_view;
	mat4			g_mvp_flat_sky_layer;
	mat4			g_mvp_objects;
};

layout (location = 0) in  vec3 vs_in_pos;
layout (location = 1) in  vec3 vs_in_clr;

layout (location = 0) out vec3 vs_out_clr;

void main() {
	gl_Position = g_mvp_mat_follow_view * vec4(vs_in_pos, 1.0);
	vs_out_clr = vs_in_clr;
}
