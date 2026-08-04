$input a_position, a_normal, a_color0
$output v_normal, v_color0, v_vpos

/*
 * Water-jet vertex stage (bundled waterjet effect,
 * docs/RenderEngine.md §5.8): billboards each seed quad at the
 * position the state step put its droplet.
 *
 * Two things separate this from a generic sprite emitter, and both are
 * why the jet reads as water rather than as a cloud of dots:
 *
 * - The sprite is stretched along the droplet's own screen-space
 *   velocity, by an amount that grows with speed. Real spray is seen
 *   as streaks because a droplet crosses several pixels per frame; a
 *   round sprite at every speed reads as fog or as gravel, never as
 *   water.
 * - Colour comes from speed, not from age: the fast core leaving the
 *   nozzle is foam-white, the slow arc at the top and the fall back
 *   are the blue of thin water. That is the same cue a photograph of
 *   a fountain carries.
 *
 * The clock is not referenced at all; every bit of motion comes out of
 * the state textures. Where no state is bound (no float render
 * targets, over the slot budget, or the step program still compiling)
 * the load reads zero and the droplets stay at their seed anchors,
 * drawn transparent rather than collapsed onto the origin.
 */

#include <bgfx_shader.sh>
#include <fc_particle.sh>

uniform vec4 u_Size;
uniform vec4 u_Streak;
uniform vec4 u_Opacity;

void main()
{
	vec2 corner = a_normal.xy;
	Particle p = fcParticleLoad(fcParticleUV(a_normal.z));
	float life = fcParticleLife(p);

	// Lifetime is exactly 0 only when no state is bound: the reset
	// pass always writes one. A negative age is a droplet queued at
	// the nozzle and not yet emitted.
	bool stateless = p.life <= 0.0;
	bool queued = p.age < 0.0;
	vec3 pos = stateless ? a_position : p.pos;

	vec4 vpos = mul(u_modelView, vec4(pos, 1.0));
	vec3 vvel = mul(u_modelView, vec4(p.vel, 0.0)).xyz;
	float speed = length(p.vel);

	// Screen-space travel direction, with a fallback for a droplet
	// that is momentarily still (the apex of the arc, or a rested
	// splash) — there the streak length is 1 anyway.
	vec2 d = vvel.xy;
	float dl = length(d);
	vec2 dir = dl > 0.0001 ? d / dl : vec2(0.0, 1.0);
	vec2 perp = vec2(-dir.y, dir.x);

	float w = u_Size.x * (0.75 + 0.25 * (1.0 - life));
	float stretch = 1.0 + u_Streak.x * clamp(speed * 0.045, 0.0, 3.0);
	vpos.xy += perp * (corner.x * w) + dir * (corner.y * w * stretch);
	gl_Position = mul(u_proj, vpos);

	// v_normal carries the sprite corner for the radial falloff.
	v_normal = vec3(corner, 1.0);
	// Spray is white. A droplet in air is far too small to carry the
	// path length that makes a body of water blue — the colour of a
	// fountain comes from what it scatters, which is daylight. Only
	// the slow fall keeps a trace of cool cast, and even that is
	// nearly white; a saturated blue reads as dyed water.
	vec3 foam = vec3(0.99, 1.00, 1.00);
	vec3 deep = vec3(0.86, 0.91, 0.95);
	vec3 tint = mix(deep, foam, clamp(speed * 0.055, 0.0, 1.0));
	// In fast, out slow: a droplet that pops into existence at full
	// opacity is visible as a flicker at the nozzle, and one that
	// vanishes at full opacity is visible as a hole in the fall.
	float fade = clamp(life * 8.0, 0.0, 1.0)
		* (1.0 - life * life * life);
	float vis = (stateless || queued) ? 0.0 : 1.0;
	v_color0 = vec4(tint, u_Opacity.x * fade * vis);
	v_vpos = vpos.xyz;
}
