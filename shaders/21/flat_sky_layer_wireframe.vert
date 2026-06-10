/******************************************************************************
 * @file    flat_sky_layer_wireframe.vert
 * @brief   vertex shader of flat sky layer wireframe (GLSL 1.20 legacy)
 *****************************************************************************/

#version 120

uniform mat4 g_mvp_mat_follow_view;
uniform mat4 g_mvp_flat_sky_layer;
uniform mat4 g_mvp_objects;

attribute vec4 vs_in_pos;

void main() {
    gl_Position = g_mvp_flat_sky_layer * vec4(vs_in_pos.x, vs_in_pos.y, 0.0, 1.0);
}
