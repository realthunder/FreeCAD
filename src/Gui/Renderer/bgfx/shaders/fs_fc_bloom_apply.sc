$input v_texcoord0

/*
 * Bloom composite: the blurred quarter-res halo added onto the scene
 * (blend ONE / ONE), scaled by the bloom intensity
 * (u_bloomParams.y). The bilinear upsample of the wide gaussian is
 * smooth enough on its own — no extra filtering.
 */

#include <bgfx_shader.sh>

SAMPLER2D(s_texBloom, 0);

uniform vec4 u_bloomParams;

void main()
{
	gl_FragColor = vec4(texture2D(s_texBloom, v_texcoord0).rgb
	                        * u_bloomParams.y,
	                    1.0);
}
