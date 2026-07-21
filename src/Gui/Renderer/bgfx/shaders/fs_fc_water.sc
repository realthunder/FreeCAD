$input v_normal, v_color0, v_vpos

/*
 * Water surface shading (vs_fc_mesh pair): the standard screen-space
 * scheme — the opaque scene behind the surface is sampled from the
 * post-volumetric scene copy through wave-perturbed UV offsets
 * (refraction), Fresnel-blended with the prefiltered environment
 * reflection, plus a sun glint from the Shadow draw style scene light.
 * The wave field is an animated sum of directional slope waves evaluated
 * in a world-space frame on the surface, so it sticks to the model under
 * camera moves and works on any surface orientation (tank sides
 * included).
 *
 * u_waterSurf: x = wave strength (slope amplitude), y = wave frequency
 *              (1/world units), z = animation time, w > 0.5 = the
 *              prepass viewZ is bound for the refraction depth reject
 * u_matColor : water body diffuse — tints the refracted scene slightly
 * u_lightDir : scene light (w > 0.5 = present), view space
 */

#include <bgfx_shader.sh>

SAMPLER2D(s_texScene, 0);
SAMPLERCUBE(s_texEnv, 1);
SAMPLER2D(s_texNormalZ, 2);
SAMPLER2D(s_texRefl, 3);
SAMPLER2D(s_texWaterBack, 4);  // pool-bottom eye distance in .z, .w = valid

uniform vec4 u_matColor;   // rgb = water tint; a > 0.5 = planar refl in s_texRefl
uniform vec4 u_lightDir;
uniform vec4 u_lightColor;
uniform vec4 u_waterSurf;
uniform vec4 u_waterAbsorb; // x = absorption strength, y = in-scatter

// Test toggle: 0 disables the water surface reflection, leaving pure
// screen-space refraction.
#define FC_WATER_REFLECT 1

void main()
{
	vec3 n = normalize(v_normal);
	// The surface reads two-sided: flip toward the viewer (GL
	// projection: perspective u_proj[2][3] != 0, viewer down -z).
	vec3 V = u_proj[2][3] != 0.0 ? normalize(-v_vpos)
	                             : vec3(0.0, 0.0, 1.0);
	if (dot(n, V) < 0.0)
		n = -n;

	// World-space wave frame on the surface.
	vec3 wp = mul(u_invView, vec4(v_vpos, 1.0)).xyz;
	vec3 nw = normalize(mul(u_invView, vec4(n, 0.0)).xyz);
	vec3 upv = abs(nw.z) < 0.9 ? vec3(0.0, 0.0, 1.0)
	                           : vec3(1.0, 0.0, 0.0);
	vec3 t1 = normalize(cross(upv, nw));
	vec3 t2 = cross(nw, t1);
	vec2 q = vec2(dot(wp, t1), dot(wp, t2)) * u_waterSurf.y;
	float t = u_waterSurf.z;

	// Sum of four directional slope waves (analytic height gradient),
	// octaves of increasing frequency and speed.
	vec2 d0 = vec2(0.86, 0.5);
	vec2 d1 = vec2(-0.5, 0.86);
	vec2 d2 = vec2(0.26, -0.97);
	vec2 d3 = vec2(-0.97, -0.26);
	vec2 grad = vec2_splat(0.0);
	grad += d0 * (0.50 * cos(dot(q, d0) * 6.28 + t) * 6.28);
	grad += d1 * (0.25 * cos(dot(q, d1) * 13.1 - t * 1.6) * 13.1);
	grad += d2 * (0.20 * cos(dot(q, d2) * 22.9 + t * 2.3) * 22.9);
	grad += d3 * (0.15 * cos(dot(q, d3) * 41.3 - t * 3.1) * 41.3);
	grad *= u_waterSurf.x * 0.02;
	vec3 npw = normalize(nw - t1 * grad.x - t2 * grad.y);
	vec3 np = normalize(mul(u_view, vec4(npw, 0.0)).xyz);

	// Screen-space refraction: offset the scene sample by the wave
	// normal delta (the flat surface samples straight through, so the
	// unperturbed result matches the plain transparent look shifted
	// only by shading). Offset samples landing on geometry in front
	// of the surface (prepass viewZ nearer than this fragment — the
	// not-submerged parts of protruding objects) fall back to the
	// straight-through sample, which is behind the surface wherever
	// the surface itself is visible.
	// The cross taps dilate the reject by ~2 px: the CAD edge lines
	// drawn on dry silhouettes don't rasterize into the prepass, so
	// the line pixels straddling the background would smear their
	// black through the offset otherwise.
	vec2 uv = gl_FragCoord.xy * u_viewTexel.xy;
	vec2 ruv = uv + (np.xy - n.xy) * 0.08;
	if (u_waterSurf.w > 0.5)
	{
		float fragZ = -v_vpos.z * 0.999;
		vec2 o = u_viewTexel.xy * 2.0;
		vec4 p0 = texture2D(s_texNormalZ, ruv);
		vec4 p1 = texture2D(s_texNormalZ, ruv + vec2(o.x, 0.0));
		vec4 p2 = texture2D(s_texNormalZ, ruv - vec2(o.x, 0.0));
		vec4 p3 = texture2D(s_texNormalZ, ruv + vec2(0.0, o.y));
		vec4 p4 = texture2D(s_texNormalZ, ruv - vec2(0.0, o.y));
		if ((p0.w > 0.5 && p0.z < fragZ)
			|| (p1.w > 0.5 && p1.z < fragZ)
			|| (p2.w > 0.5 && p2.z < fragZ)
			|| (p3.w > 0.5 && p3.z < fragZ)
			|| (p4.w > 0.5 && p4.z < fragZ))
			ruv = uv;
	}
	vec3 refr = texture2D(s_texScene, ruv).xyz;
	refr *= mix(vec3_splat(1.0), u_matColor.rgb, 0.2);

	// Depth-based absorption (Beer-Lambert): dim and tint the refracted
	// scene by the water column below this surface fragment — from the
	// surface here to the water body's back face (pool bottom, its eye
	// distance in s_texWaterBack.z) — so the water gains real body and the
	// floor recedes with depth instead of reading crystal-clear (which
	// made the surface look like it sat at the bottom). Per-channel
	// extinction: channels the water tint lacks are absorbed most, so
	// blue-green water reddens and darkens with depth, and at grazing
	// angles the long path turns opaque water-colour. u_waterSurf.w == 2
	// flags the back-depth target as bound.
	if (u_waterSurf.w > 1.5)
	{
		vec4 wb = texture2D(s_texWaterBack, uv);
		float thick = wb.w > 0.5 ? max(wb.z + v_vpos.z, 0.0) : 0.0;
		vec3 sigma = (vec3_splat(1.0) - u_matColor.rgb) * u_waterAbsorb.x
			+ vec3_splat(0.1 * u_waterAbsorb.x);
		vec3 trans = exp(-sigma * thick);
		refr = refr * trans
			+ u_matColor.rgb * (1.0 - trans) * u_waterAbsorb.y;
	}

#if FC_WATER_REFLECT
	// Environment reflection in world space, slightly rough — the
	// fallback wherever the planar reflection has nothing.
	vec3 Vw = normalize(mul(u_invView, vec4(V, 0.0)).xyz);
	vec3 rw = reflect(-Vw, npw);
	vec3 refl = textureCubeLod(s_texEnv, rw, 1.0).xyz;

	// Planar reflection: the scene is re-rendered through a camera
	// mirrored about the (horizontal) water plane into s_texRefl, so the
	// surface mirrors the actual model (cylinder, flame) exactly — no
	// screen-space march, no taper. The mirrored scene projects to the
	// same pixel the reflected eye ray exits through, so sample at the
	// fragment's own screen position, nudged by the wave normal for the
	// rippled distortion; the reflection's alpha (0 = nothing mirrored,
	// e.g. sky past the model) falls back to the environment. Flagged by
	// u_matColor.a (set only when the mirror pass actually ran).
	if (u_matColor.a > 0.5)
	{
		vec2 ruv2 = gl_FragCoord.xy * u_viewTexel.xy
			+ (np.xy - n.xy) * 0.08;
		vec4 pr = texture2D(s_texRefl, ruv2);
		refl = mix(refl, pr.xyz, pr.w);
	}

	// Schlick Fresnel, water f0.
	float f = 1.0 - max(dot(np, V), 0.0);
	float f2 = f * f;
	float fres = 0.02 + 0.98 * f2 * f2 * f;
	vec3 color = mix(refr, refl, fres);
#else
	// Reflection OFF (test): pure refraction of the scene behind.
	vec3 color = refr;
#endif

	// Sun glint from the scene light on the perturbed normal.
	if (u_lightDir.w > 0.5)
	{
		vec3 h = normalize(V - u_lightDir.xyz);
		color += u_lightColor.rgb
			* (pow(max(dot(np, h), 0.0), 250.0) * 2.0);
	}

	gl_FragColor = vec4(color, 1.0);
}
