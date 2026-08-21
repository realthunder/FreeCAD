$input v_texcoord0

/*
 * Shadow-only ground WITHOUT a quad (vs_fc_comp pair): a fullscreen
 * triangle that finds the ground plane per pixel and multiplies the
 * frame down by the shadow that falls there.
 *
 * fs_fc_groundshadow does the same shading on a rasterized quad, and
 * still serves the cases that need a surface (a ground reflection
 * depth-tests EQUAL against the quad). This one serves the mode's own
 * case, where there is no surface to speak of: the receiver is
 * INFINITE, so no sizing is needed -- which is the point, since the
 * sizing is what tied the ground to the scene's bounding box.
 *
 * Depth is written per pixel so geometry in FRONT of the plane still
 * occludes the shadow, and the plane's own depth is discarded outside
 * the frustum. Nothing is written to the depth BUFFER: a shadow is not
 * a surface, and the volumetric raymarch is supposed to carry on
 * through it.
 *
 * u_groundPlane : the plane in VIEW space, xyz unit normal and w the
 *                 offset -- a point is on it where dot(xyz, p) + w == 0
 * u_matColor    : rgb = ground colour, a = the shadow's own opacity
 *                 where it is fully dark (Coin's 1 - SoShadowTransparency)
 */

#include <bgfx_shader.sh>
#include "fc_mesh_lighting.sh"

uniform vec4 u_groundPlane;

void main()
{
	// The pixel's view-space ray. GL projection: perspective has
	// u_proj[2][3] == -1 (w = viewZ), orthographic has 0 (w = 1) --
	// the same test fc_prepassViewPos makes, and the two cases differ
	// the same way: a perspective ray fans out from the eye, an
	// orthographic one starts at the pixel and runs straight back.
	vec2 ndc = v_texcoord0 * 2.0 - vec2_splat(1.0);
	bool persp = u_proj[2][3] != 0.0;
	vec3 org;
	vec3 dir;
	if (persp)
	{
		org = vec3_splat(0.0);
		dir = vec3((ndc.x + u_proj[2][0]) / u_proj[0][0],
		           (ndc.y + u_proj[2][1]) / u_proj[1][1],
		           -1.0);
	}
	else
	{
		org = vec3((ndc.x - u_proj[3][0]) / u_proj[0][0],
		           (ndc.y - u_proj[3][1]) / u_proj[1][1],
		           0.0);
		dir = vec3(0.0, 0.0, -1.0);
	}

	float denom = dot(u_groundPlane.xyz, dir);
	// Edge on: the plane covers no pixel here, whatever it is doing
	// elsewhere.
	if (abs(denom) < 1.0e-8)
		discard;
	float t = -(dot(u_groundPlane.xyz, org) + u_groundPlane.w) / denom;
	// Behind the eye. Only a perspective ray can be: an orthographic
	// one starts on the near plane and the frustum test below is what
	// bounds it.
	if (persp && t <= 0.0)
		discard;
	vec3 vpos = org + t * dir;

	// The frustum decides the rest, exactly as it would for a
	// rasterized quad: outside it there is nothing to draw, and the
	// depth this fragment claims has to be the plane's own.
	vec4 clip = mul(u_proj, vec4(vpos, 1.0));
	if (clip.w <= 0.0)
		discard;
	float ndcz = clip.z / clip.w;
#if BGFX_SHADER_LANGUAGE_GLSL
	if (ndcz < -1.0 || ndcz > 1.0)
		discard;
	gl_FragDepth = ndcz * 0.5 + 0.5;
#else
	if (ndcz < 0.0 || ndcz > 1.0)
		discard;
	gl_FragDepth = ndcz;
#endif // BGFX_SHADER_LANGUAGE_GLSL

	// From here this is fs_fc_groundshadow verbatim, and deliberately
	// so: the two paths differ in where the receiver is, not in what a
	// shadow on it looks like.
	vec3 tint;
	float dark = 1.0 - fcSceneShadow(vpos, gl_FragCoord.xy, tint);
	float k = clamp(dark * u_matColor.a, 0.0, 1.0);
	vec3 pass = mix(vec3_splat(1.0), u_matColor.rgb * tint, k);
	gl_FragColor = vec4(pass, 1.0);
}
