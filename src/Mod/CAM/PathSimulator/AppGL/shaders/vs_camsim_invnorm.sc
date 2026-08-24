$input a_position, a_normal
$output v_normal, v_position

/*
 * VertShader3DInvNorm: identical to vs_camsim_norm but with the
 * normal negated -- the CSG tool passes render back faces as the cut
 * surface, so their normals must face the camera.
 */

#include <bgfx_shader.sh>

uniform mat4 u_simNormalRot;

void main()
{
	gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0));
	v_position = mul(u_model[0], vec4(a_position, 1.0)).xyz;
	v_normal = -mul(u_simNormalRot, vec4(a_normal, 1.0)).xyz;
}
