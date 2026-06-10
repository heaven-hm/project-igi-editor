/******************************************************************************
 * @file    flat_sky_layer.vert
 * @brief   vertex shader of flat sky layer (GLSL 1.20 / OpenGL 2.1 legacy)
 *****************************************************************************/

#version 120

uniform mat4 g_mvp_mat_follow_view;
uniform mat4 g_mvp_flat_sky_layer;
uniform mat4 g_mvp_objects;

attribute vec4 vs_in_pos;       // screen_x, screen_y, z, RHW
attribute vec2 vs_in_texcoord;
attribute vec4 vs_in_clr;

varying vec3 vs_interpolated_uvw;
varying vec4 vs_out_clr;

void main() {
    gl_Position = g_mvp_flat_sky_layer * vec4(vs_in_pos.x, vs_in_pos.y, 0.0, 1.0);

    float rhw = vs_in_pos.w; // RHW from old Direct3D7 API

    // Perspective-correct UVs: manually divide by W
    vs_interpolated_uvw.xy = vs_in_texcoord.xy * rhw;
    vs_interpolated_uvw.z  = rhw;

    vs_out_clr = vs_in_clr;
}
