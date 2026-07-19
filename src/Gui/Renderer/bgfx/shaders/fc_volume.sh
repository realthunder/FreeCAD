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
 * u_volParams : x = air medium density (1/world units), y = intensity,
 *               z = maximum march distance, w = water body active
 * u_volMedium : xyz = medium sphere center (view space), w = radius
 * u_waterSigma: xyz = water extinction per channel, w = water
 *               scattering coefficient
 * u_cloudParams: x = cloud extinction density (1/world units), y =
 *               noise domain scale (1/world units), z = drift time,
 *               w > 0.5 = cloud body active
 * u_fireParams: x = flame emission density (1/world units), y = noise
 *               domain scale (1/world units), z = rise time, w > 0.5 =
 *               fire body active
 * u_fireParams2: x = fire body minimum world z, y = 1 / body height
 *               (the vertical flame taper frame), z = soot extinction
 *               density (1/world units; a mild absorption that follows
 *               the temperature field, 0 = none)
 */

uniform vec4 u_volParams;
uniform vec4 u_volMedium;
uniform vec4 u_waterSigma;
uniform vec4 u_cloudParams;
uniform vec4 u_fireParams;
uniform vec4 u_fireParams2;

// View-space ray of a screen pixel (uv in [0,1]). GL projection:
// perspective has u_proj[2][3] == -1 (w = viewZ), orthographic has 0
// (w = 1); both viewer down -z.
void volRay(vec2 uv, out vec3 origin, out vec3 dir)
{
	vec2 ndc = uv * 2.0 - vec2_splat(1.0);
	if (u_proj[2][3] != 0.0)
	{
		origin = vec3_splat(0.0);
		dir = normalize(vec3((ndc.x + u_proj[2][0]) / u_proj[0][0],
		                     (ndc.y + u_proj[2][1]) / u_proj[1][1],
		                     -1.0));
	}
	else
	{
		origin = vec3((ndc.x - u_proj[3][0]) / u_proj[0][0],
		              (ndc.y - u_proj[3][1]) / u_proj[1][1],
		              0.0);
		dir = vec3(0.0, 0.0, -1.0);
	}
}

// Ray parameter of a positive linear view depth (perspective rays are
// not parallel to the view axis).
float volT(float viewZ, vec3 dir)
{
	return u_proj[2][3] != 0.0 ? viewZ / max(1.0e-6, -dir.z) : viewZ;
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

// Underwater interval of the ray from the water depth samples;
// (0, -1) when the pixel has no water body. A back face without a
// front face means the camera is inside the water.
vec2 volWaterSpan(vec4 wf, vec4 wb, vec3 dir)
{
	if (u_volParams.w < 0.5 || wb.w < 0.5)
		return vec2(0.0, -1.0);
	return vec2(wf.w > 0.5 ? volT(wf.z, dir) : 0.0,
	            volT(wb.z, dir));
}

// Cloud body interval of the ray from its depth target pair; (0, -1)
// when the pixel has no cloud body (the water span rules).
vec2 volCloudSpan(vec4 cf, vec4 cb, vec3 dir)
{
	if (u_cloudParams.w < 0.5 || cb.w < 0.5)
		return vec2(0.0, -1.0);
	return vec2(cf.w > 0.5 ? volT(cf.z, dir) : 0.0,
	            volT(cb.z, dir));
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

// 3-octave FBM in [0,1], drifted by the animation clock.
float cloudFBM(vec3 wp)
{
	vec3 p = wp * u_cloudParams.y
		+ vec3(u_cloudParams.z, 0.0, 0.17 * u_cloudParams.z);
	float n = 0.5 * cloudNoise(p)
		+ 0.25 * cloudNoise(p * 2.03)
		+ 0.125 * cloudNoise(p * 4.09);
	return n / 0.875;
}

// Local cloud extinction at a world position: the FBM field remapped
// so roughly half the volume is clear (puffy holes).
float cloudDensityAt(vec3 wp)
{
	return u_cloudParams.x
		* smoothstep(0.4, 0.75, cloudFBM(wp));
}

// Fire body interval of the ray from its depth target pair; (0, -1)
// when the pixel has no fire body (the water span rules).
vec2 volFireSpan(vec4 ff, vec4 fb, vec3 dir)
{
	if (u_fireParams.w < 0.5 || fb.w < 0.5)
		return vec2(0.0, -1.0);
	return vec2(ff.w > 0.5 ? volT(ff.z, dir) : 0.0,
	            volT(fb.z, dir));
}

// Flame temperature field in [0,1] at a world position: 3-octave value
// noise (the cloud lattice) rising along +z with a slight lateral
// wobble, eroded by a threshold that climbs with the normalized height
// so the flame breaks into separate tongues and dies out near the top.
float fireTempAt(vec3 wp)
{
	float h = clamp((wp.z - u_fireParams2.x) * u_fireParams2.y,
	                0.0, 1.0);
	vec3 p = wp * u_fireParams.y;
	p.z *= 0.55;  // vertically stretched noise = licking tongues
	p.z -= u_fireParams.z;
	p.x += 0.35 * sin(0.8 * u_fireParams.z + p.z * 1.7);
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
