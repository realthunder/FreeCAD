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
 *              z = depth bias, w = unused
 * u_aoKernel : hemisphere sample offsets (unit radius, z >= 0)
 */

#include <bgfx_shader.sh>

#define AO_SAMPLES 16

SAMPLER2D(s_texNormalZ, 0);
SAMPLER2D(s_texAONoise, 1);

uniform vec4 u_aoParams;
uniform vec4 u_aoKernel[AO_SAMPLES];

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
	vec4 nz = texture2D(s_texNormalZ, v_texcoord0);
	if (nz.w < 0.5)
	{
		gl_FragColor = vec4_splat(1.0);
		return;
	}

	// GL projection: perspective has u_proj[2][3] == -1 (w = viewZ),
	// orthographic has 0 (w = 1); both viewer down -z.
	bool persp = u_proj[2][3] != 0.0;
	float viewZ = nz.z;
	vec2 ndc = v_texcoord0 * 2.0 - vec2_splat(1.0);
	vec3 pos;
	if (persp)
		pos = vec3(viewZ * (ndc.x + u_proj[2][0]) / u_proj[0][0],
		           viewZ * (ndc.y + u_proj[2][1]) / u_proj[1][1],
		           -viewZ);
	else
		pos = vec3((ndc.x - u_proj[3][0]) / u_proj[0][0],
		           (ndc.y - u_proj[3][1]) / u_proj[1][1],
		           -viewZ);

	vec3 n = octDecode(nz.xy);
	// Per-pixel random rotation of the kernel from the tiled noise.
	vec2 noiseUV = v_texcoord0 * u_viewRect.zw / 4.0;
	vec2 rv = texture2D(s_texAONoise, noiseUV).xy * 2.0 - vec2_splat(1.0);
	vec3 rvec = vec3(rv, 0.0);
	vec3 tangent = normalize(rvec - n * dot(rvec, n));
	vec3 bitangent = cross(n, tangent);

	float radius = u_aoParams.x;
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
			suv = vec2((u_proj[0][0] * s.x - u_proj[2][0] * sz) / sz,
			           (u_proj[1][1] * s.y - u_proj[2][1] * sz) / sz);
		}
		else
			suv = vec2(u_proj[0][0] * s.x + u_proj[3][0],
			           u_proj[1][1] * s.y + u_proj[3][1]);
		suv = suv * 0.5 + vec2_splat(0.5);

		vec4 snz = texture2D(s_texNormalZ, suv);
		// Occluded when written geometry lies in front of the sample
		// point, faded out over distance so foreground silhouettes do
		// not darken far background surfaces.
		if (snz.w > 0.5 && snz.z < sz - bias)
			occlusion += smoothstep(0.0, 1.0,
			                        radius / abs(viewZ - snz.z));
	}

	float ao = 1.0 - u_aoParams.y * occlusion / float(AO_SAMPLES);
	gl_FragColor = vec4_splat(clamp(ao, 0.0, 1.0));
}
