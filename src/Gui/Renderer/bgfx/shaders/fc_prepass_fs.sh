/*
 * SSAO depth+normal prepass fragment shader body: encodes the
 * viewer-facing view-space normal (octahedral, float math only) and the
 * positive linear view depth into the RGBA16F prepass target.
 * .w = 1 marks a written fragment; the target clears to 0, so .w = 0
 * identifies the background in the AO pass. Included by
 * fs_fc_prepass.sc and fs_fc_prepass_clip.sc (CLIP_PLANES defined).
 */

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
	// Face the normal toward the viewer like the two-sided lighting
	// most CAD materials use; AO wants the geometric front either way.
	// The view ray is the fragment position for perspective cameras but
	// the constant view axis for orthographic ones (u_proj[2][3] is -1
	// for perspective, 0 for orthographic) — the position's lateral
	// offset would flip normals of surfaces near the eye plane.
	vec3 n = normalize(v_normal);
	vec3 viewdir = u_proj[2][3] != 0.0 ? v_vpos : vec3(0.0, 0.0, -1.0);
	if (dot(n, viewdir) > 0.0)
		n = -n;
	gl_FragColor = vec4(octEncode(n), -v_vpos.z, 1.0);
}
