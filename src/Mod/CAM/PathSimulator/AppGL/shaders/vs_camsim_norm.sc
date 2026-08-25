$input a_position, a_normal
$output v_normal, v_position

/*
 * CAM simulator lit-geometry vertex shader (VertShader3DNorm in the
 * GL path, Shader.cpp): world-space position and a CPU-supplied
 * normal rotation. gl_Position rides bgfx's predefined
 * u_modelViewProj; the GL path's separate model/view/projection
 * uniforms are the facade's setTransform/setPassTransform.
 */

#include <bgfx_shader.sh>

uniform mat4 u_simNormalRot;

void main()
{
	gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0));
	v_position = mul(u_model[0], vec4(a_position, 1.0)).xyz;
	v_normal = mul(u_simNormalRot, vec4(a_normal, 1.0)).xyz;
}
