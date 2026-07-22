$input v_texcoord0

/*
 * GTAO denoise pass: edge-aware (bilateral) blur of the ambient
 * visibility, in the spirit of XeGTAO's denoiser. The 3-slice horizon
 * estimate is inherently noisy per pixel; a plain box blur cleans it
 * but averages across depth discontinuities, bleeding occlusion over
 * silhouettes. Here each 5x5 neighbour is weighted by its prepass
 * depth similarity (relative to the center depth, so the tolerance
 * scales with distance) and normal agreement — smoothing within a
 * surface, stopping at its edges.
 *
 * s_texAO      : raw GTAO visibility (possibly reduced resolution)
 * s_texNormalZ : full-res prepass oct-normal + linear view depth
 *                (UV-addressed, so the resolutions may differ)
 */

#include <bgfx_shader.sh>

SAMPLER2D(s_texAO, 0);
SAMPLER2D(s_texNormalZ, 1);

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

void main()
{
	vec4 cnz = texture2D(s_texNormalZ, v_texcoord0);
	float cao = texture2D(s_texAO, v_texcoord0).x;
	if (cnz.w < 0.5)
	{
		gl_FragColor = vec4_splat(cao);
		return;
	}
	float cz = cnz.z;
	vec3 cn = octDecode(cnz.xy);

	float sum = cao;
	float wsum = 1.0;
	for (int y = -2; y <= 2; ++y)
	{
		for (int x = -2; x <= 2; ++x)
		{
			if (x == 0 && y == 0)
				continue;
			vec2 off = vec2(float(x), float(y)) * u_viewTexel.xy;
			vec2 suv = v_texcoord0 + off;
			vec4 snz = texture2D(s_texNormalZ, suv);
			if (snz.w < 0.5)
				continue;
			// Depth tolerance relative to the center depth (plus a
			// floor for near geometry): neighbours across a step in
			// depth belong to another surface.
			float dw = clamp(1.0 - abs(snz.z - cz)
			                 / (0.03 * cz + 0.001), 0.0, 1.0);
			// Normal agreement keeps creases (the very places GTAO
			// darkens) from bleeding onto adjacent faces.
			vec3 sn2 = octDecode(snz.xy);
			float d = max(dot(sn2, cn), 0.0);
			float nw = d * d;
			nw *= nw;   // pow(dot, 4)
			// Mild spatial falloff so far corners count less.
			float sw = 1.0 - 0.15 * float(x * x + y * y) / 8.0;
			float w = dw * nw * sw;
			sum += texture2D(s_texAO, suv).x * w;
			wsum += w;
		}
	}
	gl_FragColor = vec4_splat(sum / wsum);
}
