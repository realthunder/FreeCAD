$input v_texcoord0

/*
 * Water-jet state step (bundled waterjet effect, docs/RenderEngine.md
 * §5.8).
 *
 * One fragment = one droplet. Leaves the nozzle inside a narrow cone,
 * arcs over under gravity against light drag, and is absorbed by the
 * pool it lands in rather than bouncing forever: the impact damps the
 * droplet, scatters it sideways and shortens whatever life it had
 * left, which is what makes the fall read as water meeting water and
 * not as gravel.
 *
 * The stateless fountain companion (droplets_vs.sc) is a closed form
 * of the clock, so every droplet traces the same arc and the spray
 * reads as noise. Here the arc is integrated, so the launch jitter,
 * the drag and the splash compound per droplet.
 *
 * Param_Gravity / Param_Launch / Param_Drag / Param_Life are model
 * units per second; Param_Spread is [cone, speed jitter]; Param_Nozzle
 * is [lateral fraction of the seed box, height fraction]; Param_Splash
 * is [restitution, seconds of life left after impact]; Param_Stagger
 * spreads first emission over a fraction of a lifetime so the jet does
 * not pulse.
 */

#include <bgfx_shader.sh>
#include <fc_particle.sh>

uniform vec4 u_Gravity;
uniform vec4 u_Launch;
uniform vec4 u_Spread;
uniform vec4 u_Drag;
uniform vec4 u_Life;
uniform vec4 u_Nozzle;
uniform vec4 u_Splash;
uniform vec4 u_Stagger;

/// Height of the pool surface the jet falls back into, in model space.
float jetFloor()
{
	return mix(u_pboxMin.z, u_pboxMax.z, u_Nozzle.y);
}

/// A fresh droplet at the nozzle, aimed up inside a cone.
///
/// fcParticleSpawn seeds a uniform anchor in the emitter's seed box,
/// which is the whole mouth of the effect; a jet comes out of one
/// spot, so Param_Nozzle narrows it laterally and drops it to the
/// pool surface. The engine offers a default spawn and the step
/// program shapes it — the documented split (§5.8).
Particle jetSpawn(vec2 uv)
{
	float idx = fcParticleIndex(uv);
	Particle p = fcParticleSpawn(uv);
	vec3 r = fcParticleHash3(idx * 5.19 + 0.7);
	vec3 r2 = fcParticleHash3(idx * 2.31 + 11.4);

	vec3 lo = u_pboxMin.xyz;
	vec3 hi = u_pboxMax.xyz;
	vec3 mid = (lo + hi) * 0.5;
	p.pos.xy = mid.xy + (p.pos.xy - mid.xy) * u_Nozzle.x;
	p.pos.z = jetFloor();

	// Direction inside a cone about +Z. The sqrt keeps the sample
	// area-uniform, so the cone does not bunch along its axis and
	// leave the skirt empty.
	float ang = 6.2832 * r.x;
	float t = sqrt(r.y) * u_Spread.x;
	p.vel = normalize(vec3(cos(ang) * t, sin(ang) * t, 1.0))
		* (u_Launch.x * (1.0 - u_Spread.y * r.z));
	p.life = u_Life.x * (0.75 + 0.5 * r2.x);
	// Negative age = queued, not yet emitted (the vertex stage draws
	// nothing until it turns positive). Without it every droplet is
	// born on the same frame and the jet leaves the nozzle in visible
	// packets until the jittered lifetimes have drifted apart.
	p.age = -u_Life.x * u_Stagger.x * r2.y;
	return p;
}

void main()
{
	Particle p = fcParticleLoad(v_texcoord0);
	float dt = fcParticleStep();
	vec4 hit = fcParticleNoImpact;

	p.age += dt;
	if (p.age > p.life)
	{
		p = jetSpawn(v_texcoord0);
	}
	else if (p.age > 0.0)
	{
		// Quadratic drag opposes travel; gravity is model -Z. Drag is
		// what stops the top of the jet from being a hard parabola —
		// the fast droplets lose more of their climb than the slow
		// ones, so the plume rounds off.
		float speed = length(p.vel);
		vec3 drag = speed > 0.0001
			? -(p.vel / speed) * (speed * speed * u_Drag.x * 0.01)
			: vec3(0.0, 0.0, 0.0);
		p.vel += (vec3(0.0, 0.0, -u_Gravity.x) + drag) * dt;
		p.pos += p.vel * dt;

		float floorZ = jetFloor();
		if (p.pos.z < floorZ)
		{
			// Absorbed, not bounced: keep a little of the impact as
			// splash, throw it sideways, and give it only Param_Splash.y
			// seconds more so the surface does not collect droplets.
			vec3 s = fcParticleHash3(fcParticleIndex(v_texcoord0) * 7.7
			                         + floor(p.age * 13.0));
			// Tell the surface where it was hit and how hard, so the
			// ring it raises belongs to this droplet instead of to a
			// noise field that only looks like rain. The launch speed
			// is the natural full-strength hit: a droplet cannot come
			// down faster than it was thrown up.
			hit = fcParticleHit(vec3(p.pos.xy, floorZ),
			                    clamp(abs(p.vel.z)
			                          / max(u_Launch.x, 1.0e-4),
			                          0.0, 1.0));
			p.pos.z = floorZ;
			p.vel.z = abs(p.vel.z) * u_Splash.x;
			p.vel.xy = p.vel.xy * 0.35
				+ (s.xy - 0.5) * (u_Launch.x * 0.12);
			p.life = min(p.life, p.age + u_Splash.y);
		}
	}

	fcParticleStoreHit(p, hit);
}
