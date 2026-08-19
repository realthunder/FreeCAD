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
 * how a scene written by a later build degrades.
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

void main()
{
	vec4 scene = texture2D(s_texScene, v_texcoord0);
	float mode = u_outputParams.x;
	// Alpha is coverage, never light: it is not encoded.
	if (mode > FC_OUTPUT_SRGB - 0.5 && mode < FC_OUTPUT_SRGB + 0.5)
		scene.rgb = fcEncodeSRGB(scene.rgb);
	gl_FragColor = scene;
}
