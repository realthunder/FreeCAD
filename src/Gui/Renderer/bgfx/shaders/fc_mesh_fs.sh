/*
 * FreeCAD CAD-mesh fragment shader body: the texture environment, bump
 * mapping and OIT variants around the shared lighting core
 * (fc_mesh_lighting.sh — headlight/scene-light Blinn-Phong or PBR,
 * shadows, local effect lights, screen-space AO).
 * Included by fs_fc_mesh.sc and fs_fc_mesh_clip.sc (CLIP_PLANES defined,
 * fc_clip.sh included first).
 *
 * The OIT variant (fs_fc_mesh_oit*) is the weighted-blended OIT
 * accumulation pass (McGuire/Bavoil 2013): RT0 accumulates the
 * depth-weighted premultiplied color (blend ONE, ONE), RT1 the
 * revealage product (blend ZERO, INV_SRC_COLOR).
 */

#include "fc_color.sh"
#include "fc_matrix.sh"

#include "fc_mesh_lighting.sh"
#include "fc_finish.sh"

// Per-face texture palette (unit 10): the images the draw's faces are
// painted with, as the layers of one array texture, since a draw binds
// one sampler. Outside the TEXTURE variants deliberately -- the
// coordinates come from the face's own projection frame, so a shape
// with no texture coordinates at all can still be imaged face by face.
//
// x = on, y = millimetres of object space per tile (<= 0 = use the
// mesh's own texture coordinates, which only the TEXTURE variants
// have), z = the one layer every fragment reads (< 0 = read the
// per-face stream), w = how many layers the array holds.
SAMPLER2DARRAY(s_texFace, 10);
uniform vec4 u_faceTexParams;

#ifdef GROUND_FADE
#include "fc_ground_fade.sh"
#endif

#ifdef TEXTURE
SAMPLER2D(s_texColor, 0);
// x = texture environment (0 modulate, 1 decal, 2 blend, 3 replace),
// y = the source format carries alpha (REPLACE keeps the fragment alpha
//     for alpha-less formats, invisible to the RGBA8-expanded sampler)
uniform vec4 u_texParams;
uniform vec4 u_texBlendColor;
// Bump mapping (SoBumpMap, only ever active in the TEXTURE variants —
// the mesh must carry texcoords). x = mode (0 off, 1 tangent-space
// normal map, 2 grayscale height, 3 height + parallax-occlusion),
// y = strength (normal map slope multiplier, or the height amplitude
// in UV units), zw = one texel in UV. The tangent frame comes from
// screen-space derivatives of v_vpos and the UV (the cotangent-frame
// trick) — no vertex tangents, any UV source works.
SAMPLER2D(s_texBump, 2);
uniform vec4 u_bumpParams;
// Emissive/occlusion material maps (SoFCRenderTexture; the mesh must
// carry texcoords like bump mapping). The emissive rgb adds to the lit
// and textured color; the occlusion first channel multiplies the
// ambient/environment contribution (glTF semantics).
SAMPLER2D(s_texEmissive, 4);
SAMPLER2D(s_texOcclusion, 5);
// glTF metallic-roughness map (u_pbrParams.x = 2): the green channel
// multiplies the roughness factor, the blue channel the metallic
// factor — PBR branch only.
SAMPLER2D(s_texMetallicRoughness, 6);
#endif

void main()
{
#ifdef CLIP_PLANES
	clipDiscard(v_wpos);
#endif

	vec4 base = mix(u_matColor, v_color0, u_params.x);
	// Per-face material: u_matEmissive.w selects the baked stream
	// (v_color1 = emissive, v_color2 = specular rgb + shininess in
	// alpha, the same 0..1 scale as u_matSpecular.w) over the scalars.
	// 2 means the same stream with the two alpha slots carrying the
	// PBR factor pair instead (resolved with the factors below).
	float perFace = step(0.5, u_matEmissive.w);
	vec3 matEmissive = mix(u_matEmissive.rgb, v_color1.rgb, perFace);
	vec4 matSpec = mix(u_matSpecular, v_color2, perFace);

	vec3 n = normalize(v_normal);
	// Geometric surface normal (before any bump perturbation), oriented toward
	// the viewer for two-sided surfaces. Used to self-occlude unshadowed effect
	// lights: a solid's own body shadows its far side, so the fire light must
	// not reach faces whose GEOMETRIC normal points away from the flame -- the
	// bump map would otherwise catch that light on the back side.
	vec3 geoN = n;
	if (u_params.z > 0.5 && geoN.z < 0.0)
		geoN = -geoN;
#ifdef TEXTURE
	vec2 uv = v_texcoord0;
	if (u_bumpParams.x > 0.5)
	{
		vec3 dp1 = dFdx(v_vpos);
		vec3 dp2 = dFdy(v_vpos);
		vec2 duv1 = dFdx(uv);
		vec2 duv2 = dFdy(uv);
		vec3 dp2perp = cross(dp2, n);
		vec3 dp1perp = cross(n, dp1);
		vec3 T = dp2perp * duv1.x + dp1perp * duv2.x;
		vec3 B = dp2perp * duv1.y + dp1perp * duv2.y;
		float invmax = inversesqrt(max(dot(T, T), dot(B, B)));

		if (u_bumpParams.x > 2.5)
		{
			// Parallax-occlusion: march the tangent-space view
			// ray until it dips below the height field, then
			// refine linearly between the last two samples.
			vec3 vdir = FC_MTX(u_proj, 2, 3) != 0.0
				? normalize(-v_vpos) : vec3(0.0, 0.0, 1.0);
			vec3 vts = vec3(dot(vdir, normalize(T)),
			                dot(vdir, normalize(B)),
			                dot(vdir, n));
			if (vts.z > 0.1)
			{
				vec2 shift = -vts.xy / vts.z
					* u_bumpParams.y / 16.0;
				float layer = 1.0 / 16.0;
				float cur = 0.0;
				vec2 tuv = uv;
				float depth = 1.0
					- texture2D(s_texBump, tuv).x;
				float pdepth = depth;
				vec2 puv = tuv;
				for (int i = 0; i < 16; ++i)
				{
					if (cur >= depth)
						break;
					puv = tuv;
					pdepth = depth;
					tuv += shift;
					cur += layer;
					depth = 1.0
						- texture2D(s_texBump, tuv).x;
				}
				float after = depth - cur;
				float before = pdepth - (cur - layer);
				uv = mix(puv, tuv,
				         clamp(before
				                   / max(before - after,
				                         1.0e-4),
				               0.0, 1.0));
			}
		}

		vec3 nts;
		if (u_bumpParams.x > 1.5)
		{
			// Grayscale height to normal, central differences.
			float hx1 = texture2D(s_texBump,
				uv + vec2(u_bumpParams.z, 0.0)).x;
			float hx0 = texture2D(s_texBump,
				uv - vec2(u_bumpParams.z, 0.0)).x;
			float hy1 = texture2D(s_texBump,
				uv + vec2(0.0, u_bumpParams.w)).x;
			float hy0 = texture2D(s_texBump,
				uv - vec2(0.0, u_bumpParams.w)).x;
			nts = vec3((hx0 - hx1) * u_bumpParams.y
			               / (2.0 * u_bumpParams.z),
			           (hy0 - hy1) * u_bumpParams.y
			               / (2.0 * u_bumpParams.w),
			           1.0);
		}
		else
		{
			// Tangent-space normal map (RGB, Coin convention).
			nts = texture2D(s_texBump, uv).xyz * 2.0
				- vec3_splat(1.0);
			nts.xy *= u_bumpParams.y;
		}
		n = normalize((T * nts.x + B * nts.y) * invmax
		              + n * nts.z);
	}
#endif

	// Ambient/environment occlusion factor of the material's occlusion
	// map: indirect light only (glTF semantics) — the constant ambient
	// term of the fixed-function paths and the IBL of the PBR path;
	// direct (head/scene) light stays untouched.
	float occ = 1.0;
#ifdef TEXTURE
	if (u_texParams.w > 0.5)
		occ = texture2D(s_texOcclusion, uv).x;
#endif

#ifndef OIT
	if (u_shadowParams.x > 0.5 && u_shadowParams.w > 0.5)
	{
		// debug (FC_BGFX_DEBUG_SHADOW_VIS): red = stored
		// depth (unwarped), green = stored depth^2 moment
		// (unwarped), blue = receiver light depth
		vec4 sp = mul(u_shadowMatrix, vec4(v_vpos, 1.0));
		sp.xyz /= sp.w;
		vec2 dm = texture2D(s_texShadow, sp.xy).xy;
		gl_FragColor = u_evsm.x < 0.5
			? vec4(dm.x, dm.y, sp.z, 1.0)
			: vec4(
				log(max(dm.x, 1.0e-12)) / u_evsm.x,
				log(max(dm.y, 1.0e-12))
					/ (2.0 * u_evsm.x),
				sp.z, 1.0);
		return;
	}
#endif

	// PBR factors with the glTF metallic-roughness map folded in
	// (g = roughness, b = metallic multipliers; keep the CPU-side
	// roughness floor after the multiply).
	float metal = u_pbrParams.y;
	float rough = u_pbrParams.z;
	// Per-face PBR (u_matEmissive.w = 2): the stream's two alpha slots
	// carry the factor pair instead of a constant and the shininess.
	if (u_matEmissive.w > 1.5)
	{
		if (u_pbrParams.x > 0.5)
		{
			metal = v_color1.a;
			rough = max(v_color2.a, 0.02);
		}
		else
		{
			// A Phong frame reads that slot as a shininess, so
			// give it the shininess the roughness means (the
			// Blinn-Phong-to-GGX fit, inverted -- what the
			// stored material's Phong derivation would hold).
			matSpec.w = fcShininessFromRough(
				max(v_color2.a, 0.02));
		}
	}
#ifdef TEXTURE
	if (u_pbrParams.x > 1.5)
	{
		vec4 mrt = texture2D(s_texMetallicRoughness, uv);
		metal = clamp(metal * mrt.z, 0.0, 1.0);
		rough = clamp(rough * mrt.y, 0.02, 1.0);
	}
#endif

	// Machined surface finish (App::SurfaceFinish): the shading normal
	// only, never the geometric one -- geoN answers which side of the
	// body a face is on, and a knurl is not a side.
	// The palette entry this face names: the stream's third slot, or
	// entry 0 (the draw's own finish) for a draw that does not consume
	// the stream -- the same u_matEmissive.w gate the material fields
	// above use, so an unbound attribute is never read.
	vec3 finishSlot = v_findex * perFace;
	vec4 finishParams = fcFinishEntry(finishSlot.x);
	if (finishParams.x > 0.5)
	{
		// The unresolvable part of the pattern comes back as
		// roughness, which the Phong path spells as a shininess: give
		// it the roughness its shininess means, and take the answer
		// back the same way, so a finish coarsens a Phong highlight
		// exactly as it coarsens a PBR one.
		bool phong = u_pbrParams.x < 0.5;
		float frough = phong ? fcRoughFromShininess(matSpec.w)
		                     : rough;
		fcApplyFinish(v_opos, v_onrm, v_vpos, finishParams,
		              finishSlot.y, n, frough);
		if (phong)
			matSpec.w = fcShininessFromRough(frough);
		else
			rough = frough;
	}

	// What a generated material may ask of the geometry. Filled here
	// because this is where the varyings are readable -- the SPIR-V
	// path resolves one only inside main() -- by the fill the glass
	// stage shares (fc_openpbr.sh).
#if defined(TEXTURE) || defined(FC_USER_MATERIAL)
	vec2 mtlxUv = v_texcoord0;
#else
	vec2 mtlxUv = vec2(0.0, 0.0);
#endif
	FcMtlxGeom mtlxGeom = fcMtlxGeomFill(n, v_vpos, v_onrm, v_opos,
	                                     mtlxUv, v_color0.rgb);

	vec4 lit = fcShadeFragment(base, n, geoN, v_vpos, gl_FragCoord.xy,
	                           occ, metal, rough, matEmissive, matSpec,
	                           mtlxGeom);
	vec3 color = lit.rgb;
	float alpha = lit.a;

#ifdef TEXTURE
	// GL fixed-function texture environment, applied to the lit color
	// like GL textures the rasterized fragment (uv carries the
	// parallax offset when active).
	// A base-colour texture is a PICTURE: its texels are display
	// numbers exactly like a picked colour, so they decode on the way
	// in too. Its alpha is coverage and is left alone. The data maps
	// -- bump, metallic-roughness, occlusion -- are NOT pictures and
	// are never decoded; their channels are quantities.
	vec4 texel = fcAuthoredColor4(texture2D(s_texColor, uv));
	float texmodel = u_texParams.x;
	if (texmodel < 0.5) {        // modulate
		color *= texel.rgb;
		alpha *= texel.a;
	} else if (texmodel < 1.5) { // decal
		color = mix(color, texel.rgb, texel.a);
	} else if (texmodel < 2.5) { // blend
		color = mix(color, u_texBlendColor.rgb, texel.rgb);
		alpha *= texel.a;
	} else {                     // replace
		color = texel.rgb;
		if (u_texParams.y > 0.5)
			alpha = texel.a;
	}

	// Fully transparent texels contribute no color but would still
	// write depth (GL parity: alpha-tested textures — the NaviCube's
	// shaped face textures — discard their transparent skirt so it
	// cannot depth-occlude later draws).
	if (alpha < 0.004)
		discard;

	// Emissive map: added after the texture environment so the base
	// color texture does not modulate the glow (glTF semantics).
	if (u_texParams.z > 0.5)
		color += fcAuthoredColor(texture2D(s_texEmissive, uv).rgb);
#endif

	// The face's own image, on top of whatever the unit-0 texture did:
	// a shape may carry both, and the face image is the more specific
	// statement. Modulate, like the default texture environment -- a
	// marking on a red part comes out red-tinted, which is what the
	// same image applied through the ordinary texture path does.
	if (u_faceTexParams.x > 0.5)
	{
		// The draw's own layer, or this face's out of the stream.
		// Layer numbering starts at 1: 0 is the untextured face, and
		// the array's layer i holds palette entry i.
		float faceLayer = u_faceTexParams.z >= 0.0
			? u_faceTexParams.z : finishSlot.z;
		vec2 fuv;
		if (u_faceTexParams.y > 0.0)
			fuv = fcFrameTexUV(v_opos, v_onrm, finishSlot.y,
			                   u_faceTexParams.y);
		else
#ifdef TEXTURE
			fuv = uv;
#else
			fuv = vec2(0.0, 0.0);
#endif
		float slice = clamp(faceLayer - 1.0, 0.0,
		                    max(u_faceTexParams.w - 1.0, 0.0));
		// KEY: the sample is taken HERE, outside the per-face test
		// below, because WHICH face a fragment belongs to varies
		// across the quad. A sampler in divergent control flow has
		// no defined derivatives, so it cannot pick a mip level --
		// and an unmipped minified checker is the speckle this
		// costs. The branch above is uniform per draw (a uniform and
		// the object-space position), so the derivatives here are
		// the real ones.
		//
		// A picture, like the base colour texture: its texels are
		// display numbers and decode on the way in, its alpha is
		// coverage and does not.
		vec4 ftex = fcAuthoredColor4(
			texture2DArray(s_texFace, vec3(fuv, slice)));
		if (faceLayer > 0.5)
		{
			color *= ftex.rgb;
			alpha *= ftex.a;
			if (alpha < 0.004)
				discard;
		}
	}

#ifdef GROUND_FADE
	// The ground's rim, dissolved (fc_ground_fade.sh).
	alpha *= fcGroundFade(v_vpos);
	// Discarded rather than left to blend at zero: the quad writes
	// depth, and an invisible ground that still occludes what is behind
	// it is a worse artifact than the edge this fade removes. Early-Z
	// is what it costs, which for one quad is nothing.
	if (alpha < 0.004)
		discard;
#endif

#ifdef OIT
	// Depth weight, McGuire's eq. (10): near fragments dominate. The
	// composite pass divides the accumulated premultiplied color by the
	// accumulated weighted alpha, so the weight cancels per-surface.
	float w = alpha
		* max(1.0e-2, 3.0e3 * pow(1.0 - gl_FragCoord.z, 3.0));
	gl_FragData[0] = vec4(color * alpha, alpha) * w;
	gl_FragData[1] = vec4_splat(alpha);
#else
	gl_FragColor = vec4(color, alpha);
#endif
}
