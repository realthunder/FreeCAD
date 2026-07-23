$input v_texcoord0

/*
 * Volumetric extinction pass (full resolution): analytic per-channel
 * Beer-Lambert transmittance of the media in front of each pixel's
 * surface — the air medium over the in-sphere path with the water body
 * interval swapped to the water's per-channel extinction — multiplied
 * onto the scene color (blend ZERO / SRC_COLOR, alpha preserved) before
 * the inscatter add of the apply pass. Surfaces seen through deep water
 * darken toward the water color; the background outside the media is
 * unchanged. Runs with the scene view/projection bound (fc_volume.sh
 * reconstructs the ray).
 */

#include <bgfx_shader.sh>
#include "fc_volume.sh"

SAMPLER2D(s_texNormalZ, 0);
SAMPLER2D(s_texWaterFront, 1);
SAMPLER2D(s_texWaterBack, 2);
SAMPLER2D(s_texCloudFront, 3);
SAMPLER2D(s_texCloudBack, 4);
SAMPLER2D(s_texFireFront, 5);
SAMPLER2D(s_texFireBack, 6);

void main()
{
	vec3 origin, dir;
	volRay(v_texcoord0, origin, dir);
	float tEnd = volSurface(texture2D(s_texNormalZ, v_texcoord0), dir);

	vec2 med = volMedium(origin, dir);
	float t1 = min(med.y, tEnd);
	float len = max(0.0, t1 - med.x);

	// Water body overlap of the in-medium path (the body lies inside
	// the medium sphere).
	vec3 water = volWaterSpan(texture2D(s_texWaterFront, v_texcoord0),
	                          texture2D(s_texWaterBack, v_texcoord0),
	                          dir);
	float wlen = max(0.0, min(water.y, t1) - max(water.x, med.x));

	vec3 depth = vec3_splat(u_volParams.x * (len - wlen))
		+ u_waterSigma[int(water.z + 0.5)].xyz * wlen;

	// Cloud body overlap: the density is procedural, so the analytic
	// length is replaced by a short FBM sub-march over the stretch
	// (matching the inscatter raymarch's field). The air term keeps
	// the full non-water length — the cloud march replaces only its
	// own optical depth on top (the air density inside the body is
	// negligible next to the cloud's).
	vec3 cloud = volCloudSpan(texture2D(s_texCloudFront, v_texcoord0),
	                          texture2D(s_texCloudBack, v_texcoord0),
	                          dir);
	int cs = int(cloud.z + 0.5);
	float c0 = max(cloud.x, med.x);
	float c1 = min(cloud.y, t1);
	if (c1 > c0)
	{
		float cdt = (c1 - c0) * (1.0 / 8.0);
		float od = 0.0;
		for (int i = 0; i < 8; ++i)
		{
			float t = c0 + (float(i) + 0.5) * cdt;
			vec3 wp = mul(u_invView,
			              vec4(origin + dir * t, 1.0)).xyz;
			float fade = clamp(min(t - cloud.x, cloud.y - t)
				/ max(0.2 * (cloud.y - cloud.x), 1.0e-3),
				0.0, 1.0);
			// The 0.6 matches the raymarch's reduced eye-ward
			// extinction.
			od += (u_cloudParams[cs].w > 1.5
			       ? fountainDensityAt(wp, u_fountainFrame[cs],
			                           u_cloudParams[cs],
			                           u_fountainParams[cs])
			       : cloudDensityAt(wp, u_cloudParams[cs]))
				* fade * 0.6 * cdt;
		}
		depth += vec3_splat(od);
	}

	// Fire body overlap: the same sub-march pattern accumulates the
	// mild soot extinction along the temperature field, so the surface
	// behind the flame darkens exactly as much as the raymarch's
	// eye-ward transmittance dims the inscatter.
	vec3 fire = volFireSpan(texture2D(s_texFireFront, v_texcoord0),
	                        texture2D(s_texFireBack, v_texcoord0),
	                        dir);
	int fs = int(fire.z + 0.5);
	float f0 = max(fire.x, med.x);
	float f1 = min(fire.y, t1);
	if (f1 > f0 && u_fireParams2[fs].z > 0.0)
	{
		float fdt = (f1 - f0) * (1.0 / 8.0);
		float od = 0.0;
		for (int i = 0; i < 8; ++i)
		{
			float t = f0 + (float(i) + 0.5) * fdt;
			vec3 wp = mul(u_invView,
			              vec4(origin + dir * t, 1.0)).xyz;
			float fade = clamp(min(t - fire.x, fire.y - t)
				/ max(0.2 * (fire.y - fire.x), 1.0e-3),
				0.0, 1.0);
			od += u_fireParams2[fs].z
				* fireTempAt(wp, u_fireFrame[fs],
				             u_fireParams[fs],
				             u_fireParams2[fs]) * fade * fdt;
		}
		depth += vec3_splat(od);
	}
	gl_FragColor = vec4(exp(-depth), 1.0);
}
