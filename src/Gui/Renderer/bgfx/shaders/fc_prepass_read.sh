/*
 * Reading the geometry prepass (written by fc_prepass_fs.sh): the
 * octahedral normal decode and the unproject that turns a texel's
 * stored view depth back into a view-space position.
 *
 * Five passes carried their own copy of the decode and three their own
 * copy of the unproject. They agreed, but that is the shape the
 * hard-coded volumetric shadow bias came in: a copy nobody remembers
 * when the original changes.
 *
 * Needs only bgfx's own u_proj, so it can be included anywhere after
 * bgfx_shader.sh. The prepass target is RGBA32F: xy = the encoded
 * normal, z = view depth (positive, along -z), w = coverage.
 */

#ifndef FC_PREPASS_READ_SH
#define FC_PREPASS_READ_SH

// Inverse of the prepass octEncode: the octahedral map folded back onto
// the unit sphere.
vec3 fc_octDecode(vec2 e)
{
	vec3 n = vec3(e, 1.0 - abs(e.x) - abs(e.y));
	if (n.z < 0.0)
	{
		vec2 sn = vec2(n.x >= 0.0 ? 1.0 : -1.0,
		               n.y >= 0.0 ? 1.0 : -1.0);
		n.xy = (vec2_splat(1.0) - abs(n.yx)) * sn;
	}
	return normalize(n);
}

// View-space position of a prepass texel. GL projection: perspective
// has u_proj[2][3] == -1 (w = viewZ), orthographic has 0 (w = 1); both
// look down -z, so pass persp = u_proj[2][3] != 0.0.
vec3 fc_prepassViewPos(vec2 uv, float viewZ, bool persp)
{
	vec2 ndc = uv * 2.0 - vec2_splat(1.0);
	if (persp)
		return vec3(viewZ * (ndc.x + u_proj[2][0]) / u_proj[0][0],
		            viewZ * (ndc.y + u_proj[2][1]) / u_proj[1][1],
		            -viewZ);
	return vec3((ndc.x - u_proj[3][0]) / u_proj[0][0],
	            (ndc.y - u_proj[3][1]) / u_proj[1][1],
	            -viewZ);
}

#endif // FC_PREPASS_READ_SH
