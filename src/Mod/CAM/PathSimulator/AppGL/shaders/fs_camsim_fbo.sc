$input v_texcoord0

/*
 * FragShader2dFbo: plain fullscreen copy of a texture (the cached
 * frame blit that lets a repaint skip re-running the CSG).
 */

#include <bgfx_shader.sh>

SAMPLER2D(s_simTex, 0);

void main()
{
	gl_FragColor = texture2D(s_simTex, v_texcoord0);
}
