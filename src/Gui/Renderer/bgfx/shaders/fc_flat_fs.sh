/*
 * FreeCAD flat fragment shader body for lines and points. Included by
 * fs_fc_flat.sc and fs_fc_flat_clip.sc (CLIP_PLANES defined, fc_clip.sh
 * included first).
 *
 * u_matColor    : rgba color; used when u_params.x == 0
 * u_matEmissive : rgb emissive add (highlight tint)
 * u_params      : x = per-vertex color, y = line width in pixels
 *                 (LINE_AA; negative disables the coverage), w = alpha
 *                 ceiling (dims the depth-occluded pass of on-top lines
 *                 and the pass behind glass; 1 = no effect)
 *
 * The LINE_AA variant (paired with the vs_fc_line vertex shaders)
 * resolves the line's coverage analytically instead of leaving it to
 * the rasterizer. Without it a screen-space quad of the requested pixel
 * width lands on a different subpixel phase at every orientation, so an
 * edge visibly breathes between crisp and soft as the model turns --
 * MSAA hides some of that and none of it when MSAA is off. A box filter
 * over the pixel, coverage = clamp(halfWidth + 0.5 - |d|, 0, 1),
 * integrates across the line to exactly the requested width, so the
 * visual weight is the same at every angle and fractional widths mean
 * something. This is Blender's gpu_shader_3D_polyline model.
 *
 * The LINE_PATTERN variant (paired with the vs_fc_line_pat vertex
 * shaders) replicates glLineStipple: v_dist recovers the screen-space
 * pixel distance along the segment, each pattern bit covers `factor`
 * pixels, unset bits discard.
 *
 * u_linePattern : x = 16-bit pattern, y = pixel repeat factor
 *
 * The MARKER variant (paired with the vs_fc_marker vertex shaders)
 * draws a point as its SoMarkerSet bitmap, the way glBitmap does: a
 * fixed pixel pattern, unset bits discard. v_texcoord0 is the pixel
 * centre's offset from the point, v_vpos the atlas cell + 1 (0 for a
 * plain point, which keeps its whole square) and the bitmap's size.
 * The bitmap's lower-left pixel is the one GL puts it at,
 * floor(point - (size - 1) / 2), which is where the ceil below comes
 * from; the small bias keeps an exact tie on GL's side of it.
 *
 * s_markerAtlas : cells of MARKER_CELL pixels, MARKER_COLUMNS to a row,
 *                 one byte per pixel, rows bottom-up like the bitmaps
 */

uniform vec4 u_matColor;
uniform vec4 u_matEmissive;
uniform vec4 u_params;
#ifdef LINE_PATTERN
uniform vec4 u_linePattern;
#endif
#ifdef MARKER
#define MARKER_CELL 32.0
#define MARKER_COLUMNS 16.0
SAMPLER2D(s_markerAtlas, 0);
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

#ifdef MARKER
	float cell = floor(v_vpos.x + 0.5);
	if (cell > 0.5)
	{
		vec2 size = floor(v_vpos.yz + 0.5);
		vec2 px = ceil(v_texcoord0 + size * 0.5 - 1.001);
		if (px.x < 0.0 || px.y < 0.0 || px.x >= size.x || px.y >= size.y)
			discard;
		float c = cell - 1.0;
		vec2 origin = vec2(mod(c, MARKER_COLUMNS), floor(c / MARKER_COLUMNS))
			* MARKER_CELL;
		vec2 extent = vec2_splat(MARKER_CELL * MARKER_COLUMNS);
		if (texture2D(s_markerAtlas, (origin + px + 0.5) / extent).x < 0.5)
			discard;
	}
#endif

	float cov = 1.0;

#ifdef LINE_AA
	// A negative width is the id pass asking for hard coverage; the
	// quad it drew was not feathered either, so no ramp is possible.
	if (u_params.y > 0.0)
	{
		float halfw = 0.5 * max(u_params.y, 1.0);
		float d = abs(v_line.x / max(v_line.y, 1.0e-6));
		cov = clamp(halfw + 0.5 - d, 0.0, 1.0);
		// The feather's outermost fragments resolve to nothing.
		// Dropping them keeps the depth these draws write to the
		// line's own silhouette rather than a pixel wider than it.
		if (cov <= 0.0)
			discard;
	}
#endif

	vec4 base = mix(u_matColor, v_color0, u_params.x);
	// Coverage multiplies whatever alpha the draw already had; the
	// ceiling is a separate clamp, so a dimmed line behind glass and
	// a feathered edge compose instead of one masking the other.
	gl_FragColor = vec4(base.rgb + u_matEmissive.rgb,
	                    min(base.a, u_params.w) * cov);
}
