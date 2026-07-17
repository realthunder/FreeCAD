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
#ifdef TEXTURE
SAMPLER2D(s_texColor, 0);
// x = texture environment (0 modulate, 1 decal, 2 blend, 3 replace),
// y = the source format carries alpha (REPLACE keeps the fragment alpha
//     for alpha-less formats, invisible to the RGBA8-expanded sampler)
uniform vec4 u_texParams;
uniform vec4 u_texBlendColor;
#endif

void main()
{
#ifdef CLIP_PLANES
	clipDiscard(v_wpos);
#endif

	vec4 base = mix(u_matColor, v_color0, u_params.x);
	vec3 color = base.rgb;

	if (u_params.y > 0.5)
	{
		vec3 n = normalize(v_normal);
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

			// Headlight, L = V = +z: ndl = ndh = ndv and vdh = 1,
			// so the Fresnel term collapses to f0. GGX distribution
			// with Karis' fast Smith-joint visibility.
			// The specular term is clamped: with a headlight every
			// facing plane sits exactly on the GGX peak (1 / pi a^2),
			// which would flash whole faces white at low roughness.
			float a = rough * rough;
			float d = ndv * ndv * (a * a - 1.0) + 1.0;
			float D = a * a / (3.14159265 * d * d);
			float vis = 0.25
				/ max(ndv * (ndv * (1.0 - a) + a), 1.0e-4);
			vec3 direct = (kd * 0.31830989
				+ f0 * min(D * vis, 4.0))
				* (ndv * 1.2);

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
				* u_pbrParams.w + direct;
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

			color = base.rgb * (0.2 + 0.8 * ndl)
				+ u_matSpecular.rgb * (spec * 0.75);
		}
	}

	color += u_matEmissive.rgb;
	float alpha = base.a;

#ifdef TEXTURE
	// GL fixed-function texture environment, applied to the lit color
	// like GL textures the rasterized fragment.
	vec4 texel = texture2D(s_texColor, v_texcoord0);
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
