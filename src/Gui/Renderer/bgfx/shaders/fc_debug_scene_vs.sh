/*
 * Debug scene re-render vertex shader body (docs/RenderDebug.md modes
 * 6/8): position transform + texcoord passthrough. Included by
 * vs_fc_debug_scene.sc and vs_fc_debug_scene_clip.sc (CLIP_PLANES
 * defined, adds the world-space position for the clip fragment variant
 * — bgfx requires the VS output list to exactly match the FS input
 * list, so the unclipped program pair must not carry v_wpos).
 */

void main()
{
	gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0));
	v_texcoord0 = a_texcoord0;
#ifdef CLIP_PLANES
	v_wpos = mul(u_model[0], vec4(a_position, 1.0)).xyz;
#endif
}
