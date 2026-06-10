/******************************************************************************
 * @file    skydome.vert
 * @brief   vertex shader of skydome (GLSL 1.20 / OpenGL 2.1 legacy)
 *****************************************************************************/

#version 120

uniform mat4 g_mvp_mat_follow_view;
uniform mat4 g_mvp_flat_sky_layer;
uniform mat4 g_mvp_objects;

attribute vec3 vs_in_pos;
attribute vec3 vs_in_clr;

varying vec3 vs_out_clr;

void main() {
    gl_Position = g_mvp_mat_follow_view * vec4(vs_in_pos, 1.0);
    vs_out_clr = vs_in_clr;
}
