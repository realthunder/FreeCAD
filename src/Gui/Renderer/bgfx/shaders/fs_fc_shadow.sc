/*
 * Shadow map caster fragment shader: writes the variance shadow map
 * moments (depth, depth^2) of the light-space window depth.
 */

#include <bgfx_shader.sh>

void main()
{
	float z = gl_FragCoord.z;
	gl_FragColor = vec4(z, z * z, 0.0, 1.0);
}
