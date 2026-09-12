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
 *              z = packed: bits 0-1 flags, bits 2+ the idle
 *              accumulation sample index (see below), w = final
 *              value power (XeGTAO FinalValuePower; sharpens the
 *              visibility)
 *
 * Outputs ambient visibility (1 = open) into the R8 AO target; shares
 * the 4x4 box blur and multiply-apply with the classic pass.
 */

#include <bgfx_shader.sh>
#include "fc_matrix.sh"
#include "fc_prepass_read.sh"


SAMPLER2D(s_texNormalZ, 0);
SAMPLER2D(s_texAONoise, 1);
// Prefiltered depth pyramid (XeGTAO depth MIP chain): level m holds the
// weighted-downsampled prepass viewZ at full-res >> m. Far horizon taps
// read the coarse levels — one texel there summarizes the whole footprint
// between sparse taps, so long-range occlusion survives a fixed step
// count. Background texels carry the sentinel -6.0e4.
SAMPLER2D(s_texAOMip1, 2);
SAMPLER2D(s_texAOMip2, 3);
SAMPLER2D(s_texAOMip3, 4);
SAMPLER2D(s_texAOMip4, 5);
SAMPLER2D(s_texAOMip5, 6);
SAMPLER2D(s_texAOMip6, 7);

uniform vec4 u_aoParams;
// x = slice count, y = steps per slice side (Render_AOSlices/Steps,
// clamped by the backend), z = depth pyramid level count (0 = none),
// w = AO-target-to-full-res pixel scale (the pass may run at a reduced
// Render_AOResolution, but the pyramid halves from FULL resolution).
uniform vec4 u_aoParams2;

#define HALF_PI 1.5707963267948966
#define GOLDEN  0.6180339887498948

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
	vec2 uvRadius = 0.5 * radius * vec2(FC_MTX(u_proj, 0, 0), FC_MTX(u_proj, 1, 1));
	if (persp)
		uvRadius /= viewZ;
	// Cap the marched extent in PIXELS. With the depth pyramid the far
	// taps read coarse prefiltered levels, so a long radius stays both
	// cheap and dense-enough — cap at 1024 (or the screen diagonal if
	// smaller): the level pick saturates one octave past the coarsest
	// level, so an unbounded radius would stride ever more sparsely
	// through it and the pass turns memory-bound as the camera zooms
	// in. Without the pyramid (R32F/R16F not renderable) keep the old
	// hard cap: zoomed in, the world radius can project to thousands of
	// pixels and the fixed step count then samples hundreds of pixels
	// apart — smeared, low-res-looking shading.
	float mipCount = u_aoParams2.z;
	float radiusPx = max(length(uvRadius * u_viewRect.zw), 1.0e-4);
	float maxRadiusPx = mipCount > 0.5
	    ? min(length(u_viewRect.zw), 1024.0) : 256.0;
	if (radiusPx > maxRadiusPx) {
		uvRadius *= maxRadiusPx / radiusPx;
		radiusPx = maxRadiusPx;
	}
	// Full-res pixels per AO-target pixel: the pyramid level pick needs
	// full-res tap distances.
	float pxToFull = max(u_aoParams2.w, 1.0e-6);
	// XeGTAO's pixelTooCloseThreshold (1.3 px) as a minimum step
	// parameter: sub-pixel samples read the pixel's own quantized depth
	// and falsely darken flat surfaces.
	float minS = 1.3 / radiusPx;

	// Per-pixel spatial noise: XeGTAO's reference noise -- a 64x64
	// Hilbert-curve index through the R2 low-discrepancy sequence.
	// Blue-noise-like and DIRECTIONLESS: the 4x4 tiled texture
	// banded into rings near silhouettes, and IGN's
	// iso-values form coherent diagonal lines that printed through as
	// diagonal stripes on faces whose horizon is sensitive to the step
	// jitter.
	uint hx = uint(gl_FragCoord.x) & 63u;
	uint hy = uint(gl_FragCoord.y) & 63u;
	uint hindex = 0u;
	for (uint lvl = 32u; lvl > 0u; lvl /= 2u)
	{
		uint rx = (hx & lvl) > 0u ? 1u : 0u;
		uint ry = (hy & lvl) > 0u ? 1u : 0u;
		hindex += lvl * lvl * ((3u * rx) ^ ry);
		if (ry == 0u)
		{
			if (rx == 1u)
			{
				hx = 63u - hx;
				hy = 63u - hy;
			}
			uint tmp = hx;
			hx = hy;
			hy = tmp;
		}
	}
	// R2 sequence over the Hilbert index -- XeGTAO_SpatioTemporalNoise,
	// with its temporal index taken from the high bits of
	// u_aoParams.z (see the pack in submitAOResolve). It is the idle
	// accumulation's SAMPLE number, not a frame counter: an ordinary
	// frame passes 0 and renders exactly as before, and a refinement
	// walks each pixel along the sequence so its samples carry
	// different noise and average out. Pinning it at 0 was what left
	// AO as the one part of the frame the accumulation could not
	// converge -- it averaged one pattern with itself. Determinism
	// survives because a sample number is reproducible.
	hindex += 288u * uint(u_aoParams.z * 0.25);
	vec2 hnoise = fract(0.5 + float(hindex)
	        * vec2(0.75487766624669276005, 0.5698402909980532659114));
	float noiseSlice = hnoise.x;
	float noiseSample = hnoise.y;

	// u_aoParams.z flag bits. Bit0: interaction fast path (fewer
	// slices/steps so AO stays VISIBLE during orbit/zoom — noisier,
	// refined once idle; a uniform, not a target change, so no re-init
	// stall). Bit1: the prepass depth is fp16 (no float32 render target
	// on this GPU) — widen the coplanarity guard to its quantization
	// step. Above them, in units of 4, the accumulation sample index
	// the noise above reads -- so every flag test here has to mask
	// itself off rather than compare against the whole field.
	float pz = u_aoParams.z;
	bool fast = mod(pz, 2.0) >= 1.0;
	// Relative depth-quantization step: fp32 depth is effectively exact
	// (guard only against true coincidence); fp16 has a ~10-bit
	// mantissa, deltas below ~2e-3 * viewZ are rounding garbage.
	float depthEps = mod(pz, 4.0) >= 2.0 ? 2.0e-3 : 1.0e-5;
	int slices = fast ? 2 : int(u_aoParams2.x);
	int steps = fast ? 3 : int(u_aoParams2.y);

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

			// XeGTAO depth MIP pick: taps beyond ~2^3.3 (~10) full-res
			// pixels step one level per octave of distance
			// (depthMIPSamplingOffset 3.30).
			float mip = 0.0;
			if (mipCount > 0.5)
				mip = clamp(log2(max(s01 * radiusPx * pxToFull, 1.0))
				            - 3.3, 0.0, mipCount);

			// Every tap below is texture2DLod, not texture2D. An
			// implicit-derivative sample inside a loop whose trip
			// count is a uniform forces fxc to unroll so it can
			// compute gradients, and it then fails to ("unable to
			// unroll loop ... 95 iterations", X3511) -- which is
			// why this shader had no Direct3D 11 build. Each AO
			// mip is its own single-level texture and the prepass
			// is read at full res, so level 0 is what these taps
			// always meant; the mip CHOICE is the sampler picked,
			// not a LOD argument.
			for (int side = 0; side < 2; ++side)
			{
				vec2 suv = side == 0 ? v_texcoord0 + off
				                     : v_texcoord0 - off;
				// Off-screen taps see nothing (a clamp-sampled edge
				// texel would streak false horizons inward).
				if (suv.x < 0.0 || suv.y < 0.0
				        || suv.x > 1.0 || suv.y > 1.0)
					continue;
				float sviewZ;
				if (mip < 0.5)
				{
					// Near taps keep the full-precision prepass (exact
					// fp32 depth + validity flag).
					vec4 snz = texture2DLod(s_texNormalZ, suv, 0.0);
					if (snz.w < 0.5)
						continue;   // background raises no horizon
					sviewZ = snz.z;
				}
				else
				{
					if (mip < 1.5)
						sviewZ = texture2DLod(s_texAOMip1, suv, 0.0).x;
					else if (mip < 2.5)
						sviewZ = texture2DLod(s_texAOMip2, suv, 0.0).x;
					else if (mip < 3.5)
						sviewZ = texture2DLod(s_texAOMip3, suv, 0.0).x;
					else if (mip < 4.5)
						sviewZ = texture2DLod(s_texAOMip4, suv, 0.0).x;
					else if (mip < 5.5)
						sviewZ = texture2DLod(s_texAOMip5, suv, 0.0).x;
					else
						sviewZ = texture2DLod(s_texAOMip6, suv, 0.0).x;
					if (sviewZ < -1.0e4)
						continue;   // background sentinel
				}
				vec3 spos = fc_prepassViewPos(suv, sviewZ, persp);
				vec3 delta = spos - pos;
				float dist = length(delta);
				// Samples closer than the prepass depth quantization
				// step at this depth are coplanar within measurement
				// precision, and their delta direction is rounding
				// garbage — zoomed in it renders as ripple bands along
				// iso-depth contours.
				if (dist < depthEps * viewZ)
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
