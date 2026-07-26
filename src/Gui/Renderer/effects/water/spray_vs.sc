$input a_position, a_normal, a_color0
$output v_normal, v_color0, v_vpos

/*
 * Water-spray particle vertex stage (bundled water effect,
 * docs/RenderEngine.md §5.11): expands the generated seed quads over
 * the pool surface into soft mist billboards drifting slowly upward.
 * Stateless — position is a pure function of the seed attributes and
 * the effect clock (a_normal.xy = corner, a_normal.z = index,
 * a_color0 = per-particle random seed). Param_Rise / Param_Size tune
 * the travel height and sprite size in model units.
 */

#include <bgfx_shader.sh>

uniform vec4 u_fcTime;
uniform vec4 u_Rise;
uniform vec4 u_Size;

void main()
{
	vec2 corner = a_normal.xy;
	float idx = a_normal.z;
	vec3 seed = a_color0.xyz;
	float life = fract(u_fcTime.x * (0.06 + 0.08 * seed.y)
	                   + seed.x + idx * 7.31);
	vec3 base = a_position;
	base.z += life * u_Rise.x;
	base.x += 0.15 * u_Rise.x * sin(6.2832 * (seed.z + life * 0.5));
	base.y += 0.15 * u_Rise.x * cos(6.2832 * (seed.y + life * 0.5));
	vec4 vpos = mul(u_modelView, vec4(base, 1.0));
	vpos.xy += corner * (u_Size.x * (0.6 + 0.4 * seed.y));
	gl_Position = mul(u_proj, vpos);
	// v_normal carries the sprite corner for the soft radial falloff.
	v_normal = vec3(corner, 1.0);
	float fade = sin(life * 3.1416);
	v_color0 = vec4(0.7, 0.85, 1.0, 1.0) * (fade * 0.30);
	v_vpos = vpos.xyz;
}
