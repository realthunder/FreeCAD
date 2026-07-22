$input v_texcoord0

/*
 * SSAO apply pass: multiplies the blurred ambient visibility onto the
 * opaque scene color (blend ZERO / SRC_COLOR, alpha preserved). The
 * background multiplies by 1 and is unchanged.
 */

#include <bgfx_shader.sh>

SAMPLER2D(s_texAO, 0);

void main()
{
	float ao = texture2D(s_texAO, v_texcoord0).x;
	// Ambient occlusion on a lit surface is often a very subtle gradient
	// (a handful of 8-bit levels), which shows as banding on the final
	// 8-bit framebuffer. Break it with an ordered dither on the multiplier
	// (interleaved-gradient noise, ~+-1.5/255): the eye integrates the
	// dithered pixels into a smooth gradient. AO is the blend multiplier
	// (dst *= src), so the scene colour scales the dither with it.
	float ign = fract(52.9829189 *
	                  fract(dot(gl_FragCoord.xy,
	                            vec2(0.06711056, 0.00583715))));
	ao += (ign - 0.5) * (3.0 / 255.0);
	gl_FragColor = vec4(ao, ao, ao, 1.0);
}
