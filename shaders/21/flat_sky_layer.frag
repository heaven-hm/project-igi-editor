/******************************************************************************
 * @file    flat_sky_layer.frag
 * @brief   fragment shader of flat sky layer
 *****************************************************************************/

#version 120

// GLSL 1.20 port of shaders/41/flat_sky_layer.frag
// Varyings match BY NAME in 1.20, so the 410 inputs are renamed to the vertex
// stage's actual output names:
//   ps_interpolated_uvw -> vs_interpolated_uvw
//   ps_in_clr           -> vs_out_clr

uniform sampler2D g_tex;

// BEHAVIOURAL DIVERGENCE: the 410 original declares vs_interpolated_uvw with
// the `noperspective` qualifier, which does not exist in GLSL 1.20. The
// qualifier is dropped, so this varying is interpolated smoothly (default
// perspective-correct interpolation) instead of screen-linearly.
varying vec3 vs_interpolated_uvw;	// renamed from ps_interpolated_uvw
varying vec4 vs_out_clr;		// renamed from ps_in_clr

void main() {
	vec2 corrected_uv = vs_interpolated_uvw.xy / vs_interpolated_uvw.z;

	vec4 tex_color = texture2D(g_tex, corrected_uv);

	// modulate rgb & a
	gl_FragColor = tex_color * vs_out_clr;
}
