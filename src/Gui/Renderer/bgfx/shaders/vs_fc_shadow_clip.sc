$input a_position
$output v_wpos

/*
 * Shadow map caster vertex shader, clipped variant: carries the world
 * position for the fragment stage's clip-plane discard.
 */

#include <bgfx_shader.sh>

void main()
{
	gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0));
	v_wpos = mul(u_model[0], vec4(a_position, 1.0)).xyz;
}
