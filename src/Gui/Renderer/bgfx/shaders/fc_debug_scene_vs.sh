/*
 * Debug scene re-render vertex shader body (docs/RenderDebug.md modes
 * 6/8/11): position transform + texcoord passthrough. Included by
 * vs_fc_debug_scene.sc and vs_fc_debug_scene_clip.sc (CLIP_PLANES
 * defined, adds the world-space position for the clip fragment variant
 * — bgfx requires the VS output list to exactly match the FS input
 * list, so the unclipped program pair must not carry v_wpos).
 *
 * u_params : w = NDC depth bias, the mesh programs' polygon-offset
 *            approximation. The instance-id mode (11) is a ground truth
 *            about which draw owns a pixel, so it has to resolve a fill
 *            against its own offset edges exactly the way the beauty
 *            pass does.
 *
 * ⚠️ A bgfx uniform keeps whatever the last draw left in it, and one NDC
 * unit of bias puts the geometry past the far plane. The submitting side
 * must set u_params on every draw, never rely on it being zero — an
 * inherited bias off a line draw is what made the occlusion box test
 * answer "hidden" for a whole model (docs/FarFieldProxies.md §12.6).
 */

uniform vec4 u_params;

void main()
{
	gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0));
	gl_Position.z += u_params.w * gl_Position.w;
	v_texcoord0 = a_texcoord0;
#ifdef CLIP_PLANES
	v_wpos = mul(u_model[0], vec4(a_position, 1.0)).xyz;
#endif
}
