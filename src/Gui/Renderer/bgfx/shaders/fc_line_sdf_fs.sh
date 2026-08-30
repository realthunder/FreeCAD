/*
 * Line distance-field writer body. Included by fs_fc_line_sdf.sc and
 * fs_fc_line_sdf_clip.sc (CLIP_PLANES defined, fc_clip.sh included
 * first).
 *
 * Emits the line colour and, in alpha, the field the glass pass
 * resamples (fc_line_sdf.sh).
 *
 * The field is written EVERYWHERE the quad reaches, glass over the
 * texel or not: the glass pass samples it at the REFRACTED uv, which
 * lands outside the glass footprint as a matter of course -- reaching
 * sideways is what a lens does. Culling the field to the footprint
 * (the first version did) emptied it exactly where a strong lens
 * looks, and edges thinned or vanished mid-body. What must still be
 * kept out is a line IN FRONT of the glass, which draws normally in
 * ViewGlassLine and must not also appear warped inside the body: where
 * this texel has glass, the entry depth answers that here; where it
 * has none, no answer exists at write time (in front of WHICH glass?),
 * so the fragment's own view depth rides the aux target and the glass
 * pass compares it against its own entry.
 *
 * The nearest decoration wins each texel, which is a min over signed
 * distance. Depth does that: the distance rides gl_FragDepth and the
 * ordinary LESS test resolves it, so overlapping edges compose the way
 * a distance field must rather than the way a painter would.
 *
 * POINT_SDF switches the distance function to the box one for vertex
 * sprites; everything else is shared.
 */

uniform vec4 u_matColor;
uniform vec4 u_params;

SAMPLER2D(s_texGlassFront, 3);

void main()
{
#ifdef CLIP_PLANES
	clipDiscard(v_wpos);
#endif

	vec2 uv = gl_FragCoord.xy * u_viewTexel.xy;
	vec4 gfront = texture2D(s_texGlassFront, uv);
	float fragZ = -v_vpos.z;
	// Glass over this texel and the fragment in front of where the
	// body starts: not seen through the body, and it already draws
	// undistorted in ViewGlassLine.
	if (gfront.w > 0.5 && fragZ <= gfront.z)
		discard;

	float halfw = 0.5 * max(u_params.y, 1.0);
#ifdef POINT_SDF
	// A point sprite is a screen-aligned SQUARE (glPointSize's shape,
	// which fc_point_vs reproduces), so its distance function is the
	// box one. A radial distance would round the corners and make a
	// vertex seen through glass a different shape from the same vertex
	// beside it.
	float pw = max(v_line.y, 1.0e-6);
	vec2 off = vec2(v_line.x, v_dist.x) / pw;
	float dc = max(abs(off.x), abs(off.y));
	// A box distance DOES have a fall direction almost everywhere: it
	// is whichever axis dominates, since max() ignores the other one.
	// Writing that axis lets the glass pass scale a sprite along the
	// direction it actually falls in, the same path a line takes.
	//
	// It used to write the zero vector, which sent the reader to its
	// mean-of-both-axes branch, and the note that shipped with it
	// called the residual "a corner, not a size". It was a size: with
	// s the MEAN, the drawn half extents come out `s * halfw / |grad
	// u|` and `s * halfw / |grad v|`, so a sprite that should be
	// square on screen renders with an aspect equal to the whole local
	// warp anisotropy. Measured through an ior-1.6 ball lens
	// (scripts/lens_point_aniso.py), a 9px vertex reached 5.6 x 14.5
	// at r/R 0.81 -- 38% short one way and 61% long the other, against
	// 6.3 x 15.8 predicted by that arithmetic. Per-axis scaling makes
	// it square again.
	//
	// The axis is piecewise constant and flips across the sprite's
	// diagonals, where a box distance genuinely has no gradient; a
	// filtered fetch there blends the two into a 45-degree axis. That
	// is the error this leaves, and it IS a corner this time.
	//
	// Screen-aligned, so the sprite's local axes are the fragCoord
	// axes and no y-flip correction is needed on the top-down APIs --
	// an AXIS is sign-free, and the reader takes |J^T p|.
	vec2 axis = abs(off.x) >= abs(off.y) ? vec2(1.0, 0.0)
	                                     : vec2(0.0, 1.0);
#else
	// The SIGNED perpendicular offset is linear across the quad --
	// unlike its absolute value, whose finite differences cancel at
	// the fold on the centreline -- so its derivatives recover the
	// perpendicular axis exactly, and in gl_FragCoord space, the same
	// space the glass pass takes its Jacobian in on every backend
	// (an NDC-space axis from the vertex stage would need a y-flip on
	// the APIs whose fragCoord runs top-down).
	float sd = v_line.x / max(v_line.y, 1.0e-6);
	float dc = abs(sd);
	vec2 axis = vec2(dFdx(sd), dFdy(sd));
	float alen = length(axis);
	// An AXIS, not a direction: canonicalize the sign by the dominant
	// component, so texels contested between adjacent segments of a
	// polyline blend between near-identical vectors instead of
	// cancelling where one segment ran reversed.
	axis = alen > 1.0e-6
		? axis / alen * ((abs(axis.x) >= abs(axis.y)
		                  ? axis.x : axis.y) >= 0.0 ? 1.0 : -1.0)
		: vec2_splat(0.0);
#endif
	if (dc - halfw >= FC_LINE_SDF_RADIUS)
		discard;

	// u_params.x = 0 selects the constant colour over the vertex
	// stream, exactly as the flat shader does.
	vec4 base = mix(u_matColor, v_color0, u_params.x);
	// Ranked by distance to the EDGE, so the decoration a pixel is
	// closest to being inside wins it -- a thick line beats a thin one
	// passing the same distance from the centre.
	gl_FragDepth = clamp(0.5 + (dc - halfw)
	                     / (2.0 * FC_LINE_SDF_RADIUS), 0.0, 1.0);
	// RT0 carries the colour and the distance to the CENTRE. RT1
	// carries what turns that distance into post-lens coverage: the
	// line's own perpendicular axis (the direction the field falls
	// along, so the glass pass can project the lens's Jacobian onto
	// it -- carried explicitly because the glass pass must NOT try to
	// recover it from the resampled field, whose finite differences
	// cancel across the fold at the line's core and killed exactly
	// the pixels that matter), the fragment's view depth for the
	// behind-the-glass test, and the half width to threshold against,
	// which no derivative can tell it.
	gl_FragData[0] = vec4(base.rgb, FC_LINE_SDF_RADIUS - dc);
	gl_FragData[1] = vec4(axis, fragZ, halfw);
}
