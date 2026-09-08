#ifndef FC_COLOR_SH
#define FC_COLOR_SH

/*
 * Authored colour -> linear light (Render::OutputConfig).
 *
 * A colour picked in the Appearance dialog is a DISPLAY number: it is
 * what the screen should show, which makes it sRGB-encoded. The shading
 * here is linear -- mix(), the GGX lobe, the IBL product are arithmetic
 * on light -- so an authored colour has to be decoded on the way in,
 * exactly as the finished frame is encoded on the way out
 * (fs_fc_present). Decode in, encode out: an UNSHADED authored colour
 * then survives the round trip unchanged, and only the shading itself
 * moves -- which is the half that was wrong.
 *
 * Colours arrive here in two shapes and each is decoded exactly once:
 *
 * - As UNIFORMS (the material's own colours, the lights, the
 *   background), decoded in C++ where they are unpacked -- full float
 *   precision, once per draw rather than once per fragment.
 * - As the 8-bit VERTEX streams (v_color0-2), decoded here, in the
 *   VERTEX stage. Not in C++: re-packing a decoded colour back into 8
 *   bits would spend most of the dark end's precision, and sRGB exists
 *   precisely because 8 bits are not enough for linear light.
 *   Interpolating the decoded value is also the more correct of the two
 *   orders.
 *
 * ! ALPHA IS NEVER DECODED. It is coverage on v_color0, and on the
 *   per-face streams it is not even that: v_color2.w carries the
 *   shininess and, for a PBR-mode appearance, the metalness. Those are
 *   factors, not light.
 */

// x != 0 -> authored colours are sRGB-encoded and must be decoded.
// Zero is the unmanaged pipeline every frame before this was drawn with,
// and what OutputConfig::None keeps.
uniform vec4 u_colorSpace;

/// sRGB -> linear (IEC 61966-2-1), the inverse of fs_fc_present's
/// encode. The linear segment below the knee matters at the dark end a
/// CAD scene's shadow terms live in.
vec3 fcDecodeSRGB(vec3 c)
{
	vec3 lo = c / 12.92;
	// The max() is not slack in the curve: BOTH branches of the mix are
	// evaluated, pow() of a negative is undefined, and a NaN survives
	// being multiplied by the step's zero weight -- so without it a
	// colour below -0.055 does not take the linear segment, it poisons
	// the fragment. The clamp cannot move an in-range colour, whose
	// argument here is already >= 0.05, and the encode side has always
	// guarded its own pow this way.
	vec3 hi = pow(max((c + 0.055) / 1.055, vec3_splat(0.0)),
	              vec3_splat(2.4));
	return mix(hi, lo, step(c, vec3_splat(0.04045)));
}

/// One authored colour, decoded if the pipeline is colour managed.
vec3 fcAuthoredColor(vec3 c)
{
	return u_colorSpace.x > 0.5 ? fcDecodeSRGB(c) : c;
}

/// The same for a colour riding a vec4 whose alpha is NOT light.
vec4 fcAuthoredColor4(vec4 c)
{
	return vec4(fcAuthoredColor(c.rgb), c.a);
}

#endif // FC_COLOR_SH
