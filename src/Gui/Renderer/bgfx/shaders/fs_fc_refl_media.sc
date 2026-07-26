$input v_texcoord0

/*
 * Analytic media raymarch for the planar reflection: fountain and fire
 * bodies composited into the mirrored-scene texture, so the water (or
 * ground) reflection shows the plume/flame instead of whatever stands
 * behind it in mirror-world. Runs over the reflection target with the
 * MIRRORED view transforms bound — the ray reconstructs in mirror view
 * space and converts to world, where both density fields are fully
 * analytic; the body flow frames define bounded cylinders, so no
 * interval depth targets are needed (they belong to the main camera).
 * No shadow term — a reflection reads fine ambient-lit.
 *
 * Output is premultiplied over: rgb = inscattered light, a = coverage
 * (1 - transmittance); blend ONE / INV_SRC_ALPHA composites over the
 * mirrored scene and grows the reflection coverage mask the samplers
 * use for their env fallback.
 */

#include <bgfx_shader.sh>
#include "fc_volume.sh"

uniform vec4 u_lightColor;

#define REFL_MEDIA_STEPS 12

void main()
{
	vec3 origin, dir;
	volRay(v_texcoord0, origin, dir);
	vec3 ow = mul(u_invView, vec4(origin, 1.0)).xyz;
	vec3 dw = normalize(mul(u_invView, vec4(dir, 0.0)).xyz);
	float jitter = fract(sin(dot(gl_FragCoord.xy,
	                             vec2(12.9898, 78.233)))
	                     * 43758.5453);
	vec3 color = vec3_splat(0.0);
	float T = 1.0;
	for (int s = 0; s < MEDIUM_SLOTS; ++s)
	{
		bool fountain = u_cloudParams[s].w > 1.5;
		bool fire = u_fireParams[s].w > 0.5;
		if (!fountain && !fire)
			continue;
		mat4 frame = fountain ? u_fountainFrame[s] : u_fireFrame[s];
		float invH = fountain ? u_fountainParams[s].x
		                      : u_fireParams2[s].y;
		float invR = fountain ? u_fountainParams[s].y
		                      : u_fireParams2[s].x;
		if (invH <= 0.0 || invR <= 0.0)
			continue;
		// Ray vs the body's bounded cylinder in its local frame.
		vec3 lo = mul(frame, vec4(ow, 1.0)).xyz;
		vec3 ld = mul(frame, vec4(dw, 0.0)).xyz;
		float H = 1.0 / invH;
		float R = 1.0 / invR;
		float t0 = 0.0;
		float t1 = 1.0e30;
		if (abs(ld.z) > 1.0e-6)
		{
			float ta = (0.0 - lo.z) / ld.z;
			float tb = (H - lo.z) / ld.z;
			t0 = max(t0, min(ta, tb));
			t1 = min(t1, max(ta, tb));
		}
		else if (lo.z < 0.0 || lo.z > H)
			continue;
		vec2 od = lo.xy;
		vec2 dd = ld.xy;
		float a = dot(dd, dd);
		if (a > 1.0e-8)
		{
			float b = dot(od, dd);
			float c = dot(od, od) - R * R;
			float disc = b * b - a * c;
			if (disc <= 0.0)
				continue;
			float sq = sqrt(disc);
			t0 = max(t0, (-b - sq) / a);
			t1 = min(t1, (-b + sq) / a);
		}
		else if (dot(od, od) > R * R)
			continue;
		if (t1 <= t0)
			continue;
		float dt = (t1 - t0) / float(REFL_MEDIA_STEPS);
		for (int i = 0; i < REFL_MEDIA_STEPS; ++i)
		{
			float t = t0 + (float(i) + jitter) * dt;
			vec3 wp = ow + dw * t;
			if (fountain)
			{
				float cd = fcCloudFieldAt(s, wp);
				color += (u_lightColor.rgb * 0.45
				          + vec3_splat(0.35))
					* (cd * dt) * T;
				T *= exp(-cd * 0.6 * dt);
			}
			else
			{
				float ft = fcFireFieldAt(s, wp);
				color += fcFireRampAt(s, ft)
					* (u_fireParams[s].x * dt) * T;
				T *= exp(-u_fireParams2[s].z * ft * dt);
			}
		}
	}
	gl_FragColor = vec4(color, 1.0 - T);
}
