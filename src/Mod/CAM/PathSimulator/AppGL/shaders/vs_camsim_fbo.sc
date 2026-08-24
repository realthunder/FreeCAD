$input a_position, a_texcoord0
$output v_texcoord0

/*
 * VertShader2DFbo: the fullscreen quad of the deferred resolve and
 * G-buffer copy passes. The quad's positions are already clip-space
 * (pos2 + uv2 vertices); no Y-flip is needed because the targets it
 * samples are rendered by the same backend.
 */

#include <bgfx_shader.sh>

void main()
{
	gl_Position = vec4(a_position.xy, 0.0, 1.0);
	v_texcoord0 = a_texcoord0;
}
