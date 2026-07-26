$input v_normal, v_color0, v_vpos

/*
 * Bundled water-surface effect (docs/RenderEngine.md §5.11): the
 * identity form of the water stage — fcWaterFragment reproduces the
 * stock water surface exactly (fc_water_surface.sh declares every
 * engine-recorded uniform and sampler). Customize by combining the
 * result with Param_* uniforms or sampling the inputs directly.
 */

#include <bgfx_shader.sh>
#include "fc_user_water.sh"

void main()
{
	gl_FragColor = fcWaterFragment(v_normal, v_vpos);
}
