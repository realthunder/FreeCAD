$input v_normal, v_color0, v_color1, v_color2, v_vpos, v_opos, v_onrm, v_findex

/*
 * Glass body shading (vs_fc_mesh pair): screen-space refraction of the
 * scene-color copy — the sample offset follows the IOR-refracted view
 * direction over the body thickness (the glass front/back interval
 * depth targets), so flat panes viewed head-on sample straight through
 * while curved or oblique glass displaces — with per-channel
 * Beer-Lambert absorption tinted by the material diffuse over the same
 * thickness, Fresnel-blended environment reflection (f0 from the IOR,
 * roughness picks the prefiltered mip) and a sun glint from the scene
 * light. Roughness also frosts the transmission: the refracted sample
 * is spread over a disc that grows with roughness and thickness, and a
 * share of the light comes back diffusely (the whitish body of etched
 * glass), so a frosted pane blurs and pales what is behind it.
 *
 * u_glassParams: x = index of refraction, y = absorption density
 *                (1/world units, resolved by the backend), z = 0..1
 *                roughness, w > 0.5 = the prepass viewZ is bound for
 *                the refraction depth reject
 * u_matColor   : glass body colour -- absorption of the complement
 * u_glassTint  : rgb multiplied into the transmitted light ONCE at the
 *                surface, whatever the thickness: a MaterialX
 *                transmission_color with no depth (OpenPBR's surface
 *                tint, docs/MaterialStorage.md sec 17.21). White for
 *                every Render_Glass body.
 * u_lightDir   : scene light (w > 0.5 = present), view space
 *
 * The body lives in fc_glass_fs.sh: a MaterialX glass splices the
 * document's generated material function into the same body
 * (FC_USER_MATERIAL, paired with vs_fc_mesh_tex for the texture
 * coordinate) and reads its inputs per fragment instead of the
 * uniforms above (docs/MaterialStorage.md sec 17.22).
 */

#include <bgfx_shader.sh>
#include "fc_glass_fs.sh"
