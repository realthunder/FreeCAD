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
 * Output 0: rgb = inscattered radiance BEHIND the water surface entry
 * (everything, when the pixel has no water body or no split is
 * requested), a = the pixel's surface ray length (consumed by the
 * bilateral upsample of the apply pass).
 * Output 1: rgb = inscattered radiance IN FRONT of the water surface
 * entry, a = the front stretch's transmittance. Composited over the
 * re-rendered water surface by the front apply pass — without the
 * split, a fountain plume (or fire) standing over the water would be
 * overdrawn by the surface and read as behind it. Split only when
 * u_volParams.w > 1.5 (water + surface re-render active).
 *
 * u_lightColor: rgb = scene light color * light intensity
 */

#include <bgfx_shader.sh>
#include "fc_volume.sh"

// xy = half-res texel (unused here), z = per-frame jitter phase of the
// temporal accumulation (golden-ratio sequence; 0 when accumulation is
// off), w unused. The apply pass re-sets this uniform for itself.
uniform vec4 u_volTexel;

#define VOL_STEPS 32

SAMPLER2D(s_texNormalZ, 0);
#include "fc_volume_shadow.sh"
SAMPLER2D(s_texWaterFront, 2);
SAMPLER2D(s_texWaterBack, 3);
SAMPLER2D(s_texCloudFront, 4);
SAMPLER2D(s_texCloudBack, 5);
SAMPLER2D(s_texFireFront, 6);
SAMPLER2D(s_texFireBack, 7);

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
	vec3 water = volWaterSpan(texture2D(s_texWaterFront, v_texcoord0),
	                          texture2D(s_texWaterBack, v_texcoord0),
	                          dir);
	vec3 cloud = volCloudSpan(texture2D(s_texCloudFront, v_texcoord0),
	                          texture2D(s_texCloudBack, v_texcoord0),
	                          dir);
	vec3 fire = volFireSpan(texture2D(s_texFireFront, v_texcoord0),
	                        texture2D(s_texFireBack, v_texcoord0),
	                        dir);
	// Per-pixel appearance slots of the media (fc_volume.sh).
	int ws = int(water.z + 0.5);
	int cs = int(cloud.z + 0.5);
	int fs = int(fire.z + 0.5);
	// Henyey-Greenstein forward-scattering phase of the cloud toward
	// the light, constant along the (parallel-light) ray; the air and
	// water media keep their isotropic behavior.
	float cloudPhase = 1.0;
	if (cloud.y > cloud.x)
	{
		float g = 0.35;
		float mu = dot(dir, -normalize(u_lightDir.xyz));
		float denom = 1.0 + g * g - 2.0 * g * mu;
		cloudPhase = (1.0 - g * g)
			/ max(denom * sqrt(denom), 1.0e-3);
	}
	vec3 scatter = vec3_splat(0.0);
	vec3 emission = vec3_splat(0.0);
	vec3 scatterF = vec3_splat(0.0);
	vec3 emissionF = vec3_splat(0.0);
	// Transmittance of the WHOLE front stretch (plain air haze included,
	// not just the fountain/cloud/fire bodies): the front apply then
	// gives the re-rendered surface exactly the extinction + inscatter
	// the opaque scene gets from the ext/apply passes — anything else
	// leaves a brightness step along the split boundary (a phantom
	// "edge" at an occluder's silhouette or at the waterline, clearly
	// visible through a fountain plume).
	float Tf = 1.0;
	// Split only when the water surface is actually the visible front
	// along this ray: with opaque geometry IN FRONT of the water entry
	// (a pillar standing between the eye and the pool) the surface
	// pass never draws the pixel and the front apply skips it — the
	// whole march must stay in the main output or the media in front
	// of that pillar vanish into an object-shaped hole.
	bool split = u_volParams.w > 1.5 && water.y > water.x
	    && water.x < tEnd;
	if (t1 > t0)
	{
		// Dithered start offset decorrelating the banding of the
		// fixed step count between neighboring pixels; deterministic
		// across frames. White-noise hash, NOT interleaved gradient
		// noise: IGN's coherent 45-degree stripe structure needs
		// temporal accumulation to dissolve and reads as diagonal
		// strips across dense media (fire, fountain spray); random
		// speckle averages out in the bilateral upsample instead.
		float jitter = fract(sin(dot(gl_FragCoord.xy,
		                             vec2(12.9898, 78.233)))
		                     * 43758.5453 + u_volTexel.z);
		float density = u_volParams.x;
		float dt = (t1 - t0) / float(VOL_STEPS);
		// Per-channel eye-ward transmittance over the in-medium
		// path (Beer-Lambert), iterated with the march.
		vec3 T = vec3_splat(1.0);
		for (int i = 0; i < VOL_STEPS; ++i)
		{
			float t = t0 + (float(i) + jitter) * dt;
			bool inWater = t > water.x && t < water.y;
			bool inCloud = t > cloud.x && t < cloud.y;
			bool inFire = t > fire.x && t < fire.y;
			bool front = split && t < water.x;
			vec3 sigT = inWater ? u_waterSigma[ws].xyz
			                    : vec3_splat(density);
			float sigS = inWater ? u_waterSigma[ws].w : density;
			float phase = 1.0;
			float ambient = 0.0;
			if (inCloud)
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
				// w = 2 flags a fountain body riding the cloud
				// channel: spray density field instead of FBM.
				// The fountain's jet and fall sheet are thin
				// against the frame-wide step, so average four
				// sub-taps across the step — the cloud FBM is
				// broad and keeps the single tap.
				float cd;
				if (u_cloudParams[cs].w > 1.5)
				{
					vec3 wd = mul(u_invView,
					              vec4(dir, 0.0)).xyz * dt;
					cd = 0.0;
					for (int k = 0; k < 4; ++k)
						cd += fountainDensityAt(
						    wp + wd * (float(k) * 0.25
						               - 0.375),
						    u_fountainFrame[cs],
						    u_cloudParams[cs],
						    u_fountainParams[cs]);
					cd *= 0.25 * fade;
				}
				else
				{
					cd = cloudDensityAt(
					    wp, u_cloudParams[cs]) * fade;
				}
				sigT = vec3_splat(cd * 0.6);
				sigS = cd;
				phase = cloudPhase;
				// Spray scatters more ambient light than the
				// cloud puff (fine droplets, multiple
				// scattering) — keeps the fountain white.
				ambient = u_cloudParams[cs].w > 1.5 ? 0.45
				                                    : 0.25;
			}
			if (inFire)
			{
				// Fire stretch: emissive medium — the rising
				// FBM temperature field mapped through the
				// blackbody-style ramp adds self-lit radiance
				// (no shadow term, no light color; hot gas is
				// its own light source) attenuated by the
				// eye-ward transmittance. The interval-edge
				// fade softens the body's box silhouette. A
				// mild soot extinction rides the temperature
				// field (unburnt soot dims what lies behind
				// the tongues) — absorption only, no scatter.
				vec3 fwp = mul(u_invView,
				               vec4(origin + dir * t, 1.0)).xyz;
				float ffade = clamp(min(t - fire.x, fire.y - t)
					/ max(0.2 * (fire.y - fire.x),
					      1.0e-3), 0.0, 1.0);
				float ftemp = fireTempAt(fwp, u_fireFrame[fs],
				                         u_fireParams[fs],
				                         u_fireParams2[fs]) * ffade;
				vec3 fe = fireRamp(ftemp)
					* (u_fireParams[fs].x * dt) * T;
				if (front)
					emissionF += fe;
				else
					emission += fe;
				sigT += vec3_splat(u_fireParams2[fs].z * ftemp);
			}
			vec3 sc = (shadowVis(origin + dir * t) * phase
			           + ambient)
				* (sigS * dt) * T;
			T *= exp(-sigT * dt);
			if (front)
			{
				scatterF += sc;
				// The media are scalar (spray / soot), so one
				// channel represents their transmittance.
				Tf *= exp(-sigT.g * dt);
			}
			else
			{
				scatter += sc;
			}
		}
	}

	// Main output carries the TOTAL inscatter (front + behind): every
	// consumer of it is a pixel where the water surface did not draw
	// (no split at full res), which wants the full integral — and the
	// target then stays continuous across the split boundary, so the
	// apply pass's bilateral upsample doesn't build a rim there. The
	// front output alone is split-only (composited over the re-rendered
	// surface, whose pixels the main apply gets overwritten on).
	gl_FragData[0] = vec4(u_lightColor.rgb * (scatter + scatterF)
	                          * u_volParams.y
	                          + emission + emissionF,
	                      tEnd);
	gl_FragData[1] = vec4(u_lightColor.rgb * scatterF * u_volParams.y
	                          + emissionF,
	                      Tf);
}
