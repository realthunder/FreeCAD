$input v_texcoord0

/*
 * Bloom bright pass (quarter resolution): box-downsample the scene
 * color and keep what exceeds the brightness threshold, with a soft
 * quadratic knee below it so the cutoff doesn't shimmer. The
 * light-source emit pass then adds the emitter bodies' HDR color on
 * top of this target before the separable blur.
 *
 * u_bloomParams : x = threshold, y = intensity (applied at composite,
 *                 unused here), z = blur sigma scale (unused here),
 *                 w = soft knee width (fraction of the threshold)
 * u_bloomTexel  : xy = full-res scene texel size, zw = quarter-res
 *                 target texel size
 */

#include <bgfx_shader.sh>

SAMPLER2D(s_texScene, 0);

uniform vec4 u_bloomParams;
uniform vec4 u_bloomTexel;

void main()
{
	// Four bilinear taps at the corners of the 4x4 scene footprint of
	// this quarter-res pixel — a cheap box prefilter against
	// downsample flicker.
	vec3 c = vec3_splat(0.0);
	for (int i = 0; i < 4; ++i)
	{
		vec2 o = vec2(i == 1 || i == 3 ? 1.0 : -1.0,
		              i >= 2 ? 1.0 : -1.0);
		c += texture2D(s_texScene,
		               v_texcoord0 + o * u_bloomTexel.xy).rgb;
	}
	c *= 0.25;

	float lum = max(c.r, max(c.g, c.b));
	float knee = max(u_bloomParams.x * u_bloomParams.w, 1.0e-4);
	// Quadratic soft knee (the standard bloom prefilter curve): zero
	// below threshold - knee, quadratic ramp across the knee, linear
	// excess above the threshold.
	float soft = clamp(lum - u_bloomParams.x + knee, 0.0, 2.0 * knee);
	soft = soft * soft / (4.0 * knee);
	float contrib = max(soft, lum - u_bloomParams.x);
	gl_FragColor = vec4(c * (contrib / max(lum, 1.0e-4)), 1.0);
}
