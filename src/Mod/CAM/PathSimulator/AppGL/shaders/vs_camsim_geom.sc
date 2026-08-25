$input a_position, a_normal
$output v_normal, v_position

/*
 * VertShaderGeom: the G-buffer geometry pass. Unlike vs_camsim_norm,
 * position and normal are VIEW-space (the deferred resolve and the AO
 * effect light there). u_simParams.x holds the invertedNormals flag
 * (the GL path's bool uniform).
 */

#include <bgfx_shader.sh>

uniform vec4 u_simParams;

void main()
{
	vec4 viewPos = mul(u_modelView, vec4(a_position, 1.0));
	v_position = viewPos.xyz;
	vec3 n = u_simParams.x > 0.5 ? -a_normal : a_normal;
	v_normal = mul(u_modelView, vec4(n, 0.0)).xyz;
	gl_Position = mul(u_proj, viewPos);
}
