$input v_texcoord0

/*
 * SSAO generation pass (normal-oriented hemisphere sampling, Chapman
 * style): reconstructs the view-space position from the prepass linear
 * depth — orthographic and perspective projections both, decided from
 * the view's u_proj — and accumulates the occlusion of a fixed sample
 * kernel rotated by a tiled 4x4 noise texture. Outputs the ambient
 * visibility (1 = open) into the R8 AO target; the background stays 1.
 *
 * u_aoParams : x = world/view-space sample radius, y = intensity,
 *              z = depth bias, w = occlusion contrast power
 * u_aoParams2: x = idle-accumulation sample index (0 = an
 *              ordinary frame); the rest unused. Nothing to do
 *              with GTAO's u_aoParams2 -- these two passes give
 *              the vec4 different meanings, as they already do
 *              u_aoParams.z.
 * u_aoKernel : hemisphere sample offsets (unit radius, z >= 0)
 */

#include <bgfx_shader.sh>
#include "fc_screen.sh"
#include "fc_matrix.sh"
#include "fc_prepass_read.sh"

#define AO_SAMPLES 16

SAMPLER2D(s_texNormalZ, 0);
SAMPLER2D(s_texAONoise, 1);

uniform vec4 u_aoParams;
uniform vec4 u_aoParams2;
uniform vec4 u_aoKernel[AO_SAMPLES];

void main()
{
	vec4 nz = texture2D(s_texNormalZ, v_texcoord0);
	if (nz.w < 0.5)
	{
		gl_FragColor = vec4_splat(1.0);
		return;
	}

	bool persp = FC_MTX(u_proj, 2, 3) != 0.0;
	float viewZ = nz.z;
	vec3 pos = fc_prepassViewPos(v_texcoord0, viewZ, persp);

	vec3 n = fc_octDecode(nz.xy);
	// Per-pixel random rotation of the kernel from the tiled noise, plus a
	// per-pixel radius jitter from the noise .z (a 4x4 Bayer dither). Azimuthal
	// rotation alone leaves the RADIAL occlusion banding correlated across
	// neighbours, so the box blur cannot remove it; jittering the radius makes
	// each pixel sample at a different distance and the blur averages the tile.
	vec3 noiseTexel = texture2D(s_texAONoise, v_texcoord0 * u_viewRect.zw / 4.0).xyz;
	vec2 rv = noiseTexel.xy * 2.0 - vec2_splat(1.0);
	float rjit = noiseTexel.z;

	// Idle temporal accumulation (docs/RenderEngine.md sec 3.5). Both
	// sources of randomness above are a 4x4 texture TILED BY SCREEN
	// POSITION, so they hand every frame the identical pattern: left
	// alone, the accumulation would average one noise field with
	// itself and converge to the single-frame estimate, noise and all.
	// This is the same defect GTAO had, and the index is the same
	// accumulation SAMPLE number -- reproducible, so the frame stays
	// deterministic.
	//
	// Advance BOTH, because they decorrelate different things: the
	// azimuth spreads the kernel's directions and the radius jitter
	// spreads its distances, and the radial banding the second one
	// exists to break would otherwise survive every sample intact.
	// Golden-ratio and R2 steps rather than a random draw -- successive
	// samples then stratify instead of clumping, which is what makes a
	// 32-sample mean worth more than 32 independent guesses.
	//
	// ! Index 0 branches out entirely rather than falling through the
	// arithmetic. It has to be EXACTLY the old frame -- that is what
	// the render-verify goldens check -- and one texel of the noise
	// carries .z = 0xff, for which fract(1.0 + 0.0) is 0.0, not 1.0.
	// The identity would have failed on 1 pixel in 16.
	float tIndex = u_aoParams2.x;
	if (tIndex > 0.0)
	{
		float a = 6.2831853 * fract(tIndex * 0.6180339887498949);
		float ca = cos(a);
		float sa = sin(a);
		rv = vec2(rv.x * ca - rv.y * sa, rv.x * sa + rv.y * ca);
		rjit = fract(rjit + fract(tIndex * 0.7548776662466927));
	}

	vec3 rvec = vec3(rv, 0.0);
	vec3 tangent = normalize(rvec - n * dot(rvec, n));
	vec3 bitangent = cross(n, tangent);

	// 0.7 .. 1.15 of the nominal radius, centred so the mean occlusion is
	// close to the un-jittered result.
	float radius = u_aoParams.x * (0.7 + 0.45 * rjit);
	float bias = u_aoParams.z;
	float occlusion = 0.0;
	for (int i = 0; i < AO_SAMPLES; ++i)
	{
		vec3 k = u_aoKernel[i].xyz;
		vec3 s = pos + (tangent * k.x + bitangent * k.y + n * k.z)
		             * radius;
		float sz = -s.z;   // sample's own view depth
		vec2 suv;
		if (persp)
		{
			if (sz < 1.0e-6)
				continue;
			suv = vec2((FC_MTX(u_proj, 0, 0) * s.x - FC_MTX(u_proj, 2, 0) * sz) / sz,
			           (FC_MTX(u_proj, 1, 1) * s.y - FC_MTX(u_proj, 2, 1) * sz) / sz);
		}
		else
			suv = vec2(FC_MTX(u_proj, 0, 0) * s.x + FC_MTX(u_proj, 3, 0),
			           FC_MTX(u_proj, 1, 1) * s.y + FC_MTX(u_proj, 3, 1));
		suv = fc_clipToUv(suv);

		vec4 snz = texture2D(s_texNormalZ, suv);
		// Occluded when written geometry lies in front of the sample
		// point, faded out over distance so foreground silhouettes do
		// not darken far background surfaces.
		if (snz.w > 0.5 && snz.z < sz - bias)
			occlusion += smoothstep(0.0, 1.0,
			                        radius / abs(viewZ - snz.z));
	}

	// Contrast/power curve on the mean occlusion: full occlusion (deep
	// contacts) stays at full strength, but the faint tail is suppressed, so a
	// shallow ramp collapses to a tight band near the real contact instead of a
	// wide, low-contrast wash spread over the whole face (which only spans a
	// few 8-bit levels and bands). The visible shaded WIDTH then scales with
	// occlusion depth. u_aoParams.w = power (>= 1).
	float occ = occlusion / float(AO_SAMPLES);
	occ = pow(occ, max(u_aoParams.w, 1.0));
	float ao = 1.0 - u_aoParams.y * occ;
	gl_FragColor = vec4_splat(clamp(ao, 0.0, 1.0));
}
