$input v_normal, v_color0, v_color1, v_color2, v_vpos, v_opos, v_onrm, v_findex

/*
 * Ground reflection overlay (vs_fc_mesh pair, so the quad's depth
 * matches the shadow ground draw exactly): blends the mirrored-camera
 * scene render onto the ground plane. The reflection texture is sampled
 * at the fragment's own screen position — the mirrored scene projects
 * to the same pixel the reflected eye ray exits through — and its alpha
 * (0 where nothing reflected) scales the blend together with the
 * intensity factor.
 *
 * u_reflParams: x = reflection intensity, yzw = unused
 */

#include <bgfx_shader.sh>

SAMPLER2D(s_texScene, 0);

uniform vec4 u_reflParams;

void main()
{
	vec2 uv = gl_FragCoord.xy * u_viewTexel.xy;
	vec4 r = texture2D(s_texScene, uv);
	gl_FragColor = vec4(r.rgb, r.a * u_reflParams.x);
}
