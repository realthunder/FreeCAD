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
	// A coloured glass caster tints the shadow it throws, the same
	// transmittance the lit surfaces multiply their direct term by.
	//
	// Lit ground comes out at alpha 0 rather than discarded: the quad
	// then writes the same depth over its whole span that the solid
	// ground does, which is what a ground reflection depth-tests
	// EQUAL against -- and there is nothing under a receiver plane for
	// the depth to hide.
	gl_FragColor = vec4(u_matColor.rgb * tint, dark * u_matColor.a);
}
