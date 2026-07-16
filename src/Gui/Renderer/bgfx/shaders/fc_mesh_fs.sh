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
#ifdef OIT
	// Depth weight, McGuire's eq. (10): near fragments dominate. The
	// composite pass divides the accumulated premultiplied color by the
	// accumulated weighted alpha, so the weight cancels per-surface.
	float w = base.a
		* max(1.0e-2, 3.0e3 * pow(1.0 - gl_FragCoord.z, 3.0));
	gl_FragData[0] = vec4(color * base.a, base.a) * w;
	gl_FragData[1] = vec4_splat(base.a);
#else
	gl_FragColor = vec4(color, base.a);
#endif
}
