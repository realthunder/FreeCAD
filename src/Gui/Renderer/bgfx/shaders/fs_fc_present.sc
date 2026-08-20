$input v_texcoord0

/*
 * The output colour transform (Render::OutputConfig): the last thing
 * that happens to a frame before it is shown.
 *
 * Everything upstream of this pass is LINEAR light -- the BRDF, the
 * image based lighting product, every blend and the weighted-blended
 * transparency composite are plain arithmetic, and plain arithmetic on
 * light is only correct on linear numbers. A display is not linear: it
 * reads the byte it is handed as sRGB. So the linear frame has to be
 * encoded exactly once, here, at the point it stops being light and
 * becomes pixels -- doing it any earlier would make every blend after
 * it wrong, and doing it in the material data would trade a correct
 * metal reflectance table for a wrong one.
 *
 * u_outputParams.x selects the transform (OutputConfig::Transform);
 * a value this build does not know passes the frame through, which is
 * how a scene written by a later build degrades. .y is the
 * exposure (fcExpose).
 */

#include <bgfx_shader.sh>

SAMPLER2D(s_texScene, 0);

uniform vec4 u_outputParams;

#define FC_OUTPUT_NONE 0.0
#define FC_OUTPUT_SRGB 1.0

/// Linear -> sRGB (IEC 61966-2-1). The linear segment below the knee is
/// not a rounding detail: a pure power there has an infinite slope at
/// zero, which crushes the near-black end a CAD scene spends its shadow
/// terms in.
vec3 fcEncodeSRGB(vec3 c)
{
	c = clamp(c, 0.0, 1.0);
	vec3 lo = c * 12.92;
	vec3 hi = 1.055 * pow(c, vec3_splat(1.0 / 2.4)) - 0.055;
	return mix(hi, lo, step(c, vec3_splat(0.0031308)));
}

/// Apply the exposure without clipping what it lifts.
///
/// A plain multiply would flatten everything it pushes past one to
/// white, which is the opposite of what an exposure control is for, so
/// the multiply is paired with a compression curve -- Reinhard,
/// v / (1 + s*v), with the strength s tied to the exposure itself:
///
///   s = max(0, 1 - 1/exposure)
///
/// That choice makes the family behave at both ends. At an exposure of
/// one s is zero and the curve is the EXACT identity, which is what
/// keeps this stage from touching a frame that asked for nothing and
/// what lets an unshaded authored colour still survive the round trip.
/// Below one s is still zero -- darkening cannot overflow, so it stays
/// a plain multiply. And for any exposure the curve maps one to one
/// exactly, so the white point never moves: raising the exposure lifts
/// the midtones toward the highlights rather than dragging the
/// highlights off the top.
vec3 fcExpose(vec3 c, float exposure)
{
	float s = max(0.0, 1.0 - 1.0 / max(exposure, 1.0e-6));
	vec3 v = c * exposure;
	return v / (vec3_splat(1.0) + s * v);
}

void main()
{
	vec4 scene = texture2D(s_texScene, v_texcoord0);
	float mode = u_outputParams.x;
	// Alpha is coverage, never light: neither exposed nor encoded.
	if (mode > FC_OUTPUT_SRGB - 0.5 && mode < FC_OUTPUT_SRGB + 0.5)
	{
		// Exposure is a multiplier on LIGHT, so it belongs here, on the
		// linear image, ahead of the encode -- and only here, because
		// the same multiply applied to display numbers would not be an
		// exposure at all.
		scene.rgb = fcExpose(scene.rgb, u_outputParams.y);
		scene.rgb = fcEncodeSRGB(scene.rgb);
	}
	gl_FragColor = scene;
}
