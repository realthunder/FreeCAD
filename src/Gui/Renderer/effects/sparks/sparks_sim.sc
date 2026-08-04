$input v_texcoord0

/*
 * Spark state step (bundled sparks effect, docs/RenderEngine.md §5.8).
 *
 * One fragment = one spark. Launch upward with a random lateral
 * spread, fall under gravity against quadratic drag, bounce off the
 * emitter box floor with damping, respawn at end of life. Every one of
 * those depends on where the spark has already been — which is the
 * point: none of it can be written as a closed form of the clock, so
 * this is the motion a stateless emitter cannot express.
 *
 * Param_Gravity / Param_Launch / Param_Drag / Param_Bounce / Param_Life
 * tune it in model units per second.
 */

#include <bgfx_shader.sh>
#include <fc_particle.sh>

uniform vec4 u_Gravity;
uniform vec4 u_Launch;
uniform vec4 u_Drag;
uniform vec4 u_Bounce;
uniform vec4 u_Life;
uniform vec4 u_Nozzle;

/// A fresh spark: launched up and outward from a nozzle at the base of
/// the emitter, with a lifetime jittered per particle so the fountain
/// does not pulse.
///
/// The engine's default spawn volume is the emitter's whole bounds,
/// which for a travelling effect is deliberately much larger than the
/// seed box — EmitterMargin pads it so the auto near/far fit does not
/// clip particles in flight (§5.11). Sparks want to come off one spot,
/// not out of that whole volume, so the step program narrows it:
/// Param_Nozzle is the fraction of the bounds the nozzle spans. This is
/// the documented split — the engine offers a default spawn,
/// the step program shapes it.
Particle sparkSpawn(vec2 uv)
{
	float idx = fcParticleIndex(uv);
	Particle p = fcParticleSpawn(uv);
	vec3 r = fcParticleHash3(idx * 3.77 + 1.5);

	vec3 lo = u_pboxMin.xyz;
	vec3 hi = u_pboxMax.xyz;
	vec3 mid = (lo + hi) * 0.5;
	// Narrow to the nozzle laterally, and sit it just above the floor
	// of the bounds so the bounce below has somewhere to happen.
	p.pos.xy = mid.xy + (p.pos.xy - mid.xy) * u_Nozzle.x;
	p.pos.z = mix(lo.z, hi.z, u_Nozzle.y);

	float ang = 6.2832 * r.x;
	float lateral = u_Launch.x * 0.35 * r.y;
	p.vel = vec3(cos(ang) * lateral,
	             sin(ang) * lateral,
	             u_Launch.x * (0.55 + 0.45 * r.z));
	p.life = u_Life.x * (0.6 + 0.4 * r.y);
	return p;
}

void main()
{
	Particle p = fcParticleLoad(v_texcoord0);
	float dt = fcParticleStep();

	p.age += dt;
	if (p.age > p.life)
	{
		p = sparkSpawn(v_texcoord0);
	}
	else if (p.age > 0.0)
	{
		// Quadratic drag opposes travel; gravity is model -Z.
		float speed = length(p.vel);
		vec3 drag = speed > 0.0001
			? -(p.vel / speed) * (speed * speed * u_Drag.x * 0.01)
			: vec3(0.0, 0.0, 0.0);
		p.vel += (vec3(0.0, 0.0, -u_Gravity.x) + drag) * dt;
		p.pos += p.vel * dt;

		// The nozzle plane is the floor sparks bounce off: bounce with
		// damping, and stop the jitter of a spark that has lost almost
		// all its energy by letting it rest until its life runs out.
		float floorZ = mix(u_pboxMin.z, u_pboxMax.z, u_Nozzle.y);
		if (p.pos.z < floorZ)
		{
			p.pos.z = floorZ;
			p.vel.z = abs(p.vel.z) * u_Bounce.x;
			p.vel.xy *= 0.7;
			if (p.vel.z < u_Gravity.x * dt * 2.0)
				p.vel = vec3(0.0, 0.0, 0.0);
		}
	}

	fcParticleStore(p);
}
