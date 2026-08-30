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
 *              w   = stipple run: model-space distance from the start
 *                    of the polyline this segment belongs to (0 on a
 *                    segment that starts one)
 * i_data1    : xyz = segment endpoint B (model space)
 *              w   = this segment's model-space length (0 = unknown,
 *                    the stipple then counts from A as it used to)
 * i_data2    : per-vertex color at A
 * i_data3    : per-vertex color at B
 * u_params   : y = line width in pixels. A NEGATIVE width means the
 *                  same width with analytic coverage turned off: the
 *                  quad is not feathered and fs_fc_line does not
 *                  modulate alpha. Only the cull-audit id pass asks for
 *                  that -- it decodes exact integers out of the id
 *                  image and reads alpha < 0.5 as "no draw owns this
 *                  pixel", so a coverage ramp would both blur the ids
 *                  and orphan every edge pixel.
 *              z = NDC depth bias (positive pushes away from the viewer;
 *                  used by the stencil outline passes so the owning
 *                  polygon-offset fill still blends over its outline)
 *
 * v_line     : x = signed perpendicular distance from the segment
 *              centre in pixels, times the vertex clip w
 *              y = that clip w. The fragment shader divides x/y to undo
 *              the hardware's perspective correction (v_dist uses the
 *              same trick) and resolves coverage from the result.
 *
 * The LINE_PATTERN variant additionally outputs v_dist for the stipple
 * fragment shader: x = pixel distance along the polyline (this segment
 * plus the run before it) times the vertex clip w, y = clip w. Dividing
 * x/y in the fragment shader undoes the hardware's perspective
 * correction, i.e. yields the screen-linear distance glLineStipple
 * counts.
 */

#include "fc_color.sh"

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
		v_line = vec2(0.0, 1.0);
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
		// Half a pixel of feather on each side, which is exactly
		// where the fragment shader's box-filter coverage reaches
		// zero -- widening further would only shade fragments that
		// resolve to nothing. The width still floors at 1px, as
		// GL's rasterization does, so a hairline stays visible;
		// above that it is free to be fractional, the coverage
		// carries the remainder.
		float halfw = 0.5 * max(abs(u_params.y), 1.0);
		float edge = halfw + (u_params.y < 0.0 ? 0.0 : 0.5);
		vec2 offset = vec2(-dir.y, dir.x) * edge * side;

		vec4 pos = mix(clipA, clipB, t);
		pos.xy += offset * (2.0 / res) * pos.w;
		pos.z += u_params.z * pos.w;
		gl_Position = pos;
		v_line = vec2(edge * side * pos.w, pos.w);
		v_color0 = fcAuthoredColor4(mix(i_data2, i_data3, t));
#ifdef CLIP_PLANES
		v_wpos = mix(mul(u_model[0], vec4(i_data0.xyz, 1.0)).xyz,
		             mul(u_model[0], vec4(i_data1.xyz, 1.0)).xyz, t);
#endif
#ifdef LINE_PATTERN
		// Where this vertex falls in the pattern: the run already
		// covered before this segment plus the distance along it.
		// The run arrives in model units (i_data0.w, with the
		// segment's own model length in i_data1.w) and is scaled by
		// this segment's screen-per-model ratio — exact under an
		// orthographic camera, and under perspective continuous
		// enough along a tessellated curve that the dashes survive
		// it. Restarting per segment instead drew such a curve
		// solid, every segment being shorter than one dash.
		float mdlLen = i_data1.w;
		float runPx = mdlLen > 1.0e-9 ? i_data0.w * (len / mdlLen) : 0.0;
		v_dist = vec2((runPx + t * len) * pos.w, pos.w);
#endif
	}
}
