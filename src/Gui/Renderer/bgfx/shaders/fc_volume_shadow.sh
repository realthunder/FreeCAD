/*
 * Variance-shadow-map visibility of a view-space position, shared by the
 * volumetric raymarch (fs_fc_volume) and the water caustics pass
 * (fs_fc_caustics). The including shader must bind the shadow moments
 * at sampler stage 1.
 *
 * u_lightColor: rgb = scene light color * light intensity (the spot
 *               falloff exponent rides .w)
 * u_lightDir  : xyz = light travel direction in view space
 * u_lightPos  : spot light position in view space; w = cos of the cone
 *               cutoff for a spot light, -1 for a directional one
 * u_evsm      : x = EVSM warp exponent (0 = plain VSM), y = the plain
 *               VSM light-bleed threshold
 * u_shadowParams : y = variance epsilon (the ShadowEpsilon tunable),
 *               z = EVSM depth bias — the mesh receivers' values
 *               (fc_mesh_lighting.sh), so raising the tunables cures
 *               shaft/caustics acne together with the meshes'
 */

SAMPLER2D(s_texShadow, 1);

uniform vec4 u_lightColor;
uniform vec4 u_lightDir;
uniform vec4 u_lightPos;
uniform vec4 u_evsm;
uniform vec4 u_shadowParams;
uniform mat4 u_shadowMatrix;

// Variance shadow visibility of a view-space position (the mesh
// receiver's Chebyshev bound with the same bias/variance floor and
// light-bleed linstep); outside the map = lit. A spot light adds its
// cone falloff (and a perspective shadow projection).
float shadowVis(vec3 p)
{
	float vis = 1.0;
	if (u_lightPos.w > -0.5)
	{
		float cd = dot(normalize(p - u_lightPos.xyz),
		               u_lightDir.xyz);
		if (cd <= u_lightPos.w)
			return 0.0;
		vis = pow(max(cd, 1.0e-4), u_lightColor.w);
	}
	vec4 sp = mul(u_shadowMatrix, vec4(p, 1.0));
	sp.xyz /= sp.w;
	if (sp.x <= 0.0 || sp.x >= 1.0 || sp.y <= 0.0 || sp.y >= 1.0
	    || sp.z <= 0.0 || sp.z >= 1.0)
		return vis;
	vec2 mo = texture2DLod(s_texShadow, sp.xy, 0.0).xy;
	if (u_evsm.x < 0.5)
	{
		// Plain VSM (Coin parity, see the mesh receivers);
		// u_evsm.y carries the light-bleed threshold.
		if (sp.z <= mo.x)
			return vis;
		float va = min(max(mo.y - mo.x * mo.x, 0.0)
		                   + u_shadowParams.y,
		               1.0);
		float dd = mo.x - sp.z;
		float pmax = va / (va + dd * dd);
		pmax *= smoothstep(u_evsm.y, 1.0, pmax);
		return vis * pmax;
	}
	float d = exp(u_evsm.x * (sp.z - u_shadowParams.z));
	if (d <= mo.x)
		return vis;
	float va = max(mo.y - mo.x * mo.x,
	               u_shadowParams.y * mo.x * mo.x);
	float dd = d - mo.x;
	float pmax = va / (va + dd * dd);
	return vis * clamp((pmax - 0.3) / 0.7, 0.0, 1.0);
}
