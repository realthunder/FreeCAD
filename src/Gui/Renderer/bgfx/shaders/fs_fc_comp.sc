$input v_texcoord0

/*
 * Weighted-blended OIT composite (McGuire/Bavoil 2013): averages the
 * accumulated premultiplied color and blends it over the opaque scene
 * by coverage. Blend state: (INV_SRC_ALPHA, SRC_ALPHA) with the
 * revealage product in the output alpha — background shows through
 * exactly where every transparent layer lets it.
 */

#include <bgfx_shader.sh>

SAMPLER2D(s_texAccum, 0);
SAMPLER2D(s_texReveal, 1);

void main()
{
	vec4 accum = texture2D(s_texAccum, v_texcoord0);
	float reveal = texture2D(s_texReveal, v_texcoord0).x;
	gl_FragColor = vec4(accum.rgb / max(accum.a, 1.0e-5), reveal);
}
