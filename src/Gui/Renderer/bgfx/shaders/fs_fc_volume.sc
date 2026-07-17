$input v_texcoord0

/*
 * Volumetric lighting raymarch (half resolution): marches the view ray
 * of each pixel through the bounded scattering media (fc_volume.sh),
 * accumulating light inscattered where the variance shadow map says the
 * scene light reaches. Runs with the scene view/projection bound so the
 * predefined u_proj reconstructs view-space positions like the SSAO
 * pass; the march is clipped to the medium sphere and ends at the
 * opaque scene depth from the prepass target. Within the water body
 * interval the per-channel water extinction/scattering replaces the
 * air density, so deep shafts tint toward the water color.
 *
 * Output: rgb = inscattered radiance, a = the pixel's surface ray
 * length (consumed by the bilateral upsample of the apply pass).
 *
 * u_lightColor: rgb = scene light color * light intensity
 */

#include <bgfx_shader.sh>
#include "fc_volume.sh"

#define VOL_STEPS 32

SAMPLER2D(s_texNormalZ, 0);
SAMPLER2D(s_texShadow, 1);
SAMPLER2D(s_texWaterFront, 2);
SAMPLER2D(s_texWaterBack, 3);

uniform vec4 u_lightColor;
uniform vec4 u_lightDir;
// Spot light position in view space; w = cos of the cone cutoff for a
// spot light, -1 for a directional one (falloff exponent in
// u_lightColor.w) — matching the mesh receivers.
uniform vec4 u_lightPos;
uniform mat4 u_shadowMatrix;

// Variance shadow visibility of a view-space position (the mesh
// receiver's Chebyshev bound with the same bias/variance floor and
// light-bleed linstep); outside the map = lit. A spot light adds its
// cone falloff (and a perspective shadow projection).
float shadowVis(vec3 p)
{
	float vis = 1.0;
	if (u_lightPos.w > -0.5)
	{
		float cd = dot(normalize(p - u_lightPos.xyz),
		               u_lightDir.xyz);
		if (cd <= u_lightPos.w)
			return 0.0;
		vis = pow(max(cd, 1.0e-4), u_lightColor.w);
	}
	vec4 sp = mul(u_shadowMatrix, vec4(p, 1.0));
	sp.xyz /= sp.w;
	if (sp.x <= 0.0 || sp.x >= 1.0 || sp.y <= 0.0 || sp.y >= 1.0
	    || sp.z <= 0.0 || sp.z >= 1.0)
		return vis;
	vec2 mo = texture2DLod(s_texShadow, sp.xy, 0.0).xy;
	float d = sp.z - 0.003;
	if (d <= mo.x)
		return vis;
	float va = max(mo.y - mo.x * mo.x, 1.0e-5);
	float dd = d - mo.x;
	float pmax = va / (va + dd * dd);
	return vis * clamp((pmax - 0.3) / 0.7, 0.0, 1.0);
}

void main()
{
	vec3 origin, dir;
	volRay(v_texcoord0, origin, dir);
	float tEnd = volSurface(texture2D(s_texNormalZ, v_texcoord0), dir);

	// March only the stretch of ray inside the medium and in front of
	// the surface.
	vec2 med = volMedium(origin, dir);
	float t0 = med.x;
	float t1 = min(med.y, tEnd);
	vec2 water = volWaterSpan(texture2D(s_texWaterFront, v_texcoord0),
	                          texture2D(s_texWaterBack, v_texcoord0),
	                          dir);
	vec3 scatter = vec3_splat(0.0);
	if (t1 > t0)
	{
		// Dithered start offset (interleaved gradient noise)
		// decorrelates the banding of the fixed step count between
		// neighboring pixels; deterministic across frames.
		float jitter = fract(52.9829189
			* fract(dot(gl_FragCoord.xy,
			            vec2(0.06711056, 0.00583715))));
		float density = u_volParams.x;
		float dt = (t1 - t0) / float(VOL_STEPS);
		// Per-channel eye-ward transmittance over the in-medium
		// path (Beer-Lambert), iterated with the march.
		vec3 T = vec3_splat(1.0);
		for (int i = 0; i < VOL_STEPS; ++i)
		{
			float t = t0 + (float(i) + jitter) * dt;
			bool inWater = t > water.x && t < water.y;
			vec3 sigT = inWater ? u_waterSigma.xyz
			                    : vec3_splat(density);
			float sigS = inWater ? u_waterSigma.w : density;
			scatter += shadowVis(origin + dir * t)
				* (sigS * dt) * T;
			T *= exp(-sigT * dt);
		}
	}

	gl_FragColor = vec4(u_lightColor.rgb * scatter * u_volParams.y,
	                    tEnd);
}
