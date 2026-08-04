$input v_normal, v_color0, v_vpos

/*
 * Water-jet fragment stage: a soft round droplet.
 *
 * The shape lives in the ALPHA alone and the colour stays flat across
 * the sprite. That is not a stylistic choice — the blend is ordinary
 * non-premultiplied source-alpha, so anything that dims the colour
 * towards the rim makes the mid-radius darker than both the bright
 * centre and the transparent edge, and against a bright sky every
 * droplet reads as a small dark ring. (Which is what a boosted "wet"
 * core does, and what premultiplying the falloff into the colour does
 * as well.)
 *
 * Note for anyone reusing this under Blend=Additive: additive never
 * consults the alpha channel, so a sprite shaped only in alpha is
 * drawn there as a hard opaque quad. An additive particle has to carry
 * its falloff in the colour instead — the bundled sparks effect does
 * exactly that.
 */

#include <bgfx_shader.sh>

void main()
{
	float r = dot(v_normal.xy, v_normal.xy);
	float a = max(0.0, 1.0 - r);
	gl_FragColor = vec4(v_color0.rgb, v_color0.a * a * a);
}
