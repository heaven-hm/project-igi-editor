/******************************************************************************
 * @file    flat_sky_layer_wireframe.frag
 * @brief   fragment shader of flat sky layer in wireframe mode
 *****************************************************************************/

#version 120

// GLSL 1.20 port of shaders/41/flat_sky_layer_wireframe.frag
// The single `layout (location = 0) out vec4 ps_out_clr;` output is replaced by
// the built-in gl_FragColor.

void main() {
	gl_FragColor = vec4(0.1, 0.1, 0.1, 1.0);
}
