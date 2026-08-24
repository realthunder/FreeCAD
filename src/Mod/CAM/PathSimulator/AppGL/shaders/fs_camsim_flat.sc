$input v_normal, v_position

/*
 * FragShaderFlat: constant color. Pairs with vs_camsim_norm (the GL
 * path's "Null" shader used for the stencil-only CSG passes), so it
 * must declare that shader's outputs even though it uses neither.
 */

#include <bgfx_shader.sh>

uniform vec4 u_simObjectColor;

void main()
{
	gl_FragColor = vec4(u_simObjectColor.rgb, 1.0);
}
