$input v_normal, v_color0, v_vpos

/*
 * Rain particle fragment stage: a soft vertical streak — narrow
 * falloff across the sprite, gentle fade toward both ends; the
 * additive blend and disabled depth write come from the program's
 * Blend/DepthWrite properties.
 */

#include <bgfx_shader.sh>

void main()
{
	float ax = max(0.0, 1.0 - v_normal.x * v_normal.x);
	float ay = max(0.0, 1.0 - v_normal.y * v_normal.y);
	gl_FragColor = v_color0 * (ax * ax * ay);
}
