$input a_position, a_normal, a_color0
$output v_normal, v_color0, v_vpos

/*
 * Ember particle vertex stage (bundled fire effect,
 * docs/RenderEngine.md §5.11): expands the generated seed quads over
 * the flame body into small bright sparks rising with a turbulent
 * sway and a fast flicker. Stateless — position is a pure function of
 * the seed attributes and the effect clock. Param_Rise / Param_Size
 * tune the travel height and spark size in model units.
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
	float life = fract(u_fcTime.x * (0.18 + 0.22 * seed.y)
	                   + seed.x + idx * 3.7);
	vec3 base = a_position;
	base.z += life * u_Rise.x;
	base.x += 0.20 * u_Rise.x * life
	    * sin(6.2832 * seed.z + life * 9.0);
	base.y += 0.20 * u_Rise.x * life
	    * cos(6.2832 * seed.y + life * 7.0);
	vec4 vpos = mul(u_modelView, vec4(base, 1.0));
	vpos.xy += corner * (u_Size.x * (0.5 + 0.5 * seed.x));
	gl_Position = mul(u_proj, vpos);
	v_normal = vec3(corner, 1.0);
	float flick = 0.75 + 0.25 * sin(u_fcTime.x * 20.0 + seed.z * 40.0);
	v_color0 = vec4(1.0, 0.45, 0.12, 1.0) * ((1.0 - life) * flick);
	v_vpos = vpos.xyz;
}
