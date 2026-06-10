/******************************************************************************
 * @file    skydome.frag
 * @brief   fragment shader of skydome (GLSL 1.20 / OpenGL 2.1 legacy)
 *****************************************************************************/

#version 120

varying vec3 vs_out_clr;

void main() {
    gl_FragColor = vec4(vs_out_clr, 1.0);
}
