/******************************************************************************
 * @file    terrain_fog.vert
 * @brief   vertex shader to apply fog (GLSL 1.20 / OpenGL 2.1 legacy)
 *****************************************************************************/

#version 120

uniform mat4 g_mvp_mat_follow_view;
uniform mat4 g_mvp_flat_sky_layer;
uniform mat4 g_mvp_objects;

attribute vec3 vs_in_pos_local;

varying vec3 vs_out_pos_in_view;

void main() {
    vec4 v4 = vec4(vs_in_pos_local, 1.0);

    gl_Position        = g_mvp_objects * v4;
    vs_out_pos_in_view = (g_mvp_objects * v4).xyz;
}
