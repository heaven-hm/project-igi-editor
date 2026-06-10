/******************************************************************************
 * @file    terrain_wireframe.frag
 * @brief   fragment shader of terrain wireframe (GLSL 1.20 / OpenGL 2.1 legacy)
 *****************************************************************************/

#version 120

varying vec3  vs_out_pos_in_view;
varying float vs_out_tex_alpha;
varying vec2  vs_out_uv;

void main() {
    gl_FragColor = vec4(0.1, 0.1, 0.1, 1.0);
}
