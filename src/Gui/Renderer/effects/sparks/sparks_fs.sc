$input v_normal, v_color0, v_vpos

/*
 * Spark fragment stage: a bright core with a soft radial falloff, the
 * sprite corner riding v_normal.xy. Additive blend and disabled depth
 * write come from the program's Blend/DepthWrite properties, so a
 * spark never occludes the geometry it bounces off.
 */

#include <bgfx_shader.sh>

void main()
{
	float r = dot(v_normal.xy, v_normal.xy);
	float a = max(0.0, 1.0 - r);
	gl_FragColor = v_color0 * (a * a * (0.6 + 0.4 * a));
}
