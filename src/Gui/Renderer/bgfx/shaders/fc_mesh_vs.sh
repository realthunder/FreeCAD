/*
 * FreeCAD CAD-mesh vertex shader body: transforms position and carries
 * the view-space normal and per-vertex color to the fragment stage.
 * Included by vs_fc_mesh.sc and vs_fc_mesh_clip.sc (CLIP_PLANES defined,
 * adds the world-space position for the clip fragment variant — bgfx
 * requires the VS output list to exactly match the FS input list, so the
 * unclipped program pair must not carry v_wpos).
 *
 * u_params.w : NDC depth bias (glPolygonOffset approximation); positive
 *              pushes the fragment away from the viewer.
 */

uniform vec4 u_params;

void main()
{
	gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0));
	gl_Position.z += u_params.w * gl_Position.w;
	v_normal = mul(u_modelView, vec4(a_normal, 0.0)).xyz;
	v_color0 = a_color0;
#ifdef CLIP_PLANES
	v_wpos = mul(u_model[0], vec4(a_position, 1.0)).xyz;
#endif
}
