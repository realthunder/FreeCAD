/*
 * FreeCAD flat fragment shader body for lines and points. Included by
 * fs_fc_flat.sc and fs_fc_flat_clip.sc (CLIP_PLANES defined, fc_clip.sh
 * included first).
 *
 * u_matColor    : rgba color; used when u_params.x == 0
 * u_matEmissive : rgb emissive add (highlight tint)
 * u_params      : x = per-vertex color, w = alpha ceiling (dims the
 *                 depth-occluded pass of on-top lines; 1 = no effect)
 */

uniform vec4 u_matColor;
uniform vec4 u_matEmissive;
uniform vec4 u_params;

void main()
{
#ifdef CLIP_PLANES
	clipDiscard(v_wpos);
#endif

	vec4 base = mix(u_matColor, v_color0, u_params.x);
	gl_FragColor = vec4(base.rgb + u_matEmissive.rgb, min(base.a, u_params.w));
}
