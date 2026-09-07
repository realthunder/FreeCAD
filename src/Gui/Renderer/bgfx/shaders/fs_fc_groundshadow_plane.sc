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
#include "fc_screen.sh"
#include "fc_matrix.sh"
#include "fc_mesh_lighting.sh"

uniform vec4 u_groundPlane;

void main()
{
	// The pixel's view-space ray. A perspective projection has -1 in
	// the w row's z entry (w = viewZ) and an orthographic one 0
	// (w = 1) -- the same test fc_prepassViewPos makes, and the two
	// cases differ the same way: a perspective ray fans out from the
	// eye, an orthographic one starts at the pixel and runs straight
	// back.
	vec2 ndc = fc_uvToNdc(v_texcoord0);
	bool persp = FC_MTX(u_proj, 2, 3) != 0.0;
	vec3 org;
	vec3 dir;
	if (persp)
	{
		org = vec3_splat(0.0);
		dir = vec3((ndc.x + FC_MTX(u_proj, 2, 0)) / FC_MTX(u_proj, 0, 0),
		           (ndc.y + FC_MTX(u_proj, 2, 1)) / FC_MTX(u_proj, 1, 1),
		           -1.0);
	}
	else
	{
		org = vec3((ndc.x - FC_MTX(u_proj, 3, 0)) / FC_MTX(u_proj, 0, 0),
		           (ndc.y - FC_MTX(u_proj, 3, 1)) / FC_MTX(u_proj, 1, 1),
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
	// one starts on the near plane, and a hit in front of that is
	// depth-clamped below like any other.
	if (persp && t <= 0.0)
		discard;
	vec3 vpos = org + t * dir;

	// The frustum does NOT decide the rest. The receiver is
	// infinite, so the camera's depth bracket is none of its
	// business: during an orthographic spin the bracket transiently
	// hugs the model alone (NavigationStyle::reorientCamera writes
	// near/far, and this pass consumes the camera before Coin's
	// auto-clip corrects them), and a discard here cut the shadow
	// along the far plane on isolated frames. Clamp instead. Past
	// the far plane the fragment claims a depth just inside it, so
	// real geometry -- all of it nearer than far -- still occludes
	// the shadow while the cleared background does not; nearer than
	// near it claims the near plane itself, where anything that
	// could have occluded it was clipped anyway.
	vec4 clip = mul(u_proj, vec4(vpos, 1.0));
	if (clip.w <= 0.0)
		discard;
	float ndcz = clip.z / clip.w;
#if BGFX_SHADER_LANGUAGE_GLSL
	gl_FragDepth = clamp(ndcz * 0.5 + 0.5, 0.0, 0.99999);
#else
	gl_FragDepth = clamp(ndcz, 0.0, 0.99999);
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
