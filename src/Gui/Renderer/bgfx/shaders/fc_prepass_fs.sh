/*
 * SSAO depth+normal prepass fragment shader body: encodes the
 * viewer-facing view-space normal (octahedral, float math only) and the
 * positive linear view depth into the RGBA16F prepass target.
 * .w = 1 marks a written fragment; the target clears to 0, so .w = 0
 * identifies the background in the AO pass. Included by
 * fs_fc_prepass.sc and fs_fc_prepass_clip.sc (CLIP_PLANES defined).
 */

#include "fc_matrix.sh"

#ifdef GROUND_FADE
#include "fc_ground_fade.sh"
#endif

vec2 octEncode(vec3 n)
{
	n /= abs(n.x) + abs(n.y) + abs(n.z);
	if (n.z < 0.0)
	{
		vec2 sn = vec2(n.x >= 0.0 ? 1.0 : -1.0,
		               n.y >= 0.0 ? 1.0 : -1.0);
		return (vec2_splat(1.0) - abs(n.yx)) * sn;
	}
	return n.xy;
}

void main()
{
#ifdef CLIP_PLANES
	clipDiscard(v_wpos);
#endif
#ifdef GROUND_FADE
	// The ground's faded rim is not there, so it does not occupy space
	// either: the volumetric raymarch ends its rays on this depth, and
	// a shaft must carry on through a ground it cannot see.
	if (fcGroundFade(v_vpos) < 0.004)
		discard;
#endif
	// Face the normal toward the viewer like the two-sided lighting
	// most CAD materials use; AO wants the geometric front either way.
	// The view ray is the fragment position for perspective cameras but
	// the constant view axis for orthographic ones (the w row's z
	// entry is -1
	// for perspective, 0 for orthographic) — the position's lateral
	// offset would flip normals of surfaces near the eye plane.
	vec3 n = normalize(v_normal);
	vec3 viewdir = FC_MTX(u_proj, 2, 3) != 0.0 ? v_vpos : vec3(0.0, 0.0, -1.0);
	if (dot(n, viewdir) > 0.0)
		n = -n;
	gl_FragColor = vec4(octEncode(n), -v_vpos.z, 1.0);
}
