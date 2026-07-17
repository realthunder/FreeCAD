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
#ifdef TEXTURE
uniform mat4 u_texMatrix;
#endif

void main()
{
	gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0));
	gl_Position.z += u_params.w * gl_Position.w;
	v_normal = mul(u_modelView, vec4(a_normal, 0.0)).xyz;
	v_color0 = a_color0;
#ifdef TEXTURE
	// GL texture matrix on (s, t, 0, 1); the per-fragment projective
	// divide collapses to a per-vertex one (exact for affine matrices).
	vec4 tc = mul(u_texMatrix, vec4(a_texcoord0, 0.0, 1.0));
	v_texcoord0 = tc.xy / (tc.w != 0.0 ? tc.w : 1.0);
#endif
	// View-space position for the bump mapping tangent frame and the
	// shadow map lookup in the fragment stage.
	v_vpos = mul(u_modelView, vec4(a_position, 1.0)).xyz;
#ifdef CLIP_PLANES
	v_wpos = mul(u_model[0], vec4(a_position, 1.0)).xyz;
#endif
}
