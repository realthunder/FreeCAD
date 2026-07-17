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
	vec2 water = volWaterSpan(texture2D(s_texWaterFront, v_texcoord0),
	                          texture2D(s_texWaterBack, v_texcoord0),
	                          dir);
	float wlen = max(0.0, min(water.y, t1) - max(water.x, med.x));

	vec3 depth = vec3_splat(u_volParams.x * (len - wlen))
		+ u_waterSigma.xyz * wlen;
	gl_FragColor = vec4(exp(-depth), 1.0);
}
