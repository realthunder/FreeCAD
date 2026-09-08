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

#include "fc_screen.sh"
#include "fc_matrix.sh"

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

// View-space position of a prepass texel. The camera projection is
// remapped to the backend's clip depth before it is bound, but only in
// its z row, so the handedness test is the same everywhere: the w row's
// z entry is -1 for a perspective projection (w = viewZ) and 0 for an
// orthographic one (w = 1); both look down -z, so pass
// persp = FC_MTX(u_proj, 2, 3) != 0.0. Nothing here reads the z row, so
// the remap does not reach this unproject.
vec3 fc_prepassViewPos(vec2 uv, float viewZ, bool persp)
{
	vec2 ndc = fc_uvToNdc(uv);
	if (persp)
		return vec3(viewZ * (ndc.x + FC_MTX(u_proj, 2, 0)) / FC_MTX(u_proj, 0, 0),
		            viewZ * (ndc.y + FC_MTX(u_proj, 2, 1)) / FC_MTX(u_proj, 1, 1),
		            -viewZ);
	return vec3((ndc.x - FC_MTX(u_proj, 3, 0)) / FC_MTX(u_proj, 0, 0),
	            (ndc.y - FC_MTX(u_proj, 3, 1)) / FC_MTX(u_proj, 1, 1),
	            -viewZ);
}

#endif // FC_PREPASS_READ_SH
