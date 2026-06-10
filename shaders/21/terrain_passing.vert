/******************************************************************************
 * @file    terrain_passing.vert
 * @brief   common vertex shader for terrain (GLSL 1.20 / OpenGL 2.1 legacy)
 *****************************************************************************/

#version 120

uniform mat4 g_mvp_mat_follow_view;
uniform mat4 g_mvp_flat_sky_layer;
uniform mat4 g_mvp_objects;

attribute vec4 vs_in_pos_and_alpha;
attribute vec2 vs_in_uv;

varying vec3  vs_out_pos_in_view;
varying float vs_out_tex_alpha;
varying vec2  vs_out_uv;

void main() {
    vec4 v4 = vec4(vs_in_pos_and_alpha.xyz, 1.0);

    gl_Position        = g_mvp_objects * v4;
    vs_out_pos_in_view = (g_mvp_objects * v4).xyz;

    vs_out_tex_alpha = vs_in_pos_and_alpha.w;
    vs_out_uv.x      = vs_in_uv.x;
    // flip texture y coordinate for GL
    vs_out_uv.y      = 1.0 - vs_in_uv.y;
}
