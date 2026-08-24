$input v_normal, v_position

/*
 * FragShaderGeom: the G-buffer write -- color, view-space position,
 * view-space normal (the GL path's RGB32F attachments become RGBA32F,
 * bgfx has no 3-channel float targets).
 */

#include <bgfx_shader.sh>

uniform vec4 u_simObjectColor;

void main()
{
	gl_FragData[0] = vec4(u_simObjectColor.rgb, 1.0);
	gl_FragData[1] = vec4(v_position, 0.0);
	gl_FragData[2] = vec4(normalize(v_normal), 0.0);
}
