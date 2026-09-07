$input v_texcoord0

/*
 * Render debugging buffer visualization (docs/RenderDebug.md): routes
 * an intermediate render target to the screen instead of the shaded
 * scene. Submitted as a fullscreen blit into the scene color before the
 * on-top/highlight/overlay passes (BGFXView::submitDebug).
 *
 * u_debugParams: x = mode (1 = linearized depth, 2 = view-space
 *                normal, 3 = AO term, 4 = shadow term, 5 = shadow
 *                tile/atlas coverage, 6 = overdraw heatmap, 7 = shadow
 *                filtering-precision probe, 8 = UV, 9 = planar
 *                reflection, 10 = particle impact map, 11 = per-instance
 *                draw id),
 *                y = 1 / max linear view depth (depth normalization),
 *                z = shadow state valid this frame,
 *                w = scene shadow map size in texels (mode 7)
 * Stage 0: prepass normal+depth (octahedral view-space normal +
 *          positive linear view depth, .w = 0 marks background).
 * Stage 1: shadow moments (fc_volume_shadow.sh's fixed stage).
 * Stage 2: the finished AO term.
 * Stage 3: the bulb shadow tile atlas (mode 5 coverage tint).
 * Stage 5: the planar reflection target (mode 9).
 * Stage 4: the debug scene re-render target (modes 6/8/11 — overdraw
 *          counts in .x, texcoords in .xy with .w marking coverage, or
 *          the owning draw's id in .xyz as three raw byte lanes).
 */

#include <bgfx_shader.sh>
#include "fc_screen.sh"
#include "fc_matrix.sh"
#include "fc_prepass_read.sh"
#include "fc_volume_shadow.sh"

SAMPLER2D(s_texNormalZ, 0);
SAMPLER2D(s_texAO, 2);
SAMPLER2D(s_texBulbShadow, 3);
SAMPLER2D(s_texDebugScene, 4);
SAMPLER2D(s_texRefl, 5);
SAMPLER2D(s_texImpact, 6);

uniform vec4 u_debugParams;
// The water surface's own impact-map configuration (fc_water_surface):
// x = resolution, y = the clock the records are stamped against,
// z = ring lifetime, w = ring strength. Mode 10 ages the map with it,
// so the debug view and the surface read the same records the same way.
uniform vec4 u_waterImpactCfg;
// Bootstrap fallback pool of the dynamic named-parameter binding
// (docs/RenderDebug.md §2.5): lanes a no-compiler tier (stock WASM
// binaries) can map RenderDebug_userParams onto. Lane 0 here is an
// output transform — x = scale, y = bias (backend default 1/0), to
// amplify subtle differences in captured debug buffers; z overrides
// the mode-specific scale (mode 6: heatmap full-red count, default 8;
// mode 7: probe amplification, default 4096).
uniform vec4 u_userParams[4];

// Local effect lights + bulb shadow tiles (fc_mesh_fs.sh's block, the
// same uniforms the mesh receivers bind): mode 5 replicates the mesh
// shader's tile selection to show which atlas tile covers each pixel.
#define LOCAL_LIGHTS 8
#define BULB_SHADOW_TILES 16
#define BULB_SHADOW_GRID 4
#define BULB_SLOTS 4
uniform vec4 u_localLight[LOCAL_LIGHTS];
uniform vec4 u_localLightColor[LOCAL_LIGHTS];
uniform mat4 u_bulbShadowMtx[BULB_SHADOW_TILES];
uniform vec4 u_bulbShadowConf[BULB_SLOTS];
uniform mat4 u_bulbShadowRot;

// View-space ray of a screen pixel — fc_volume.sh's volRay, duplicated
// so this pass does not pull in the volumetric uniform block.
void debugRay(vec2 uv, out vec3 origin, out vec3 dir)
{
	vec2 ndc = fc_uvToNdc(uv);
	if (FC_MTX(u_proj, 2, 3) != 0.0)
	{
		origin = vec3_splat(0.0);
		dir = normalize(vec3((ndc.x + FC_MTX(u_proj, 2, 0)) / FC_MTX(u_proj, 0, 0),
		                     (ndc.y + FC_MTX(u_proj, 2, 1)) / FC_MTX(u_proj, 1, 1),
		                     -1.0));
	}
	else
	{
		origin = vec3((ndc.x - FC_MTX(u_proj, 3, 0)) / FC_MTX(u_proj, 0, 0),
		              (ndc.y - FC_MTX(u_proj, 3, 1)) / FC_MTX(u_proj, 1, 1),
		              0.0);
		dir = vec3(0.0, 0.0, -1.0);
	}
}

// Distinct color per shadow atlas tile index: a hue wheel over the 16
// tiles (t=0 red, stepping ~90 degrees so neighboring indices contrast).
vec3 tileColor(int t)
{
	float h = fract(float(t) * 0.3125);   // 5/16: large hue steps
	vec3 c = clamp(abs(fract(vec3(h, h + 0.6666667, h + 0.3333333))
	                   * 6.0 - vec3_splat(3.0)) - vec3_splat(1.0),
	               0.0, 1.0);
	return mix(vec3_splat(1.0), c, 0.85);
}

// Overdraw heatmap ramp: 0 = black, 1 = blue, then cyan-green-yellow
// up to red at full count and white beyond.
vec3 heatRamp(float t)
{
	vec3 c = vec3_splat(0.0);
	c = mix(c, vec3(0.0, 0.15, 1.0), clamp(t * 4.0, 0.0, 1.0));
	c = mix(c, vec3(0.0, 0.9, 0.4), clamp(t * 4.0 - 1.0, 0.0, 1.0));
	c = mix(c, vec3(1.0, 0.9, 0.0), clamp(t * 4.0 - 2.0, 0.0, 1.0));
	c = mix(c, vec3(1.0, 0.1, 0.0), clamp(t * 4.0 - 3.0, 0.0, 1.0));
	c = mix(c, vec3_splat(1.0), clamp(t - 1.0, 0.0, 1.0));
	return c;
}

void main()
{
	float mode = u_debugParams.x;
	vec3 rgb;
	if (mode > 5.5 && mode < 6.5)
	{
		// Overdraw heatmap: the counting re-render accumulated one
		// per rasterized fragment (u_userParams[0].z overrides the
		// full-red count, default 8).
		float full = u_userParams[0].z > 0.0 ? u_userParams[0].z : 8.0;
		float count = texture2D(s_texDebugScene, v_texcoord0).x;
		rgb = heatRamp(count / full);
	}
	else if (mode > 10.5)
	{
		// Per-instance id (docs/RenderDebug.md §2.3b): the re-render
		// target holds the exact draw id in three raw byte lanes. THAT
		// is the ground truth, read back on the CPU; what is drawn here
		// is a hashed palette, because consecutive ids differ by one
		// and would otherwise be a black screen with an invisible
		// gradient. Neighbouring draws in different colours is the only
		// thing the eye needs from this mode; the screen is never the
		// thing the audit reads.
		vec4 idc = texture2D(s_texDebugScene, v_texcoord0);
		float id = idc.x + idc.y * 256.0 + idc.z * 65536.0;
		if (idc.w < 0.5)
			rgb = vec3_splat(0.0);   // background: no draw owns it
		else
			rgb = vec3(fract(id * 0.6180339887),
			           fract(id * 0.4142135624),
			           fract(id * 0.7320508076)) * 0.85
			      + vec3_splat(0.15);
	}
	else if (mode > 9.5)
	{
		// The particle impact map (docs/RenderEngine.md §5.8), laid
		// over the screen: green where a hit is recorded, its
		// brightness the age of that record against the ring lifetime,
		// red where the cell has never been struck. It answers, on its
		// own, whether nothing was reported, whether it was reported in
		// the wrong place, or whether the surface simply fails to show
		// what is there.
		vec4 im = texture2D(s_texImpact, v_texcoord0);
		float life = max(u_waterImpactCfg.z, 1.0e-3);
		float age = (u_waterImpactCfg.y - im.z) / life;
		rgb = im.w > 0.0
			? vec3(0.0, clamp(1.0 - age, 0.0, 1.0), im.w)
			: vec3(0.15, 0.0, 0.0);
	}
	else if (mode > 8.5)
	{
		// The planar reflection target: what the mirrored camera
		// actually rendered, before the water surface samples it.
		// Alpha is the mirror's own coverage (the pass clears to 0 =
		// nothing reflected), so an empty mirror and a mirror the
		// surface merely fails to show are different pictures here.
		vec4 rf = texture2D(s_texRefl, v_texcoord0);
		rgb = mix(vec3(0.15, 0.0, 0.0), rf.xyz, rf.w);
	}
	else if (mode > 7.5)
	{
		// UV / texcoord of the visible surface (the depth-tested
		// re-render; .w marks coverage). The blue floor separates
		// covered geometry from the background — untextured meshes
		// carry no texcoord attribute and read as UV (0,0), which
		// would otherwise be background-black.
		vec4 t = texture2D(s_texDebugScene, v_texcoord0);
		rgb = t.w > 0.5 ? vec3(fract(t.x), fract(t.y), 0.25)
		                : vec3_splat(0.0);
	}
	else
	{
		vec4 nz = texture2D(s_texNormalZ, v_texcoord0);
		if (nz.w < 0.5)
		{
			// Background (no prepass fragment): black.
			gl_FragColor = vec4(0.0, 0.0, 0.0, 1.0);
			return;
		}
		if (mode < 1.5)         // linearized depth, near = white
			rgb = vec3_splat(
				1.0 - clamp(nz.z * u_debugParams.y, 0.0, 1.0));
		else if (mode < 2.5)    // view-space normal
			rgb = fc_octDecode(nz.xy) * 0.5 + vec3_splat(0.5);
		else if (mode < 3.5)    // ambient occlusion term
			rgb = vec3_splat(texture2D(s_texAO, v_texcoord0).x);
		else
		{
			// Modes 4/5/7 reconstruct the view-space position from
			// the prepass depth along the pixel ray.
			vec3 origin, dir;
			debugRay(v_texcoord0, origin, dir);
			vec3 p = FC_MTX(u_proj, 2, 3) != 0.0
				? dir * (nz.z / max(1.0e-6, -dir.z))
				: origin + dir * nz.z;
			if (mode < 4.5)     // shadow term (lit = white)
			{
				float vis = 1.0;
				if (u_debugParams.z > 0.5)
					vis = shadowVis(p);
				rgb = vec3_splat(vis);
			}
			else if (mode < 5.5)
			{
				// Shadow tile coverage: dark gray where the scene
				// shadow map projects, a distinct color per bulb
				// atlas tile that would be sampled (fc_mesh_fs.sh's
				// selection: cube face by dominant axis when
				// extended, in-tile-bounds with the 2-texel guard).
				rgb = vec3_splat(0.0);
				if (u_debugParams.z > 0.5)
				{
					vec4 sp = mul(u_shadowMatrix, vec4(p, 1.0));
					sp.xyz /= sp.w;
					if (sp.x > 0.0 && sp.x < 1.0
					    && sp.y > 0.0 && sp.y < 1.0
					    && sp.z > 0.0 && sp.z < 1.0)
						rgb = vec3_splat(0.3);
				}
				for (int s = 0; s < BULB_SLOTS; ++s)
				{
					int fi = LOCAL_LIGHTS - BULB_SLOTS + s;
					if (u_localLightColor[fi].w <= 0.5
					    || u_localLight[fi].w <= 0.0)
						continue;
					vec4 conf = u_bulbShadowConf[s];
					int t = int(conf.x + 0.5);
					if (conf.y > 1.5)
					{
						vec3 dw = mul(u_bulbShadowRot,
							vec4(p - u_localLight[fi].xyz,
							     0.0)).xyz;
						vec3 ad = abs(dw);
						if (ad.x >= ad.y && ad.x >= ad.z)
							t += dw.x > 0.0 ? 0 : 1;
						else if (ad.y >= ad.z)
							t += dw.y > 0.0 ? 2 : 3;
						else
							t += dw.z > 0.0 ? 4 : 5;
					}
					vec4 sp = mul(u_bulbShadowMtx[t],
					              vec4(p, 1.0));
					if (sp.w <= 1.0e-4)
						continue;
					sp.xyz /= sp.w;
					int txi = t - (t / BULB_SHADOW_GRID)
						* BULB_SHADOW_GRID;
					int tyi = t / BULB_SHADOW_GRID;
					float ts = 1.0 / float(BULB_SHADOW_GRID);
					float m = 2.0
						/ (512.0 * float(BULB_SHADOW_GRID));
					vec2 b0 = vec2(ts * float(txi) + m,
					               ts * float(tyi) + m);
					vec2 b1 = b0 + vec2_splat(ts - 2.0 * m);
					if (sp.x > b0.x && sp.x < b1.x
					    && sp.y > b0.y && sp.y < b1.y
					    && sp.z > 0.0 && sp.z < 1.0)
						rgb = tileColor(t);
				}
			}
			else
			{
				// Filtering-precision probe (the RG16F stipple
				// class): hardware bilinear of the shadow moments
				// vs the same four texels blended at full shader
				// precision — a nonzero difference is exactly the
				// sampler's internal filtering quantization. R/G =
				// per-moment |HW - SW| amplified (u_userParams[0].z
				// overrides the gain, default 4096), B = the
				// variance term those moments produce.
				rgb = vec3_splat(0.0);
				if (u_debugParams.z > 0.5 && u_debugParams.w > 0.5)
				{
					vec4 sp = mul(u_shadowMatrix, vec4(p, 1.0));
					sp.xyz /= sp.w;
					if (sp.x > 0.0 && sp.x < 1.0
					    && sp.y > 0.0 && sp.y < 1.0
					    && sp.z > 0.0 && sp.z < 1.0)
					{
						float sz = u_debugParams.w;
						vec2 tuv = sp.xy * sz - vec2_splat(0.5);
						vec2 base = floor(tuv);
						vec2 f = tuv - base;
						vec2 m00 = texture2DLod(s_texShadow,
							(base + vec2(0.5, 0.5)) / sz, 0.0).xy;
						vec2 m10 = texture2DLod(s_texShadow,
							(base + vec2(1.5, 0.5)) / sz, 0.0).xy;
						vec2 m01 = texture2DLod(s_texShadow,
							(base + vec2(0.5, 1.5)) / sz, 0.0).xy;
						vec2 m11 = texture2DLod(s_texShadow,
							(base + vec2(1.5, 1.5)) / sz, 0.0).xy;
						vec2 sw = mix(mix(m00, m10, f.x),
						              mix(m01, m11, f.x), f.y);
						vec2 hw = texture2DLod(s_texShadow,
						                       sp.xy, 0.0).xy;
						float amp = u_userParams[0].z > 0.0
							? u_userParams[0].z : 4096.0;
						// RELATIVE difference: the EVSM warp stores
						// moments up to e^42 — an absolute diff would
						// saturate on any ULP-level deviation there,
						// reading "differs at all" instead of "differs
						// meaningfully". Relative ~2^-9 (the classic
						// 8-bit fixed-point filter-weight class)
						// saturates at the default gain.
						vec2 d = abs(hw - sw)
							/ max(abs(sw), vec2_splat(1.0e-12))
							* amp;
						float va = max(hw.y - hw.x * hw.x, 0.0);
						rgb = clamp(vec3(d.x, d.y, va * amp),
						            0.0, 1.0);
					}
				}
			}
		}
	}
	rgb = rgb * u_userParams[0].x + vec3_splat(u_userParams[0].y);
	gl_FragColor = vec4(rgb, 1.0);
}
