$input v_index

/*
 * FragShader3DLine: tool-path coloring -- segments not yet milled
 * (index above u_simParams.z, the current segment) draw in the
 * translucent color, milled ones in the solid color.
 */

#include <bgfx_shader.sh>

uniform vec4 u_simObjectColor;
uniform vec4 u_simObjectColorAlpha;
uniform vec4 u_simParams;

void main()
{
	if (v_index > u_simParams.z) {
		gl_FragColor = u_simObjectColorAlpha;
	}
	else {
		gl_FragColor = vec4(u_simObjectColor.rgb, u_simObjectColorAlpha.a);
	}
}
