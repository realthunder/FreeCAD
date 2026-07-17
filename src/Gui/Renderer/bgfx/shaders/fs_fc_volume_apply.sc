$input v_texcoord0

/*
 * Volumetric lighting apply pass (full resolution): bilateral upsample
 * of the half-resolution inscatter target — the four nearest half-res
 * texels are blended with bilinear weights modulated by the similarity
 * of their surface ray length to this pixel's own — composited onto the
 * scene with premultiplied-alpha blending (ONE / INV_SRC_ALPHA): the
 * alpha carries the medium extinction over the in-medium path in front
 * of the surface, so dst = inscatter + transmittance * scene, a
 * physically consistent lerp toward the light color as the optical
 * depth grows. Runs with the scene view/projection bound (fc_volume.sh
 * reconstructs the ray like the raymarch pass).
 *
 * u_volTexel : xy = half-res texel size, zw = half-res target size
 */

#include <bgfx_shader.sh>
#include "fc_volume.sh"

SAMPLER2D(s_texNormalZ, 0);
SAMPLER2D(s_texVol, 1);

uniform vec4 u_volTexel;

void main()
{
	// This pixel's own surface ray length, recomputed exactly like
	// the raymarch pass so the depth weights compare like with like.
	vec3 origin, dir;
	volRay(v_texcoord0, origin, dir);
	float tEnd = volSurface(texture2D(s_texNormalZ, v_texcoord0), dir);

	// Four-tap bilateral gather of the point-sampled half-res target.
	vec2 pos = v_texcoord0 * u_volTexel.zw - vec2_splat(0.5);
	vec2 base = floor(pos);
	vec2 f = pos - base;
	vec4 wb = vec4((1.0 - f.x) * (1.0 - f.y), f.x * (1.0 - f.y),
	               (1.0 - f.x) * f.y, f.x * f.y);
	float range = 0.05 * u_volParams.z;
	vec3 inscatter = vec3_splat(0.0);
	float wsum = 0.0;
	for (int i = 0; i < 4; ++i)
	{
		vec2 offs = vec2(i == 1 || i == 3 ? 1.0 : 0.0,
		                 i >= 2 ? 1.0 : 0.0);
		vec2 uv = (base + offs + vec2_splat(0.5)) * u_volTexel.xy;
		vec4 s = texture2D(s_texVol, uv);
		float w = wb[i] / (1.0 + abs(s.a - tEnd) / range);
		inscatter += s.rgb * w;
		wsum += w;
	}
	inscatter /= max(wsum, 1.0e-6);

	// Extinction over the in-medium path in front of the surface.
	vec2 med = volMedium(origin, dir);
	float pathLen = max(0.0, min(med.y, tEnd) - med.x);
	float extinction = 1.0 - exp(-u_volParams.x * pathLen);
	gl_FragColor = vec4(inscatter, extinction);
}
