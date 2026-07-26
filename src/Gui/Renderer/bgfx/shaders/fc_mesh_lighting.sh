/*
 * FreeCAD CAD-mesh lighting core: the uniforms, samplers and shading
 * function shared by the stock mesh fragment shader (fc_mesh_fs.sh)
 * and user material-stage shaders (via fc_user_lighting.sh, docs/
 * RenderDebug.md §6.5) — single headlight Blinn-Phong, or a
 * metallic/roughness BRDF with image based lighting when the PBR
 * branch is enabled per draw (u_pbrParams.x), the shadowed scene
 * light of the Shadow draw style, the local effect lights
 * (Render_Light bulbs incl. their shadow tiles, fire flames) and
 * screen-space AO. bgfx uniforms and samplers are global by name, so
 * any program declaring these picks up the values and textures the
 * engine records with the draw.
 *
 * u_matColor    : rgba diffuse; used when u_params.x == 0
 * u_matEmissive : rgb emissive add
 * u_matSpecular : rgb specular, w = shininess (0..1 Coin convention)
 * u_params      : x = per-vertex color, y = lighting on, z = two-sided
 * u_pbrParams   : x = PBR branch on (2 = with a metallic-roughness
 *                 map), y = metallic, z = roughness,
 *                 w = environment intensity
 * u_envSH       : irradiance spherical harmonics of the environment,
 *                 cosine-convolved with the basis and 1/pi constants
 *                 folded in (Ramamoorthi's polynomial form, world space)
 * s_texEnv      : GGX-prefiltered environment cubemap, world space,
 *                 mip = roughness * 5 (a dummy is bound when PBR is off)
 */

uniform vec4 u_matColor;
uniform vec4 u_matEmissive;
uniform vec4 u_matSpecular;
uniform vec4 u_params;
uniform vec4 u_pbrParams;
uniform vec4 u_envSH[9];
SAMPLERCUBE(s_texEnv, 1);
// Shadow draw style: a directional scene light replaces the headlight
// (u_lightDir.w > 0.5; xyz = light direction in view space, the way the
// light travels) and a variance shadow map attenuates its contribution.
// u_shadowParams: x = this draw receives shadows, y = minimum variance,
// z = depth bias, w unused. u_shadowMatrix maps view space to shadow
// map uv (xy) + light window depth (z). u_lightColor.rgb carries the
// light color premultiplied by its intensity.
SAMPLER2D(s_texShadow, 3);
// Shadow tint map: per-channel transmittance of glass casters rendered
// from the light camera (white where no glass blocks the light). It
// multiplies the direct scene-light term beside the variance shadow
// factor, so glass casts a softer shadow, tinted when colored.
SAMPLER2D(s_texShadowTint, 7);
uniform vec4 u_shadowParams;
uniform vec4 u_lightDir;
// Spot light position in view space; w = cos of the cone cutoff for a
// spot light, -1 for a directional one. The falloff exponent
// (dropOffRate * 128) rides u_lightColor.w.
uniform vec4 u_lightPos;
uniform vec4 u_lightColor;
// EVSM warp exponent (x): the moments store exp(c z), exp(c z)^2 of
// the light window depth. y = Coin's VsmLookup smoothstep threshold.
// zw = the Coin SoShadowGroup N-tap spread kernel (ShadowSpreadSize /
// ShadowSpreadSampleSize): z = tap spacing in shadow map UV (Coin's
// swidth * 0.001, the 0.1 spot factor folded in), w = kernel mode —
// 0 single tap, 1 Coin's 4-tap dithered kernel, N >= 3 an N x N grid.
uniform vec4 u_evsm;
uniform mat4 u_shadowMatrix;
// Local effect lights: unshadowed point lights added on top of
// whatever lighting model runs (the usual engine effect-light shortcut
// — no shadow map from them). The first half of the array carries the
// fire body flame lights (color premultiplied by the fire intensity
// and the CPU-clock flicker), the second half the light-source bodies
// (Render_Light bulbs, color premultiplied by their intensity).
// xyz = light position in view space, w = 1 / range^2 (0 = inactive
// slot).
#define LOCAL_LIGHTS 8
uniform vec4 u_localLight[LOCAL_LIGHTS];
uniform vec4 u_localLightColor[LOCAL_LIGHTS];
// Bulb shadow atlas (Render_LightShadow): 4x4 plain-VSM tiles.
// u_localLightColor[slot].w > 0.5 flags a shadowed bulb;
// u_bulbShadowConf[slot] = (first tile, tile count) — 1 tile = the
// plain downward cone, 6 = world-axis cube faces picked by the
// dominant axis of the light-to-fragment direction (rotated to world
// by u_bulbShadowRot). u_bulbShadowMtx maps camera view space to a
// tile's uv/depth (perspective — divide by w).
#define BULB_SHADOW_TILES 16
#define BULB_SHADOW_GRID 4
#define BULB_SLOTS 4
uniform mat4 u_bulbShadowMtx[BULB_SHADOW_TILES];
uniform vec4 u_bulbShadowConf[BULB_SLOTS];
uniform mat4 u_bulbShadowRot;
SAMPLER2D(s_texBulbShadow, 8);
// Screen-space ambient occlusion (the SSAO/GTAO chain result, its own
// resolution, normalized uv): folded into the ambient / IBL terms
// only. The directional scene light and the local effect lights carry
// their own shadow terms, and the viewer headlight shines along the
// view ray — a visible fragment is by definition unoccluded toward
// the camera — so the geometric crease darkening must not attenuate
// either (a fullscreen post-multiply left gray AO bands on faces
// point-blank lit by a Render_Light bulb, and AO on the headlight
// fill re-created them). Exception: the lone-headlight mode (no scene
// light) keeps the old full multiply — there AO is the only depth cue
// the viewport has. A white stand-in is bound when AO is off or the
// draw renders outside the main opaque pass (reflection re-render,
// overlays, on-top, transparent).
SAMPLER2D(s_texAOScreen, 9);

// One variance shadow map tap at uv against receiver light-window
// depth z. Plain VSM runs Coin SoShadowGroup's exact VsmLookup
// (including its far-plane map.x < 0.9999 early-out — cleared texels
// read as lit); EVSM (SmoothBorder blur active) warps the biased depth
// like the caster.
float fc_shadowTap(vec2 uv, float z)
{
	vec2 mo = texture2D(s_texShadow, uv).xy;
	if (u_evsm.x < 0.5)
	{
		// Plain VSM, Coin SoShadowGroup parity (its VsmLookup):
		// epsilon (u_shadowParams.y) adds to the variance outright
		// and the threshold (u_evsm.y) smoothsteps the tail —
		// moment interpolation across a depth gap makes the soft
		// distance-growing penumbra of the GL Shadow style.
		if (mo.x >= 0.9999)
			return 1.0;
		float lit = z <= mo.x ? 1.0 : 0.0;
		float va = min(max(mo.y - mo.x * mo.x, 0.0)
		                   + u_shadowParams.y,
		               1.0);
		float dd = mo.x - z;
		float pmax = va / (va + dd * dd);
		pmax *= smoothstep(u_evsm.y, 1.0, pmax);
		return max(lit, pmax);
	}
	// EVSM: the variance floor scales with the warped moment.
	float p = exp(u_evsm.x * (z - u_shadowParams.z));
	if (p > mo.x)
	{
		float va = max(mo.y - mo.x * mo.x,
		               u_shadowParams.y * mo.x * mo.x);
		float dd = p - mo.x;
		float pmax = va / (va + dd * dd);
		return clamp((pmax - 0.3) / 0.7, 0.0, 1.0);
	}
	return 1.0;
}

/* The full stock shading of one fragment: base color in, lit color +
 * alpha out (emissive included; the texture environment and OIT
 * weighting stay with the caller).
 *
 *   base  : base color and alpha (the stock draw's is
 *           mix(u_matColor, v_color0, u_params.x))
 *   n     : shading normal, normalized, view space (bump-perturbed by
 *           the stock caller)
 *   geoN  : geometric surface normal before any perturbation, oriented
 *           toward the viewer for two-sided draws — self-occludes the
 *           unshadowed effect lights
 *   vpos  : view-space position (v_vpos)
 *   fragCoord : gl_FragCoord.xy, passed in because shaderc's spirv
 *           path resolves gl_FragCoord only inside main()
 *   occ   : material occlusion-map factor of the ambient/IBL terms
 *           (1.0 without one)
 *   metal, rough : PBR factors, the metallic-roughness map already
 *           folded in by the caller (pass u_pbrParams.y / .z without
 *           one)
 */
vec4 fcShadeFragment(vec4 base, vec3 n, vec3 geoN, vec3 vpos,
                     vec2 fragCoord, float occ, float metal, float rough)
{
	vec3 color = base.rgb;

	// Screen-space AO factor of the ambient-like terms below (the
	// white stand-in reads 1 when inapplicable).
	float ao = texture2D(s_texAOScreen,
	                     fragCoord * u_viewTexel.xy).x;

	// Variance shadow map factor of the scene light (Chebyshev upper
	// bound with light-bleed reduction); fragments outside the map stay
	// lit. Only attenuates the direct light term below. The tint map
	// adds the per-channel glass-caster transmittance.
	float shadow = 1.0;
	vec3 shadowTint = vec3_splat(1.0);
	if (u_shadowParams.x > 0.5)
	{
		vec4 sp = mul(u_shadowMatrix, vec4(vpos, 1.0));
		// Spot lights render the map under a perspective camera.
		sp.xyz /= sp.w;
		if (sp.x > 0.0 && sp.x < 1.0 && sp.y > 0.0 && sp.y < 1.0
		    && sp.z > 0.0 && sp.z < 1.0)
		{
			shadowTint = texture2D(s_texShadowTint, sp.xy).rgb;
			if (u_evsm.w > 0.5 && u_evsm.z > 0.0)
			{
				// Coin's N-tap spread kernel (ShadowSpreadSize
				// / SpreadSampleSize): taps average, spacing
				// scales with the homogeneous w like Coin's
				// shadowCoord.w (1 for a directional light).
				float sw = u_evsm.z * sp.w;
				shadow = 0.0;
				if (u_evsm.w < 1.5)
				{
					// SpreadSampleSize 0: Coin's fixed
					// 4-tap kernel dithered by the pixel
					// parity.
					vec2 dith = mod(floor(fragCoord),
					                vec2(2.0, 2.0));
					dith.y = -dith.y;
					shadow += fc_shadowTap(
					    sp.xy + (vec2(-1.5, 1.5) + dith) * sw,
					    sp.z);
					shadow += fc_shadowTap(
					    sp.xy + (vec2(-1.5, -0.5) + dith) * sw,
					    sp.z);
					shadow += fc_shadowTap(
					    sp.xy + (vec2(0.5, 1.5) + dith) * sw,
					    sp.z);
					shadow += fc_shadowTap(
					    sp.xy + (vec2(0.5, -0.5) + dith) * sw,
					    sp.z);
					shadow *= 0.25;
				}
				else
				{
					// SpreadSampleSize >= 1: an N x N grid,
					// N = min(2 * size + 1, 8) with Coin's
					// integer-centered offsets.
					int ntap = int(u_evsm.w + 0.5);
					int cen = ntap / 2;
					for (int j = 0; j < 8; ++j)
					{
						if (j >= ntap)
							break;
						for (int k = 0; k < 8; ++k)
						{
							if (k >= ntap)
								break;
							shadow += fc_shadowTap(
							    sp.xy
							        + vec2(float(k - cen),
							               float(j - cen))
							            * sw,
							    sp.z);
						}
					}
					shadow /= float(ntap * ntap);
				}
			}
			else
			{
				shadow = fc_shadowTap(sp.xy, sp.z);
			}
		}
	}

	// Scene light vector at this fragment: constant for a directional
	// light, position-dependent for a spot light, whose cone falloff
	// folds into the shadow factor (both only feed the scene-light
	// branches below).
	vec3 sceneL = u_lightDir.xyz;
	if (u_lightPos.w > -0.5)
	{
		sceneL = normalize(vpos - u_lightPos.xyz);
		float cd = dot(sceneL, u_lightDir.xyz);
		shadow *= cd > u_lightPos.w
			? pow(max(cd, 1.0e-4), u_lightColor.w) : 0.0;
	}

	if (u_params.y > 0.5)
	{
		if (u_pbrParams.x > 0.5)
		{
			// Metallic/roughness BRDF: a white headlight down the
			// view axis plus image based lighting from the fixed
			// world-space environment. Two-sided surfaces flip the
			// normal toward the viewer; single-sided back faces go
			// dark like the fixed-function headlight.
			if (u_params.z > 0.5 && n.z < 0.0)
				n = -n;
			float ndv = max(n.z, 1.0e-4);
			vec3 f0 = mix(vec3_splat(0.04), base.rgb, metal);
			vec3 kd = base.rgb * (1.0 - metal);

			// Direct light: the headlight (L = V = +z: ndl = ndh =
			// ndv and vdh = 1, so Fresnel collapses to f0), or the
			// directional scene light of the Shadow draw style,
			// shadowed, with Schlick Fresnel. GGX distribution with
			// Karis' fast Smith-joint visibility; the specular term
			// is clamped — facing planes sit exactly on the GGX
			// peak (1 / pi a^2) under a headlight, which would
			// flash whole faces white at low roughness.
			float a = rough * rough;
			// The unshadowed headlight always contributes (like
			// the Blinn-Phong path: Coin's SoShadowGroup keeps
			// the viewer headlight beside the shadow light). No
			// AO on it: the headlight shines along the view ray,
			// and a visible fragment is by definition unoccluded
			// toward the camera ...
			vec3 direct;
			{
				float d = ndv * ndv * (a * a - 1.0) + 1.0;
				float D = a * a / (3.14159265 * d * d);
				float vis = 0.25
					/ max(ndv * (ndv * (1.0 - a) + a),
					      1.0e-4);
				direct = (kd * 0.31830989
						+ f0 * min(D * vis, 4.0))
					* (ndv * 1.2);
			}
			// ... and the shadowed scene light adds on top.
			if (u_lightDir.w > 0.5)
			{
				vec3 l = -sceneL;
				float ndl = dot(n, l);
				if (u_params.z > 0.5)
					ndl = abs(ndl);
				ndl = max(ndl, 0.0);
				vec3 h = normalize(l + vec3(0.0, 0.0, 1.0));
				float ndh = max(dot(n, h), 0.0);
				float vdh = max(h.z, 0.0);
				float d = ndh * ndh * (a * a - 1.0) + 1.0;
				float D = a * a / (3.14159265 * d * d);
				float vis = 0.5
					/ max(mix(2.0 * ndl * ndv, ndl + ndv,
					          a),
					      1.0e-4);
				vec3 F = f0 + (vec3_splat(1.0) - f0)
					* exp2((-5.55473 * vdh - 6.98316)
					       * vdh);
				direct += (kd * 0.31830989
						+ F * min(D * vis, 4.0))
					* u_lightColor.rgb * shadowTint
					* (ndl * 1.2 * shadow);
			}

			// IBL in world space (the environment does not follow
			// the camera): SH irradiance for the diffuse part, the
			// prefiltered mip chain plus Lazarov's analytic
			// environment BRDF for the specular part.
			vec3 nw = normalize(
				mul(u_invView, vec4(n, 0.0)).xyz);
			vec3 rw = normalize(mul(u_invView,
				vec4(2.0 * n.z * n - vec3(0.0, 0.0, 1.0),
				     0.0)).xyz);
			vec3 irr = u_envSH[0].xyz
				+ u_envSH[1].xyz * nw.y
				+ u_envSH[2].xyz * nw.z
				+ u_envSH[3].xyz * nw.x
				+ u_envSH[4].xyz * (nw.x * nw.y)
				+ u_envSH[5].xyz * (nw.y * nw.z)
				+ u_envSH[6].xyz * (3.0 * nw.z * nw.z - 1.0)
				+ u_envSH[7].xyz * (nw.x * nw.z)
				+ u_envSH[8].xyz * (nw.x * nw.x - nw.y * nw.y);
			vec3 pref = textureCubeLod(s_texEnv, rw,
			                           rough * 5.0).xyz;
			vec4 r4 = rough * vec4(-1.0, -0.0275, -0.572, 0.022)
				+ vec4(1.0, 0.0425, 1.04, -0.04);
			float a004 = min(r4.x * r4.x, exp2(-9.28 * ndv))
				* r4.x + r4.y;
			vec2 ab = vec2(-1.04, 1.04) * a004 + r4.zw;
			color = (kd * max(irr, vec3_splat(0.0))
				+ pref * (f0 * ab.x + vec3_splat(ab.y)))
				* (u_pbrParams.w * occ * ao) + direct;
		}
		else if (u_lightDir.w > 0.5)
		{
			// Scene light (Shadow draw style), shadowed, on top
			// of the unshadowed headlight: Coin's SoShadowGroup
			// keeps the viewer headlight as an "other light"
			// (the fork's viewer root always carries it), so
			// replacing the headlight rendered much darker than
			// the GL Shadow style.
			vec3 l = -sceneL;
			float ndl = dot(n, l);
			float hdl = n.z;
			if (u_params.z > 0.5)
			{
				ndl = abs(ndl);
				hdl = abs(hdl);
			}
			else
			{
				ndl = max(ndl, 0.0);
				hdl = max(hdl, 0.0);
			}

			vec3 h = normalize(l + vec3(0.0, 0.0, 1.0));
			float shininess = max(u_matSpecular.w * 128.0, 1.0);
			float spec = pow(max(abs(dot(n, h)), 0.0), shininess);
			float hspec = pow(max(abs(n.z), 0.0), shininess);

			// AO occludes only the true ambient floor. The
			// headlight fill shines along the view ray — a
			// visible fragment is by definition unoccluded toward
			// the camera, so AO on it is wrong (it re-darkened
			// bulb-lit contacts: the fill dominates viewer-facing
			// surfaces). The scene light keeps its own shadow
			// term.
			color = base.rgb
					* (vec3_splat(0.2 * occ * ao + 0.8 * hdl)
					+ u_lightColor.rgb * shadowTint
						* (ndl * shadow))
				+ u_matSpecular.rgb * (hspec * 0.75)
				+ u_matSpecular.rgb * u_lightColor.rgb
					* shadowTint * (spec * 0.75 * shadow);
		}
		else
		{
			// headlight along the view axis; AO deliberately
			// covers the whole term here (matching the old
			// fullscreen multiply) even though a headlight
			// cannot be occluded — with no other light in this
			// mode, AO is the viewport's only depth cue and
			// would otherwise shrink to the 0.2 ambient floor.
			float ndl = n.z;
			if (u_params.z > 0.5)
				ndl = abs(ndl);
			else
				ndl = max(ndl, 0.0);

			float shininess = max(u_matSpecular.w * 128.0, 1.0);
			float spec = pow(max(abs(n.z), 0.0), shininess);

			color = (base.rgb * (0.2 * occ + 0.8 * ndl)
				+ u_matSpecular.rgb * (spec * 0.75)) * ao;
		}

		// Local effect lights (fire flames, Render_Light bulbs):
		// diffuse plus a Blinn specular from each light position,
		// distance-attenuated. They add to every lit branch (the PBR
		// path takes them as a plain Lambert add — a full BRDF
		// evaluation is not worth it for an effect light).
		for (int fi = 0; fi < LOCAL_LIGHTS; ++fi)
		{
			if (u_localLight[fi].w <= 0.0)
				continue;
			vec3 fl = u_localLight[fi].xyz - vpos;
			// Self-occlusion: a face whose geometric normal points away from
			// the light is shadowed by the object's own body, so this
			// unshadowed effect light must not light it (no penetrating a solid).
			if (dot(geoN, fl) <= 0.0)
				continue;
			float d2 = dot(fl, fl);
			vec3 l = normalize(fl);
			float ndl = dot(n, l);
			if (u_params.z > 0.5)
				ndl = abs(ndl);
			ndl = max(ndl, 0.0);
			float att = 1.0 / (1.0 + d2 * u_localLight[fi].w);
			// Bulb shadow tiles (upper-half slots with the flag):
			// perspective plain-VSM tap. A plain bulb has one
			// downward tile (outside its cone the light stays
			// unshadowed); an extended one six cube faces picked by
			// the dominant world axis of the fragment direction.
			if (fi >= LOCAL_LIGHTS - BULB_SLOTS
			    && u_localLightColor[fi].w > 0.5)
			{
				vec4 conf =
					u_bulbShadowConf[fi - (LOCAL_LIGHTS - BULB_SLOTS)];
				int t = int(conf.x + 0.5);
				vec3 spos = vpos;
				if (conf.y > 1.5)
				{
					vec3 dw = mul(u_bulbShadowRot,
						vec4(vpos - u_localLight[fi].xyz,
						     0.0)).xyz;
					vec3 ad = abs(dw);
					if (ad.x >= ad.y && ad.x >= ad.z)
						t += dw.x > 0.0 ? 0 : 1;
					else if (ad.y >= ad.z)
						t += dw.y > 0.0 ? 2 : 3;
					else
						t += dw.z > 0.0 ? 4 : 5;
					// Normal-offset sampling: surfaces grazing a
					// side face alias badly in its 512-texel map;
					// lift the receiver by ~1.5 texels' world
					// footprint (100 deg fov) along the surface
					// normal, scaled up as the light grazes the
					// surface (the offset a texel needs grows with
					// the depth slope), before projecting.
					float slope = 1.0
						+ (1.0 - clamp(dot(geoN, l), 0.0, 1.0));
					spos += geoN * (0.007 * sqrt(d2) * slope);
				}
				vec4 sp = mul(u_bulbShadowMtx[t],
				              vec4(spos, 1.0));
				if (sp.w > 1.0e-4)
				{
					sp.xyz /= sp.w;
					int txi = t - (t / BULB_SHADOW_GRID)
						* BULB_SHADOW_GRID;
					int tyi = t / BULB_SHADOW_GRID;
					float ts = 1.0 / float(BULB_SHADOW_GRID);
					// Tile bounds with a 2-texel guard so the
					// bilinear tap never bleeds a neighbor tile.
					float m = 2.0 / (512.0 * float(BULB_SHADOW_GRID));
					vec2 b0 = vec2(ts * float(txi) + m,
					               ts * float(tyi) + m);
					vec2 b1 = b0 + vec2_splat(ts - 2.0 * m);
					if (sp.x > b0.x && sp.x < b1.x
					    && sp.y > b0.y && sp.y < b1.y
					    && sp.z > 0.0 && sp.z < 1.0)
					{
						// 2x2 spread of bilinear taps = a 3x3
						// tent over the moments — the tiles skip
						// the scene map's smooth-border blur, so
						// filter here (VSM moments average
						// soundly; the 2-texel bounds guard
						// covers the spread).
						// 0.75 texel: wider spreads smear the
						// depth step at contact silhouettes into
						// a speckle band (bilinear mixes caster
						// and receiver depths). Far-plane texels
						// (cleared to 1,1) carry no occluder --
						// averaging them in dilutes the moments at
						// silhouette borders into speckle, so only
						// valid taps count.
						float so = 0.75
							/ (512.0 * float(BULB_SHADOW_GRID));
						vec2 moSum = vec2_splat(0.0);
						float moW = 0.0;
						vec2 mt;
						mt = texture2D(s_texBulbShadow,
							sp.xy + vec2(-so, -so)).xy;
						if (mt.x < 0.9999) { moSum += mt; moW += 1.0; }
						mt = texture2D(s_texBulbShadow,
							sp.xy + vec2(so, -so)).xy;
						if (mt.x < 0.9999) { moSum += mt; moW += 1.0; }
						mt = texture2D(s_texBulbShadow,
							sp.xy + vec2(-so, so)).xy;
						if (mt.x < 0.9999) { moSum += mt; moW += 1.0; }
						mt = texture2D(s_texBulbShadow,
							sp.xy + vec2(so, so)).xy;
						if (mt.x < 0.9999) { moSum += mt; moW += 1.0; }
						if (moW > 0.5)
						{
							vec2 mo = moSum / moW;
							float lit = sp.z <= mo.x
								? 1.0 : 0.0;
							// Coin VsmLookup shape with the
							// tunable minimum variance
							// (ShadowEpsilon, u_shadowParams.y).
							// conf.w = the format-dependent floor:
							// fp16 tiles quantize the z^2 moment,
							// so their variance needs a floor
							// above that quantization noise.
							float va = max(mo.y - mo.x * mo.x,
							               0.0)
								+ max(u_shadowParams.y,
								      max(conf.w, 1.0e-5));
							float dd = mo.x - sp.z;
							float pmax = va / (va + dd * dd);
							pmax *= smoothstep(0.2, 1.0, pmax);
							att *= max(lit, pmax);
						}
					}
				}
			}
			vec3 h = normalize(l + vec3(0.0, 0.0, 1.0));
			float shininess = max(u_matSpecular.w * 128.0, 1.0);
			float spec = pow(max(abs(dot(n, h)), 0.0), shininess);
			color += base.rgb * u_localLightColor[fi].rgb
					* (ndl * att)
				+ u_matSpecular.rgb * u_localLightColor[fi].rgb
					* (spec * att * 0.75);
		}
	}

	color += u_matEmissive.rgb;
	return vec4(color, base.a);
}
