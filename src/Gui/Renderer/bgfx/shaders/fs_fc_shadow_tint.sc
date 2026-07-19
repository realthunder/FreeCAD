/*
 * Glass shadow tint caster (vs_fc_shadow pair): writes the glass
 * body's per-channel light transmittance into the shadow tint map with
 * multiplicative blending (the target clears to white; stacked glass
 * multiplies). u_matColor carries the glass diffuse — a colored glass
 * tints the light it passes, a white one only softens it.
 */

#include <bgfx_shader.sh>

uniform vec4 u_matColor;

void main()
{
	gl_FragColor = vec4(vec3_splat(0.3) + 0.5 * u_matColor.rgb, 1.0);
}
