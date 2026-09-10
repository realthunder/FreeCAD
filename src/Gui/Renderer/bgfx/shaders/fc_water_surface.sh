/*
 * FreeCAD water-surface shading core: the uniforms, samplers and the
 * shading function shared by the stock water surface shader
 * (fs_fc_water.sc) and user "water"-stage shaders (via
 * fc_user_water.sh, docs/RenderEngine.md §5.11). The engine records
 * every uniform and sampler here with the water-surface draw
 * (BGFXRenderer submitWaterSurface); bgfx uniforms are global by
 * name, so any program declaring these picks the values up unchanged.
 *
 * u_waterSurf  : x = wave strength (slope amplitude), y = wave
 *                frequency (1/world units), z = animation time,
 *                w > 0.5 = prepass viewZ bound (refraction depth
 *                reject), w > 1.5 = water back depth bound too
 *                (Beer-Lambert depth absorption)
 * u_matColor   : water body diffuse tint; a > 0.5 = planar refl bound
 * u_waterAbsorb: x = absorption, y = in-scatter, z = refl mode
 *                (0 off, 1 env, 2 SSR, 3 planar), w = refraction on
 * u_lightDir   : scene light (w > 0.5 = present), view space
 */

SAMPLER2D(s_texScene, 0);
SAMPLERCUBE(s_texEnv, 1);
SAMPLER2D(s_texNormalZ, 2);
SAMPLER2D(s_texRefl, 3);
SAMPLER2D(s_texWaterBack, 4);  // pool-bottom eye distance in .z, .w = valid
// Scene-light variance shadow map (same target the mesh receivers sample),
// bound only when the water surface should receive the scene light's
// shadow (u_shadowParams.x > 0.5).
SAMPLER2D(s_texShadow, 5);
// Front-segment volumetric target (fs_fc_volume output 1, half res):
// inscatter of the media stretch between the eye and the water entry in
// rgb, its transmittance in a. Composited here, in the surface pass
// itself, so exactly the drawn pixels get it — a separate fullscreen
// apply pass depth-gating against the prepass disagrees with this
// pass's own depth test along the waterline and dots a phantom edge.
SAMPLER2D(s_texVolFront, 6);
// Particle impact map (fs_fc_pimpact, docs/RenderEngine.md §5.8): the
// most recent hit in each cell of the water's world footprint —
// xy = where it landed, z = when, w = how hard. Point sampled: a cell
// is a record, not a sample of a field, so there is nothing between
// two of them to interpolate.
SAMPLER2D(s_texImpact, 7);

uniform vec4 u_matColor;   // rgb = water tint; a > 0.5 = planar refl in s_texRefl
uniform vec4 u_lightDir;
uniform vec4 u_lightColor;
uniform vec4 u_waterSurf;
uniform vec4 u_waterAbsorb; // x=absorption, y=in-scatter, z=refl mode, w=refract
// x = the surface receives the scene light's shadow, y = min variance
// (VSM epsilon), z = EVSM depth bias, w unused.
uniform vec4 u_shadowParams;
// x = EVSM warp exponent (0 = plain VSM), y = plain-VSM light-bleed threshold.
uniform vec4 u_evsm;
#include "fc_shadow_tap.sh"   // the shared VSM/EVSM bound
#include "fc_screen.sh"
#include "fc_matrix.sh"
// Maps view space to shadow map uv (xy) + light-window depth (z).
uniform mat4 u_shadowMatrix;
// x = ambient ripple type (0 = directional waves, 1 = rain drops,
// 2 = none — a still surface, leaving only the rings the scene's own
// events raise), y = rain drop density (cells per wave-frequency unit).
uniform vec4 u_waterRipple;
// Fountain splash sources: xyz = world base center, w = impact ring
// radius (0 = slot inactive). Continuous ring trains added on top of
// either ripple type.
uniform vec4 u_waterSplash[4];
// xy = half-res texel size of the front volumetric target, zw = its
// size in texels; x <= 0 = no front volumetric this frame.
uniform vec4 u_volTexel;
// Impact map frame: xy = world min corner of its footprint,
// z = 1 / its world extent (square, so one number), w = resolution.
uniform vec4 u_waterImpact;
// x = map resolution in cells (0 = no impact map this frame),
// y = the animation clock in seconds, on the same reading the map's
// records were stamped with, z = ring lifetime in seconds,
// w = ring amplitude scale.
uniform vec4 u_waterImpactCfg;

// Per-cell random pair/scalar for the rain drop field.
vec2 fc_rainHash2(vec2 cell)
{
	vec2 h = vec2(dot(cell, vec2(127.1, 311.7)),
	              dot(cell, vec2(269.5, 183.3)));
	return fract(sin(h) * 43758.5453);
}

// One variance shadow map tap at uv against receiver light-window depth z,
// matching the mesh receivers' fc_shadowTap (Coin SoShadowGroup VsmLookup
// parity for plain VSM, EVSM warp when the SmoothBorder blur is active).
float fc_waterShadowTap(vec2 uv, float z)
{
	return fc_vsmVisibility(texture2D(s_texShadow, uv).xy, z);
}

// The stock water-surface shading: screen-space refraction with depth
// absorption, environment/SSR/planar reflection, Fresnel blend, the
// received scene-light shadow band, sun glint and the front-segment
// volumetric composite. normal/vpos are the raw mesh varyings
// (v_normal, v_vpos); fragCoord = gl_FragCoord.xy (pass it from the
// caller -- shaderc's spirv path resolves gl_FragCoord only in main()).
vec4 fcWaterShadeFragment(vec3 normal, vec3 vpos, vec2 fragCoord)
{
	vec3 n = normalize(normal);
	// The surface reads two-sided: flip toward the viewer (GL
	// projection: perspective FC_MTX(u_proj, 2, 3) != 0, viewer down -z).
	vec3 V = FC_MTX(u_proj, 2, 3) != 0.0 ? normalize(-vpos)
	                             : vec3(0.0, 0.0, 1.0);
	if (dot(n, V) < 0.0)
		n = -n;

	// World-space wave frame on the surface.
	vec3 wp = mul(u_invView, vec4(vpos, 1.0)).xyz;
	vec3 nw = normalize(mul(u_invView, vec4(n, 0.0)).xyz);
	vec3 upv = abs(nw.z) < 0.9 ? vec3(0.0, 0.0, 1.0)
	                           : vec3(1.0, 0.0, 0.0);
	vec3 t1 = normalize(cross(upv, nw));
	vec3 t2 = cross(nw, t1);
	vec2 q = vec2(dot(wp, t1), dot(wp, t2)) * u_waterSurf.y;
	float t = u_waterSurf.z;

	// The wave field: a q-space height gradient (grad, world-space
	// slope after the strength scale below) and the matching
	// dimensionless height (hq). The ripple types share the outputs
	// so refraction, glint and the shadow wobble follow any of them —
	// including type 2, which contributes nothing and leaves the
	// splash and impact rings below as the only thing moving.
	vec2 grad = vec2_splat(0.0);
	float hq = 0.0;
	if (u_waterRipple.x < 0.5)
	{
		// Directional waves: sum of four slope waves (analytic
		// height gradient), octaves of increasing frequency and
		// speed. hq takes the sin where the gradient took the cos,
		// without the frequency factor.
		vec2 d0 = vec2(0.86, 0.5);
		vec2 d1 = vec2(-0.5, 0.86);
		vec2 d2 = vec2(0.26, -0.97);
		vec2 d3 = vec2(-0.97, -0.26);
		grad += d0 * (0.50 * cos(dot(q, d0) * 6.28 + t) * 6.28);
		grad += d1 * (0.25 * cos(dot(q, d1) * 13.1 - t * 1.6) * 13.1);
		grad += d2 * (0.20 * cos(dot(q, d2) * 22.9 + t * 2.3) * 22.9);
		grad += d3 * (0.15 * cos(dot(q, d3) * 41.3 - t * 3.1) * 41.3);
		hq += 0.50 * sin(dot(q, d0) * 6.28 + t);
		hq += 0.25 * sin(dot(q, d1) * 13.1 - t * 1.6);
		hq += 0.20 * sin(dot(q, d2) * 22.9 + t * 2.3);
		hq += 0.15 * sin(dot(q, d3) * 41.3 - t * 3.1);
	}
	else if (u_waterRipple.x < 1.5)
	{
		// Rain drops: the surface tiles into hashed cells (density =
		// cells per q unit), each cycling a drop at a random spot and
		// phase — an expanding damped ring of ~3 wavelengths. The 3x3
		// neighborhood covers rings crossing cell borders. The radial
		// derivative is analytic; the chain rule back to q space
		// multiplies by the density.
		// Base scale: ~3 cells per q unit at density 1 (the q frame
		// is ~4 units across the water diagonal), so default density
		// reads as rain rather than a couple of giant rings.
		float D = max(u_waterRipple.y, 0.01) * 3.0;
		vec2 gq = q * D;
		vec2 cell0 = floor(gq);
		for (int j = -1; j <= 1; ++j)
		{
			for (int k = -1; k <= 1; ++k)
			{
				vec2 cell = cell0 + vec2(float(j), float(k));
				vec2 rnd = fc_rainHash2(cell);
				float ph = fc_rainHash2(cell + 17.31).x;
				float age = fract(t * 0.35 + ph);
				vec2 dvec = gq - (cell + rnd);
				float rc = max(length(dvec), 1.0e-4);
				float x = rc * 18.0 - age * 12.0;
				float env = 2.5 * (1.0 - age) * exp(-2.0 * rc)
				    * smoothstep(0.0, 0.08, age);
				hq += env * sin(x);
				float dh = env * (18.0 * cos(x) - 2.0 * sin(x));
				grad += (dvec / rc) * (dh * D);
			}
		}
	}
	// Fountain splash rings: a continuous ring train travelling out
	// from each active source's impact circle, on top of either
	// ripple type. Amplitude and wavelength scale with the impact
	// radius; the radial derivative converts to the q-space slope
	// convention via the wave frequency.
	for (int si = 0; si < 4; ++si)
	{
		float R0 = u_waterSplash[si].w;
		if (R0 <= 0.0)
			continue;
		vec3 dw = wp - u_waterSplash[si].xyz;
		vec2 dq = vec2(dot(dw, t1), dot(dw, t2));
		float rw = max(length(dq), 1.0e-4);
		float x = rw - R0 * 0.6;
		if (x < 0.0)
			continue;
		float k = 10.0 / R0;
		// Gentle decay: the ring train stays readable out to a few
		// impact radii beyond the fountain bound before fading.
		float dk = 1.1 / R0;
		float env = 1.2 * exp(-x * dk);
		float ph = x * k - t * 5.0;
		hq += env * sin(ph);
		float dh = env * (k * cos(ph) - dk * sin(ph));
		grad += (dq / rw) * (dh / max(u_waterSurf.y, 1.0e-4));
	}
	// Droplet impact rings: a wave packet expanding from every place a
	// particle actually struck the surface (the impact map, §5.8). The
	// rain field above is the same shape of ring put wherever a hash
	// puts it — which is exactly the tell, since a fountain's rings
	// belong under its falling water and nowhere else.
	//
	// A record's own world position rides in the texel, so the ring is
	// centred on the hit rather than on the cell that caught it. The
	// ring reaches two cells before it dies, which is what makes the
	// 5x5 neighbourhood complete: no ring can reach this fragment from
	// a cell further out than that.
	if (u_waterImpactCfg.x > 0.5)
	{
		float res = u_waterImpactCfg.x;
		float cell = 1.0 / max(u_waterImpact.z * res, 1.0e-6);
		float life = max(u_waterImpactCfg.z, 1.0e-3);
		float speed = 2.0 * cell / life;
		float lam = max(cell * 0.55, 1.0e-4);
		float kk = 6.2832 / lam;
		float sig = lam * 0.8;
		vec2 gc = floor((wp.xy - u_waterImpact.xy) * u_waterImpact.z
		                * res);
		for (int j = -2; j <= 2; ++j)
		{
			for (int m = -2; m <= 2; ++m)
			{
				vec2 cc = gc + vec2(float(j), float(m));
				vec4 rec = texture2DLod(s_texImpact,
				                        (cc + 0.5) / res, 0.0);
				float age = u_waterImpactCfg.y - rec.z;
				if (rec.w <= 0.0 || age <= 0.0 || age >= life)
					continue;
				vec3 dw3 = vec3(wp.xy - rec.xy, 0.0);
				vec2 dq = vec2(dot(dw3, t1), dot(dw3, t2));
				float rw = max(length(dq), 1.0e-4);
				// Distance behind the travelling front. The packet is
				// a couple of wavelengths wide, so what passes is a
				// short train and not an endless corrugation.
				float x = rw - speed * age;
				float packet = exp(-(x * x) / (2.0 * sig * sig));
				if (packet < 0.002)
					continue;
				// Dies with age, and spreads its energy round an ever
				// longer circle on the way out.
				float fade = (1.0 - age / life)
				    * smoothstep(0.0, 0.05 * life, age);
				// The gain sets what a strength of one means: an
				// impact ring reads about as deep as the rain
				// field's own rings, so the two are interchangeable
				// at their default settings.
				float amp = 3.0 * u_waterImpactCfg.w * rec.w * fade
				    / sqrt(1.0 + rw / lam);
				float ph = kk * x;
				hq += amp * packet * sin(ph);
				float dh = amp * packet
				    * (kk * cos(ph) - (x / (sig * sig)) * sin(ph));
				grad += (dq / rw) * (dh / max(u_waterSurf.y, 1.0e-4));
			}
		}
	}
	grad *= u_waterSurf.x * 0.02;
	vec3 npw = normalize(nw - t1 * grad.x - t2 * grad.y);
	vec3 np = normalize(mul(u_view, vec4(npw, 0.0)).xyz);

	// Wave HEIGHT: the q-space slope was used as a world-space slope,
	// so dividing by the q scale keeps the implied height consistent
	// with the rendered normals. Displacing the shadow tap by it makes
	// the shadow band ripple with the waves instead of lying rigid on
	// an animated surface.
	float waveH = hq * u_waterSurf.x * 0.02
	    / max(u_waterSurf.y, 1.0e-4);

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
	vec2 uv = fragCoord * u_viewTexel.xy;

	// --- Refraction (u_waterAbsorb.w toggles it) -------------------------
	vec3 refr;
	if (u_waterAbsorb.w > 0.5)
	{
		vec2 ruv = uv + (np.xy - n.xy) * 0.08;
		if (u_waterSurf.w > 0.5)
		{
			// Reject offset samples landing on geometry in front of the
			// surface (the dry parts of protruding objects); cross taps
			// dilate the reject ~2px past CAD edge lines.
			float fragZ = -vpos.z * 0.999;
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
			else if (p0.w > 0.5)
			{
				// Nearer-than-fragment alone misses FARTHER
				// protruding geometry (a pillar standing out of
				// the pool beyond this fragment): its dry pixels
				// would refract into the water area as a ghost
				// projection. Reconstruct the sample's world
				// position from its prepass depth and reject
				// anything above the surface plane.
				vec2 sndc = fc_uvToNdc(ruv);
				vec3 sv;
				if (FC_MTX(u_proj, 2, 3) != 0.0)
					sv = vec3(
					    (sndc.x + FC_MTX(u_proj, 2, 0)) / FC_MTX(u_proj, 0, 0),
					    (sndc.y + FC_MTX(u_proj, 2, 1)) / FC_MTX(u_proj, 1, 1),
					    -1.0) * p0.z;
				else
					sv = vec3(
					    (sndc.x - FC_MTX(u_proj, 3, 0)) / FC_MTX(u_proj, 0, 0),
					    (sndc.y - FC_MTX(u_proj, 3, 1)) / FC_MTX(u_proj, 1, 1),
					    -p0.z);
				vec3 sw = mul(u_invView, vec4(sv, 1.0)).xyz;
				if (dot(sw - wp, nw) > 0.05)
					ruv = uv;
			}
		}
		refr = texture2D(s_texScene, ruv).xyz;
		refr *= mix(vec3_splat(1.0), u_matColor.rgb, 0.2);

		// Depth-based absorption (Beer-Lambert) over the water column from
		// the surface to the body back face (s_texWaterBack.z; the water
		// gains body and the floor recedes with depth). u_waterSurf.w == 2
		// flags the back-depth target as bound.
		if (u_waterSurf.w > 1.5)
		{
			vec4 wb = texture2D(s_texWaterBack, uv);
			float thick = wb.w > 0.5 ? max(wb.z + vpos.z, 0.0) : 0.0;
			vec3 sigma = (vec3_splat(1.0) - u_matColor.rgb)
				* u_waterAbsorb.x + vec3_splat(0.1 * u_waterAbsorb.x);
			vec3 trans = exp(-sigma * thick);
			refr = refr * trans
				+ u_matColor.rgb * (1.0 - trans) * u_waterAbsorb.y;
		}
	}
	else
	{
		// Refraction off: flat water tint, no see-through.
		refr = u_matColor.rgb;
	}

	// --- Reflection (u_waterAbsorb.z: 0 off, 1 env, 2 SSR, 3 planar) -----
	int rmode = int(u_waterAbsorb.z + 0.5);
	vec3 refl = refr;
	if (rmode > 0)
	{
		// Environment cubemap: the base, and the fallback for the
		// screen-space / planar modes wherever they find nothing.
		vec3 Vw = normalize(mul(u_invView, vec4(V, 0.0)).xyz);
		vec3 rw = reflect(-Vw, npw);
		refl = textureCubeLod(s_texEnv, rw, 1.0).xyz;

		if (rmode == 2 && u_waterSurf.w > 0.5)
		{
			// Screen-space reflection: distance-adaptive coarse march +
			// binary refine of the reflected view ray through the prepass
			// depth. Reflects only on-screen geometry (tapers past it).
			vec3 Rv = reflect(normalize(vpos), np);
			float stp = 0.5;
			vec3 P = vpos, Pprev = vpos;
			vec2 suv = vec2_splat(-1.0);
			bool crossed = false;
			for (int i = 0; i < 28; ++i)
			{
				Pprev = P;
				P += Rv * stp;
				vec4 c = mul(u_proj, vec4(P, 1.0));
				suv = fc_clipToUv(c.xy / c.w);
				if (suv.x < 0.0 || suv.x > 1.0
					|| suv.y < 0.0 || suv.y > 1.0)
					break;
				vec4 s = texture2D(s_texNormalZ, suv);
				if (s.w > 0.5 && -P.z > s.z) { crossed = true; break; }
				stp *= 1.15;
			}
			if (crossed)
			{
				vec3 a = Pprev, b = P;
				for (int j = 0; j < 5; ++j)
				{
					vec3 m = (a + b) * 0.5;
					vec4 c = mul(u_proj, vec4(m, 1.0));
					suv = fc_clipToUv(c.xy / c.w);
					vec4 s = texture2D(s_texNormalZ, suv);
					if (s.w > 0.5 && -m.z > s.z) b = m; else a = m;
				}
				vec3 hitP = (a + b) * 0.5;
				vec4 hs = texture2D(s_texNormalZ, suv);
				if (hs.w > 0.5 && abs(-hitP.z - hs.z) < 2.0)
				{
					vec2 e = smoothstep(vec2_splat(0.0),
						vec2_splat(0.12), suv)
						* smoothstep(vec2_splat(0.0),
						vec2_splat(0.12), vec2_splat(1.0) - suv);
					refl = mix(refl,
						texture2D(s_texScene, suv).xyz, e.x * e.y);
				}
			}
		}
		else if (rmode == 3 && u_matColor.a > 0.5)
		{
			// Planar reflection: sample the mirror-camera scene render
			// (s_texRefl) at the fragment's own screen pos, wave-offset;
			// exact, no taper. Alpha 0 = nothing mirrored -> env.
			vec2 ruv2 = uv + (np.xy - n.xy) * 0.08;
			vec4 pr = texture2D(s_texRefl, ruv2);
			refl = mix(refl, pr.xyz, pr.w);
		}
	}

	// Schlick Fresnel, water f0; reflection off (rmode 0) -> no reflection.
	float f = 1.0 - max(dot(np, V), 0.0);
	float f2 = f * f;
	float fres = rmode > 0 ? (0.02 + 0.98 * f2 * f2 * f) : 0.0;
	vec3 color = mix(refr, refl, fres);

	// Scene-light shadow cast onto the water surface: sample the same
	// variance shadow map as the mesh receivers at this fragment's world
	// position. The refracted floor already carries its own shadow (from
	// the shaded scene copy), so darken only moderately to read as a
	// shadow band on the surface without doubling it to black; the sun
	// glint below is killed outright where shadowed.
	float shadow = 1.0;
	if (u_shadowParams.x > 0.5)
	{
		// Tap at the wave-displaced surface point, scaled by the
		// wobble factor (u_evsm.z, WaterShadowWobble): the shadow
		// boundary wobbles in step with the ripple field. Displacing
		// along the perturbed normal adds a slope-proportional
		// lateral component, so exaggerated wobble ripples sideways
		// too instead of only up and down. 0 = straight boundary.
		vec4 sp = mul(u_shadowMatrix,
		              vec4(vpos + np * (waveH * u_evsm.z), 1.0));
		sp.xyz /= sp.w;  // spot lights render a perspective map
		if (sp.x > 0.0 && sp.x < 1.0 && sp.y > 0.0 && sp.y < 1.0
		    && sp.z > 0.0 && sp.z < 1.0)
			shadow = fc_waterShadowTap(sp.xy, sp.z);
	}
	color *= mix(0.6, 1.0, shadow);

	// Sun glint from the scene light on the perturbed normal (shadowed).
	if (u_lightDir.w > 0.5)
	{
		vec3 h = normalize(V - u_lightDir.xyz);
		color += u_lightColor.rgb
			* (pow(max(dot(np, h), 0.0), 250.0) * 2.0 * shadow);
	}

	// Front-segment volumetric composite: the media stretch between the
	// eye and the water entry (air haze, a fountain plume or fire
	// standing over the pool) extinguishes and inscatters over the
	// finished surface color — the same treatment the ext/apply passes
	// give the opaque scene, so no brightness step appears along the
	// waterline or an occluder's silhouette. Validity-weighted 4-tap
	// upsample of the half-res target: texels that did NOT split hold
	// the exact identity (0,0,0,1) and plain filtering would average it
	// in — a dark rim right along the split boundary.
	if (u_volTexel.x > 0.0)
	{
		vec2 pos = uv * u_volTexel.zw - vec2_splat(0.5);
		vec2 base = floor(pos);
		vec2 f = pos - base;
		vec4 wb = vec4((1.0 - f.x) * (1.0 - f.y),
		               f.x * (1.0 - f.y),
		               (1.0 - f.x) * f.y, f.x * f.y);
		vec4 acc = vec4_splat(0.0);
		vec4 accB = vec4_splat(0.0);
		float wsum = 0.0;
		for (int i = 0; i < 4; ++i)
		{
			vec2 offs = vec2(i == 1 || i == 3 ? 1.0 : 0.0,
			                 i >= 2 ? 1.0 : 0.0);
			vec2 tuv = (base + offs + vec2_splat(0.5))
				* u_volTexel.xy;
			vec4 s = texture2D(s_texVolFront, tuv);
			// A texel that split always carries a transmittance
			// below 1 (air haze at least) or some inscatter.
			float valid = (s.a < 0.999
			               || max(s.r, max(s.g, s.b)) > 0.0)
				? 1.0 : 0.0;
			acc += s * (wb[i] * valid);
			accB += s * wb[i];
			wsum += wb[i] * valid;
		}
		if (wsum <= 1.0e-6)
		{
			// Right against an occluder's silhouette the whole 2x2
			// neighborhood can be non-split (the half-res rays hit
			// the occluder before the water): search the 3x3 ring
			// around the nearest texel so the surface sliver still
			// picks up the front haze instead of standing out as a
			// saturated dot.
			vec2 c = floor(uv * u_volTexel.zw) + vec2_splat(0.5);
			for (int j = 0; j < 9; ++j)
			{
				vec2 offs = vec2(float(j - (j / 3) * 3 - 1),
				                 float(j / 3 - 1));
				vec4 s = texture2D(s_texVolFront,
				                   (c + offs) * u_volTexel.xy);
				float valid = (s.a < 0.999
				               || max(s.r, max(s.g, s.b)) > 0.0)
					? 1.0 : 0.0;
				acc += s * valid;
				wsum += valid;
			}
		}
		vec4 fr = wsum > 1.0e-6 ? acc / wsum : accB;
		color = fr.rgb + color * fr.a;
	}

	return vec4(color, 1.0);
}
