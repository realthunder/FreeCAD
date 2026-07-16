$input v_color0

/*
 * FreeCAD flat fragment shader for lines and points.
 *
 * u_matColor    : rgba color; used when u_params.x == 0
 * u_matEmissive : rgb emissive add (highlight tint)
 * u_params      : x = per-vertex color
 */

#include <bgfx_shader.sh>

uniform vec4 u_matColor;
uniform vec4 u_matEmissive;
uniform vec4 u_params;

void main()
{
	vec4 base = mix(u_matColor, v_color0, u_params.x);
	gl_FragColor = vec4(base.rgb + u_matEmissive.rgb, base.a);
}
