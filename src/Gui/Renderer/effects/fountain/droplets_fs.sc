$input v_normal, v_color0, v_vpos

/*
 * Fountain-droplet particle fragment stage: a soft round sprite —
 * the corner rides v_normal.xy, the additive blend and disabled
 * depth write come from the program's Blend/DepthWrite properties.
 */

#include <bgfx_shader.sh>

void main()
{
	float r = dot(v_normal.xy, v_normal.xy);
	float a = max(0.0, 1.0 - r);
	gl_FragColor = v_color0 * (a * a);
}
