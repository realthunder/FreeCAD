$input v_texcoord0

/*
 * GTAO depth-pyramid downsample pass: one level of XeGTAO's prefiltered
 * depth MIP chain, ported from the compute prefilter to a per-level
 * fullscreen fragment pass. Each output texel folds a 2x2 block of the
 * source into one viewZ using XeGTAO_DepthMIPFilter: samples are
 * weighted toward the FARTHEST of the four (near outliers fade out over
 * the effect-radius falloff), which keeps coarse levels conservative —
 * a thin near silhouette read at a coarse level must not smear
 * occlusion over the open background behind it.
 *
 * u_aoParams : x = source select (0 = the full-res prepass s_texNormalZ,
 *                  viewZ in .z / validity in .w; else = the previous
 *                  pyramid level, viewZ in .x),
 *              y = view-space effect radius (falloff weighting),
 *              z, w = source width/height in texels.
 *
 * Background texels carry the sentinel -6.0e4 (fits R16F; valid view
 * depths are never that negative) and drop out of the filter weights;
 * an all-background block propagates the sentinel up the chain.
 */

#include <bgfx_shader.sh>

SAMPLER2D(s_texNormalZ, 0);

uniform vec4 u_aoParams;

#define BG_SENTINEL -6.0e4

void main()
{
	ivec2 base = ivec2(gl_FragCoord.xy) * 2;
	ivec2 srcMax = ivec2(u_aoParams.zw) - ivec2(1, 1);
	bool fromPrepass = u_aoParams.x < 0.5;

	float d[4];
	for (int i = 0; i < 4; ++i)
	{
		ivec2 c = min(base + ivec2(i & 1, i / 2), srcMax);
		vec4 t = texelFetch(s_texNormalZ, c, 0);
		if (fromPrepass)
			d[i] = t.w < 0.5 ? BG_SENTINEL : t.z;
		else
			d[i] = t.x;
	}

	// XeGTAO_DepthMIPFilter (depthRangeScaleFactor 0.75, falloff range
	// 0.615 like the gen pass): weight each sample by its distance from
	// the farthest of the block.
	float maxD = BG_SENTINEL;
	for (int i = 0; i < 4; ++i)
		if (d[i] > -1.0e4)
			maxD = max(maxD, d[i]);
	if (maxD <= -1.0e4)
	{
		gl_FragColor = vec4(BG_SENTINEL, 0.0, 0.0, 0.0);
		return;
	}

	float radius = 0.75 * u_aoParams.y;
	float falloffRange = 0.615 * radius;
	float falloffFrom = radius - falloffRange;
	float falloffMul = -1.0 / max(falloffRange, 1.0e-6);
	float falloffAdd = falloffFrom / max(falloffRange, 1.0e-6) + 1.0;

	float wsum = 0.0;
	float dsum = 0.0;
	for (int i = 0; i < 4; ++i)
	{
		if (d[i] <= -1.0e4)
			continue;
		float w = clamp((maxD - d[i]) * falloffMul + falloffAdd,
		                0.0, 1.0);
		wsum += w;
		dsum += w * d[i];
	}
	gl_FragColor = vec4(dsum / max(wsum, 1.0e-6), 0.0, 0.0, 0.0);
}
