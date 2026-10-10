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

vec3 fcCyclesDecode(vec3 c)
{
	vec3 lo = c / 12.92;
	// Guarded like fcCyclesEncode above, and for the same reason: both
	// branches evaluate, and pow() of a negative is a NaN the step
	// cannot weight away.
	vec3 hi = pow(max((c + 0.055) / 1.055, vec3_splat(0.0)),
	              vec3_splat(2.4));
	return mix(lo, hi, step(vec3_splat(0.04045), c));
}

void main()
{
	// vs_fc_comp hands over the UV of a RENDER TARGET: fc_clipToUv turns v
	// over wherever a target's first row is its top one, which is every
	// backend but OpenGL. This image is not a target. It is uploaded,
	// bottom row first (FrameImageConsumer), and an uploaded texture's
	// first row is at v = 0 on every backend -- so on Direct3D, Vulkan and
	// Metal the path tracer's picture came out upside down, which for a
	// model with Z up reads as mirrored in the XY plane. The turn is
	// taken back out here, under the very condition it was put in by.
	vec2 uv = v_texcoord0;
#if !BGFX_SHADER_LANGUAGE_GLSL
	uv.y = 1.0 - uv.y;
#endif
	vec4 c = texture2D(s_cyclesImage, uv);
	// Un-premultiply, transform, re-premultiply: the blend is
	// one / one-minus-src-alpha either way.
	if (u_cyclesBlit.x > 0.5) {
		// A linear image for a display-space target.
		float a = max(c.a, 1e-5);
		c.rgb = fcCyclesEncode(clamp(c.rgb / a, 0.0, 1.0)) * c.a;
	}
	else if (u_cyclesBlit.y > 0.5) {
		// An sRGB-encoded image (the streamed frame) for a linear
		// target.
		float a = max(c.a, 1e-5);
		c.rgb = fcCyclesDecode(clamp(c.rgb / a, 0.0, 1.0)) * c.a;
	}
	gl_FragColor = c;
}
