$input v_texcoord0

/*
 * Variance shadow map blur (ShadowSmoothBorder): one direction of a
 * separable gaussian over the moments texture. u_shadowBlur.xy is the
 * blur direction scaled to the base tap step in texels; the linear
 * sampler folds the 9-tap kernel into 5 fetches.
 */

#include <bgfx_shader.sh>

SAMPLER2D(s_texShadow, 0);

uniform vec4 u_shadowBlur;

void main()
{
	vec2 stp = u_shadowBlur.xy * u_viewTexel.xy;
	vec2 o1 = stp * 1.3846153846;
	vec2 o2 = stp * 3.2307692308;
	vec2 m = texture2D(s_texShadow, v_texcoord0).xy * 0.2270270270;
	m += texture2D(s_texShadow, v_texcoord0 + o1).xy * 0.3162162162;
	m += texture2D(s_texShadow, v_texcoord0 - o1).xy * 0.3162162162;
	m += texture2D(s_texShadow, v_texcoord0 + o2).xy * 0.0702702703;
	m += texture2D(s_texShadow, v_texcoord0 - o2).xy * 0.0702702703;
	gl_FragColor = vec4(m, 0.0, 1.0);
}
