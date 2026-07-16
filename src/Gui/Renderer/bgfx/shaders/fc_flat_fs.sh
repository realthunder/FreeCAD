/*
 * FreeCAD flat fragment shader body for lines and points. Included by
 * fs_fc_flat.sc and fs_fc_flat_clip.sc (CLIP_PLANES defined, fc_clip.sh
 * included first).
 *
 * u_matColor    : rgba color; used when u_params.x == 0
 * u_matEmissive : rgb emissive add (highlight tint)
 * u_params      : x = per-vertex color, w = alpha ceiling (dims the
 *                 depth-occluded pass of on-top lines; 1 = no effect)
 *
 * The LINE_PATTERN variant (paired with the vs_fc_line_pat vertex
 * shaders) replicates glLineStipple: v_dist recovers the screen-space
 * pixel distance along the segment, each pattern bit covers `factor`
 * pixels, unset bits discard.
 *
 * u_linePattern : x = 16-bit pattern, y = pixel repeat factor
 */

uniform vec4 u_matColor;
uniform vec4 u_matEmissive;
uniform vec4 u_params;
#ifdef LINE_PATTERN
uniform vec4 u_linePattern;
#endif

void main()
{
#ifdef CLIP_PLANES
	clipDiscard(v_wpos);
#endif

#ifdef LINE_PATTERN
	// Float-only bit test ((pattern >> bit) & 1): bgfx's GL backend may
	// emit version-less GLSL where integer bit ops are unavailable.
	float dist = v_dist.x / max(v_dist.y, 1.0e-6);
	float bit = mod(floor(dist / max(u_linePattern.y, 1.0)), 16.0);
	if (mod(floor(u_linePattern.x / exp2(bit)), 2.0) < 0.5)
		discard;
#endif

	vec4 base = mix(u_matColor, v_color0, u_params.x);
	gl_FragColor = vec4(base.rgb + u_matEmissive.rgb, min(base.a, u_params.w));
}
