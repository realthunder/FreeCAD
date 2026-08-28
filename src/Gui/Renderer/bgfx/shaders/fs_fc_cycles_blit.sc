$input v_texcoord0

/*
 * The Cycles viewport blit (vs_fc_comp pair, docs/CyclesIntegration.md
 * sec 5.2): the path tracer's half-float frame, premultiplied and
 * LINEAR, drawn as one quad over the host's scene target with depth
 * off. u_cyclesBlit.x is 1 when the host target holds display-encoded
 * colour (a frame without the colour-managed output transform), in
 * which case the linear light is encoded here; a linear target takes
 * it as it is and the host's output transform encodes once.
 */

#include <bgfx_shader.sh>

SAMPLER2D(s_cyclesImage, 0);
uniform vec4 u_cyclesBlit;

vec3 fcCyclesEncode(vec3 c)
{
	vec3 lo = c * 12.92;
	vec3 hi = 1.055 * pow(max(c, vec3_splat(0.0)), vec3_splat(1.0 / 2.4)) - 0.055;
	return mix(lo, hi, step(vec3_splat(0.0031308), c));
}

void main()
{
	vec4 c = texture2D(s_cyclesImage, v_texcoord0);
	if (u_cyclesBlit.x > 0.5) {
		// Un-premultiply, encode, re-premultiply: the blend is
		// one / one-minus-src-alpha either way.
		float a = max(c.a, 1e-5);
		c.rgb = fcCyclesEncode(clamp(c.rgb / a, 0.0, 1.0)) * c.a;
	}
	gl_FragColor = c;
}
