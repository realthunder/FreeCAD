/*
 * FreeCAD section-cap fragment shader body: flat fill color modulated by
 * the hatch texture (GL_MODULATE parity with lighting disabled; a 1x1
 * white texture stands in when hatching is off). Included by fs_fc_cap.sc
 * and fs_fc_cap_clip.sc (CLIP_PLANES defined, fc_clip.sh included first).
 *
 * u_matColor : rgba cap fill color (possibly inverted, SectionFillInvert)
 */

uniform vec4 u_matColor;

SAMPLER2D(s_texHatch, 0);

void main()
{
#ifdef CLIP_PLANES
	clipDiscard(v_wpos);
#endif

	gl_FragColor = u_matColor * texture2D(s_texHatch, v_texcoord0);
}
