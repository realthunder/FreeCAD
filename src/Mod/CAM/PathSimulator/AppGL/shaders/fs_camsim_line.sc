$input v_index

/*
 * FragShader3DLine: tool-path coloring -- segments not yet milled
 * (index above u_simParams.z, the current segment) draw in the
 * translucent color, milled ones in the solid color.
 *
 * The path draws as an overlay-run pass into the composite's
 * destination (docs/CAMSimRenderPort.md sec 10.4). u_simParams.x is
 * set when that destination holds LINEAR light: the colours are
 * display-space constants and are decoded here, the way the
 * composite decodes its image. Alpha is coverage, never light: not
 * decoded.
 */

#include <bgfx_shader.sh>

uniform vec4 u_simObjectColor;
uniform vec4 u_simObjectColorAlpha;
uniform vec4 u_simParams;

/// sRGB -> linear, the inverse of the engine's fcEncodeSRGB. Keep in
/// step with fs_camsim_fbo's camsimDecodeSRGB.
vec3 camsimLineDecodeSRGB(vec3 c)
{
	vec3 lo = c / 12.92;
	vec3 hi = pow((c + vec3_splat(0.055)) / 1.055, vec3_splat(2.4));
	return mix(hi, lo, step(c, vec3_splat(0.04045)));
}

void main()
{
	vec4 color;
	if (v_index > u_simParams.z) {
		color = u_simObjectColorAlpha;
	}
	else {
		color = vec4(u_simObjectColor.rgb, u_simObjectColorAlpha.a);
	}
	if (u_simParams.x > 0.5) {
		color.rgb = camsimLineDecodeSRGB(color.rgb);
	}
	gl_FragColor = color;
}
