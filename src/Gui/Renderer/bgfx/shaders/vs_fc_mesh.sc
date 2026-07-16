$input a_position, a_normal, a_color0
$output v_normal, v_color0

/*
 * FreeCAD CAD-mesh vertex shader: transforms position and carries the
 * view-space normal and per-vertex color to the fragment stage.
 */

#include <bgfx_shader.sh>

void main()
{
	gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0));
	v_normal = mul(u_modelView, vec4(a_normal, 0.0)).xyz;
	v_color0 = a_color0;
}
