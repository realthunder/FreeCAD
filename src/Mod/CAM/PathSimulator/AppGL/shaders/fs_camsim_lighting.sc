$input v_texcoord0

/*
 * FragShaderSSAOLighting: the deferred resolve -- ambient + diffuse
 * from the G-buffer, modulated by the AO texture when u_simParams.y
 * is set (the AO texture now comes from the engine's AO effect, not
 * the GL path's own SSAO chain). Inputs are view-space, so the light
 * position must be handed over in view space too.
 */

#include <bgfx_shader.sh>

SAMPLER2D(s_simColor, 0);
SAMPLER2D(s_simPosition, 1);
SAMPLER2D(s_simNormal, 2);
SAMPLER2D(s_simAo, 3);

uniform vec4 u_simLightPos;
uniform vec4 u_simLightColor;
uniform vec4 u_simLightAmbient;
uniform vec4 u_simParams;

vec3 ambAndDiffuse(vec3 pos, vec3 norm, vec3 diff, float ao)
{
	ao = pow(ao, 4.0);
	vec3 ambient = u_simLightAmbient.rgb * diff * ao;
	vec3 s = normalize(u_simLightPos.xyz - pos);
	float sDotN = max(dot(s, norm), 0.0);
	return ambient + u_simLightColor.rgb * diff * sDotN;
}

void main()
{
	vec3 pos = texture2D(s_simPosition, v_texcoord0).xyz;
	vec3 norm = texture2D(s_simNormal, v_texcoord0).xyz;
	vec4 diffColorA = texture2D(s_simColor, v_texcoord0);
	float aoVal = u_simParams.y > 0.5 ? texture2D(s_simAo, v_texcoord0).r : 1.0;

	vec3 col = ambAndDiffuse(pos, norm, diffColorA.rgb, aoVal);
	col = pow(col, vec3_splat(1.0 / 2.2));

	gl_FragColor = vec4(col, diffColorA.a);
}
