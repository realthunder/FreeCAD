/*
 * FreeCAD CAD-mesh lighting core: the uniforms, samplers and shading
 * function shared by the stock mesh fragment shader (fc_mesh_fs.sh)
 * and user material-stage shaders (via fc_user_lighting.sh, docs/
 * RenderDebug.md §6.5) — single headlight Blinn-Phong, or a
 * metallic/roughness BRDF with image based lighting when the PBR
 * branch is enabled per draw (u_pbrParams.x), the shadowed scene
 * light of the Shadow draw style, the local effect lights
 * (Render_Light bulbs incl. their shadow tiles, fire flames) and
 * screen-space AO. bgfx uniforms and samplers are global by name, so
 * any program declaring these picks up the values and textures the
 * engine records with the draw.
 *
 * u_matColor    : rgba diffuse; used when u_params.x == 0
 * u_matEmissive : rgb emissive add; w = per-face material flag (the
 *                 stock caller shades emissive/specular/shininess from
 *                 the v_color1/v_color2 stream instead of the scalars;
 *                 2 = that stream's two alpha slots carry the PBR
 *                 factor pair, which the caller resolves into the
 *                 metal/rough arguments below)
 * u_matSpecular : rgb specular, w = shininess (0..1 Coin convention)
 * u_params      : x = per-vertex color, y = lighting on, z = two-sided
 * u_pbrParams   : x = PBR branch on (2 = with a metallic-roughness
 *                 map), y = metallic, z = roughness,
 *                 w = environment intensity
 * u_envSH       : irradiance spherical harmonics of the environment,
 *                 cosine-convolved with the basis and 1/pi constants
 *                 folded in (Ramamoorthi's polynomial form, world space)
 * s_texEnv      : GGX-prefiltered environment cubemap, world space,
 *                 mip = roughness * 5 (a dummy is bound when PBR is off)
 */

uniform vec4 u_matColor;
uniform vec4 u_matEmissive;
uniform vec4 u_matSpecular;
// The draw's ambient term. rgb = the colour to add outright, which is
// Coin/GL's material ambient colour times LIGHT_MODEL_AMBIENT (the
// engine multiplies the two). w = 1 for that; w = 0 means the feed
// carried no Coin lighting and the legacy floor applies instead --
// 0.2 * base with a 0.8 diffuse weight, the pair of fudges that used
// to stand in for Coin's ambient+diffuse and which an old scene dump
// was drawn with.
uniform vec4 u_ambient;
// The same ambient WITHOUT the material factor: rgb = LIGHT_MODEL_AMBIENT
// alone (SoEnvironment's colour times its intensity), w = 1 when fed.
// The metallic/roughness branch wants it in this form -- there the
// surface is stated by the BRDF, not by a Phong ambient colour -- and
// takes it as a uniform-radiance environment.
uniform vec4 u_envAmbient;
uniform vec4 u_params;
uniform vec4 u_pbrParams;
// Matcap shading: x = enabled, y = preset (see fc_matcap.sh),
// z = how much the object's own color tints it (0 = one uniform
// material for the whole scene). Overrides the PBR branch while on.
uniform vec4 u_matcapParams;
uniform vec4 u_envSH[9];
SAMPLERCUBE(s_texEnv, 1);
// Shadow draw style: a directional scene light replaces the headlight
// (u_lightDir.w > 0.5; xyz = light direction in view space, the way the
// light travels) and a variance shadow map attenuates its contribution.
// u_shadowParams: x = this draw receives shadows, y = minimum variance,
// z = depth bias, w = the shadow-moment debug visualization
// (FC_BGFX_DEBUG_SHADOW_VIS; read in fc_mesh_fs.sh).
// u_shadowMatrix maps view space to shadow
// map uv (xy) + light window depth (z). u_lightColor.rgb carries the
// light color premultiplied by its intensity.
SAMPLER2D(s_texShadow, 3);
// Shadow tint map: per-channel transmittance of glass casters rendered
// from the light camera (white where no glass blocks the light). It
// multiplies the direct scene-light term beside the variance shadow
// factor, so glass casts a softer shadow, tinted when colored.
SAMPLER2D(s_texShadowTint, 7);
uniform vec4 u_shadowParams;
uniform vec4 u_lightDir;
// Spot light position in view space; w = cos of the cone cutoff for a
// spot light, -1 for a directional one. The falloff exponent
// (dropOffRate * 128) rides u_lightColor.w.
uniform vec4 u_lightPos;
uniform vec4 u_lightColor;
// EVSM warp exponent (x): the moments store exp(c z), exp(c z)^2 of
// the light window depth. y = Coin's VsmLookup smoothstep threshold.
// zw = the Coin SoShadowGroup N-tap spread kernel (ShadowSpreadSize /
// ShadowSpreadSampleSize): z = tap spacing in shadow map UV (Coin's
// swidth * 0.001, the 0.1 spot factor folded in), w = kernel mode —
// 0 single tap, 1 Coin's 4-tap dithered kernel, N >= 3 an N x N grid.
uniform vec4 u_evsm;
#include "fc_openpbr.sh"      // the OpenPBR surface (the PBR branch)
#include "fc_shadow_tap.sh"   // the shared VSM/EVSM bound (needs both above)
#include "fc_matcap.sh"       // procedural matcaps (needs nothing but a normal)
uniform mat4 u_shadowMatrix;
// The ordinary Coin lights of the frame: the viewer's headlight and
// backlight, and any SoDirectionalLight / SoPointLight the document
// adds. Unshadowed (only the scene light above carries a map) and
// added on top of the ambient floor by both lighting models.
//   u_viewLight[i]      xyz = view-space direction the light travels
//                       (directional) or its view-space position
//                       (positional/spot); w = 0 inactive,
//                       1 directional, 2 positional, 3 spot,
//                       4 a spot's cone slot (see below)
//   u_viewLightColor[i] rgb = colour premultiplied by intensity;
//                       w = a spot's falloff exponent
//                       (dropOffRate * 128)
//   u_viewLightAtt[i]   xyz = Coin's squared/linear/constant distance
//                       attenuation (SoEnvironment::attenuation order);
//                       w = a spot's cone cutoff cosine
// A spot light (kind 3) is a positional light with a cone, and its axis
// does not fit in the twelve floats a slot holds, so it takes the NEXT
// slot whole: xyz = the view-space direction the light travels, w = 4
// flagging it as a continuation rather than a light of its own. The
// engine never packs a spot into the last slot, so reading i + 1 here is
// always in range.
// The engine always fills at least slot 0: a feed that carries no
// lights of its own gets the fixed white headlight down the view axis
// written in as a stand-in, so there is no "unlit" special case here.
// Active slots are packed from 0 and the tail is zeroed, which is what
// lets the loops below stop at the first empty one.
//
// The count is Coin's own light cap, so nothing the traversal holds has
// to be dropped. Keep in step with Render::MaxViewLights.
#define VIEW_LIGHTS 8
uniform vec4 u_viewLight[VIEW_LIGHTS];
uniform vec4 u_viewLightColor[VIEW_LIGHTS];
uniform vec4 u_viewLightAtt[VIEW_LIGHTS];

/* The i-th ordinary light at a fragment: unit vector toward the light
 * in view space, and its colour with distance attenuation already
 * folded in. False for an inactive slot.
 */
bool fcViewLight(int i, vec3 vpos, out vec3 l, out vec3 lcol)
{
	float kind = u_viewLight[i].w;
	if (kind < 0.5)
	{
		l = vec3(0.0, 0.0, 1.0);
		lcol = vec3_splat(0.0);
		return false;
	}
	if (kind > 3.5)
	{
		// The cone slot of the spot light before this one: not a
		// light, shaded as one of zero colour so the caller's loop
		// walks past it. Costs one iteration of arithmetic on a
		// black light, only in a frame that has a spot at all.
		l = vec3(0.0, 0.0, 1.0);
		lcol = vec3_splat(0.0);
		return true;
	}
	if (kind > 1.5)
	{
		vec3 d = u_viewLight[i].xyz - vpos;
		float dist = length(d);
		l = dist > 0.0 ? d / dist : vec3(0.0, 0.0, 1.0);
		float denom = u_viewLightAtt[i].x * dist * dist
			+ u_viewLightAtt[i].y * dist
			+ u_viewLightAtt[i].z;
		lcol = u_viewLightColor[i].rgb
			* (denom > 0.0 ? 1.0 / denom : 1.0);
		if (kind > 2.5)
		{
			// Spot cone, the same law the scene light's spot runs
			// (GL's): outside the cutoff nothing, inside it a
			// cosine raised to the falloff exponent. `l` points
			// toward the light, the axis is the way it travels.
			// The index is bounded for the compiler's sake --
			// the packer already guarantees it, but an
			// out-of-range dynamic index is undefined in GLSL
			// even on a branch that never runs.
			int ci = i + 1 < VIEW_LIGHTS ? i + 1 : i;
			float cd = dot(-l, u_viewLight[ci].xyz);
			lcol *= cd > u_viewLightAtt[i].w
				? pow(max(cd, 1.0e-4), u_viewLightColor[i].w)
				: 0.0;
		}
	}
	else
	{
		l = -u_viewLight[i].xyz;
		lcol = u_viewLightColor[i].rgb;
	}
	return true;
}

/* Blinn-Phong exponent <-> GGX roughness, the one fit the whole engine
 * uses (App::Material::shininessToRoughness holds the C++ copy, and
 * BGFXView::setTriangleFrameState derives a draw's roughness with it).
 *
 * Coin states shininess in 0..1 and GL's exponent is n = s * 128. The
 * classical microfacet match is on the GGX WIDTH, alpha = sqrt(2/(n+2));
 * roughness is the square root of the width, because every consumer
 * squares it back (`a = rough * rough` below). Hence the fourth root
 * one way and the fourth power the other.
 */
float fcRoughFromShininess(float shininess)
{
	return pow(2.0 / (max(shininess, 0.0) * 128.0 + 2.0), 0.25);
}

float fcShininessFromRough(float rough)
{
	float a = max(rough * rough, 1.0e-3);
	return clamp((2.0 / (a * a) - 2.0) / 128.0, 0.0, 1.0);
}

/* A Blinn-Phong specular COLOUR read as metallic/roughness material data.
 *
 * That branch has no specular slot: its reflectance is f0, which it
 * builds from the base colour and the metalness alone. A Phong
 * appearance states its reflectance directly instead -- a gold's
 * gold-ness lives entirely in its specular colour, and several of the
 * stock presets (Steel, Satin, Metalized, Shiny plastic) carry a BLACK
 * diffuse with a bright specular, which read as base colour alone shade
 * nearly black.
 *
 * This is Khronos' specular-glossiness to metallic-roughness conversion,
 * written when KHR_materials_pbrSpecularGlossiness was deprecated: solve
 * the metalness for which the dielectric f0 of 0.04 and a base colour
 * reproduce the diffuse/specular pair, then recombine the base colour
 * from both readings, trusting the specular one as the surface turns
 * metallic. Only asked where NOTHING states a metalness -- the engine
 * signals that by passing a negative one.
 */
float fcPerceivedBrightness(vec3 c)
{
	return sqrt(dot(c * c, vec3(0.299, 0.587, 0.114)));
}

vec3 fcBaseFromSpecular(vec3 diffuse, vec3 spec, out float metal)
{
	float dielectric = 0.04;
	float oneMinusSS = 1.0 - max(max(spec.r, spec.g), spec.b);
	float ds = fcPerceivedBrightness(diffuse);
	float ss = fcPerceivedBrightness(spec);
	// Quadratic in the metalness, from f0 = mix(0.04, base, metal) with
	// the base solved out of the diffuse.
	float b = ds * oneMinusSS / (1.0 - dielectric) + ss - 2.0 * dielectric;
	float c = dielectric - ss;
	float disc = max(b * b - 4.0 * dielectric * c, 0.0);
	metal = ss < dielectric
		? 0.0
		: clamp((-b + sqrt(disc)) / (2.0 * dielectric), 0.0, 1.0);
	vec3 fromDiffuse = diffuse * (oneMinusSS / (1.0 - dielectric)
		/ max(1.0 - metal, 1.0e-4));
	vec3 fromSpec = (spec - vec3_splat(dielectric) * (1.0 - metal))
		/ max(metal, 1.0e-4);
	return clamp(mix(fromDiffuse, fromSpec, metal * metal),
	             vec3_splat(0.0), vec3_splat(1.0));
}

/* The user material-stage splice. A MaterialX document generates a
 * function that states the OpenPBR parameters (Renderer/MaterialXGen.cpp);
 * the assembled variant defines FC_USER_MATERIAL and appends that
 * function after this file, so the prototype here is what lets the
 * shading branch below call it. Without the define this compiles to
 * nothing and the branch shades the draw's own appearance -- the same
 * arrangement the volume stage uses for its medium functions
 * (fc_volume.sh).
 *
 * The document IS the surface: it runs after the defaults and before
 * anything is evaluated, so what it does not state keeps OpenPBR's own
 * default rather than the draw's.
 */
#ifdef FC_USER_MATERIAL
void fcUserMaterialInputs(inout FcOpenPbr m, FcMtlxGeom g);
#endif

/* One light's contribution to the OpenPBR branch. The light vector `l`
 * points from the surface toward the light in view space; the BSDF is
 * evaluated in the surface's own local frame, so it is transformed
 * there first. A two-sided draw shades a back-lit face as if the light
 * were on this side, the way GL's two-sided lighting reverses the
 * normal. The caller multiplies by the light's colour.
 *
 * The gain on the cosine is not physics: it is the direct-light gain
 * this branch has carried since PBR arrived (249741eb69). It is kept
 * so that swapping the shading model does not also change every
 * document's brightness -- one change at a time -- and it is the knob
 * to revisit when the appearance presets are re-authored as OpenPBR
 * materials.
 */
#define FC_PBR_DIRECT_GAIN 1.2

vec3 fcOpenPbrLight(FcOpenPbr m, FcPbrWeights w, vec3 vloc, vec3 l,
                    vec3 tx, vec3 ty, vec3 n, float twoside)
{
	vec3 lloc = vec3(dot(l, tx), dot(l, ty), dot(l, n));
	if (twoside > 0.5)
		lloc.z = abs(lloc.z);
	if (lloc.z <= 0.0)
		return vec3_splat(0.0);
	return fcOpenPbrBsdf(m, w, vloc, lloc)
		* (lloc.z * FC_PBR_DIRECT_GAIN);
}
// Local effect lights: unshadowed point lights added on top of
// whatever lighting model runs (the usual engine effect-light shortcut
// — no shadow map from them). The first half of the array carries the
// fire body flame lights (color premultiplied by the fire intensity
// and the CPU-clock flicker), the second half the light-source bodies
// (Render_Light bulbs, color premultiplied by their intensity).
// xyz = light position in view space, w = 1 / range^2 (0 = inactive
// slot).
#define LOCAL_LIGHTS 8
uniform vec4 u_localLight[LOCAL_LIGHTS];
uniform vec4 u_localLightColor[LOCAL_LIGHTS];
// Bulb shadow atlas (Render_LightShadow): 4x4 plain-VSM tiles.
// u_localLightColor[slot].w > 0.5 flags a shadowed bulb;
// u_bulbShadowConf[slot] = (first tile, tile count) — 1 tile = the
// plain downward cone, 6 = world-axis cube faces picked by the
// dominant axis of the light-to-fragment direction (rotated to world
// by u_bulbShadowRot). u_bulbShadowMtx maps camera view space to a
// tile's uv/depth (perspective — divide by w).
#define BULB_SHADOW_TILES 16
#define BULB_SHADOW_GRID 4
#define BULB_SLOTS 4
uniform mat4 u_bulbShadowMtx[BULB_SHADOW_TILES];
uniform vec4 u_bulbShadowConf[BULB_SLOTS];
uniform mat4 u_bulbShadowRot;
SAMPLER2D(s_texBulbShadow, 8);
// Screen-space ambient occlusion (the SSAO/GTAO chain result, its own
// resolution, normalized uv): folded into the ambient / IBL terms
// only. The directional scene light and the local effect lights carry
// their own shadow terms, and the viewer headlight shines along the
// view ray — a visible fragment is by definition unoccluded toward
// the camera — so the geometric crease darkening must not attenuate
// either (a fullscreen post-multiply left gray AO bands on faces
// point-blank lit by a Render_Light bulb, and AO on the headlight
// fill re-created them). Exception: the lone-headlight mode (no scene
// light) keeps the old full multiply — there AO is the only depth cue
// the viewport has. A white stand-in is bound when AO is off or the
// draw renders outside the main opaque pass (reflection re-render,
// overlays, on-top, transparent).
SAMPLER2D(s_texAOScreen, 9);

// One variance shadow map tap at uv against receiver light-window
// depth z. Plain VSM runs Coin SoShadowGroup's exact VsmLookup
// (including its far-plane map.x < 0.9999 early-out — cleared texels
// read as lit); EVSM (SmoothBorder blur active) warps the biased depth
// like the caster.
float fc_shadowTap(vec2 uv, float z)
{
	return fc_vsmVisibility(texture2D(s_texShadow, uv).xy, z);
}

/* The scene light's shadow factor at one fragment: 1 fully lit, 0
 * fully shadowed, a spot light's cone falloff folded in -- the one
 * number every scene-light term is attenuated by. \a tint comes back
 * as the per-channel transmittance of a glass caster over the
 * fragment.
 *
 * Its own function because the shadow-only ground
 * (fs_fc_groundshadow) paints exactly this and nothing else. A second
 * copy of the tap and its spread kernel is how the volumetric pass
 * came to hard-code a bias the mesh read from a tunable.
 */
float fcSceneShadow(vec3 vpos, vec2 fragCoord, out vec3 tint)
{
	// Variance shadow map factor of the scene light (Chebyshev upper
	// bound with light-bleed reduction); fragments outside the map stay
	// lit. Only attenuates the direct light term below. The tint map
	// adds the per-channel glass-caster transmittance.
	float shadow = 1.0;
	tint = vec3_splat(1.0);
	if (u_shadowParams.x > 0.5)
	{
		vec4 sp = mul(u_shadowMatrix, vec4(vpos, 1.0));
		// Spot lights render the map under a perspective camera.
		sp.xyz /= sp.w;
		if (sp.x > 0.0 && sp.x < 1.0 && sp.y > 0.0 && sp.y < 1.0
		    && sp.z > 0.0 && sp.z < 1.0)
		{
			tint = texture2D(s_texShadowTint, sp.xy).rgb;
			if (u_evsm.w > 0.5 && u_evsm.z > 0.0)
			{
				// Coin's N-tap spread kernel (ShadowSpreadSize
				// / SpreadSampleSize): taps average, spacing
				// scales with the homogeneous w like Coin's
				// shadowCoord.w (1 for a directional light).
				float sw = u_evsm.z * sp.w;
				shadow = 0.0;
				if (u_evsm.w < 1.5)
				{
					// SpreadSampleSize 0: Coin's fixed
					// 4-tap kernel dithered by the pixel
					// parity.
					vec2 dith = mod(floor(fragCoord),
					                vec2(2.0, 2.0));
					dith.y = -dith.y;
					shadow += fc_shadowTap(
					    sp.xy + (vec2(-1.5, 1.5) + dith) * sw,
					    sp.z);
					shadow += fc_shadowTap(
					    sp.xy + (vec2(-1.5, -0.5) + dith) * sw,
					    sp.z);
					shadow += fc_shadowTap(
					    sp.xy + (vec2(0.5, 1.5) + dith) * sw,
					    sp.z);
					shadow += fc_shadowTap(
					    sp.xy + (vec2(0.5, -0.5) + dith) * sw,
					    sp.z);
					shadow *= 0.25;
				}
				else
				{
					// SpreadSampleSize >= 1: an N x N grid,
					// N = min(2 * size + 1, 8) with Coin's
					// integer-centered offsets.
					int ntap = int(u_evsm.w + 0.5);
					int cen = ntap / 2;
					for (int j = 0; j < 8; ++j)
					{
						if (j >= ntap)
							break;
						for (int k = 0; k < 8; ++k)
						{
							if (k >= ntap)
								break;
							shadow += fc_shadowTap(
							    sp.xy
							        + vec2(float(k - cen),
							               float(j - cen))
							            * sw,
							    sp.z);
						}
					}
					shadow /= float(ntap * ntap);
				}
			}
			else
			{
				shadow = fc_shadowTap(sp.xy, sp.z);
			}
		}
	}

	// A spot light's cone falloff folds into the factor: it darkens
	// exactly what the map darkens, and every caller wants the two
	// together.
	if (u_lightPos.w > -0.5)
	{
		vec3 sceneL = normalize(vpos - u_lightPos.xyz);
		float cd = dot(sceneL, u_lightDir.xyz);
		shadow *= cd > u_lightPos.w
			? pow(max(cd, 1.0e-4), u_lightColor.w) : 0.0;
	}
	return shadow;
}

/* The full stock shading of one fragment: base color in, lit color +
 * alpha out (emissive included; the texture environment and OIT
 * weighting stay with the caller).
 *
 *   base  : base color and alpha (the stock draw's is
 *           mix(u_matColor, v_color0, u_params.x))
 *   n     : shading normal, normalized, view space (bump-perturbed by
 *           the stock caller)
 *   geoN  : geometric surface normal before any perturbation, oriented
 *           toward the viewer for two-sided draws — self-occludes the
 *           unshadowed effect lights
 *   vpos  : view-space position (v_vpos)
 *   fragCoord : gl_FragCoord.xy, passed in because shaderc's spirv
 *           path resolves gl_FragCoord only inside main()
 *   occ   : material occlusion-map factor of the ambient/IBL terms
 *           (1.0 without one)
 *   metal, rough : PBR factors, the metallic-roughness map already
 *           folded in by the caller (pass u_pbrParams.y / .z without
 *           one)
 *   matEmissive, matSpec : the draw's emissive rgb and specular
 *           rgb + shininess-in-w. The stock caller resolves them from
 *           the scalars or the per-face stream (u_matEmissive.w); the
 *           trailing overload below fills in the scalars for callers
 *           written before the stream existed (user shaders).
 */
vec4 fcShadeFragment(vec4 base, vec3 n, vec3 geoN, vec3 vpos,
                     vec2 fragCoord, float occ, float metal, float rough,
                     vec3 matEmissive, vec4 matSpec, FcMtlxGeom mtlxGeom)
{
	vec3 color = base.rgb;

	// Screen-space AO factor of the ambient-like terms below (the
	// white stand-in reads 1 when inapplicable).
	float ao = texture2D(s_texAOScreen,
	                     fragCoord * u_viewTexel.xy).x;

	// The scene light's shadow factor, and the transmittance of any
	// glass caster standing in it.
	vec3 shadowTint;
	float shadow = fcSceneShadow(vpos, fragCoord, shadowTint);

	// Scene light vector at this fragment: constant for a directional
	// light, position-dependent for a spot light -- whose cone falloff
	// the factor above already carries.
	vec3 sceneL = u_lightDir.xyz;
	if (u_lightPos.w > -0.5)
		sceneL = normalize(vpos - u_lightPos.xyz);

	// Which shading model this draw runs. A GENERATED material (a
	// MaterialX document) is an OpenPBR surface by construction, so it
	// takes that branch whatever the frame's shading mode says: a
	// matcap or a Phong evaluation has nowhere to put what the document
	// states, and the draw would silently render as though no document
	// were attached -- which is exactly how this arrived, with every
	// leg of the splice probe reading byte-identical to its control.
#ifdef FC_USER_MATERIAL
	bool matcapBranch = false;
	bool openPbrBranch = true;
	// The frame carries no PBR configuration when its PBR mode is off,
	// and its environment intensity is then zero. A document asks for a
	// physically shaded surface, so it gets the environment at full
	// strength rather than an unlit one.
	float envIntensity = u_pbrParams.x > 0.5 ? u_pbrParams.w : 1.0;
#else
	bool matcapBranch = u_matcapParams.x > 0.5;
	bool openPbrBranch = u_pbrParams.x > 0.5;
	float envIntensity = u_pbrParams.w;
#endif

	if (u_params.y > 0.5)
	{
		if (matcapBranch)
		{
			// Matcap: the whole shading is a camera-fixed studio looked
			// up by the view normal. No lights, no shadow tap -- that is
			// the point, form reads the same wherever the scene light
			// sits. Screen-space AO still applies: occlusion is not a
			// light, and contact darkening is exactly the cue an
			// inspection view wants kept.
			if (u_params.z > 0.5 && n.z < 0.0)
				n = -n;
			vec3 mc = fc_matcap(u_matcapParams.y, n);
			color = mix(mc, mc * base.rgb, u_matcapParams.z)
				* (occ * ao);
		}
		else if (openPbrBranch)
		{
			// OpenPBR (fc_openpbr.sh): the layered surface that this
			// rasterizer and the Cycles path tracer both describe, under
			// the ordinary Coin lights, the shadowed scene light and the
			// fixed world-space environment. Two-sided surfaces flip the
			// normal toward the viewer; single-sided back faces go dark
			// like the fixed-function headlight.
			if (u_params.z > 0.5 && n.z < 0.0)
				n = -n;
			float ndv = max(n.z, 1.0e-4);

			// A negative metalness asks for the Phong specular colour to
			// be read as material data -- nothing authored one, so the
			// appearance's own reflectance is the best statement of the
			// surface there is. That solve is unchanged: this branch
			// swapped its shading MODEL, not the inputs to it, so every
			// tuned appearance preset still arrives as the base colour,
			// metalness and roughness it always did.
			vec3 pbrBase = base.rgb;
			float pbrMetal = metal;
			if (metal < 0.0)
				pbrBase = fcBaseFromSpecular(base.rgb, matSpec.rgb,
				                             pbrMetal);

			// The rest of OpenPBR's parameters keep their spec defaults,
			// which is exactly the stock CAD surface: no coat, no fuzz, a
			// white specular colour at IOR 1.5. Nothing states them yet
			// -- a MaterialX document is what will.
			FcOpenPbr m;
			fcOpenPbrDefaults(m);
			m.baseColor = pbrBase;
			m.baseMetalness = pbrMetal;
			m.specularRoughness = rough;
#ifdef FC_USER_MATERIAL
			// A generated material replaces the whole surface:
			// the document states it, so the draw's own colour,
			// metalness and roughness above are only what it
			// leaves unstated.
			fcUserMaterialInputs(m, mtlxGeom);
			// The normal it states (a normal map, carried to
			// geometry_normal) replaces the mesh's, on the mesh's
			// side of the surface; everything below -- the frame,
			// the lights, the environment lookups -- reads it.
			n = fcOpenPbrShadingNormal(m, n);
			ndv = max(n.z, 1.0e-4);
#endif
			fcOpenPbrClamp(m);

			// The local shading frame, and the view vector in it.
			vec3 tx, ty;
			fcOpenPbrFrame(n, tx, ty);
			vec3 vloc = vec3(sqrt(max(0.0, 1.0 - ndv * ndv)), 0.0, ndv);
			FcPbrWeights w;
			fcOpenPbrWeights(m, ndv, w);

			// The ordinary lights always contribute, and unshadowed
			// (Coin's SoShadowGroup keeps them beside the shadow light
			// the same way). No AO on them: the dominant one is the
			// headlight, which shines along the view ray, and a visible
			// fragment is by definition unoccluded toward the camera ...
			vec3 direct = vec3_splat(0.0);
			for (int vi = 0; vi < VIEW_LIGHTS; ++vi)
			{
				vec3 vl, vlcol;
				// Active slots are packed from 0 with the tail zeroed, so
				// the first empty one ends the list -- eight slots cost
				// what the scene actually uses.
				if (!fcViewLight(vi, vpos, vl, vlcol))
					break;
				direct += fcOpenPbrLight(m, w, vloc, vl, tx, ty, n,
				                         u_params.z) * vlcol;
			}
			// ... and the shadowed scene light adds on top.
			if (u_lightDir.w > 0.5)
				direct += fcOpenPbrLight(m, w, vloc, -sceneL, tx, ty, n,
				                         u_params.z)
					* (u_lightColor.rgb * shadowTint * shadow);

			// IBL in world space (the environment does not follow the
			// camera): SH irradiance for the diffuse-like lobes, and the
			// prefiltered mip chain in the mirror direction for the
			// microfacet ones. A coat is smoother than what it covers, so
			// it reads a mip of its own.
			vec3 nw = normalize(
				mul(u_invView, vec4(n, 0.0)).xyz);
			vec3 rw = normalize(mul(u_invView,
				vec4(2.0 * n.z * n - vec3(0.0, 0.0, 1.0),
				     0.0)).xyz);
			vec3 irr = u_envSH[0].xyz
				+ u_envSH[1].xyz * nw.y
				+ u_envSH[2].xyz * nw.z
				+ u_envSH[3].xyz * nw.x
				+ u_envSH[4].xyz * (nw.x * nw.y)
				+ u_envSH[5].xyz * (nw.y * nw.z)
				+ u_envSH[6].xyz * (3.0 * nw.z * nw.z - 1.0)
				+ u_envSH[7].xyz * (nw.x * nw.z)
				+ u_envSH[8].xyz * (nw.x * nw.x - nw.y * nw.y);
			vec3 prefSpec = textureCubeLod(s_texEnv, rw,
			                               m.specularRoughness * 5.0).xyz;
			vec3 prefCoat = m.coatWeight > 0.0
				? textureCubeLod(s_texEnv, rw, m.coatRoughness * 5.0).xyz
				: vec3_splat(0.0);

			// The scene ambient (Coin's LIGHT_MODEL_AMBIENT, i.e.
			// SoEnvironment) taken for what it physically is here: a
			// uniform-radiance environment. It therefore reaches every
			// lobe, which is the only form that reaches a METAL -- a
			// metal has no diffuse at all, so an ambient folded into the
			// diffuse alone would leave it exactly as dark as before. It
			// is the light-model quantity on its own, NOT the material's
			// ambient colour times it the way Blinn-Phong wants: the BSDF
			// here already states the surface, and the ambient slot of a
			// material read as OpenPBR means nothing.
			//
			// Deliberately outside u_pbrParams.w: that knob says how
			// bright the user's environment map is, and this is a light
			// beside it, not part of it.
			vec3 ambRad = u_envAmbient.w > 0.5
				? u_envAmbient.rgb : vec3_splat(0.0);

			color = fcOpenPbrEnv(m, w, ndv, max(irr, vec3_splat(0.0)),
			                     prefSpec, prefCoat)
					* (envIntensity * occ * ao)
				+ fcOpenPbrEnv(m, w, ndv, ambRad, ambRad, ambRad)
					* (occ * ao)
				+ direct;
		}
		else
		{
			// Blinn-Phong. The ordinary Coin lights first -- the
			// headlight, plus the backlight and any document light
			// the traversal holds. This used to be one hard-coded
			// white light along the view axis, which is what a
			// single default headlight resolves to, so a stock
			// scene shades exactly as before.
			// Coin adds the ambient outright -- it is not a
			// fraction of the diffuse and does not scale with any
			// light -- and gives the diffuse full weight.
			bool trueAmb = u_ambient.w > 0.5;
			vec3 amb = trueAmb ? u_ambient.rgb : base.rgb * 0.2;
			float dw = trueAmb ? 1.0 : 0.8;
			vec3 vdiff = vec3_splat(0.0);
			vec3 vspec = vec3_splat(0.0);
			float shininess = max(matSpec.w * 128.0, 1.0);
			for (int vi = 0; vi < VIEW_LIGHTS; ++vi)
			{
				vec3 vl, vlcol;
				// Active slots are packed from 0 with the tail
				// zeroed, so the first empty one ends the list --
				// eight slots cost what the scene actually uses.
				if (!fcViewLight(vi, vpos, vl, vlcol))
					break;
				float ndl = dot(n, vl);
				vec3 h = normalize(vl + vec3(0.0, 0.0, 1.0));
				float ndh = dot(n, h);
				if (u_params.z > 0.5)
				{
					// Two-sided: GL shades a back face with the
					// normal reversed, which negates both dots.
					ndl = abs(ndl);
					ndh = abs(ndh);
				}
				else
				{
					ndl = max(ndl, 0.0);
					ndh = max(ndh, 0.0);
				}
				// GL's `f` factor: no highlight at all where the
				// light does not reach the surface. Without it a
				// broad lobe carries the specular straight past the
				// terminator -- four times Coin's spill at
				// shininess 0.05, measured on a ball lit across the
				// view.
				vdiff += vlcol * (dw * ndl);
				vspec += vlcol * (ndl > 0.0 ? pow(ndh, shininess)
				                            : 0.0);
			}

			if (u_lightDir.w > 0.5)
			{
				// Scene light (Shadow draw style), shadowed, on top
				// of the unshadowed ordinary lights: Coin's
				// SoShadowGroup keeps the viewer headlight as an
				// "other light" (the fork's viewer root always
				// carries it), so replacing it rendered much darker
				// than the GL Shadow style.
				//
				// AO occludes only the true ambient floor. The
				// ordinary-light fill is dominated by the headlight,
				// which shines along the view ray -- a visible
				// fragment is by definition unoccluded toward the
				// camera, so AO on it is wrong (it re-darkened
				// bulb-lit contacts: the fill dominates
				// viewer-facing surfaces). The scene light keeps its
				// own shadow term.
				vec3 l = -sceneL;
				float ndl = dot(n, l);
				vec3 h = normalize(l + vec3(0.0, 0.0, 1.0));
				float ndh = dot(n, h);
				if (u_params.z > 0.5)
				{
					ndl = abs(ndl);
					ndh = abs(ndh);
				}
				else
				{
					ndl = max(ndl, 0.0);
					ndh = max(ndh, 0.0);
				}
				// Gated on the diffuse term like the lights above.
				float spec = ndl > 0.0 ? pow(ndh, shininess) : 0.0;

				color = amb * (occ * ao)
					+ base.rgb
						* (vdiff + u_lightColor.rgb * shadowTint
							* (ndl * shadow))
					+ matSpec.rgb * vspec
					+ matSpec.rgb * u_lightColor.rgb
						* shadowTint * (spec * shadow);
			}
			else
			{
				// No scene light: AO deliberately covers the whole
				// term here (matching the old fullscreen multiply)
				// even though a headlight cannot be occluded -- with
				// nothing else lighting the scene, AO is the
				// viewport's only depth cue and the shading would
				// otherwise shrink to the 0.2 ambient floor.
				color = (amb * occ + base.rgb * vdiff
					+ matSpec.rgb * vspec) * ao;
			}
		}

		// Local effect lights (fire flames, Render_Light bulbs):
		// diffuse plus a Blinn specular from each light position,
		// distance-attenuated. They add to every lit branch (the PBR
		// path takes them as a plain Lambert add — a full BRDF
		// evaluation is not worth it for an effect light).
		for (int fi = 0; fi < LOCAL_LIGHTS; ++fi)
		{
			if (u_localLight[fi].w <= 0.0)
				continue;
			vec3 fl = u_localLight[fi].xyz - vpos;
			// Self-occlusion: a face whose geometric normal points away from
			// the light is shadowed by the object's own body, so this
			// unshadowed effect light must not light it (no penetrating a solid).
			if (dot(geoN, fl) <= 0.0)
				continue;
			float d2 = dot(fl, fl);
			vec3 l = normalize(fl);
			float ndl = dot(n, l);
			if (u_params.z > 0.5)
				ndl = abs(ndl);
			ndl = max(ndl, 0.0);
			float att = 1.0 / (1.0 + d2 * u_localLight[fi].w);
			// Bulb shadow tiles (upper-half slots with the flag):
			// perspective plain-VSM tap. A plain bulb has one
			// downward tile (outside its cone the light stays
			// unshadowed); an extended one six cube faces picked by
			// the dominant world axis of the fragment direction.
			if (fi >= LOCAL_LIGHTS - BULB_SLOTS
			    && u_localLightColor[fi].w > 0.5)
			{
				vec4 conf =
					u_bulbShadowConf[fi - (LOCAL_LIGHTS - BULB_SLOTS)];
				int t = int(conf.x + 0.5);
				vec3 spos = vpos;
				if (conf.y > 1.5)
				{
					vec3 dw = mul(u_bulbShadowRot,
						vec4(vpos - u_localLight[fi].xyz,
						     0.0)).xyz;
					vec3 ad = abs(dw);
					if (ad.x >= ad.y && ad.x >= ad.z)
						t += dw.x > 0.0 ? 0 : 1;
					else if (ad.y >= ad.z)
						t += dw.y > 0.0 ? 2 : 3;
					else
						t += dw.z > 0.0 ? 4 : 5;
					// Normal-offset sampling: surfaces grazing a
					// side face alias badly in its 512-texel map;
					// lift the receiver by ~1.5 texels' world
					// footprint (100 deg fov) along the surface
					// normal, scaled up as the light grazes the
					// surface (the offset a texel needs grows with
					// the depth slope), before projecting.
					float slope = 1.0
						+ (1.0 - clamp(dot(geoN, l), 0.0, 1.0));
					spos += geoN * (0.007 * sqrt(d2) * slope);
				}
				vec4 sp = mul(u_bulbShadowMtx[t],
				              vec4(spos, 1.0));
				if (sp.w > 1.0e-4)
				{
					sp.xyz /= sp.w;
					int txi = t - (t / BULB_SHADOW_GRID)
						* BULB_SHADOW_GRID;
					int tyi = t / BULB_SHADOW_GRID;
					float ts = 1.0 / float(BULB_SHADOW_GRID);
					// Tile bounds with a 2-texel guard so the
					// bilinear tap never bleeds a neighbor tile.
					float m = 2.0 / (512.0 * float(BULB_SHADOW_GRID));
					vec2 b0 = vec2(ts * float(txi) + m,
					               ts * float(tyi) + m);
					vec2 b1 = b0 + vec2_splat(ts - 2.0 * m);
					if (sp.x > b0.x && sp.x < b1.x
					    && sp.y > b0.y && sp.y < b1.y
					    && sp.z > 0.0 && sp.z < 1.0)
					{
						// 2x2 spread of bilinear taps = a 3x3
						// tent over the moments — the tiles skip
						// the scene map's smooth-border blur, so
						// filter here (VSM moments average
						// soundly; the 2-texel bounds guard
						// covers the spread).
						// 0.75 texel: wider spreads smear the
						// depth step at contact silhouettes into
						// a speckle band (bilinear mixes caster
						// and receiver depths). Far-plane texels
						// (cleared to 1,1) carry no occluder --
						// averaging them in dilutes the moments at
						// silhouette borders into speckle, so only
						// valid taps count.
						float so = 0.75
							/ (512.0 * float(BULB_SHADOW_GRID));
						vec2 moSum = vec2_splat(0.0);
						float moW = 0.0;
						vec2 mt;
						mt = texture2D(s_texBulbShadow,
							sp.xy + vec2(-so, -so)).xy;
						if (mt.x < 0.9999) { moSum += mt; moW += 1.0; }
						mt = texture2D(s_texBulbShadow,
							sp.xy + vec2(so, -so)).xy;
						if (mt.x < 0.9999) { moSum += mt; moW += 1.0; }
						mt = texture2D(s_texBulbShadow,
							sp.xy + vec2(-so, so)).xy;
						if (mt.x < 0.9999) { moSum += mt; moW += 1.0; }
						mt = texture2D(s_texBulbShadow,
							sp.xy + vec2(so, so)).xy;
						if (mt.x < 0.9999) { moSum += mt; moW += 1.0; }
						if (moW > 0.5)
						{
							vec2 mo = moSum / moW;
							float lit = sp.z <= mo.x
								? 1.0 : 0.0;
							// Coin VsmLookup shape with the
							// tunable minimum variance
							// (ShadowEpsilon, u_shadowParams.y).
							// conf.w = the format-dependent floor:
							// fp16 tiles quantize the z^2 moment,
							// so their variance needs a floor
							// above that quantization noise.
							float va = max(mo.y - mo.x * mo.x,
							               0.0)
								+ max(u_shadowParams.y,
								      max(conf.w, 1.0e-5));
							float dd = mo.x - sp.z;
							float pmax = va / (va + dd * dd);
							pmax *= smoothstep(0.2, 1.0, pmax);
							att *= max(lit, pmax);
						}
					}
				}
			}
			vec3 h = normalize(l + vec3(0.0, 0.0, 1.0));
			float shininess = max(matSpec.w * 128.0, 1.0);
			float spec = pow(max(abs(dot(n, h)), 0.0), shininess);
			// The 0.75 the Coin-fed branches above dropped stays
			// here on purpose: an effect light has no light node
			// behind it and no GL term to match, so this weight is
			// a tuned one (the fire and fountain scenes were lit
			// with it), not a parity claim. Same for the ungated
			// lobe -- these lights are already self-occluded by the
			// geoN test above.
			color += base.rgb * u_localLightColor[fi].rgb
					* (ndl * att)
				+ matSpec.rgb * u_localLightColor[fi].rgb
					* (spec * att * 0.75);
		}
	}

	color += matEmissive;
	return vec4(color, base.a);
}

// The geometry a caller that has none can pass. A generated material
// asking for a coordinate the draw does not carry reads zero, which is
// what the generator warned about when it emitted the reference.
FcMtlxGeom fcMtlxGeomNone()
{
	FcMtlxGeom g;
	g.normalWorld = vec3(0.0, 0.0, 1.0);
	g.tangentWorld = vec3(1.0, 0.0, 0.0);
	g.positionWorld = vec3_splat(0.0);
	g.normalObject = vec3(0.0, 0.0, 1.0);
	g.positionObject = vec3_splat(0.0);
	g.texcoord0 = vec2(0.0, 0.0);
	g.color0 = vec3_splat(1.0);
	return g;
}

// Geometry-less overload, for the callers that predate the splice.
vec4 fcShadeFragment(vec4 base, vec3 n, vec3 geoN, vec3 vpos,
                     vec2 fragCoord, float occ, float metal, float rough,
                     vec3 matEmissive, vec4 matSpec)
{
	return fcShadeFragment(base, n, geoN, vpos, fragCoord, occ, metal,
	                       rough, matEmissive, matSpec, fcMtlxGeomNone());
}

// Scalar-material overload: the signature user material-stage shaders
// were written against (fc_user_lighting.sh). Per-face material draws
// carrying a user shader shade with the scalars, like every other
// consumer that predates the stream.
vec4 fcShadeFragment(vec4 base, vec3 n, vec3 geoN, vec3 vpos,
                     vec2 fragCoord, float occ, float metal, float rough)
{
	return fcShadeFragment(base, n, geoN, vpos, fragCoord, occ,
	                       metal, rough,
	                       u_matEmissive.rgb, u_matSpecular);
}
