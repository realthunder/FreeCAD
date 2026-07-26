$input v_normal, v_color0, v_vpos

/*
 * Ember particle fragment stage: a hard bright core with a soft
 * falloff — the sprite corner rides v_normal.xy; additive blend and
 * disabled depth write come from the program's Blend/DepthWrite
 * properties.
 */

#include <bgfx_shader.sh>

void main()
{
	float r = dot(v_normal.xy, v_normal.xy);
	float a = max(0.0, 1.0 - r);
	gl_FragColor = v_color0 * (a * a * (0.5 + 0.5 * a));
}
