$input v_normal, v_color0, v_vpos

/*
 * Water-jet fragment stage: a soft droplet with a bright core.
 *
 * The falloff is squared for the body and raised to a high power for
 * the highlight, so a droplet has an edge instead of being a gaussian
 * blob — spray is made of things with surfaces, and a pure gaussian
 * reads as smoke. The alpha blend (Blend=Alpha, depth write off) is
 * what keeps a deep jet from blowing out to white the way the
 * additive stateless fountain does.
 */

#include <bgfx_shader.sh>

void main()
{
	float r = dot(v_normal.xy, v_normal.xy);
	float a = max(0.0, 1.0 - r);
	float body = a * a;
	float core = body * body * body;
	vec3 c = v_color0.rgb * (0.82 + 0.75 * core);
	gl_FragColor = vec4(c, v_color0.a * body);
}
