$input v_wpos

/*
 * Shadow map caster fragment shader, clipped variant: section-clipped
 * geometry does not cast shadows from its clipped-away parts.
 */

#include <bgfx_shader.sh>
#include "fc_clip.sh"

void main()
{
	clipDiscard(v_wpos);
	float z = gl_FragCoord.z;
	gl_FragColor = vec4(z, z * z, 0.0, 1.0);
}
