$input v_wpos

/*
 * Shadow map caster fragment shader, clipped variant: section-clipped
 * geometry does not cast shadows from its clipped-away parts.
 */

#include <bgfx_shader.sh>
#include "fc_clip.sh"

uniform vec4 u_evsm;

void main()
{
	clipDiscard(v_wpos);
	float e = exp(u_evsm.x * gl_FragCoord.z);
	gl_FragColor = vec4(e, e * e, 0.0, 1.0);
}
