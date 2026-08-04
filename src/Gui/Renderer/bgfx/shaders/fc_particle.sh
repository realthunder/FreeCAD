/*
 * Stateful particle authoring contract (docs/RenderEngine.md §5.8).
 *
 * A stateful emitter keeps its particles in two RGBA32F textures that
 * ping-pong once per fixed simulation step:
 *
 *   s_pstate0 : xyz = position in the emitter's model space,
 *               w   = age in seconds
 *   s_pstate1 : xyz = velocity in model units per second,
 *               w   = lifetime in seconds
 *
 * A third target carries what the step reports rather than what it
 * remembers:
 *
 *   impact    : xyz = where the particle struck, in model space,
 *               w   = strength (0 = no impact this step)
 *
 * It is written every step and read by nothing the particle owns — the
 * engine scatters it into the water impact map (fcParticleStoreHit).
 *
 * The step program (App::ShaderProgram.SimulateProgram) is a fragment
 * shader over that grid: one fragment = one particle. It reads the
 * previous state through fcParticleLoad, advances it, and writes the
 * next through fcParticleStore. The vertex stage of the beauty draw
 * reads the same textures at the same texel, so motion and look stay
 * in step without either side knowing how the other works.
 *
 * Everything here is plain vertex/fragment GLSL — no compute shaders,
 * no storage buffers — so the desktop and the browser tier run the
 * identical program (the reason the simulation is a fragment pass at
 * all, see §5.8).
 */

#ifndef FC_PARTICLE_SH
#define FC_PARTICLE_SH

SAMPLER2D(s_pstate0, 10);
SAMPLER2D(s_pstate1, 11);

/// x,y = state grid size in texels, z = particle count,
/// w = the fixed step in seconds
uniform vec4 u_pgrid;
/// xyz = emitter seed box minimum (model space), w = emitter seed
uniform vec4 u_pboxMin;
/// xyz = emitter seed box maximum (model space), w = simulation time
/// of the step being computed, in seconds since the emitter's reset
uniform vec4 u_pboxMax;

struct Particle
{
	vec3 pos;
	float age;
	vec3 vel;
	float life;
};

/// The fixed step length in seconds. Constant across a frame and
/// across machines — that is what makes a stateful emitter reproduce.
float fcParticleStep()
{
	return u_pgrid.w;
}

/// Seconds since this emitter last reset.
float fcParticleTime()
{
	return u_pboxMax.w;
}

/// 0 at birth, 1 at death.
float fcParticleLife(Particle p)
{
	return p.life > 0.0 ? clamp(p.age / p.life, 0.0, 1.0) : 0.0;
}

/// Integer index of the particle a texel belongs to.
float fcParticleIndex(vec2 uv)
{
	vec2 t = floor(uv * u_pgrid.xy);
	return t.y * u_pgrid.x + t.x;
}

/// Hash of a float to [0,1) — the PCG-style integer scramble done in
/// float math, because GLSL ES 1.00 targets have no integer ops.
float fcParticleHash(float v)
{
	return fract(sin(v * 12.9898 + u_pboxMin.w * 0.7213) * 43758.5453);
}

/// Three independent hashes of the same value.
vec3 fcParticleHash3(float v)
{
	return vec3(fcParticleHash(v),
	            fcParticleHash(v + 17.13),
	            fcParticleHash(v + 41.79));
}

/// Fresh particle for a texel: a uniform random anchor in the seed
/// box, at rest, with a lifetime of one second. Deterministic in the
/// texel and the emitter seed — reset the emitter and the same
/// particles come back. Step programs typically call this for the
/// initial spawn and again whenever a particle dies, overriding
/// velocity and lifetime as the effect needs.
Particle fcParticleSpawn(vec2 uv)
{
	float idx = fcParticleIndex(uv);
	vec3 r = fcParticleHash3(idx * 1.618);
	Particle p;
	p.pos = mix(u_pboxMin.xyz, u_pboxMax.xyz, r);
	p.age = 0.0;
	p.vel = vec3(0.0, 0.0, 0.0);
	p.life = 1.0;
	return p;
}

/// Texel of the particle a seed quad belongs to, from the 0..1 index
/// the seed geometry carries in a_normal.z. The vertex stage of a
/// stateful emitter starts here.
vec2 fcParticleUV(float idx01)
{
	float i = floor(idx01 * max(1.0, u_pgrid.z - 1.0) + 0.5);
	float row = floor(i / u_pgrid.x);
	float col = i - row * u_pgrid.x;
	return vec2((col + 0.5) / u_pgrid.x, (row + 0.5) / u_pgrid.y);
}

/// Previous state of the particle at this texel. Explicit LOD: the
/// vertex stage reads this too, and a vertex fetch has no implicit
/// derivatives to pick a mip from.
Particle fcParticleLoad(vec2 uv)
{
	vec4 s0 = texture2DLod(s_pstate0, uv, 0.0);
	vec4 s1 = texture2DLod(s_pstate1, uv, 0.0);
	Particle p;
	p.pos = s0.xyz;
	p.age = s0.w;
	p.vel = s1.xyz;
	p.life = s1.w;
	return p;
}

/// A step that struck nothing. The value a step program starts its
/// impact from, and what fcParticleStore reports on its behalf.
#define fcParticleNoImpact vec4(0.0, 0.0, 0.0, 0.0)

/// An impact report: where the particle struck, in the emitter's model
/// space, and how hard — roughly 0..1, the relative violence of the
/// hit, which scales the ring the water surface raises from it.
///
/// It is a per-step event, not a state: report it on the step that
/// detects the hit, not for as long as the particle sits where it
/// landed.
#define fcParticleHit(_pos, _strength) \
	vec4(_pos, max(_strength, 0.0))

/// Next state, and nothing struck. Must be the last thing a step
/// program does.
///
/// A macro, not a function: shaderc rewrites gl_FragData into out
/// parameters of main, so only main can write them.
#define fcParticleStore(_p)                          \
	{                                                \
		gl_FragData[0] = vec4((_p).pos, (_p).age);   \
		gl_FragData[1] = vec4((_p).vel, (_p).life);  \
		gl_FragData[2] = fcParticleNoImpact;         \
	}

/// Next state, plus what this step struck (fcParticleHit, or
/// fcParticleNoImpact for a step that struck nothing). The engine
/// scatters the reports into the water impact map, where the water
/// surface reads them as the origins of its rings
/// (docs/RenderEngine.md §5.8).
///
/// The report travels as an argument rather than through a variable
/// the header owns: a mutable file-scope global does not survive the
/// runtime translation of a user shader — the program silently fails
/// to build and the emitter falls back to its stateless stage, which
/// looks exactly like an emitter that draws nothing.
#define fcParticleStoreHit(_p, _hit)                 \
	{                                                \
		gl_FragData[0] = vec4((_p).pos, (_p).age);   \
		gl_FragData[1] = vec4((_p).vel, (_p).life);  \
		gl_FragData[2] = (_hit);                     \
	}

#endif // FC_PARTICLE_SH
