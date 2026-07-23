$input v_texcoord0

/*
 * Front-segment volumetric apply: composites the raymarch's
 * in-front-of-the-water inscatter (fs_fc_volume output 1) over the
 * re-rendered water surface — dst = inscatter + dst * transmittance
 * (blend ONE, SRC_ALPHA). Runs right after the water surface pass;
 * without it a fountain plume or fire standing over the water is
 * overdrawn by the surface and reads as behind it.
 *
 * Gated to pixels where the surface actually drew: a water front face
 * exists and no opaque geometry sits in front of it (there the main
 * apply already composited the full inscatter).
 */

#include <bgfx_shader.sh>

SAMPLER2D(s_texVolFront, 0);
SAMPLER2D(s_texWaterFront, 1);
SAMPLER2D(s_texNormalZ, 2);

void main()
{
	vec4 wf = texture2D(s_texWaterFront, v_texcoord0);
	if (wf.w < 0.5)
	{
		gl_FragColor = vec4(0.0, 0.0, 0.0, 1.0);
		return;
	}
	vec4 nz = texture2D(s_texNormalZ, v_texcoord0);
	if (nz.w > 0.5 && nz.z < wf.z - 0.05)
	{
		gl_FragColor = vec4(0.0, 0.0, 0.0, 1.0);
		return;
	}
	gl_FragColor = texture2D(s_texVolFront, v_texcoord0);
}
