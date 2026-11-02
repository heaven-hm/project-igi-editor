/******************************************************************************
 * @file    terrain_wireframe.frag
 * @brief   fragment shader of terrain in wireframe mode
 *****************************************************************************/

#version 120

// GLSL 1.20 port of shaders/41/terrain_wireframe.frag
// terrain_passing.vert already names its outputs vs_out_pos_in_view /
// vs_out_tex_alpha / vs_out_uv, so the varying names are unchanged (they agree).
// They stay declared (and unused), as in the original.

varying vec3  vs_out_pos_in_view;
varying float vs_out_tex_alpha;
varying vec2  vs_out_uv;

void main() {
	gl_FragColor = vec4(0.1, 0.1, 0.1, 1.0);
}
