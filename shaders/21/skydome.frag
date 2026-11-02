/******************************************************************************
 * @file    skydome.frag
 * @brief   fragment shader of skydome
 *****************************************************************************/

#version 120

// GLSL 1.20 port of shaders/41/skydome.frag
// Varyings match BY NAME in 1.20, so the 410 input `ps_in_clr` (matched to the
// vertex stage only by explicit layout location) is renamed to the vertex
// stage's actual output name, `vs_out_clr`.

varying vec3 vs_out_clr;	// renamed from ps_in_clr

void main() {
	gl_FragColor = vec4(vs_out_clr, 1.0);
}
