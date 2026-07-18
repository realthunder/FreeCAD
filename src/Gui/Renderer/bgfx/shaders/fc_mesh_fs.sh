/*
 * FreeCAD CAD-mesh fragment shader body: single headlight Blinn-Phong,
 * or a metallic/roughness BRDF with image based lighting when the PBR
 * branch is enabled per draw (u_pbrParams.x).
 * Included by fs_fc_mesh.sc and fs_fc_mesh_clip.sc (CLIP_PLANES defined,
 * fc_clip.sh included first).
 *
 * u_matColor    : rgba diffuse; used when u_params.x == 0
 * u_matEmissive : rgb emissive add
 * u_matSpecular : rgb specular, w = shininess (0..1 Coin convention)
 * u_params      : x = per-vertex color, y = lighting on, z = two-sided
 * u_texParams   : x = texture environment, y = source carries alpha,
 *                 z = emissive map on, w = occlusion map on
 * u_pbrParams   : x = PBR branch on, y = metallic, z = roughness,
 *                 w = environment intensity
 * u_envSH       : irradiance spherical harmonics of the environment,
 *                 cosine-convolved with the basis and 1/pi constants
 *                 folded in (Ramamoorthi's polynomial form, world space)
 * s_texEnv      : GGX-prefiltered environment cubemap, world space,
 *                 mip = roughness * 5 (a dummy is bound when PBR is off)
 *
 * The OIT variant (fs_fc_mesh_oit*) is the weighted-blended OIT
 * accumulation pass (McGuire/Bavoil 2013): RT0 accumulates the
 * depth-weighted premultiplied color (blend ONE, ONE), RT1 the
 * revealage product (blend ZERO, INV_SRC_COLOR).
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
uniform vec4 u_shadowParams;
uniform vec4 u_lightDir;
// Spot light position in view space; w = cos of the cone cutoff for a
// spot light, -1 for a directional one. The falloff exponent
// (dropOffRate * 128) rides u_lightColor.w.
uniform vec4 u_lightPos;
uniform vec4 u_lightColor;
// EVSM warp exponent (x): the moments store exp(c z), exp(c z)^2 of
// the light window depth.
uniform vec4 u_evsm;
uniform mat4 u_shadowMatrix;
#ifdef TEXTURE
SAMPLER2D(s_texColor, 0);
// x = texture environment (0 modulate, 1 decal, 2 blend, 3 replace),
// y = the source format carries alpha (REPLACE keeps the fragment alpha
//     for alpha-less formats, invisible to the RGBA8-expanded sampler)
uniform vec4 u_texParams;
uniform vec4 u_texBlendColor;
// Bump mapping (SoBumpMap, only ever active in the TEXTURE variants —
// the mesh must carry texcoords). x = mode (0 off, 1 tangent-space
// normal map, 2 grayscale height, 3 height + parallax-occlusion),
// y = strength (normal map slope multiplier, or the height amplitude
// in UV units), zw = one texel in UV. The tangent frame comes from
// screen-space derivatives of v_vpos and the UV (the cotangent-frame
// trick) — no vertex tangents, any UV source works.
SAMPLER2D(s_texBump, 2);
uniform vec4 u_bumpParams;
// Emissive/occlusion material maps (SoFCRenderTexture; the mesh must
// carry texcoords like bump mapping). The emissive rgb adds to the lit
// and textured color; the occlusion first channel multiplies the
// ambient/environment contribution (glTF semantics).
SAMPLER2D(s_texEmissive, 4);
SAMPLER2D(s_texOcclusion, 5);
#endif

void main()
{
#ifdef CLIP_PLANES
	clipDiscard(v_wpos);
#endif

	vec4 base = mix(u_matColor, v_color0, u_params.x);
	vec3 color = base.rgb;

	vec3 n = normalize(v_normal);
#ifdef TEXTURE
	vec2 uv = v_texcoord0;
	if (u_bumpParams.x > 0.5)
	{
		vec3 dp1 = dFdx(v_vpos);
		vec3 dp2 = dFdy(v_vpos);
		vec2 duv1 = dFdx(uv);
		vec2 duv2 = dFdy(uv);
		vec3 dp2perp = cross(dp2, n);
		vec3 dp1perp = cross(n, dp1);
		vec3 T = dp2perp * duv1.x + dp1perp * duv2.x;
		vec3 B = dp2perp * duv1.y + dp1perp * duv2.y;
		float invmax = inversesqrt(max(dot(T, T), dot(B, B)));

		if (u_bumpParams.x > 2.5)
		{
			// Parallax-occlusion: march the tangent-space view
			// ray until it dips below the height field, then
			// refine linearly between the last two samples.
			vec3 vdir = u_proj[2][3] != 0.0
				? normalize(-v_vpos) : vec3(0.0, 0.0, 1.0);
			vec3 vts = vec3(dot(vdir, normalize(T)),
			                dot(vdir, normalize(B)),
			                dot(vdir, n));
			if (vts.z > 0.1)
			{
				vec2 shift = -vts.xy / vts.z
					* u_bumpParams.y / 16.0;
				float layer = 1.0 / 16.0;
				float cur = 0.0;
				vec2 tuv = uv;
				float depth = 1.0
					- texture2D(s_texBump, tuv).x;
				float pdepth = depth;
				vec2 puv = tuv;
				for (int i = 0; i < 16; ++i)
				{
					if (cur >= depth)
						break;
					puv = tuv;
					pdepth = depth;
					tuv += shift;
					cur += layer;
					depth = 1.0
						- texture2D(s_texBump, tuv).x;
				}
				float after = depth - cur;
				float before = pdepth - (cur - layer);
				uv = mix(puv, tuv,
				         clamp(before
				                   / max(before - after,
				                         1.0e-4),
				               0.0, 1.0));
			}
		}

		vec3 nts;
		if (u_bumpParams.x > 1.5)
		{
			// Grayscale height to normal, central differences.
			float hx1 = texture2D(s_texBump,
				uv + vec2(u_bumpParams.z, 0.0)).x;
			float hx0 = texture2D(s_texBump,
				uv - vec2(u_bumpParams.z, 0.0)).x;
			float hy1 = texture2D(s_texBump,
				uv + vec2(0.0, u_bumpParams.w)).x;
			float hy0 = texture2D(s_texBump,
				uv - vec2(0.0, u_bumpParams.w)).x;
			nts = vec3((hx0 - hx1) * u_bumpParams.y
			               / (2.0 * u_bumpParams.z),
			           (hy0 - hy1) * u_bumpParams.y
			               / (2.0 * u_bumpParams.w),
			           1.0);
		}
		else
		{
			// Tangent-space normal map (RGB, Coin convention).
			nts = texture2D(s_texBump, uv).xyz * 2.0
				- vec3_splat(1.0);
			nts.xy *= u_bumpParams.y;
		}
		n = normalize((T * nts.x + B * nts.y) * invmax
		              + n * nts.z);
	}
#endif

	// Ambient/environment occlusion factor of the material's occlusion
	// map: indirect light only (glTF semantics) — the constant ambient
	// term of the fixed-function paths and the IBL of the PBR path;
	// direct (head/scene) light stays untouched.
	float occ = 1.0;
#ifdef TEXTURE
	if (u_texParams.w > 0.5)
		occ = texture2D(s_texOcclusion, uv).x;
#endif

	// Variance shadow map factor of the scene light (Chebyshev upper
	// bound with light-bleed reduction); fragments outside the map stay
	// lit. Only attenuates the direct light term below.
	float shadow = 1.0;
	if (u_shadowParams.x > 0.5)
	{
		vec4 sp = mul(u_shadowMatrix, vec4(v_vpos, 1.0));
		// Spot lights render the map under a perspective camera.
		sp.xyz /= sp.w;
#ifndef OIT
		if (u_shadowParams.w > 0.5)
		{
			// debug (FC_BGFX_DEBUG_SHADOW_VIS): red = stored
			// depth (unwarped), green = stored depth^2 moment
			// (unwarped), blue = receiver light depth
			vec2 dm = texture2D(s_texShadow, sp.xy).xy;
			gl_FragColor = vec4(
				log(max(dm.x, 1.0e-12)) / u_evsm.x,
				log(max(dm.y, 1.0e-12)) / (2.0 * u_evsm.x),
				sp.z, 1.0);
			return;
		}
#endif
		if (sp.x > 0.0 && sp.x < 1.0 && sp.y > 0.0 && sp.y < 1.0
		    && sp.z > 0.0 && sp.z < 1.0)
		{
			vec2 mo = texture2D(s_texShadow, sp.xy).xy;
			// Warp the (biased) receiver depth like the caster;
			// the variance floor scales with the warped moment.
			float p = exp(u_evsm.x * (sp.z - u_shadowParams.z));
			if (p > mo.x)
			{
				float va = max(mo.y - mo.x * mo.x,
				               u_shadowParams.y
				                   * mo.x * mo.x);
				float dd = p - mo.x;
				float pmax = va / (va + dd * dd);
				shadow = clamp((pmax - 0.3) / 0.7, 0.0, 1.0);
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
		sceneL = normalize(v_vpos - u_lightPos.xyz);
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
			float metal = u_pbrParams.y;
			float rough = u_pbrParams.z;
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
			// the viewer headlight beside the shadow light) ...
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
					* u_lightColor.rgb
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
				* (u_pbrParams.w * occ) + direct;
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

			color = base.rgb * (vec3_splat(0.2 * occ + 0.8 * hdl)
					+ u_lightColor.rgb * (ndl * shadow))
				+ u_matSpecular.rgb * (hspec * 0.75)
				+ u_matSpecular.rgb * u_lightColor.rgb
					* (spec * 0.75 * shadow);
		}
		else
		{
			// headlight along the view axis
			float ndl = n.z;
			if (u_params.z > 0.5)
				ndl = abs(ndl);
			else
				ndl = max(ndl, 0.0);

			float shininess = max(u_matSpecular.w * 128.0, 1.0);
			float spec = pow(max(abs(n.z), 0.0), shininess);

			color = base.rgb * (0.2 * occ + 0.8 * ndl)
				+ u_matSpecular.rgb * (spec * 0.75);
		}
	}

	color += u_matEmissive.rgb;
	float alpha = base.a;

#ifdef TEXTURE
	// GL fixed-function texture environment, applied to the lit color
	// like GL textures the rasterized fragment (uv carries the
	// parallax offset when active).
	vec4 texel = texture2D(s_texColor, uv);
	float texmodel = u_texParams.x;
	if (texmodel < 0.5) {        // modulate
		color *= texel.rgb;
		alpha *= texel.a;
	} else if (texmodel < 1.5) { // decal
		color = mix(color, texel.rgb, texel.a);
	} else if (texmodel < 2.5) { // blend
		color = mix(color, u_texBlendColor.rgb, texel.rgb);
		alpha *= texel.a;
	} else {                     // replace
		color = texel.rgb;
		if (u_texParams.y > 0.5)
			alpha = texel.a;
	}

	// Emissive map: added after the texture environment so the base
	// color texture does not modulate the glow (glTF semantics).
	if (u_texParams.z > 0.5)
		color += texture2D(s_texEmissive, uv).rgb;
#endif

#ifdef OIT
	// Depth weight, McGuire's eq. (10): near fragments dominate. The
	// composite pass divides the accumulated premultiplied color by the
	// accumulated weighted alpha, so the weight cancels per-surface.
	float w = alpha
		* max(1.0e-2, 3.0e3 * pow(1.0 - gl_FragCoord.z, 3.0));
	gl_FragData[0] = vec4(color * alpha, alpha) * w;
	gl_FragData[1] = vec4_splat(alpha);
#else
	gl_FragColor = vec4(color, alpha);
#endif
}
