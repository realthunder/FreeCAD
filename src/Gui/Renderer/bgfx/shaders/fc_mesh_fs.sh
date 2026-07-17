/*
 * FreeCAD CAD-mesh fragment shader body: single headlight Blinn-Phong.
 * Included by fs_fc_mesh.sc and fs_fc_mesh_clip.sc (CLIP_PLANES defined,
 * fc_clip.sh included first).
 *
 * u_matColor    : rgba diffuse; used when u_params.x == 0
 * u_matEmissive : rgb emissive add
 * u_matSpecular : rgb specular, w = shininess (0..1 Coin convention)
 * u_params      : x = per-vertex color, y = lighting on, z = two-sided
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
