/*
 * Water-surface helper for user "water"-stage shaders
 * (docs/RenderEngine.md §5.11). Include it after bgfx_shader.sh in a
 * fragment program written against the mesh contract ($input
 * v_normal, v_color0, v_vpos) bound on a water body: the engine runs
 * its water pass set (scene copy, planar reflection, water back
 * depth) for the draw and records every uniform and sampler the core
 * declares, so
 *
 *   $input v_normal, v_color0, v_vpos
 *   #include <bgfx_shader.sh>
 *   #include "fc_user_water.sh"
 *   void main()
 *   {
 *       gl_FragColor = fcWaterFragment(v_normal, v_vpos);
 *   }
 *
 * reproduces the stock water surface exactly. Custom effects combine
 * the result (or the individual samplers/uniforms fc_water_surface.sh
 * declares) with their own Param_* uniforms.
 */

#include "fc_water_surface.sh"

// A macro so gl_FragCoord resolves at the caller: shaderc's spirv path
// only knows it inside main().
#define fcWaterFragment(normal, vpos) \
	fcWaterShadeFragment(normal, vpos, gl_FragCoord.xy)
