$input v_texcoord0

/*
 * Visible sun (LightConfig::sunDisc): a bright disc plus limb glow
 * added over the background along the directional scene light, before
 * the opaque pass — geometry then overdraws it, so occlusion is
 * automatic, and the bloom bright pass picks the disc up for its halo.
 * Runs with the scene projection bound (the predefined u_proj
 * reconstructs the pixel's view-space direction); orthographic cameras
 * have no per-pixel sky direction, so the pass outputs nothing there.
 *
 * u_lightDir   : xyz = light direction in view space (the direction
 *                the light travels; the sun sits at -u_lightDir)
 * u_lightColor : rgb = scene light color * intensity
 * u_sunParams  : x = cos(disc angular radius), y = cos(inner radius —
 *                the soft edge spans x..y), z = limb glow strength,
 *                w = limb glow exponent
 */

#include <bgfx_shader.sh>

uniform vec4 u_lightDir;
uniform vec4 u_lightColor;
uniform vec4 u_sunParams;

void main()
{
	if (u_proj[2][3] == 0.0)
	{
		gl_FragColor = vec4_splat(0.0);
		return;
	}
	vec2 ndc = v_texcoord0 * 2.0 - vec2_splat(1.0);
	vec3 dir = normalize(vec3((ndc.x + u_proj[2][0]) / u_proj[0][0],
	                          (ndc.y + u_proj[2][1]) / u_proj[1][1],
	                          -1.0));
	float mu = dot(dir, -normalize(u_lightDir.xyz));
	float disc = smoothstep(u_sunParams.x, u_sunParams.y, mu);
	float glow = u_sunParams.z
		* pow(max(mu, 0.0), u_sunParams.w);
	gl_FragColor = vec4(u_lightColor.rgb * (disc + glow), 1.0);
}
