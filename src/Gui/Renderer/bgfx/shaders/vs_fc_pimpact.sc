$input a_position
$output v_color0

/*
 * Particle impact splat, vertex stage (docs/RenderEngine.md §5.8).
 *
 * One point per particle, scattered into the water impact map: the
 * step programs report where a particle struck (fcParticleImpact) into
 * a per-particle target, and this pass turns that per-PARTICLE record
 * into a per-PLACE one, which is the form the water surface can read.
 * The surface has no way to visit fourteen hundred droplets per pixel;
 * it can visit the nine map cells around itself.
 *
 * a_position.x is the particle index and .yz the corner of its quad —
 * the vertex buffer carries nothing else, because the payload is
 * fetched from the state grid.
 *
 * A quad snapped to one texel rather than a point sprite: gl_PointSize
 * does not exist on every profile this compiles for (SPIR-V has no
 * such builtin), and a point whose size is not written is a point of
 * whatever size the backend feels like.
 *
 * The map is written in the same NDC-to-uv relation a fullscreen pass
 * uses (vs_fc_comp), so writer and reader agree on which texel a place
 * is without either knowing the backend's framebuffer orientation.
 */

#include <bgfx_shader.sh>
#include <fc_particle.sh>
#include "fc_screen.sh"

SAMPLER2D(s_pimpsrc, 12);

/// xy = world-space min corner of the map's footprint,
/// z = 1 / its world extent (square, so one number), w = its
/// resolution in cells.
uniform vec4 u_impactFrame;
/// x = the scene animation clock at this step, in seconds.
uniform vec4 u_impactNow;

void main()
{
	float i = a_position.x;
	float row = floor(i / max(u_pgrid.x, 1.0));
	float col = i - row * max(u_pgrid.x, 1.0);
	vec2 uv = vec2((col + 0.5) / max(u_pgrid.x, 1.0),
	               (row + 0.5) / max(u_pgrid.y, 1.0));
	vec4 rec = texture2DLod(s_pimpsrc, uv, 0.0);

	if (rec.w <= 0.0)
	{
		// Nothing struck on this step. Park the quad outside the clip
		// volume: the map keeps what it holds, because a ring outlives
		// by far the step that started it.
		v_color0 = vec4(0.0, 0.0, 0.0, 0.0);
		gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
		return;
	}

	vec3 wp = mul(u_model[0], vec4(rec.xyz, 1.0)).xyz;
	// The exact world position travels in the payload, not in the
	// texel address: a cell is a bucket of places, and a ring drawn
	// from the middle of its bucket would snap to a visible lattice.
	v_color0 = vec4(wp.xy, u_impactNow.x, rec.w);

	// Snap to the cell the hit lands in and cover exactly that texel:
	// a record is about a cell, and half of one is not a smaller
	// record, it is a torn one.
	float res = max(u_impactFrame.w, 1.0);
	vec2 cell = floor((wp.xy - u_impactFrame.xy) * u_impactFrame.z * res);
	vec2 corner = (cell + a_position.yz) / res;
	gl_Position = vec4(fc_uvToNdc(corner), 0.0, 1.0);
}
