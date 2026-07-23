$input v_texcoord0

/*
 * Bloom blur: one direction of a separable gaussian over the
 * quarter-res bloom source (the shadow-moments blur pattern — every
 * texel pair folded into one bilinear fetch at the weight-balanced
 * offset, dense at any radius). u_bloomBlur.xy is the unit blur
 * direction, .z the gaussian sigma in texels, .w the fetch pair count.
 */

#include <bgfx_shader.sh>

SAMPLER2D(s_texBloom, 0);

uniform vec4 u_bloomBlur;

void main()
{
	vec2 dir = u_bloomBlur.xy * u_viewTexel.xy;
	float s2 = 2.0 * u_bloomBlur.z * u_bloomBlur.z;
	int n = int(u_bloomBlur.w + 0.5);
	vec3 m = texture2D(s_texBloom, v_texcoord0).rgb;
	float wsum = 1.0;
	for (int i = 1; i <= 64; ++i)
	{
		if (i > n)
			break;
		float x1 = float(2 * i - 1);
		float x2 = float(2 * i);
		float w1 = exp(-x1 * x1 / s2);
		float w2 = exp(-x2 * x2 / s2);
		float w = w1 + w2;
		float o = (x1 * w1 + x2 * w2) / w;
		m += texture2D(s_texBloom, v_texcoord0 + dir * o).rgb * w;
		m += texture2D(s_texBloom, v_texcoord0 - dir * o).rgb * w;
		wsum += 2.0 * w;
	}
	gl_FragColor = vec4(m / wsum, 1.0);
}
