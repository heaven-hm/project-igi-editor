/******************************************************************************
 * @file    terrain_fog.vert
 * @brief   vertex shader to apply fog
 *
 * Only gl_Position is produced: the fragment shader recovers the view depth from
 * gl_FragCoord.w (= 1 / gl_Position.w), which is the engine's `vz * gWorldScale`.
 * The old `vs_out_pos_in_view` varying was the CLIP-space position, and the
 * fragment shader's `length()` of it was not a distance in any usable unit --
 * see shaders/41/terrain_fog.frag.
 *****************************************************************************/

#version 450 core

layout (std140, binding = 0) uniform ub_mats
{
	mat4			g_mvp_mat_follow_view;
	mat4			g_mvp_flat_sky_layer;
	mat4			g_mvp_objects;
};

layout (location = 0) in vec3  vs_in_pos_local;

void main() {
	gl_Position = g_mvp_objects * vec4(vs_in_pos_local, 1.0);
}
