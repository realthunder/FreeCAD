$input a_position, a_normal, a_color0
$output v_normal, v_color0, v_vpos

/*
 * Spark vertex stage (bundled sparks effect, docs/RenderEngine.md
 * §5.8): billboards each seed quad at the position the state step put
 * its particle, and colors it by how fast it is still moving — hot
 * white-yellow while it climbs, deep ember-red once it has bounced and
 * slowed.
 *
 * The clock is not referenced at all: every bit of motion here comes
 * out of the state textures. Where no state is bound (no float render
 * targets, over the slot budget, or the step program still compiling)
 * the load reads zero, and the fallback below keeps the sparks at
 * their seed anchors instead of collapsing them all onto the origin.
 */

#include <bgfx_shader.sh>
#include <fc_particle.sh>

uniform vec4 u_Size;

void main()
{
	vec2 corner = a_normal.xy;
	Particle p = fcParticleLoad(fcParticleUV(a_normal.z));
	float life = fcParticleLife(p);

	// No state bound: lifetime is exactly 0, which never happens once
	// the reset pass has run.
	bool stateless = p.life <= 0.0;
	vec3 pos = stateless ? a_position : p.pos;

	vec4 vpos = mul(u_modelView, vec4(pos, 1.0));
	float speed = length(p.vel);
	// Sparks shrink as they cool, so a resting ember is a dim point
	// rather than a lingering blob.
	float scale = u_Size.x * (0.45 + 0.55 * (1.0 - life));
	vpos.xy += corner * scale;
	gl_Position = mul(u_proj, vpos);

	// v_normal carries the sprite corner for the radial falloff.
	v_normal = vec3(corner, 1.0);
	vec3 hot = vec3(1.0, 0.92, 0.62);
	vec3 cool = vec3(1.0, 0.28, 0.06);
	vec3 tint = mix(cool, hot, clamp(speed * 0.06, 0.0, 1.0));
	// Fade in over the first tenth of life so a respawn does not pop.
	float fade = clamp(life * 10.0, 0.0, 1.0) * (1.0 - life * life);
	v_color0 = vec4(tint, 1.0) * (stateless ? 0.0 : fade);
	v_vpos = vpos.xyz;
}
