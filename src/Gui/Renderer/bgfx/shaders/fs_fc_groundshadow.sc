$input v_normal, v_color0, v_color1, v_color2, v_vpos, v_opos, v_onrm, v_findex

/*
 * Shadow-only ground (vs_fc_mesh pair, so the quad's depth matches the
 * solid ground draw exactly): the receiver plane carries the shadow and
 * nothing else -- transparent where the scene light reaches it.
 *
 * This is Coin's TRANSPARENT_SHADOWED shadow style
 * (SoShadowGroup.cpp: "mydiffuse.a = shadow_alpha" where the fragment
 * is at all shadowed), which the RenderShadow_GroundTransparency = 1
 * case selected there and this backend had not ported: it drew no
 * ground at all instead.
 *
 * Coin switched the alpha on a threshold (anything darker than
 * accumShade 0.999 got the full shadow alpha), which puts a hard step
 * around a soft shadow's border. The alpha follows the shadow factor
 * here instead, so the penumbra fades out as it does on any other
 * receiver.
 *
 * u_matColor : rgb = ground colour, a = the shadow's own opacity where
 *              it is fully dark (Coin's 1 - SoShadowTransparency)
 */

#include <bgfx_shader.sh>
#include "fc_mesh_lighting.sh"

void main()
{
	vec3 tint;
	float dark = 1.0 - fcSceneShadow(v_vpos, gl_FragCoord.xy, tint);
	// A shadow DARKENS what is behind it, and this draw is submitted
	// multiplied (BGFX_STATE_BLEND_MULTIPLY) rather than blended over
	// the frame so that it always does. Blending a mid grey over the
	// background only darkens a background lighter than the grey; over
	// a dark one it would paint the shadow BRIGHTER than the scene it
	// falls on.
	//
	// So the fragment is not a colour but a transmittance: how much of
	// what is behind survives. u_matColor is what survives where the
	// shadow is fully dark (the ground colour, linear -- the CPU
	// decoded it), its alpha how far toward that a partly shadowed
	// fragment goes, and a coloured glass caster's tint rides along as
	// the transmittance it already is.
	//
	// Fully lit ground comes out as white -- a multiply by one, which
	// changes nothing -- rather than discarded: the quad then writes
	// the same depth over its whole span that the solid ground does,
	// which is what a ground reflection depth-tests EQUAL against.
	float k = clamp(dark * u_matColor.a, 0.0, 1.0);
	vec3 transmitted = mix(vec3_splat(1.0), u_matColor.rgb * tint, k);
	gl_FragColor = vec4(transmitted, 1.0);
}
