/******************************************************************************
 * @file    terrain_lmp.frag
 * @brief   fragment shader of terrain light map (GLSL 1.20 / OpenGL 2.1 legacy)
 *****************************************************************************/

#version 120

uniform sampler2D g_tex;

varying vec3  vs_out_pos_in_view;
varying float vs_out_tex_alpha;   // not used
varying vec2  vs_out_uv;

void main() {
    vec4  tex_color = texture2D(g_tex, vs_out_uv);
    // avoid the surface getting too dark
    float f = 0.333333 + 0.666667 * tex_color.x;
    gl_FragColor = vec4(f, f, f, f);
}
