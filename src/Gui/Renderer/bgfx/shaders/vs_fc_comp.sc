$input a_position
$output v_texcoord0

/*
 * Fullscreen composite vertex shader (weighted-blended OIT resolve):
 * the input is a clip-space triangle covering the viewport, the UV is
 * derived from it. The OIT targets are rendered by the same backend, so
 * no cross-API Y-flip is needed.
 */

#include <bgfx_shader.sh>

void main()
{
	gl_Position = vec4(a_position.xy, 0.0, 1.0);
	v_texcoord0 = a_position.xy * 0.5 + 0.5;
}
