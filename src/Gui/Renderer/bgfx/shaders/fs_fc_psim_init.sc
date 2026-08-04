$input v_texcoord0

/*
 * Particle state reset (vs_fc_comp pair, docs/RenderEngine.md §5.8):
 * writes the birth state of every particle of a stateful emitter into
 * the ping-pong targets. Run when the emitter appears, when its count
 * or seed changes, and as the first of the fixed warm-up steps of a
 * frozen frame — which is what makes a freeze-frame capture of a
 * stateful effect byte-reproducible: the state never depends on how
 * long the session has been running, only on the seed and the number
 * of steps since this pass.
 *
 * Deliberately stock rather than user code: a reset that a step
 * program could get wrong would take the determinism guarantee with
 * it. Step programs shape the spawn instead (fcParticleSpawn gives
 * them the same anchors on respawn).
 */

#include <bgfx_shader.sh>
#include <fc_particle.sh>

void main()
{
	Particle p = fcParticleSpawn(v_texcoord0);
	// Stagger the birth ages over one lifetime so the emitter is
	// already in full flow at t = 0 instead of pulsing every particle
	// in unison. Negative ages are pre-birth: a step program sees a
	// particle that has not spawned yet, and the beauty stage sees
	// life = 0 (fcParticleLife clamps).
	float idx = fcParticleIndex(v_texcoord0);
	p.age = -fcParticleHash(idx * 2.718) * p.life;
	fcParticleStore(p);
}
