/******************************************************************************
 * @file    terrain_mat.frag
 * @brief   fragment shader of terrain material
 *****************************************************************************/

#version 120

// GLSL 1.20 port of shaders/41/terrain_mat.frag
// terrain_passing.vert already names its outputs vs_out_pos_in_view /
// vs_out_tex_alpha / vs_out_uv, so the varying names are unchanged (they agree).

uniform sampler2D g_tex;

varying vec3  vs_out_pos_in_view;
varying float vs_out_tex_alpha;
varying vec2  vs_out_uv;

void main() {
	gl_FragColor = texture2D(g_tex, vs_out_uv) * vs_out_tex_alpha;
}
