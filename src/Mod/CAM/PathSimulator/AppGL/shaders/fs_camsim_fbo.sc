$input v_texcoord0

/*
 * The composite: the simulator's finished image carried into whatever
 * the draw surface composites into (SimPassComposite,
 * docs/CAMSimRenderPort.md sec 8.4).
 *
 * u_simComposite.x is set when that destination holds LINEAR light --
 * the engine's colour-managed RGBA16F scene target, which its present
 * pass exposes and encodes on the way to the screen. The simulator
 * shades in display space, so its colour has to be decoded here or it
 * would go through that encode a second time and come out washed out.
 * Standalone the destination is the surface's own display-space
 * backbuffer and this is a plain copy.
 *
 * Only the decode is undone. The view's exposure still applies, which
 * is the point of putting the image into the scene buffer at all: it
 * is light in there like everything else, and the default exposure of
 * one is the exact identity.
 */

#include <bgfx_shader.sh>

SAMPLER2D(s_simTex, 0);

uniform vec4 u_simComposite;

/// sRGB -> linear (IEC 61966-2-1), the inverse of the engine's
/// fcEncodeSRGB in fs_fc_present.sc. Keep the two in step.
vec3 camsimDecodeSRGB(vec3 c)
{
	vec3 lo = c / 12.92;
	vec3 hi = pow((c + vec3_splat(0.055)) / 1.055, vec3_splat(2.4));
	return mix(hi, lo, step(c, vec3_splat(0.04045)));
}

void main()
{
	vec4 color = texture2D(s_simTex, v_texcoord0);
	if (u_simComposite.x > 0.5)
	{
		// Alpha is coverage, never light: it is not decoded.
		color.rgb = camsimDecodeSRGB(color.rgb);
	}
	gl_FragColor = color;
}
