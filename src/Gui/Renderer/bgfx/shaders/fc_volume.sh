/*
 * Shared setup of the volumetric lighting passes (fs_fc_volume,
 * fs_fc_volume_apply): per-pixel view ray reconstruction from the scene
 * projection, and the bounded scattering medium — a sphere around the
 * scene (view-space center/radius in u_volMedium). Bounding the medium
 * keeps the eye-to-model distance out of the optical depth (with an
 * unbounded medium the camera's stand-off distance alone would fog out
 * the whole scene) and is the same entry/exit-bounds core the planned
 * water medium extends.
 *
 * The optional water body (scene draws flagged Material::water) is a
 * second, per-channel medium: its front/back depth targets bound the
 * underwater stretch of each ray, with extinction/scattering from
 * u_waterSigma instead of the air density.
 *
 * The water/cloud/fire parameter uniforms are MEDIUM_SLOTS-entry
 * arrays: each body of a medium kind gets an appearance slot, the
 * interval depth writer stamps the slot index into the targets' .x
 * (fc_meddepth_fs.sh), and the span helpers return it so every pixel
 * reads its own body's parameters. Bodies of one kind overlapping on
 * screen still merge into one interval with the nearest body's slot —
 * the remaining (much smaller) limitation.
 *
 * u_volParams : x = air medium density (1/world units), y = intensity,
 *               z = maximum march distance, w = water body active
 * u_volMedium : xyz = medium sphere center (view space), w = radius
 * u_waterSigma[s]: xyz = water extinction per channel, w = water
 *               scattering coefficient
 * u_cloudParams[s]: x = cloud extinction density (1/world units), y =
 *               noise domain scale (1/world units), z = drift time,
 *               w > 0.5 = slot active
 * u_fireParams[s]: x = flame emission density (1/world units), y =
 *               noise domain scale (1/world units), z = rise time,
 *               w > 0.5 = slot active
 * u_fireParams2[s]: x = 1 / body lateral radius (the reflection media
 *               pass's analytic cylinder bound), y = 1 / body extent
 *               along its up axis (the flame taper frame), z = soot
 *               extinction density (1/world units; a mild absorption
 *               that follows the temperature field, 0 = none), w unused
 * u_fireFrame[s]: world -> fire-local transform — z rises along the
 *               body placement's up axis (not world z), origin at the
 *               bottom center of the body, unit world scale. The
 *               noise field lives in this frame, so the flame rides a
 *               moved or tilted body.
 */

#include "fc_screen.sh"
#include "fc_matrix.sh"

#define MEDIUM_SLOTS 4

uniform vec4 u_volParams;
uniform vec4 u_volMedium;
uniform vec4 u_waterSigma[MEDIUM_SLOTS];
uniform vec4 u_cloudParams[MEDIUM_SLOTS];
uniform vec4 u_fireParams[MEDIUM_SLOTS];
uniform vec4 u_fireParams2[MEDIUM_SLOTS];
uniform mat4 u_fireFrame[MEDIUM_SLOTS];
// Fountain bodies share the cloud medium channel (targets, slots and
// u_cloudParams entry); u_cloudParams[s].w = 2 flags the fountain
// density field instead of the cloud FBM.
// u_fountainParams[s]: x = 1 / body height, y = 1 / lateral radius
// u_fountainFrame[s]: world -> fountain-local flow frame (z up along
//               the body placement's up axis, origin bottom center).
uniform vec4 u_fountainParams[MEDIUM_SLOTS];
uniform mat4 u_fountainFrame[MEDIUM_SLOTS];

// Appearance slot index stamped into a medium interval sample's .x by
// the depth writer; the front sample wins, the back sample covers the
// camera-inside-the-body case (no front faces on screen).
int mediumSlot(vec4 front, vec4 back)
{
	float s = front.w > 0.5 ? front.x : back.x;
	return int(clamp(s, 0.0, float(MEDIUM_SLOTS - 1)) + 0.5);
}

// View-space ray of a screen pixel (uv in [0,1]). The w row's z entry
// is -1 for a perspective projection (w = viewZ) and 0 for an
// orthographic one (w = 1) -- the row the clip-depth remap leaves
// alone; both viewer down -z.
void volRay(vec2 uv, out vec3 origin, out vec3 dir)
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

// Ray parameter of a positive linear view depth (perspective rays are
// not parallel to the view axis).
float volT(float viewZ, vec3 dir)
{
	return FC_MTX(u_proj, 2, 3) != 0.0 ? viewZ / max(1.0e-6, -dir.z) : viewZ;
}

// Ray length to the opaque surface behind the pixel (prepass linear
// view depth in nz, .w = 0 marks background), capped at the frame's
// maximum march distance.
float volSurface(vec4 nz, vec3 dir)
{
	float tEnd = u_volParams.z;
	if (nz.w > 0.5)
		tEnd = min(tEnd, volT(nz.z, dir));
	return tEnd;
}

// Underwater interval of the ray from the water depth samples, with
// the body's appearance slot in .z; (0, -1, 0) when the pixel has no
// water body. A back face without a front face means the camera is
// inside the water.
vec3 volWaterSpan(vec4 wf, vec4 wb, vec3 dir)
{
	if (u_volParams.w < 0.5 || wb.w < 0.5)
		return vec3(0.0, -1.0, 0.0);
	return vec3(wf.w > 0.5 ? volT(wf.z, dir) : 0.0,
	            volT(wb.z, dir),
	            float(mediumSlot(wf, wb)));
}

// Cloud body interval of the ray from its depth target pair, slot in
// .z; (0, -1, 0) when the pixel has no cloud body (the water span
// rules, plus the per-slot active flag — a stale target from a frame
// with the medium disabled reads all slots inactive).
vec3 volCloudSpan(vec4 cf, vec4 cb, vec3 dir)
{
	if (cb.w < 0.5)
		return vec3(0.0, -1.0, 0.0);
	int s = mediumSlot(cf, cb);
	if (u_cloudParams[s].w < 0.5)
		return vec3(0.0, -1.0, 0.0);
	return vec3(cf.w > 0.5 ? volT(cf.z, dir) : 0.0,
	            volT(cb.z, dir), float(s));
}

// Value-noise FBM for the cloud density field (float math only, no bit
// ops — WebGL2/GLSL-140 safe). Domain in scaled world units.
float cloudHash(vec3 p)
{
	return fract(sin(dot(p, vec3(12.9898, 78.233, 37.719)))
	             * 43758.5453);
}

float cloudNoise(vec3 p)
{
	vec3 i = floor(p);
	vec3 f = p - i;
	vec3 u = f * f * (vec3_splat(3.0) - 2.0 * f);
	float n000 = cloudHash(i);
	float n100 = cloudHash(i + vec3(1.0, 0.0, 0.0));
	float n010 = cloudHash(i + vec3(0.0, 1.0, 0.0));
	float n110 = cloudHash(i + vec3(1.0, 1.0, 0.0));
	float n001 = cloudHash(i + vec3(0.0, 0.0, 1.0));
	float n101 = cloudHash(i + vec3(1.0, 0.0, 1.0));
	float n011 = cloudHash(i + vec3(0.0, 1.0, 1.0));
	float n111 = cloudHash(i + vec3(1.0, 1.0, 1.0));
	return mix(mix(mix(n000, n100, u.x), mix(n010, n110, u.x), u.y),
	           mix(mix(n001, n101, u.x), mix(n011, n111, u.x), u.y),
	           u.z);
}

// 3-octave FBM in [0,1], drifted by the animation clock; cp = the
// body slot's u_cloudParams entry.
float cloudFBM(vec3 wp, vec4 cp)
{
	vec3 p = wp * cp.y + vec3(cp.z, 0.0, 0.17 * cp.z);
	float n = 0.5 * cloudNoise(p)
		+ 0.25 * cloudNoise(p * 2.03)
		+ 0.125 * cloudNoise(p * 4.09);
	return n / 0.875;
}

// Local cloud extinction at a world position: the FBM field remapped
// so roughly half the volume is clear (puffy holes).
float cloudDensityAt(vec3 wp, vec4 cp)
{
	return cp.x * smoothstep(0.4, 0.75, cloudFBM(wp, cp));
}

// Water-spray density of a fountain body at a world position: a
// rising jet column plus the parabolic fall envelope (the ballistic
// sheet water follows leaving the jet), modulated by streak noise
// advected up the jet and down the outer fall. cp = the slot's
// u_cloudParams entry (x = density, y = noise scale, z = flow time),
// fp = its u_fountainParams entry.
float fountainDensityAt(vec3 wp, mat4 frame, vec4 cp, vec4 fp)
{
	vec3 lp = mul(frame, vec4(wp, 1.0)).xyz;
	float h = clamp(lp.z * fp.x, 0.0, 1.0);
	float rr = length(lp.xy) * fp.y;
	// Jet column, widening slightly as it rises; brightest core.
	float rj = rr / (0.09 + 0.08 * h);
	float jet = 1.5 * exp(-rj * rj) * (1.1 - 0.4 * h * h);
	// Fall envelope: a thin ballistic sheet peaking over the jet and
	// landing at the rim — densest at the tip-over arc near the top,
	// fading as the water falls and spreads.
	float dr = clamp((rr - 0.10) / 0.90, 0.0, 1.0);
	float hs = 1.0 - dr * dr;
	float ds = (h - hs) * 6.0;
	float sheet = exp(-ds * ds) * smoothstep(0.04, 0.18, rr);
	// The crown: dense white water at the tip-over arc right above
	// the jet; below it the sheet exists only as separate falling
	// streams (radial noise, constant along rays from the axis,
	// slowly rotating) — a real fountain sheds arcs, not a dome.
	float crown = smoothstep(0.55, 0.95, hs);
	vec3 sd = vec3(lp.xy * (2.5 / max(rr, 0.05)), cp.z * 0.15);
	float streaks = smoothstep(0.4, 0.75, cloudNoise(sd * cp.y * 4.0));
	sheet *= crown + (1.0 - crown)
	    * (0.05 + 2.2 * streaks * streaks) * (1.0 - 0.5 * dr);
	float shape = max(jet, sheet);
	// Streak noise: vertically stretched, scrolling up inside the
	// jet and down (faster) along the outer fall.
	float flow = mix(1.0, -1.5, smoothstep(0.12, 0.3, rr));
	vec3 p = lp * cp.y;
	p.z = p.z * 0.35 - cp.z * flow;
	float n = 0.5 * cloudNoise(p)
		+ 0.25 * cloudNoise(p * 2.03)
		+ 0.125 * cloudNoise(p * 4.09);
	n /= 0.875;
	return cp.x * shape * (0.2 + 0.8 * smoothstep(0.3, 0.7, n));
}

// Fire body interval of the ray from its depth target pair, slot in
// .z; (0, -1, 0) when the pixel has no fire body (the cloud span
// rules).
vec3 volFireSpan(vec4 ff, vec4 fb, vec3 dir)
{
	if (fb.w < 0.5)
		return vec3(0.0, -1.0, 0.0);
	int s = mediumSlot(ff, fb);
	if (u_fireParams[s].w < 0.5)
		return vec3(0.0, -1.0, 0.0);
	return vec3(ff.w > 0.5 ? volT(ff.z, dir) : 0.0,
	            volT(fb.z, dir), float(s));
}

// Flame temperature field in [0,1] at a world position: 3-octave value
// noise (the cloud lattice) rising along the body's up axis with a
// slight lateral wobble, eroded by a threshold that climbs with the
// normalized height so the flame breaks into separate tongues and dies
// out near the top. All sampling happens in the fire-local frame;
// frame/fp/fp2 = the body slot's u_fireFrame/u_fireParams/
// u_fireParams2 entries.
float fireTempAt(vec3 wp, mat4 frame, vec4 fp, vec4 fp2)
{
	vec3 lp = mul(frame, vec4(wp, 1.0)).xyz;
	float h = clamp(lp.z * fp2.y, 0.0, 1.0);
	vec3 p = lp * fp.y;
	p.z *= 0.55;  // vertically stretched noise = licking tongues
	p.z -= fp.z;
	p.x += 0.35 * sin(0.8 * fp.z + p.z * 1.7);
	float n = 0.5 * cloudNoise(p)
		+ 0.25 * cloudNoise(p * 2.03)
		+ 0.125 * cloudNoise(p * 4.09);
	n /= 0.875;
	return smoothstep(0.3 + 0.5 * h, 0.85, n) * (1.0 - 0.55 * h * h);
}

// Blackbody-style flame color ramp: dark red through orange to a
// yellow-white core as the temperature rises.
vec3 fireRamp(float t)
{
	return vec3(smoothstep(0.0, 0.25, t),
	            smoothstep(0.15, 0.75, t) * 0.85,
	            smoothstep(0.45, 1.0, t) * 0.65);
}

// ---- Medium-function seam (docs/RenderEngine.md §5.11) --------------
// Every pass that evaluates a body's medium (the volumetric raymarch,
// the extinction sub-march, the planar-reflection media pass) goes
// through these slot-aware wrappers instead of the field primitives
// above. A user "volume"-stage shader replaces the field / ramp of its
// body's slot in a spliced shader variant; the stock paths compile to
// exactly the primitive calls.

// Stock fire-channel field/ramp of a slot, also the authoring API a
// user medium function uses to build on the stock flame
// (fc_user_volume.sh: the identity medium is
// fcStockFireField/fcStockFireRamp of its own slot).
float fcStockFireField(int s, vec3 wp)
{
	return fireTempAt(wp, u_fireFrame[s], u_fireParams[s],
	                  u_fireParams2[s]);
}

vec3 fcStockFireRamp(int s, float t)
{
	return fireRamp(t);
}

// User volume-stage splice hooks: the assembled shader variant
// prototypes fcUserField_<slot>/fcUserRamp_<slot> and defines
// FC_USER_FIRE_<slot> before including this file; the user source
// (defining the functions) is appended after, so it can use every
// helper above. Without the defines this compiles to the stock calls.
#ifdef FC_USER_FIRE_0
float fcUserField_0(vec3 wp);
vec3 fcUserRamp_0(float t);
#endif
#ifdef FC_USER_FIRE_1
float fcUserField_1(vec3 wp);
vec3 fcUserRamp_1(float t);
#endif
#ifdef FC_USER_FIRE_2
float fcUserField_2(vec3 wp);
vec3 fcUserRamp_2(float t);
#endif
#ifdef FC_USER_FIRE_3
float fcUserField_3(vec3 wp);
vec3 fcUserRamp_3(float t);
#endif

// Fire channel: temperature-like scalar field in [0,1] at a world
// position. Drives the emission ramp below and the soot extinction
// (u_fireParams2[s].z * field) at the call sites.
float fcFireFieldAt(int s, vec3 wp)
{
#ifdef FC_USER_FIRE_0
	if (s == 0) return fcUserField_0(wp);
#endif
#ifdef FC_USER_FIRE_1
	if (s == 1) return fcUserField_1(wp);
#endif
#ifdef FC_USER_FIRE_2
	if (s == 2) return fcUserField_2(wp);
#endif
#ifdef FC_USER_FIRE_3
	if (s == 3) return fcUserField_3(wp);
#endif
	return fcStockFireField(s, wp);
}

// Fire channel: radiance color of a field value (per unit length,
// scaled by the intensity u_fireParams[s].x at the call sites).
vec3 fcFireRampAt(int s, float t)
{
#ifdef FC_USER_FIRE_0
	if (s == 0) return fcUserRamp_0(t);
#endif
#ifdef FC_USER_FIRE_1
	if (s == 1) return fcUserRamp_1(t);
#endif
#ifdef FC_USER_FIRE_2
	if (s == 2) return fcUserRamp_2(t);
#endif
#ifdef FC_USER_FIRE_3
	if (s == 3) return fcUserRamp_3(t);
#endif
	return fcStockFireRamp(s, t);
}

// Stock cloud-channel scattering density of a slot — the fountain
// spray field when the slot flags one (u_cloudParams[s].w > 1.5), the
// FBM puff otherwise. Also the authoring API a user scatter medium
// uses to build on the stock field (fc_user_volume.sh: the identity
// medium is fcStockCloudField of its own slot).
float fcStockCloudField(int s, vec3 wp)
{
	return u_cloudParams[s].w > 1.5
	    ? fountainDensityAt(wp, u_fountainFrame[s], u_cloudParams[s],
	                        u_fountainParams[s])
	    : cloudDensityAt(wp, u_cloudParams[s]);
}

// User scatter-channel splice hooks, mirroring the fire hooks above:
// FC_USER_SCATTER_<slot> prototypes fcUserScatter_<slot> and the user
// source (its fcMediumScatter macro-renamed to the slot's dispatch
// target) is appended after this file.
#ifdef FC_USER_SCATTER_0
float fcUserScatter_0(vec3 wp);
#endif
#ifdef FC_USER_SCATTER_1
float fcUserScatter_1(vec3 wp);
#endif
#ifdef FC_USER_SCATTER_2
float fcUserScatter_2(vec3 wp);
#endif
#ifdef FC_USER_SCATTER_3
float fcUserScatter_3(vec3 wp);
#endif

// Cloud channel: scattering density at a world position.
float fcCloudFieldAt(int s, vec3 wp)
{
#ifdef FC_USER_SCATTER_0
	if (s == 0) return fcUserScatter_0(wp);
#endif
#ifdef FC_USER_SCATTER_1
	if (s == 1) return fcUserScatter_1(wp);
#endif
#ifdef FC_USER_SCATTER_2
	if (s == 2) return fcUserScatter_2(wp);
#endif
#ifdef FC_USER_SCATTER_3
	if (s == 3) return fcUserScatter_3(wp);
#endif
	return fcStockCloudField(s, wp);
}

// Entry/exit distances of the ray through the medium sphere, entry
// clamped to the eye; (0, -1) when the ray misses.
vec2 volMedium(vec3 origin, vec3 dir)
{
	vec3 oc = origin - u_volMedium.xyz;
	float b = dot(oc, dir);
	float disc = b * b - (dot(oc, oc)
	                      - u_volMedium.w * u_volMedium.w);
	if (disc <= 0.0)
		return vec2(0.0, -1.0);
	float s = sqrt(disc);
	return vec2(max(0.0, -b - s), -b + s);
}
