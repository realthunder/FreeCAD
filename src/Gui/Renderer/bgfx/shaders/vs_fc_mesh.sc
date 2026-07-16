$input a_position, a_normal, a_color0
$output v_normal, v_color0

/*
 * FreeCAD CAD-mesh vertex shader: transforms position and carries the
 * view-space normal and per-vertex color to the fragment stage.
 *
 * u_params.w : NDC depth bias (glPolygonOffset approximation); positive
 *              pushes the fragment away from the viewer.
 */

#include <bgfx_shader.sh>

uniform vec4 u_params;

void main()
{
	gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0));
	gl_Position.z += u_params.w * gl_Position.w;
	v_normal = mul(u_modelView, vec4(a_normal, 0.0)).xyz;
	v_color0 = a_color0;
}
