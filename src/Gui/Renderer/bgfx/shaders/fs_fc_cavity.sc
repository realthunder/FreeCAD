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
 * A hard crease would otherwise come out as a staircase, and one that no
 * multisampling reaches: the prepass holds one normal a pixel, this pass
 * runs after the scene is resolved, and a pair of neighbours either
 * straddles the crease or does not, so the band it darkens is a whole
 * number of pixels wide everywhere. The pass has to smooth it itself, and
 * it has what that takes. Each neighbour's normal is read as the AVERAGE
 * over its pixel: where the pixel next to it lies on another face, the two
 * faces' planes -- a normal and a view position each, both in the prepass
 * -- say where their crease crosses the screen, to a fraction of a pixel,
 * and the far face is weighed in by the part of the pixel it covers
 * (fc_cavityNormal). The term is then a difference of two box-filtered
 * normals, which moves smoothly as a crease moves across the pixel grid,
 * and the darkening a crease holds across its width is what it was.
 *
 * s_texNormalZ    : prepass oct-normal (xy) + linear view depth (z) +
 *                   coverage (w); background stays untouched
 * u_cavityParams  : x = valley strength, y = ridge strength,
 *                   zw = the baseline, a whole number of prepass texels
 * u_cavityParams2 : xy = one prepass texel, z = 1 when the prepass depth
 *                   is full float (a half-float one cannot place a crease
 *                   within a pixel, and the normals are read as they are)
 */

#include <bgfx_shader.sh>
#include "fc_matrix.sh"
#include "fc_prepass_read.sh"

SAMPLER2D(s_texNormalZ, 0);

uniform vec4 u_cavityParams;
uniform vec4 u_cavityParams2;

// How fast a surface may recede and still be one surface: a depth
// difference between two texels of more than this many times their
// distance across the screen is a step from one surface to another (a
// silhouette), and anything less is a slope -- 16 is a face 86 degrees from
// facing the eye. Measured against the distance across, not against the
// depth itself: a fraction of the depth is the same thing under a
// perspective camera, where a pixel's size grows with the depth, but under
// an orthographic one it has nothing to do with the pixel's size, and
// there the creases of whatever lay near the camera dropped out -- sooner
// the wider the baseline and the further the view was zoomed out.
#define FC_CAVITY_STEEP 16.0

// Whether two texels that are close in depth still lie on two surfaces
// rather than on one that folds: a face in front of another, by its edge.
// nA and nB are their normals, ab leads from the first one's position to
// the second's. Two faces of one folded surface each lie on the same side
// of the other's plane -- both behind for a ridge, both in front for a
// valley. A point that lies behind the other face's plane while that
// face's point lies in front of its own is on a surface that passes behind
// the other one; the slope test alone let those through wherever the gap
// was small, as a faint stroke up every edge that rises from a face it
// stands on. Within three degrees of a common plane the signs say nothing
// (a smooth surface that turns from convex to concave has them opposed
// too), and nothing is concluded.
bool fc_cavityStep(vec3 nA, vec3 nB, vec3 ab)
{
	float offA = dot(nA, ab);
	float offB = -dot(nB, ab);
	return offA * offB < 0.0
	    && min(abs(offA), abs(offB)) > 0.05 * length(ab);
}

// The normal of the prepass texel at uv (nz is its content, n and p its
// normal and view position), averaged over the texel's square.
//
// Inside a face that is the texel's own normal. Next to a crease the
// square holds two faces, and which one the single sample fell on is what
// a staircase is made of. So for each of the four texels beside this one
// that lies on another face: the two faces are planes, this one through p
// with normal n and that one through pB with normal nB, and the surface
// follows the first up to the line where they meet. g is the second
// plane's equation taken along the first, as a function of the screen
// position: g0 at this texel's centre, growing by (g.x, g.y) a pixel. It
// is zero on the crease, so the crease runs |g0| / length(g) pixels from
// the centre, and the part of a unit square beyond a line at that distance
// is, to the usual approximation, 0.5 - |g0| / (|g.x| + |g.y|). One crease
// is taken, the one that takes most of the square.
//
// It is a blend of two normals that are both in the prepass, by at most a
// half, so whatever the geometry it cannot make up a normal that is not
// there; and it fades out below a turn of a few degrees, where a
// tessellated curve's interpolated normals and flat facets no longer say
// where anything meets, which leaves smooth curvature exactly as it was.
vec3 fc_cavityNormal(vec2 uv, vec4 nz, vec3 n, vec3 p, bool persp)
{
	if (u_cavityParams2.z < 0.5)
		return n;
	// The line of sight through the texel, scaled to one unit of view
	// depth, and the view-space size of a pixel at this depth.
	vec3 ray = persp ? p / nz.z : vec3(0.0, 0.0, -1.0);
	float nRay = dot(n, ray);
	// Seen edge-on, the face's plane says nothing about where it goes
	// across the screen.
	if (abs(nRay) < 0.02)
		return n;
	vec2 pixel = 2.0 * u_cavityParams2.xy * (persp ? nz.z : 1.0)
	    / abs(vec2(FC_MTX(u_proj, 0, 0), FC_MTX(u_proj, 1, 1)));

	vec2 side[4];
	side[0] = vec2(u_cavityParams2.x, 0.0);
	side[1] = vec2(-u_cavityParams2.x, 0.0);
	side[2] = vec2(0.0, u_cavityParams2.y);
	side[3] = vec2(0.0, -u_cavityParams2.y);

	float most = 0.0;
	vec3 other = n;
	for (int k = 0; k < 4; ++k)
	{
		vec2 uvB = uv + side[k];
		vec4 nzB = texture2D(s_texNormalZ, uvB);
		// The same tests the pairs below make: off the geometry or
		// across a depth step is not the same surface.
		if (nzB.w < 0.5 || abs(nzB.z - nz.z)
		        > FC_CAVITY_STEEP * (k < 2 ? pixel.x : pixel.y))
			continue;
		// The same face, read off the encoded normals before anything
		// is decoded: this is nearly every texel of the frame.
		vec2 enc = nzB.xy - nz.xy;
		if (dot(enc, enc) < 1.0e-6)
			continue;
		vec3 nB = fc_octDecode(nzB.xy);
		vec3 dn = nB - n;
		// 2 to 6 degrees of turn between the two texels.
		float crease = smoothstep(0.0012, 0.011, dot(dn, dn));
		if (crease <= 0.0)
			continue;
		vec3 pB = fc_prepassViewPos(uvB, nzB.z, persp);
		if (fc_cavityStep(n, nB, pB - p))
			continue;
		float g0 = dot(nB, p - pB);
		vec2 g = pixel * (nB.xy - n.xy * (dot(nB, ray) / nRay));
		float part = crease
		    * clamp(0.5 - abs(g0) / max(abs(g.x) + abs(g.y), 1.0e-20),
		            0.0, 0.5);
		if (part > most)
		{
			most = part;
			other = nB;
		}
	}
	return n + (other - n) * most;
}

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
	// The baseline in view units at the centre's depth, each way, times
	// the slope allowed: what a neighbour's depth may differ by.
	vec2 reach = FC_CAVITY_STEEP * 2.0 * texel * (persp ? cz : 1.0)
	    / abs(vec2(FC_MTX(u_proj, 0, 0), FC_MTX(u_proj, 1, 1)));

	float curv = 0.0;
	for (int i = 0; i < 2; ++i)
	{
		vec2 uvM = v_texcoord0 - pairOff[i];
		vec2 uvP = v_texcoord0 + pairOff[i];
		vec4 nzM = texture2D(s_texNormalZ, uvM);
		vec4 nzP = texture2D(s_texNormalZ, uvP);
		// Off the geometry, or across a depth step: curvature read over
		// a silhouette is meaningless and would draw a halo around every
		// object. The tolerance is the baseline's own length across the
		// screen (FC_CAVITY_STEEP), so it holds at any distance and
		// under either camera.
		// Both sides must be on the same surface, otherwise the pair
		// says nothing -- fall back to the centre, which contributes 0.
		float most = (i == 0) ? reach.x : reach.y;
		if (nzM.w < 0.5 || nzP.w < 0.5
		    || abs(nzM.z - cz) > most
		    || abs(nzP.z - cz) > most)
			continue;
		vec3 pM = fc_prepassViewPos(uvM, nzM.z, persp);
		vec3 pP = fc_prepassViewPos(uvP, nzP.z, persp);
		vec3 axis = pP - pM;
		float len = length(axis);
		if (len <= 0.0)
			continue;
		vec3 nM = fc_octDecode(nzM.xy);
		vec3 nP = fc_octDecode(nzP.xy);
		if (fc_cavityStep(nM, nP, axis))
			continue;
		nM = fc_cavityNormal(uvM, nzM, nM, pM, persp);
		nP = fc_cavityNormal(uvP, nzP, nP, pP, persp);
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
