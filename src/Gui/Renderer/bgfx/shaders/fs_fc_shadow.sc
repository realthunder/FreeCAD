/*
 * Shadow map caster fragment shader: writes the variance shadow map
 * moments of the exponentially warped light-space window
 * depth (EVSM: exp(c z), exp(c z)^2; u_evsm.x = c).
 */

#include <bgfx_shader.sh>

uniform vec4 u_evsm;

void main()
{
	float e = exp(u_evsm.x * gl_FragCoord.z);
	gl_FragColor = vec4(e, e * e, 0.0, 1.0);
}
