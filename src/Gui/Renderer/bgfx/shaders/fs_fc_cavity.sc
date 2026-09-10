$input v_texcoord0

/*
 * Screen-space cavity (curvature) shading: one fullscreen multiply over
 * the finished opaque scene that darkens concave creases and convex
 * ridges, so surface shape reads without depending on the lighting.
 *
 * Curvature is the screen-space divergence of the prepass NORMAL field,
 * per opposed neighbour pair:
 *
 *     term = dot(n_minus - n_plus, normalize(p_plus - p_minus))
 *
 * positive where the neighbourhood's normals tilt inward (a valley --
 * inside corners, fillets, pockets), negative where they splay outward
 * (a ridge -- outside corners, chamfers).
 *
 * Deriving it from normals rather than from positions is what makes it
 * usable on tessellated CAD geometry. The obvious estimator -- the
 * neighbours' signed distance from the centre's tangent plane -- reads
 * the *positions*, which are piecewise linear across a tessellation, so
 * every facet boundary on a sphere or a cylinder registers as a crease
 * and the surface comes out wearing its own triangle grid. Interpolated
 * normals vary smoothly across those same facets while still jumping at
 * a real edge (which exceeds the crease angle and splits the normals),
 * so the artifact goes and the feature stays.
 *
 * The positions are still read, but only to orient each pair, over a
 * two-texel baseline: that keeps the sign right without assuming how
 * the UV axes map onto view space.
 *
 * Both terms darken. The pass multiplies the 8-bit scene color, which
 * cannot brighten past white, so the ridge *highlight* a workbench
 * renderer would add is deliberately not attempted here -- see
 * CavityRidge in RenderParams.py.
 *
 * s_texNormalZ   : prepass oct-normal (xy) + linear view depth (z) +
 *                  coverage (w); background stays untouched
 * u_cavityParams : x = valley strength, y = ridge strength,
 *                  zw = prepass texel size
 */

#include <bgfx_shader.sh>
#include "fc_matrix.sh"
#include "fc_prepass_read.sh"

SAMPLER2D(s_texNormalZ, 0);

uniform vec4 u_cavityParams;

void main()
{
	vec4 cnz = texture2D(s_texNormalZ, v_texcoord0);
	// No geometry here: multiply by white so the background, and every
	// pass that already composited into it, is left exactly as it was.
	if (cnz.w < 0.5)
	{
		gl_FragColor = vec4_splat(1.0);
		return;
	}

	bool persp = FC_MTX(u_proj, 2, 3) != 0.0;
	float cz = cnz.z;

	vec2 texel = u_cavityParams.zw;
	vec2 pairOff[2];
	pairOff[0] = vec2(texel.x, 0.0);
	pairOff[1] = vec2(0.0, texel.y);

	float curv = 0.0;
	for (int i = 0; i < 2; ++i)
	{
		vec2 uvM = v_texcoord0 - pairOff[i];
		vec2 uvP = v_texcoord0 + pairOff[i];
		vec4 nzM = texture2D(s_texNormalZ, uvM);
		vec4 nzP = texture2D(s_texNormalZ, uvP);
		// Off the geometry, or across a depth step: curvature read over
		// a silhouette is meaningless and would draw a halo around every
		// object. The tolerance is relative so it holds at any distance.
		// Both sides must be on the same surface, otherwise the pair
		// says nothing -- fall back to the centre, which contributes 0.
		if (nzM.w < 0.5 || nzP.w < 0.5
		    || abs(nzM.z - cz) > 0.02 * cz
		    || abs(nzP.z - cz) > 0.02 * cz)
			continue;
		vec3 pM = fc_prepassViewPos(uvM, nzM.z, persp);
		vec3 pP = fc_prepassViewPos(uvP, nzP.z, persp);
		vec3 axis = pP - pM;
		float len = length(axis);
		if (len <= 0.0)
			continue;
		vec3 nM = fc_octDecode(nzM.xy);
		vec3 nP = fc_octDecode(nzP.xy);
		curv += dot(nM - nP, axis / len);
	}
	curv *= 0.5;

	float valley = max(curv, 0.0) * u_cavityParams.x;
	float ridge = max(-curv, 0.0) * u_cavityParams.y;
	float mult = 1.0 - clamp(valley + ridge, 0.0, 1.0);

	// The multiplier is a smooth ramp landing on an 8-bit target, so it
	// bands. Break it with interleaved-gradient noise, faded out with
	// the darkening itself: where nothing is darkened (mult == 1, i.e.
	// every flat surface and the whole background) the dither must
	// contribute nothing at all, or it shows up as noise over the scene.
	float ign = fract(52.9829189 *
	                  fract(dot(gl_FragCoord.xy,
	                            vec2(0.06711056, 0.00583715))));
	mult += (ign - 0.5) * (3.0 / 255.0) * (1.0 - mult);

	gl_FragColor = vec4(mult, mult, mult, 1.0);
}
