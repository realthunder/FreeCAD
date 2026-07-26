$input a_position, a_normal, a_color0
$output v_normal, v_color0, v_vpos

/*
 * Fountain-droplet particle vertex stage (bundled fountain effect,
 * docs/RenderEngine.md §5.11): expands the generated seed quads near
 * the plume top into droplet billboards arcing outward and falling
 * back under gravity. Stateless — position is a pure function of the
 * seed attributes and the effect clock (a_normal.xy = corner,
 * a_normal.z = index, a_color0 = per-particle random seed).
 * Param_Arc / Param_Size tune the travel scale and sprite size in
 * model units.
 */

#include <bgfx_shader.sh>

uniform vec4 u_fcTime;
uniform vec4 u_Arc;
uniform vec4 u_Size;

void main()
{
	vec2 corner = a_normal.xy;
	float idx = a_normal.z;
	vec3 seed = a_color0.xyz;
	float life = fract(u_fcTime.x * (0.25 + 0.20 * seed.y)
	                   + seed.x + idx * 7.31);
	// Ballistic arc: launched upward from the seed, drifting outward
	// radially while gravity pulls the droplet back down.
	float ang = 6.2832 * seed.z;
	vec2 outv = vec2(cos(ang), sin(ang)) * (0.3 + 0.7 * seed.y);
	vec3 base = a_position;
	base.xy += outv * (life * u_Arc.x * 0.6);
	base.z += u_Arc.x * (1.2 * life - 1.4 * life * life);
	vec4 vpos = mul(u_modelView, vec4(base, 1.0));
	vpos.xy += corner * (u_Size.x * (0.5 + 0.5 * seed.y));
	gl_Position = mul(u_proj, vpos);
	// v_normal carries the sprite corner for the soft radial falloff.
	v_normal = vec3(corner, 1.0);
	float fade = sin(life * 3.1416);
	v_color0 = vec4(0.75, 0.85, 1.0, 1.0) * (fade * 0.35);
	v_vpos = vpos.xyz;
}
