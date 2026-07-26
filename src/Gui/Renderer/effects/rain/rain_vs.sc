$input a_position, a_normal, a_color0
$output v_normal, v_color0, v_vpos

/*
 * Rain particle vertex stage (bundled rain effect,
 * docs/RenderEngine.md §5.11): expands the generated seed quads —
 * spread in a slab above the bound target — into thin streak
 * billboards falling straight down and wrapping back to the top.
 * Stateless — position is a pure function of the seed attributes and
 * the effect clock (a_normal.xy = corner, a_normal.z = index,
 * a_color0 = per-particle random seed). Param_Fall / Param_Size tune
 * the fall distance and streak length in model units. Rain is a
 * particle-only effect: no main-stage program, the streaks are the
 * whole treatment.
 */

#include <bgfx_shader.sh>

uniform vec4 u_fcTime;
uniform vec4 u_Fall;
uniform vec4 u_Size;

void main()
{
	vec2 corner = a_normal.xy;
	float idx = a_normal.z;
	vec3 seed = a_color0.xyz;
	float life = fract(u_fcTime.x * (0.5 + 0.3 * seed.y)
	                   + seed.x + idx * 7.31);
	vec3 base = a_position;
	base.z -= life * u_Fall.x;
	// Slight wind shear so the sheet does not read as a rigid grid.
	base.x += 0.03 * u_Fall.x * life * (seed.z - 0.5);
	vec4 vpos = mul(u_modelView, vec4(base, 1.0));
	// Thin view-space streak: narrow across, elongated along the
	// fall; length scales with the per-particle speed.
	vpos.xy += corner * vec2(u_Size.x * 0.06,
	                         u_Size.x * (0.6 + 0.4 * seed.y));
	gl_Position = mul(u_proj, vpos);
	// v_normal carries the sprite corner for the streak falloff.
	v_normal = vec3(corner, 1.0);
	// Constant faint sheet; short fade at the wrap ends masks pop-in.
	float fade = min(1.0, 4.0 * sin(life * 3.1416));
	v_color0 = vec4(0.65, 0.72, 0.85, 1.0) * (fade * 0.16);
	v_vpos = vpos.xyz;
}
