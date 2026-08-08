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
#include "fc_shadow_tap.sh"   // the shared VSM/EVSM bound
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
	return vis * fc_vsmVisibility(mo, sp.z);
}
