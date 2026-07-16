/*
 * Shared clip-plane (section) support for the fs_*_clip fragment shader
 * variants. Planes are world-space equations; a fragment survives when
 * dot(pos, plane.xyz) + plane.w >= 0 holds for every plane, or, in
 * concave mode, for at least one plane (GL parity: SectionConcave renders
 * with one enabled plane per pass, i.e. the union of the half-spaces).
 *
 * u_clipParams : x = active plane count, y = concave (union) mode
 */

uniform vec4 u_clipParams;
uniform vec4 u_clipPlanes[6];

void clipDiscard(vec3 wpos)
{
	int nclip = int(u_clipParams.x);
	bool concave = u_clipParams.y > 0.5;
	bool keep = !concave;
	for (int i = 0; i < 6; ++i)
	{
		if (i >= nclip)
			break;
		float d = dot(vec4(wpos, 1.0), u_clipPlanes[i]);
		if (concave)
		{
			if (d >= 0.0)
				keep = true;
		}
		else if (d < 0.0)
			keep = false;
	}
	if (!keep)
		discard;
}
