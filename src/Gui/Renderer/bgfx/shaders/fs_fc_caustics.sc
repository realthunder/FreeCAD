$input v_texcoord0

/*
 * Water caustics pass (full resolution): projects an animated procedural
 * caustic pattern onto opaque surfaces inside the water body interval —
 * the industry-standard light-space fake, not a physical refraction
 * accumulation. For each prepass pixel the view-space surface position
 * is reconstructed (fc_volume.sh), tested against the water front/back
 * depth targets, and lit by the caustic pattern in a light-perpendicular
 * world-space frame, modulated by the shadow-map visibility (light that
 * does not reach the water surface casts no caustics), the surface's
 * N.L, and a Beer-Lambert light-path tint over the depth below the water
 * entry. The pass adds onto the scene color (blend ONE/ONE) before the
 * volumetric extinction multiply, so the eye-ward underwater absorption
 * applies to the caustic light like to the surface it lights.
 *
 * u_causticParams[s] (per water body appearance slot, indexed by the
 * span's per-pixel slot): x = intensity, y = pattern frequency
 * (1/world units), z = animation time (speed folded in), w = unused;
 * x or y <= 0 = no caustics for the slot.
 */

#include <bgfx_shader.sh>
#include "fc_volume.sh"

SAMPLER2D(s_texNormalZ, 0);
#include "fc_volume_shadow.sh"
SAMPLER2D(s_texWaterFront, 2);
SAMPLER2D(s_texWaterBack, 3);

uniform vec4 u_causticParams[MEDIUM_SLOTS];

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

// Animated caustic web: three mutually warped sine layers; each layer's
// zero lines sharpen into thin bright filaments, and the layer sum
// brightens their crossings. Periodic, float-only, deterministic per
// (uv, time).
float causticPattern(vec2 uv, float time)
{
	vec2 p = uv * 6.2831853;
	vec2 q = p;
	float c = 0.0;
	for (int i = 0; i < 3; ++i)
	{
		float t = time * (1.0 - 0.31 * float(i))
			+ float(i) * 2.4;
		q = p + vec2(cos(q.y + t) + sin(q.x * 0.71 - t * 1.3),
		             sin(q.x - t) + cos(q.y * 0.83 + t));
		float v = min(abs(sin(q.x)), abs(sin(q.y)));
		float s = 1.0 - v;
		s *= s;   // pow(1 - v, 8): thin filaments
		s *= s;
		c += s * s;
	}
	return c * 0.6;
}

void main()
{
	vec4 nz = texture2D(s_texNormalZ, v_texcoord0);
	if (nz.w < 0.5)
		discard;

	vec3 origin, dir;
	volRay(v_texcoord0, origin, dir);
	float t = volT(nz.z, dir);

	// Underwater test against the water body interval; receivers just
	// behind the body's back face (a tank floor coincident with the
	// water bottom) keep their caustics through the slack.
	vec3 water = volWaterSpan(texture2D(s_texWaterFront, v_texcoord0),
	                          texture2D(s_texWaterBack, v_texcoord0),
	                          dir);
	if (water.y < water.x)
		discard;
	int ws = int(water.z + 0.5);
	vec4 cp = u_causticParams[ws];
	if (cp.x <= 0.0 || cp.y <= 0.0)
		discard;
	float slack = 0.1 * (water.y - water.x);
	if (t <= water.x || t > water.y + slack)
		discard;

	vec3 p = origin + dir * t;
	float vis = shadowVis(p);
	vec3 n = octDecode(nz.xy);
	float ndl = max(dot(n, -u_lightDir.xyz), 0.0);
	if (vis * ndl <= 0.0)
		discard;

	// Pattern coordinates: the world position projected onto a frame
	// perpendicular to the light (world space, so the pattern sticks
	// to the model under camera moves).
	vec3 lw = normalize(mul(u_invView, vec4(u_lightDir.xyz, 0.0)).xyz);
	vec3 wp = mul(u_invView, vec4(p, 1.0)).xyz;
	vec3 up = abs(lw.z) < 0.9 ? vec3(0.0, 0.0, 1.0)
	                          : vec3(1.0, 0.0, 0.0);
	vec3 b1 = normalize(cross(up, lw));
	vec3 b2 = cross(lw, b1);
	vec2 uv = vec2(dot(wp, b1), dot(wp, b2)) * cp.y;
	float c = causticPattern(uv, cp.z);

	// Light-path tint: the depth below the water entry stands in for
	// the in-water light leg (the light arrives otherwise untinted).
	vec3 tint = exp(-u_waterSigma[ws].xyz * max(0.0, t - water.x));

	gl_FragColor = vec4(u_lightColor.rgb * tint
	                        * (c * ndl * vis * cp.x),
	                    0.0);
}
