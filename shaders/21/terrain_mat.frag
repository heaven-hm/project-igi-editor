/******************************************************************************
 * @file    terrain_mat.frag
 * @brief   fragment shader of terrain material (GLSL 1.20 / OpenGL 2.1 legacy)
 *****************************************************************************/

#version 120

uniform sampler2D g_tex;

varying vec3  vs_out_pos_in_view;
varying float vs_out_tex_alpha;
varying vec2  vs_out_uv;

void main() {
    gl_FragColor = texture2D(g_tex, vs_out_uv) * vs_out_tex_alpha;
}
