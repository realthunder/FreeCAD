/*
 * Shadow map caster fragment shader: writes the variance shadow map
 * moments of the light-space window depth — plain (z, z^2) when
 * u_evsm.x = 0 (Coin SoShadowGroup parity), exponentially warped
 * (EVSM: exp(c z), exp(c z)^2; u_evsm.x = c) otherwise.
 */

#include <bgfx_shader.sh>

uniform vec4 u_evsm;

void main()
{
	float z = gl_FragCoord.z;
	if (u_evsm.x < 0.5)
	{
		gl_FragColor = vec4(z, z * z, 0.0, 1.0);
	}
	else
	{
		float e = exp(u_evsm.x * z);
		gl_FragColor = vec4(e, e * e, 0.0, 1.0);
	}
}
