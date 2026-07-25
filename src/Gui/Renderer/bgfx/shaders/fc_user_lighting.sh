/*
 * Lighting helper for user material-stage shaders (docs/RenderDebug.md
 * §6.5/§6.6). Include it after bgfx_shader.sh in a fragment program
 * written against the material contract ($input v_normal, v_color0,
 * v_vpos) to shade a custom base color exactly like the stock CAD mesh
 * shader: viewer headlight or the shadowed scene light of the Shadow
 * draw style, Render_Light bulbs including their shadow tiles, fire
 * lights, screen-space AO, and the metallic/roughness PBR branch when
 * it is active on the draw. bgfx uniforms and samplers are global by
 * name, so the values and textures the engine records with the draw
 * feed these declarations unchanged.
 *
 *   $input v_normal, v_color0, v_vpos
 *   #include <bgfx_shader.sh>
 *   #include "fc_user_lighting.sh"
 *   void main()
 *   {
 *       vec4 base = vec4(...custom albedo..., 1.0);
 *       gl_FragColor = fcLightFragment(base, v_normal, v_vpos);
 *   }
 *
 * fcStockBase() returns the draw's stock base color (material diffuse
 * or per-vertex color) — fcLightFragment(fcStockBase(), v_normal,
 * v_vpos) reproduces the standard rendering. Callers that perturb the
 * normal or bring their own occlusion/PBR factors can use the
 * fcShadeFragment() core (fc_mesh_lighting.sh) directly.
 */

#include "fc_mesh_lighting.sh"

vec4 fcStockBase()
{
	return mix(u_matColor, v_color0, u_params.x);
}

vec4 fcLightFragmentAt(vec4 base, vec3 normal, vec3 vpos, vec2 fragCoord)
{
	vec3 n = normalize(normal);
	vec3 geoN = n;
	if (u_params.z > 0.5 && geoN.z < 0.0)
		geoN = -geoN;
	return fcShadeFragment(base, n, geoN, vpos, fragCoord, 1.0,
	                       u_pbrParams.y, u_pbrParams.z);
}

// A macro so gl_FragCoord resolves at the caller: shaderc's spirv path
// only knows it inside main().
#define fcLightFragment(base, normal, vpos) \
	fcLightFragmentAt(base, normal, vpos, gl_FragCoord.xy)
