$input v_texcoord0

/*
 * Plain fullscreen copy of a texture (vs_fc_comp pair): resolves the
 * scene color into the sampleable copy the water surface refraction
 * reads (rendering into the copy framebuffer also makes bgfx resolve a
 * multisampled scene attachment before the water pass samples it).
 */

#include <bgfx_shader.sh>

SAMPLER2D(s_texScene, 0);

void main()
{
	gl_FragColor = texture2D(s_texScene, v_texcoord0);
}
