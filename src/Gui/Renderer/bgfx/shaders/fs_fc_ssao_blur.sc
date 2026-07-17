$input v_texcoord0

/*
 * SSAO blur pass: a 4x4 box average matching the 4x4 noise tile,
 * removing the per-pixel rotation banding of the generation pass.
 */

#include <bgfx_shader.sh>

SAMPLER2D(s_texAO, 0);

void main()
{
	float sum = 0.0;
	for (int y = 0; y < 4; ++y)
	{
		for (int x = 0; x < 4; ++x)
		{
			vec2 off = vec2(float(x) - 1.5, float(y) - 1.5)
			         * u_viewTexel.xy;
			sum += texture2D(s_texAO, v_texcoord0 + off).x;
		}
	}
	gl_FragColor = vec4_splat(sum / 16.0);
}
