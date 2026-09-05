$input v_texcoord0

/*
 * PBR environment background (PBRConfig::envBackground): draws the
 * image based lighting environment itself instead of the gradient
 * background quad, so reflective surfaces visibly mirror their
 * surroundings. Runs on the background view with the scene view/proj
 * bound (the shared fullscreen vertex shader ignores the matrices;
 * the predefined u_proj/u_invView reconstruct each pixel's world
 * direction). Orthographic cameras have no per-pixel ray fan, so a
 * fixed 45-degree virtual field of view around the view axis keeps
 * the sky gradient readable there.
 *
 * The softening (Render_PBREnvBlur) is a LENS, not a mip level. One
 * textureCubeLod up the chain is what this used to be, and it looked
 * it: a mip is a box average of a box average, so a wide setting drew
 * a 16x16 face magnified across the window -- visible bilinear
 * diamonds, cube seams where a face's box filter never saw its
 * neighbour, and a bright sun that dimmed into a grey square instead
 * of spreading into a disc. So the direction is convolved with the
 * aperture instead: taps spread uniformly over the cone the aperture
 * subtends (Render::envBlurAngle), each read at the mip whose texel
 * matches the SPACING between taps. That is what removes the
 * pixelation -- the tap footprints tile the disc with no gaps, at any
 * radius -- and it is a disc of directions, in linear radiance, so a
 * small bright source spreads into an even bokeh disc that keeps its
 * energy rather than being averaged away.
 *
 * u_pbrParams : x = cos of the aperture half-angle (1 = sharp),
 *               y = mip level to read each tap at,
 *               z = tap count, w = environment intensity (same slot
 *               as the mesh lighting)
 * s_texEnv    : GGX-prefiltered environment cubemap, world space
 */

#include <bgfx_shader.sh>

uniform vec4 u_pbrParams;
SAMPLERCUBE(s_texEnv, 1);

// Ceiling on the loop, not the count: the tap count comes down with
// the radius (u_pbrParams.z), because a narrow aperture has nothing
// for the extra taps to find.
#define FC_ENV_MAX_TAPS 32

// Golden angle. Successive taps land the full turn apart, which is
// the arrangement that stays even at every prefix of the sequence --
// so stopping at any tap count still covers the disc.
#define FC_ENV_GOLDEN 2.39996323

void main()
{
	vec2 ndc = v_texcoord0 * 2.0 - vec2_splat(1.0);
	vec3 dir;
	if (u_proj[2][3] != 0.0)
		dir = vec3((ndc.x + u_proj[2][0]) / u_proj[0][0],
		           (ndc.y + u_proj[2][1]) / u_proj[1][1],
		           -1.0);
	else
		dir = vec3(ndc.x * 0.41421356, ndc.y * 0.41421356, -1.0);
	vec3 dw = normalize(mul(u_invView, vec4(dir, 0.0)).xyz);

	float cosA = u_pbrParams.x;
	vec3 col;
	if (cosA >= 1.0) {
		col = textureCubeLod(s_texEnv, dw, u_pbrParams.y).rgb;
	} else {
		// Any tangent frame will do -- the taps are rotationally
		// symmetric about dw -- as long as the seed axis is never
		// parallel to it.
		vec3 seed = abs(dw.y) < 0.999 ? vec3(0.0, 1.0, 0.0)
		                              : vec3(1.0, 0.0, 0.0);
		vec3 tx = normalize(cross(seed, dw));
		vec3 ty = cross(dw, tx);
		float taps = u_pbrParams.z;
		float lod = u_pbrParams.y;
		vec3 sum = vec3_splat(0.0);
		for (int i = 0; i < FC_ENV_MAX_TAPS; ++i) {
			float fi = float(i);
			if (fi >= taps)
				break;
			// Uniform over the cap's SOLID angle, which is what an
			// aperture covers: cos runs linearly from 1 to cosA, so
			// the taps thin out with radius exactly as a disc does.
			float u = (fi + 0.5) / taps;
			float ct = mix(1.0, cosA, u);
			float st = sqrt(max(1.0 - ct * ct, 0.0));
			float ph = fi * FC_ENV_GOLDEN;
			vec3 d = dw * ct + (tx * cos(ph) + ty * sin(ph)) * st;
			sum += textureCubeLod(s_texEnv, d, lod).rgb;
		}
		col = sum / taps;
	}
	gl_FragColor = vec4(col * u_pbrParams.w, 1.0);
}
