/******************************************************************************
 * @file    skydome_wireframe.frag
 * @brief   fragment shader to draw skydome in wireframe mode
 *****************************************************************************/

#version 120

// GLSL 1.20 port of shaders/41/skydome_wireframe.frag
// Varyings match BY NAME in 1.20, so the 410 input `ps_in_clr` is renamed to
// the skydome.vert output name, `vs_out_clr`. It stays declared (and unused),
// as in the original.

varying vec3 vs_out_clr;	// renamed from ps_in_clr

void main() {
	gl_FragColor = vec4(0.1, 0.1, 0.1, 1.0);
}
