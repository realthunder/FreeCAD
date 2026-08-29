$input v_texcoord0

/*
 * Weighted-blended OIT composite (McGuire/Bavoil 2013): averages the
 * accumulated premultiplied color and blends it over the opaque scene
 * by coverage. The output alpha is the COVERAGE, 1 - revealage, and
 * the blend state is the plain "over": RGB (SRC_ALPHA, INV_SRC_ALPHA),
 * alpha (ONE, INV_SRC_ALPHA) -- background shows through exactly where
 * every transparent layer lets it, and the framebuffer's alpha ends up
 * as the coverage a caller reading it as premultiplied expects.
 *
 * It used to hand back the revealage itself under (INV_SRC_ALPHA,
 * SRC_ALPHA), which is the same colour arithmetic -- but a blend pair
 * applies to alpha too, and that wrote reveal * (1 - reveal) over a
 * transparent background instead of 1 - reveal. Nothing on screen reads
 * alpha, so it only showed where a capture does: the material icons,
 * which un-premultiply by it, came out as flat white discs.
 */

#include <bgfx_shader.sh>

SAMPLER2D(s_texAccum, 0);
SAMPLER2D(s_texReveal, 1);

void main()
{
	vec4 accum = texture2D(s_texAccum, v_texcoord0);
	float reveal = texture2D(s_texReveal, v_texcoord0).x;
	gl_FragColor = vec4(accum.rgb / max(accum.a, 1.0e-5), 1.0 - reveal);
}
