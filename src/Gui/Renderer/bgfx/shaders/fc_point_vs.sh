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
 *
 * MARKER (vs_fc_marker*): i_data0.w says what the point is drawn as,
 * a SoMarkerSet bitmap from the marker atlas in place of the square:
 *   0      a plain point, the square above;
 *   < 0    nothing (SoMarkerSet::NONE -- GL draws nothing either);
 *   else   atlas cell + 1, plus 4096 * (width + 64 * height).
 * The quad is the bitmap plus a pixel of margin all round, so that the
 * pixels fs_fc_marker reads a bit for are always fully covered; the
 * pixel offset from the point and the cell/size go to the fragment
 * stage in v_texcoord0 and v_vpos.
 */

#include "fc_color.sh"
#ifdef POINT_SDF
#include "fc_line_sdf.sh"
#endif

uniform vec4 u_params;

void main()
{
	vec4 clipP = mul(u_modelViewProj, vec4(i_data0.xyz, 1.0));

	float eps = 1.0e-4;
#ifdef MARKER
	float code = i_data0.w;
	if (clipP.w < eps || code < 0.0)
#else
	if (clipP.w < eps)
#endif
	{
		// Behind the camera: emit a clipped vertex.
		gl_Position = vec4(0.0, 0.0, 2.0, 1.0);
		v_color0 = vec4_splat(0.0);
#ifdef POINT_SDF
		v_line = vec2(0.0, 1.0);
		v_dist = vec2(0.0, 0.0);
		v_vpos = vec3_splat(0.0);
#endif
#ifdef MARKER
		v_texcoord0 = vec2_splat(0.0);
		v_vpos = vec3_splat(0.0);
#endif
#ifdef CLIP_PLANES
		v_wpos = vec3_splat(0.0);
#endif
	}
	else
	{
		vec2 res = u_viewRect.zw;
#ifdef POINT_SDF
		// Support around the sprite, sized by how far the lens can
		// compress rather than by the sprite -- see fc_line_vs.sh.
		float reach = FC_LINE_SDF_RADIUS;
		vec2 offset = vec2(a_position.x - 0.5, a_position.y * 0.5)
			* (2.0 * reach);
#elif defined(MARKER)
		float cell = mod(code, 4096.0);
		float dims = floor(code / 4096.0);
		vec2 bitmap = vec2(mod(dims, 64.0), floor(dims / 64.0));
		float quad = cell > 0.5 ? max(bitmap.x, bitmap.y) + 2.0
			: max(u_params.y, 1.0);
		vec2 offset = vec2(a_position.x - 0.5, a_position.y * 0.5) * quad;
		v_texcoord0 = offset;
		v_vpos = vec3(cell, bitmap);
#else
		vec2 offset = vec2(a_position.x - 0.5, a_position.y * 0.5)
			* max(u_params.y, 1.0);
#endif
		clipP.xy += offset * (2.0 / res) * clipP.w;
		clipP.z += u_params.z * clipP.w;
		gl_Position = clipP;
		v_color0 = fcAuthoredColor4(i_data1);
#ifdef POINT_SDF
		// The corner's offset from the sprite centre in pixels, split
		// across the two spare varyings and pre-multiplied by w so the
		// fragment stage can undo the perspective divide the same way
		// the line path does.
		v_line = vec2(offset.x * clipP.w, clipP.w);
		v_dist = vec2(offset.y * clipP.w, 0.0);
		v_vpos = mul(u_modelView, vec4(i_data0.xyz, 1.0)).xyz;
#endif
#ifdef CLIP_PLANES
		v_wpos = mul(u_model[0], vec4(i_data0.xyz, 1.0)).xyz;
#endif
	}
}
