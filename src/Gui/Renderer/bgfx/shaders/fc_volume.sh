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
 */

uniform vec4 u_volParams;
uniform vec4 u_volMedium;
uniform vec4 u_waterSigma;

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
