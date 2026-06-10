/******************************************************************************
 * @file    flat_sky_layer.frag
 * @brief   fragment shader of flat sky layer (GLSL 1.20 / OpenGL 2.1 legacy)
 *****************************************************************************/

#version 120

uniform sampler2D g_tex;

varying vec3 vs_interpolated_uvw;
varying vec4 vs_out_clr;

void main() {
    vec2 corrected_uv = vs_interpolated_uvw.xy / vs_interpolated_uvw.z;
    vec4 tex_color    = texture2D(g_tex, corrected_uv);
    // modulate rgb & a
    gl_FragColor = tex_color * vs_out_clr;
}
