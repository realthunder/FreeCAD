/*
 * The variance shadow test every receiver shares: mesh surfaces
 * (fc_mesh_lighting.sh), the water surface (fc_water_surface.sh) and
 * the volumetric media (fc_volume_shadow.sh) each owned a copy of this
 * Chebyshev bound. The copies are how the volumetric pass came to
 * hard-code its bias while the mesh read the tunable — raising
 * ShadowEpsilon fixed the meshes and left the shafts with acne.
 *
 * The tap takes the sampled moments rather than a uv, because each
 * receiver binds the shadow map on its own sampler slot (and the
 * volumetric one samples with an explicit lod).
 *
 * Include AFTER declaring:
 *   u_evsm         : x = EVSM exponent (< 0.5 selects plain VSM),
 *                    y = light-bleed threshold
 *   u_shadowParams : y = variance epsilon (the ShadowEpsilon tunable),
 *                    z = the EVSM depth reference
 */

#ifndef FC_SHADOW_TAP_SH
#define FC_SHADOW_TAP_SH

// Visibility in [0,1] of a receiver at light-window depth z, given the
// (depth, depth^2) moments sampled from the shadow map.
float fc_vsmVisibility(vec2 mo, float z)
{
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

#endif // FC_SHADOW_TAP_SH
