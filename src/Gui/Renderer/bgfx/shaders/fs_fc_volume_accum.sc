$input v_texcoord0

/*
 * Temporal accumulation of the half-res volumetric raymarch: copies the
 * current frame's outputs (main + front-of-water) into the history
 * targets under a constant-factor blend — hist = cur * k + hist *
 * (1 - k), k from the frames-static count. Combined with the raymarch's
 * per-frame jitter phase this integrates the march's dither noise away
 * within a fraction of a second whenever the camera holds still; any
 * camera or scene change resets k to 1 (history replaced, no ghosting).
 */

#include <bgfx_shader.sh>

SAMPLER2D(s_texVol, 0);
SAMPLER2D(s_texVolFront, 1);

void main()
{
	gl_FragData[0] = texture2D(s_texVol, v_texcoord0);
	gl_FragData[1] = texture2D(s_texVolFront, v_texcoord0);
}
