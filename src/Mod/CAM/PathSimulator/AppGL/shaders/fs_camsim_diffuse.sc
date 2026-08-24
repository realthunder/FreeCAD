$input v_normal, v_position

/*
 * FragShaderNorm: single-light diffuse for the forward-lit passes
 * (stock and tool bodies during CSG). Light vectors are world-space,
 * matching vs_camsim_norm/invnorm.
 */

#include <bgfx_shader.sh>

uniform vec4 u_simLightPos;
uniform vec4 u_simLightColor;
uniform vec4 u_simLightAmbient;
uniform vec4 u_simObjectColor;

void main()
{
	vec3 norm = normalize(v_normal);
	vec3 lightDir = normalize(u_simLightPos.xyz - v_position);
	float diff = max(dot(norm, lightDir), 0.0);
	vec3 diffuse = diff * u_simLightColor.rgb;
	vec3 result = (u_simLightAmbient.rgb + diffuse) * u_simObjectColor.rgb;
	gl_FragColor = vec4(result, 1.0);
}
