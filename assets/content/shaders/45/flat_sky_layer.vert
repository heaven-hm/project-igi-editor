/******************************************************************************
 * @file    flat_sky_layer.vert
 * @brief   vertex shader of flat sky layer
 *****************************************************************************/

#version 450 core

layout (std140, binding = 0) uniform ub_mats
{
	mat4			g_mvp_mat_follow_view;
	mat4			g_mvp_flat_sky_layer;
	mat4			g_mvp_objects;
};

layout (location = 0) in  vec4 vs_in_pos;	// screen_x, screen_y, z, RHW
layout (location = 1) in  vec2 vs_in_texcoord;
layout (location = 2) in  vec4 vs_in_clr;

layout (location = 0) noperspective out vec3 vs_interpolated_uvw;
layout (location = 1) out vec4 vs_out_clr;

void main() {
	gl_Position = g_mvp_flat_sky_layer * vec4(vs_in_pos.x, vs_in_pos.y, 0.0, 1.0);

	float rhw = vs_in_pos.w;

	// since we are using orthographic projection
	// we need to calculate perspecitve correct texture coordinates
	// Manually divied by W (not gl_Position.w but passed RHW value)
	vs_interpolated_uvw.xy = vs_in_texcoord.xy * rhw;

    vs_interpolated_uvw.z = rhw;

	vs_out_clr = vs_in_clr;
}
