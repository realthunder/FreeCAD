/*
 * FreeCAD thick-line vertex shader body: expands one line segment
 * (fed as per-instance data) into a screen-space quad of the requested
 * pixel width. Included by vs_fc_line.sc and vs_fc_line_clip.sc
 * (CLIP_PLANES defined, adds the world-space position for the clip
 * fragment variant).
 *
 * a_position : quad corner; x = end of the segment (0 = A, 1 = B),
 *              y = side of the line (-1 / +1)
 * i_data0    : xyz = segment endpoint A (model space)
 * i_data1    : xyz = segment endpoint B (model space)
 * i_data2    : per-vertex color at A
 * i_data3    : per-vertex color at B
 * u_params   : y = line width in pixels
 *              z = NDC depth bias (positive pushes away from the viewer;
 *                  used by the stencil outline passes so the owning
 *                  polygon-offset fill still blends over its outline)
 *
 * The LINE_PATTERN variant additionally outputs v_dist for the stipple
 * fragment shader: x = pixel distance along the segment from A times the
 * vertex clip w, y = clip w. Dividing x/y in the fragment shader undoes
 * the hardware's perspective correction, i.e. yields the screen-linear
 * distance glLineStipple counts.
 */

uniform vec4 u_params;

void main()
{
	vec4 clipA = mul(u_modelViewProj, vec4(i_data0.xyz, 1.0));
	vec4 clipB = mul(u_modelViewProj, vec4(i_data1.xyz, 1.0));

	float t = a_position.x;
	float side = a_position.y;

	float eps = 1.0e-4;
	if (clipA.w < eps && clipB.w < eps)
	{
		// Entirely behind the camera: emit a clipped vertex.
		gl_Position = vec4(0.0, 0.0, 2.0, 1.0);
		v_color0 = vec4_splat(0.0);
#ifdef CLIP_PLANES
		v_wpos = vec3_splat(0.0);
#endif
#ifdef LINE_PATTERN
		v_dist = vec2(0.0, 1.0);
#endif
	}
	else
	{
		// Clamp the segment to the near plane so the screen-space
		// direction stays stable when one endpoint is behind the camera.
		if (clipA.w < eps)
			clipA = mix(clipA, clipB, (eps - clipA.w) / (clipB.w - clipA.w));
		else if (clipB.w < eps)
			clipB = mix(clipB, clipA, (eps - clipB.w) / (clipA.w - clipB.w));

		vec2 res = u_viewRect.zw;
		vec2 screenA = clipA.xy / clipA.w * res * 0.5;
		vec2 screenB = clipB.xy / clipB.w * res * 0.5;
		vec2 dir = screenB - screenA;
		float len = length(dir);
		dir = len > 1.0e-6 ? dir / len : vec2(1.0, 0.0);
		vec2 offset = vec2(-dir.y, dir.x)
			* (0.5 * max(u_params.y, 1.0)) * side;

		vec4 pos = mix(clipA, clipB, t);
		pos.xy += offset * (2.0 / res) * pos.w;
		pos.z += u_params.z * pos.w;
		gl_Position = pos;
		v_color0 = mix(i_data2, i_data3, t);
#ifdef CLIP_PLANES
		v_wpos = mix(mul(u_model[0], vec4(i_data0.xyz, 1.0)).xyz,
		             mul(u_model[0], vec4(i_data1.xyz, 1.0)).xyz, t);
#endif
#ifdef LINE_PATTERN
		v_dist = vec2(t * len * pos.w, pos.w);
#endif
	}
}
