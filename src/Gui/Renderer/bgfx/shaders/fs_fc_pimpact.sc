$input v_color0

/*
 * Particle impact splat, fragment stage (docs/RenderEngine.md §5.8):
 * the impact record, stored as it is.
 *
 * xy = world position of the hit, z = the clock reading at the hit,
 * w = its strength. No blending and no clear — the map is a standing
 * record of the most recent hit in each cell, and an old one reads as
 * an expired ring on its own, because the surface ages every record it
 * finds against the current clock.
 */

#include <bgfx_shader.sh>

void main()
{
	gl_FragColor = v_color0;
}
