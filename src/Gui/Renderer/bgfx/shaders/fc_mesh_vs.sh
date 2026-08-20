/*
 * FreeCAD CAD-mesh vertex shader body: transforms position and carries
 * the view-space normal and per-vertex color to the fragment stage.
 * Included by vs_fc_mesh.sc and vs_fc_mesh_clip.sc (CLIP_PLANES defined,
 * adds the world-space position for the clip fragment variant — bgfx
 * requires the VS output list to exactly match the FS input list, so the
 * unclipped program pair must not carry v_wpos).
 *
 * u_params.w : constant NDC depth bias — glPolygonOffset's `units * r`
 *              term; positive pushes the fragment away from the viewer.
 * u_polyOffset.x : glPolygonOffset's `factor`, multiplying the depth
 *              slope computed below. 0 disables the slope term.
 * u_polyOffset.y : ceiling on the depth gradient the slope term will
 *              track, in NDC depth per NDC screen unit — 1 means a
 *              surface crossing the whole depth range within one screen
 *              width. A polygon seen edge-on has an unbounded gradient;
 *              GL never has to deal with that because such a polygon
 *              covers no pixels, but a per-vertex normal on the
 *              silhouette of a tessellated cylinder does, and without a
 *              ceiling the band around it would sink through whatever is
 *              behind it. Bounding the gradient rather than the
 *              resulting depth keeps the term scaling with the
 *              projection the way GL's own does.
 *
 * INSTANCED variant (vs_fc_mesh_inst / vs_fc_mesh_tex_inst): the model
 * transform comes from the per-instance data instead of the predefined
 * u_model chain — i_data0-3 are the columns of the instance's model
 * matrix (the same GL-layout 16 floats DrawCall::model holds), i_data4
 * its diffuse color. u_instParams.x selects the per-vertex color stream
 * over the per-instance color (the fragment stage always reads v_color0
 * in the instanced path, u_params.x is forced to 1 by the submitter).
 * The TEXTURE combination carries the texture coordinates exactly like
 * the non-instanced textured variant.
 */

#include "fc_color.sh"

uniform vec4 u_params;
uniform vec4 u_polyOffset;
#ifdef TEXTURE
uniform mat4 u_texMatrix;
#endif
#ifdef INSTANCED
uniform vec4 u_instParams;
#endif

/*
 * The slope half of glPolygonOffset: GL offsets a filled polygon by
 * `factor * m + units * r`, where m is the polygon's maximum depth slope
 * in window space (depth per pixel) and r is one depth-buffer LSB. The
 * constant half rides in u_params.w; this is m, times factor.
 *
 * m comes from the plane of the surface at this vertex, taken from the
 * view-space position and normal. Transforming that plane into clip
 * space and dividing the plane equation through by w expresses z_ndc as
 * an affine function of the NDC x/y — the same fact that lets the
 * rasterizer interpolate depth linearly in screen space — so the screen
 * gradient of depth is constant over the polygon and reads straight off
 * the coefficients. Scaling by the NDC size of a pixel turns it into
 * depth-per-pixel. The window/NDC depth scale (2 on OpenGL and WebGL2,
 * 1 on the [0,1] backends) cancels between m and the clip-space z this
 * result is added to, so it appears here only in u_params.w, which the
 * submitter scales.
 *
 * A plane transforms by the inverse transpose of the point transform;
 * u_invProj is that inverse, and mul(vector, matrix) is the transpose
 * multiply. Using the shading normal rather than the facet normal is
 * deliberate: on a tessellated cylinder it is the true surface slope,
 * which is what the edge drawn along that surface has to clear.
 */
float fcPolygonOffsetSlope(vec3 vpos, vec3 vnormal)
{
	if (u_polyOffset.x == 0.0)
		return 0.0;
	vec4 plane = vec4(vnormal, -dot(vnormal, vpos));
	vec4 pc = mul(plane, u_invProj);
	vec2 grad = min(abs(pc.xy) / max(abs(pc.z), 1.0e-9),
	                vec2_splat(u_polyOffset.y));
	vec2 perPixel = grad * 2.0 / max(u_viewRect.zw, vec2(1.0, 1.0));
	return u_polyOffset.x * max(perPixel.x, perPixel.y);
}

void main()
{
	// Object-space position and normal, for the procedural surface
	// finish (fc_finish.sh): its pattern is anchored to the geometry
	// rather than to the world, so an instanced or moved copy carries
	// the same finish instead of one that swims as it is placed. The
	// same in both variants: the model transform is exactly what they
	// differ in, and it is applied after this.
	v_opos = a_position;
	v_onrm = a_normal;
#ifdef INSTANCED
	mat4 model = mtxFromCols(i_data0, i_data1, i_data2, i_data3);
	vec4 wpos = mul(model, vec4(a_position, 1.0));
	gl_Position = mul(u_viewProj, wpos);
	v_normal = mul(u_view, vec4(mul(model, vec4(a_normal, 0.0)).xyz,
	                            0.0)).xyz;
	v_color0 = fcAuthoredColor4(
		u_instParams.x > 0.5 ? a_color0 : i_data4);
	v_color1 = fcAuthoredColor4(a_color1);
	v_color2 = fcAuthoredColor4(a_color2);
	v_findex = a_color3.xy;
	v_vpos = mul(u_view, wpos).xyz;
	gl_Position.z += (u_params.w + fcPolygonOffsetSlope(v_vpos, v_normal))
		* gl_Position.w;
#ifdef TEXTURE
	vec4 tc = mul(u_texMatrix, vec4(a_texcoord0, 0.0, 1.0));
	v_texcoord0 = tc.xy / (tc.w != 0.0 ? tc.w : 1.0);
#endif
#else
	gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0));
	v_normal = mul(u_modelView, vec4(a_normal, 0.0)).xyz;
	v_color0 = fcAuthoredColor4(a_color0);
	// Per-face material stream (bound only for meshes that carry one;
	// elsewhere the attributes read the API default, and the fragment
	// stage multiplies them out — u_matEmissive.w selects).
	v_color1 = fcAuthoredColor4(a_color1);
	v_color2 = fcAuthoredColor4(a_color2);
	// The stream's third slot: the surface finish palette index and,
	// beside it, the index of the PROJECTION FRAME that finish is laid
	// out in. Integers 0..255 rather than normalized bytes (the
	// attribute is unnormalized) - the same u_matEmissive.w gate
	// decides whether the fragment stage reads them.
	v_findex = a_color3.xy;
#ifdef TEXTURE
	// GL texture matrix on (s, t, 0, 1); the per-fragment projective
	// divide collapses to a per-vertex one (exact for affine matrices).
	vec4 tc = mul(u_texMatrix, vec4(a_texcoord0, 0.0, 1.0));
	v_texcoord0 = tc.xy / (tc.w != 0.0 ? tc.w : 1.0);
#endif
	// View-space position for the bump mapping tangent frame and the
	// shadow map lookup in the fragment stage — and for the depth
	// slope, which is why it is computed before the bias is applied.
	v_vpos = mul(u_modelView, vec4(a_position, 1.0)).xyz;
	gl_Position.z += (u_params.w + fcPolygonOffsetSlope(v_vpos, v_normal))
		* gl_Position.w;
#ifdef CLIP_PLANES
	v_wpos = mul(u_model[0], vec4(a_position, 1.0)).xyz;
#endif
#endif
}
