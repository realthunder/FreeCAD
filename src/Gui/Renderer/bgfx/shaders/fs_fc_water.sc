$input v_normal, v_color0, v_vpos

/*
 * Water surface shading (vs_fc_mesh pair): thin wrapper over the
 * shared shading core in fc_water_surface.sh -- the same function a
 * user "water"-stage shader reaches through fc_user_water.sh, so the
 * identity user shader reproduces this program exactly.
 */

#include <bgfx_shader.sh>
#include "fc_water_surface.sh"

void main()
{
	gl_FragColor = fcWaterShadeFragment(v_normal, v_vpos,
	                                    gl_FragCoord.xy);
}
