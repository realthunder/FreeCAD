$input v_texcoord0

/*
 * PBR environment background (PBRConfig::envBackground): draws the
 * image based lighting environment itself instead of the gradient
 * background quad, so reflective surfaces visibly mirror their
 * surroundings. Runs on the background view with the scene view/proj
 * bound (the shared fullscreen vertex shader ignores the matrices;
 * the predefined u_proj/u_invView reconstruct each pixel's world
 * direction). Orthographic cameras have no per-pixel ray fan, so a
 * fixed 45-degree virtual field of view around the view axis keeps
 * the sky gradient readable there.
 *
 * u_pbrParams : y = softening (cubemap lod), w = environment
 *               intensity (same slot as the mesh lighting)
 * s_texEnv    : GGX-prefiltered environment cubemap, world space
 */

#include <bgfx_shader.sh>

uniform vec4 u_pbrParams;
SAMPLERCUBE(s_texEnv, 1);

void main()
{
	vec2 ndc = v_texcoord0 * 2.0 - vec2_splat(1.0);
	vec3 dir;
	if (u_proj[2][3] != 0.0)
		dir = vec3((ndc.x + u_proj[2][0]) / u_proj[0][0],
		           (ndc.y + u_proj[2][1]) / u_proj[1][1],
		           -1.0);
	else
		dir = vec3(ndc.x * 0.41421356, ndc.y * 0.41421356, -1.0);
	vec3 dw = normalize(mul(u_invView, vec4(dir, 0.0)).xyz);
	vec3 col = textureCubeLod(s_texEnv, dw, u_pbrParams.y).rgb
		* u_pbrParams.w;
	gl_FragColor = vec4(col, 1.0);
}
