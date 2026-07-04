/******************************************************************************
 * @file    terrain_fog.frag
 * @brief   vertex shader to apply fog
 *****************************************************************************/

#version 450 core

layout (std140, binding = 0) uniform ub_mats
{
	mat4			g_mvp_mat_follow_view;
	mat4			g_mvp_flat_sky_layer;
	mat4			g_mvp_objects;
};

layout (location = 0) in vec3  vs_in_pos_local;

layout (location = 0) out vec3 vs_out_pos_in_view;

void main() {
	vec4 v4 = vec4(vs_in_pos_local, 1.0);

	gl_Position = g_mvp_objects * v4;
	vs_out_pos_in_view = (g_mvp_objects * v4).xyz;
}
