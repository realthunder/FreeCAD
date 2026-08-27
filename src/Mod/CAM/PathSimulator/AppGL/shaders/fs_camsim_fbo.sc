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
 *
 * u_simComposite.y asks this pass to place the simulator's stock in
 * the destination's depth buffer -- shared with a host scene when
 * attached, so the two occlude each other; the surface's own when
 * standalone, where the tool-path passes test against it
 * (docs/CAMSimRenderPort.md sec 10.3). The simulator's own depth
 * buffer is no use for that -- it is a private frustum, derived from
 * the stock size rather than from the camera -- so the depth is
 * rebuilt from the G-buffer's view-space position through
 * u_simDepthXform, which carries a texel from the simulator's view
 * space all the way into the target camera's clip space (the host's
 * camera attached, the sim's own standalone). .z says which clip
 * convention that lands in (see DrawDevice::homogeneousDepth).
 */

#include <bgfx_shader.sh>

SAMPLER2D(s_simTex, 0);
SAMPLER2D(s_simPosition, 1);

uniform vec4 u_simComposite;
uniform mat4 u_simDepthXform;

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
	if (color.a <= 0.0)
	{
		// Nothing of the simulator here. Discarding rather than
		// blending a transparent texel keeps this pass out of the
		// depth buffer everywhere it has nothing to say.
		discard;
	}
	if (u_simComposite.x > 0.5)
	{
		// Alpha is coverage, never light: it is not decoded.
		color.rgb = camsimDecodeSRGB(color.rgb);
	}
	// Always assigned: a shader that writes gl_FragDepth on only some
	// paths leaves it undefined on the others. The quad's own depth is
	// what the standalone path wants, and there depth writes are off
	// anyway.
	float depth = gl_FragCoord.z;
	if (u_simComposite.y > 0.5)
	{
		vec4 pos = texture2D(s_simPosition, v_texcoord0);
		// Texels the geometry pass never wrote have no position to
		// place and take the far plane. Since the tool path moved to
		// the overlay run (sec 10.4) every covered texel should carry
		// the marker; the guard stays so a future colour-only writer
		// cannot place phantom geometry at the view-space origin.
		depth = 1.0;
		if (pos.w > 0.5)
		{
			vec4 clip = mul(u_simDepthXform, vec4(pos.xyz, 1.0));
			float ndc = clip.z / clip.w;
			depth = u_simComposite.z > 0.5 ? ndc * 0.5 + 0.5 : ndc;
			depth = clamp(depth, 0.0, 1.0);
		}
	}
	gl_FragDepth = depth;
	gl_FragColor = color;
}
