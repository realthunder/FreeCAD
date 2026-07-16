/*
 * FreeCAD flat vertex shader body for lines and points (no lighting).
 * Included by vs_fc_flat.sc and vs_fc_flat_clip.sc (CLIP_PLANES defined,
 * adds the world-space position for the clip fragment variant).
 */

void main()
{
	gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0));
	v_color0 = a_color0;
#ifdef CLIP_PLANES
	v_wpos = mul(u_model[0], vec4(a_position, 1.0)).xyz;
#endif
}
