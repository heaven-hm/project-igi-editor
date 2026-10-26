/******************************************************************************
 * @file    terrain_lmp.frag
 * @brief   fragment shader of terrain light map
 *****************************************************************************/

#version 120

// GLSL 1.20 port of shaders/41/terrain_lmp.frag
// terrain_passing.vert already names its outputs vs_out_pos_in_view /
// vs_out_tex_alpha / vs_out_uv, so the varying names are unchanged (they agree).

uniform sampler2D g_tex;

varying vec3  vs_out_pos_in_view;
varying float vs_out_tex_alpha;	// not used
varying vec2  vs_out_uv;

void main() {
	vec4 tex_color = texture2D(g_tex, vs_out_uv);

	// avoid the surface get too dark
	float f = 0.333333 + 0.666667 * tex_color.x;

	gl_FragColor = vec4(f);
}
