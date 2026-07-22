$input v_texcoord0

/*
 * GTAO generation pass: ground-truth ambient occlusion (Jimenez et al.
 * 2016) following Intel's XeGTAO reference implementation (MIT), ported
 * from compute to a plain fragment pass over the same prepass inputs as
 * the classic SSAO shader (oct-encoded view-space normal + linear view
 * depth in s_texNormalZ).
 *
 * Per screen-space slice through the pixel, the depth buffer is marched
 * on both sides to find the horizon angles, and the cosine-weighted
 * visibility arc between them is integrated analytically. Occlusion is
 * therefore only produced by geometry that actually raises the horizon,
 * giving physically-correct falloff (tight contact darkening, no
 * wide low-contrast wash on open faces at large radii — the artifact
 * the classic pass needs its contrast power-curve for).
 *
 * u_aoParams : x = world/view-space effect radius, y = intensity,
 *              z = unused (classic-SSAO depth bias), w = final value
 *              power (XeGTAO FinalValuePower; sharpens the visibility)
 *
 * Outputs ambient visibility (1 = open) into the R8 AO target; shares
 * the 4x4 box blur and multiply-apply with the classic pass.
 */

#include <bgfx_shader.sh>

#define GTAO_SLICES 3
#define GTAO_STEPS  6

SAMPLER2D(s_texNormalZ, 0);
SAMPLER2D(s_texAONoise, 1);

uniform vec4 u_aoParams;

#define HALF_PI 1.5707963267948966
#define GOLDEN  0.6180339887498948

vec3 octDecode(vec2 e)
{
	vec3 n = vec3(e, 1.0 - abs(e.x) - abs(e.y));
	if (n.z < 0.0)
	{
		vec2 sn = vec2(n.x >= 0.0 ? 1.0 : -1.0,
		               n.y >= 0.0 ? 1.0 : -1.0);
		n.xy = (vec2_splat(1.0) - abs(n.yx)) * sn;
	}
	return normalize(n);
}

/// View-space position of a prepass texel: shared NDC-unproject with the
/// classic pass (perspective when u_proj[2][3] != 0, else orthographic).
vec3 viewPos(vec2 uv, float viewZ, bool persp)
{
	vec2 ndc = uv * 2.0 - vec2_splat(1.0);
	if (persp)
		return vec3(viewZ * (ndc.x + u_proj[2][0]) / u_proj[0][0],
		            viewZ * (ndc.y + u_proj[2][1]) / u_proj[1][1],
		            -viewZ);
	return vec3((ndc.x - u_proj[3][0]) / u_proj[0][0],
	            (ndc.y - u_proj[3][1]) / u_proj[1][1],
	            -viewZ);
}

void main()
{
	vec4 nz = texture2D(s_texNormalZ, v_texcoord0);
	if (nz.w < 0.5)
	{
		gl_FragColor = vec4_splat(1.0);
		return;
	}

	bool persp = u_proj[2][3] != 0.0;
	float viewZ = nz.z;
	vec3 pos = viewPos(v_texcoord0, viewZ, persp);
	vec3 n = octDecode(nz.xy);
	// Unit vector toward the camera; the orbit/GL camera looks down -z,
	// so orthographic rays are all along +z.
	vec3 V = persp ? normalize(-pos) : vec3(0.0, 0.0, 1.0);

	float radius = u_aoParams.x;
	// XeGTAO falloff: occlusion fades out over the outer 61.5% of the
	// effect radius (weight 1 inside, 0 at the radius).
	float falloffRange = 0.615 * radius;
	float falloffFrom = radius - falloffRange;
	float falloffMul = -1.0 / falloffRange;
	float falloffAdd = falloffFrom / falloffRange + 1.0;

	// Screen-space (UV) extent of the world-space radius at this depth,
	// per axis (the projection is anisotropic in x/y).
	vec2 uvRadius = 0.5 * radius * vec2(u_proj[0][0], u_proj[1][1]);
	if (persp)
		uvRadius /= viewZ;
	// Cap the marched extent in PIXELS: zoomed in, the world radius can
	// project to thousands of pixels, and the fixed step count then
	// samples hundreds of pixels apart — smeared, low-res-looking
	// shading. Capping keeps the taps dense (contact detail stays crisp
	// up close) at the cost of very-long-range occlusion; the proper
	// long-range answer is a prefiltered depth mip chain like XeGTAO's.
	float radiusPx = max(length(uvRadius * u_viewRect.zw), 1.0e-4);
	const float maxRadiusPx = 256.0;
	if (radiusPx > maxRadiusPx) {
		uvRadius *= maxRadiusPx / radiusPx;
		radiusPx = maxRadiusPx;
	}
	// XeGTAO's pixelTooCloseThreshold (1.3 px) as a minimum step
	// parameter: sub-pixel samples read the pixel's own quantized depth
	// and falsely darken flat surfaces.
	float minS = 1.3 / radiusPx;

	// Per-pixel spatial noise (deterministic across frames): interleaved
	// gradient noise, decorrelated per use. The 4x4 tiled noise texture
	// the classic pass uses has only 16 distinct values repeating every
	// 4 px — around silhouettes the slowly-varying horizon turns that
	// repetition into concentric ring/moire banding; IGN never tiles, so
	// the estimation error stays unstructured grain the edge-aware
	// denoise can average.
	float noiseSlice = fract(52.9829189 *
	        fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
	float noiseSample = fract(52.9829189 *
	        fract(dot(gl_FragCoord.xy + vec2(37.0, 17.0),
	                  vec2(0.00583715, 0.06711056))));

	// Interaction fast path (u_aoParams.z > 0.5, set by the viewer while
	// the camera moves): fewer slices/steps so AO stays VISIBLE during
	// orbit/zoom — noisier, refined automatically once idle restores the
	// full counts. A uniform, not a target change, so no re-init stall.
	bool fast = u_aoParams.z > 0.5;
	int slices = fast ? 2 : GTAO_SLICES;
	int steps = fast ? 3 : GTAO_STEPS;

	float visibility = 0.0;
	for (int i = 0; i < slices; ++i)
	{
		float phi = (float(i) + noiseSlice) * (3.14159265 / float(slices));
		vec2 omega = vec2(cos(phi), sin(phi));

		// Slice frame: the normal projected into the plane spanned by
		// the view vector and the slice direction.
		vec3 directionVec = vec3(omega, 0.0);
		vec3 orthoDirectionVec = directionVec - V * dot(directionVec, V);
		vec3 axisVec = normalize(cross(orthoDirectionVec, V));
		vec3 projN = n - axisVec * dot(n, axisVec);
		float projNLen = length(projN);
		float signNorm = sign(dot(orthoDirectionVec, projN));
		float cosNorm = clamp(dot(projN, V) / max(projNLen, 1.0e-6),
		                      0.0, 1.0);
		float nAngle = signNorm * acos(cosNorm);

		// Initializing the horizons at the projected-normal hemisphere
		// edges folds the [n - pi/2, n + pi/2] clamp into the max().
		float lowHorizonCos0 = cos(nAngle + HALF_PI);
		float lowHorizonCos1 = cos(nAngle - HALF_PI);
		float horizonCos0 = lowHorizonCos0;
		float horizonCos1 = lowHorizonCos1;

		for (int s = 0; s < steps; ++s)
		{
			// Per-slice/step decorrelation (XeGTAO's R1 sequence), and a
			// squared distribution packing samples toward the center.
			float stepNoise = fract(noiseSample
			        + float(i + s * steps) * GOLDEN);
			float s01 = (float(s) + stepNoise) / float(steps);
			s01 = s01 * s01 + minS;   // squared distribution + min offset
			vec2 off = s01 * omega * uvRadius;

			for (int side = 0; side < 2; ++side)
			{
				vec2 suv = side == 0 ? v_texcoord0 + off
				                     : v_texcoord0 - off;
				vec4 snz = texture2D(s_texNormalZ, suv);
				if (snz.w < 0.5)
					continue;   // background raises no horizon
				vec3 spos = viewPos(suv, snz.z, persp);
				vec3 delta = spos - pos;
				float dist = length(delta);
				if (dist < 1.0e-6)
					continue;
				float shc = dot(delta / dist, V);
				// Distance falloff blends the sample toward the open
				// hemisphere edge instead of hard-dropping it.
				float weight = clamp(dist * falloffMul + falloffAdd,
				                     0.0, 1.0);
				if (side == 0)
					horizonCos0 = max(horizonCos0,
					        mix(lowHorizonCos0, shc, weight));
				else
					horizonCos1 = max(horizonCos1,
					        mix(lowHorizonCos1, shc, weight));
			}
		}

		// Analytic cosine-weighted visibility arc between the horizons
		// (XeGTAO/GTAO inner integral).
		float h0 = -acos(clamp(horizonCos1, -1.0, 1.0));
		float h1 = acos(clamp(horizonCos0, -1.0, 1.0));
		float sinN = sin(nAngle);
		float iarc0 = (cosNorm + 2.0 * h0 * sinN
		               - cos(2.0 * h0 - nAngle)) / 4.0;
		float iarc1 = (cosNorm + 2.0 * h1 * sinN
		               - cos(2.0 * h1 - nAngle)) / 4.0;
		visibility += projNLen * (iarc0 + iarc1);
	}
	visibility /= float(slices);
	// FinalValuePower: mild contrast on the visibility (XeGTAO default
	// ~2.2), reusing the classic pass's power uniform; then XeGTAO's
	// 0.03 visibility floor (never fully black).
	visibility = pow(clamp(visibility, 0.0, 1.0),
	                 max(u_aoParams.w, 1.0));
	visibility = max(0.03, visibility);

	float ao = 1.0 - u_aoParams.y * (1.0 - visibility);
	gl_FragColor = vec4_splat(clamp(ao, 0.0, 1.0));
}
