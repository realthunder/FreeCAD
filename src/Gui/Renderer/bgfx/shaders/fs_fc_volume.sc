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
#include "fc_volume_shadow.sh"
SAMPLER2D(s_texWaterFront, 2);
SAMPLER2D(s_texWaterBack, 3);
SAMPLER2D(s_texCloudFront, 4);
SAMPLER2D(s_texCloudBack, 5);

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
	vec2 cloud = volCloudSpan(texture2D(s_texCloudFront, v_texcoord0),
	                          texture2D(s_texCloudBack, v_texcoord0),
	                          dir);
	// Henyey-Greenstein forward-scattering phase of the cloud toward
	// the light, constant along the (parallel-light) ray; the air and
	// water media keep their isotropic behavior.
	float cloudPhase = 1.0;
	if (u_cloudParams.w > 0.5)
	{
		float g = 0.35;
		float mu = dot(dir, -normalize(u_lightDir.xyz));
		float denom = 1.0 + g * g - 2.0 * g * mu;
		cloudPhase = (1.0 - g * g)
			/ max(denom * sqrt(denom), 1.0e-3);
	}
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
			float phase = 1.0;
			float ambient = 0.0;
			if (t > cloud.x && t < cloud.y)
			{
				// Cloud stretch: FBM density in a stable
				// world frame replaces the air density, faded
				// near the interval ends so the body's box
				// silhouette softens. The eye-ward extinction
				// is reduced (0.6) and an unshadowed ambient
				// floor added — the usual cheap stand-ins for
				// the multiple scattering that keeps real
				// clouds bright.
				vec3 wp = mul(u_invView,
				              vec4(origin + dir * t, 1.0)).xyz;
				float fade = clamp(min(t - cloud.x,
				                       cloud.y - t)
					/ max(0.2 * (cloud.y - cloud.x),
					      1.0e-3), 0.0, 1.0);
				float cd = cloudDensityAt(wp) * fade;
				sigT = vec3_splat(cd * 0.6);
				sigS = cd;
				phase = cloudPhase;
				ambient = 0.25;
			}
			scatter += (shadowVis(origin + dir * t) * phase
			            + ambient)
				* (sigS * dt) * T;
			T *= exp(-sigT * dt);
		}
	}

	gl_FragColor = vec4(u_lightColor.rgb * scatter * u_volParams.y,
	                    tEnd);
}
