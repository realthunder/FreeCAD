/*
 * FreeCAD section-cap vertex shader body: a world-space quad lying in the
 * clip plane, with hatch texture coordinates. Included by vs_fc_cap.sc and
 * vs_fc_cap_clip.sc (CLIP_PLANES defined, adds the world-space position
 * for the clip fragment variant — the cap of one plane is clipped by the
 * remaining planes).
 */

void main()
{
	gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0));
	v_texcoord0 = a_texcoord0;
#ifdef CLIP_PLANES
	v_wpos = mul(u_model[0], vec4(a_position, 1.0)).xyz;
#endif
}
