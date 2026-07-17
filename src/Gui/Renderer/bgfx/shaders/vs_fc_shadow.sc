$input a_position

/*
 * Shadow map caster vertex shader: position-only transform under the
 * light's view/projection (set as the shadow view's transform).
 */

#include <bgfx_shader.sh>

void main()
{
	gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0));
}
