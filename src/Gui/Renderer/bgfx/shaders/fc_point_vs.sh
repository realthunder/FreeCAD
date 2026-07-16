/*
 * FreeCAD point sprite vertex shader body: expands one point (fed as
 * per-instance data) into a screen-space quad of the requested pixel
 * size, replicating glPointSize's screen-aligned square. Included by
 * vs_fc_point.sc and vs_fc_point_clip.sc (CLIP_PLANES defined, adds the
 * world-space position for the clip fragment variant).
 *
 * a_position : quad corner, reusing the line quad geometry;
 *              x in {0, 1} and y in {-1, +1} map to the four corners
 * i_data0    : xyz = point position (model space)
 * i_data1    : per-vertex color
 * u_params   : y = point size in pixels
 *              z = NDC depth bias (positive pushes away from the viewer;
 *                  see fc_line_vs.sh)
 */

uniform vec4 u_params;

void main()
{
	vec4 clipP = mul(u_modelViewProj, vec4(i_data0.xyz, 1.0));

	float eps = 1.0e-4;
	if (clipP.w < eps)
	{
		// Behind the camera: emit a clipped vertex.
		gl_Position = vec4(0.0, 0.0, 2.0, 1.0);
		v_color0 = vec4_splat(0.0);
#ifdef CLIP_PLANES
		v_wpos = vec3_splat(0.0);
#endif
	}
	else
	{
		vec2 res = u_viewRect.zw;
		vec2 offset = vec2(a_position.x - 0.5, a_position.y * 0.5)
			* max(u_params.y, 1.0);
		clipP.xy += offset * (2.0 / res) * clipP.w;
		clipP.z += u_params.z * clipP.w;
		gl_Position = clipP;
		v_color0 = i_data1;
#ifdef CLIP_PLANES
		v_wpos = mul(u_model[0], vec4(i_data0.xyz, 1.0)).xyz;
#endif
	}
}
