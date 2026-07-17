/*
 * SSAO depth+normal prepass vertex shader body: transforms position and
 * carries the view-space normal and view-space position to the fragment
 * stage. Included by vs_fc_prepass.sc and vs_fc_prepass_clip.sc
 * (CLIP_PLANES defined, adds the world-space position for the clip
 * fragment variant — bgfx requires the VS output list to exactly match
 * the FS input list, so the unclipped program pair must not carry
 * v_wpos).
 */

void main()
{
	gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0));
	v_normal = mul(u_modelView, vec4(a_normal, 0.0)).xyz;
	v_vpos = mul(u_modelView, vec4(a_position, 1.0)).xyz;
#ifdef CLIP_PLANES
	v_wpos = mul(u_model[0], vec4(a_position, 1.0)).xyz;
#endif
}
