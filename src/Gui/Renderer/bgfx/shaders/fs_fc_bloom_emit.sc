$input v_normal, v_color0, v_vpos

/*
 * Bloom light-source emit pass: light-source bodies (Render_Light)
 * re-rendered into the quarter-res bloom source additively at their
 * HDR emission color (diffuse * intensity, premultiplied CPU-side into
 * u_matColor) — the halo then scales with the intensity even though
 * the LDR scene target clips the body's own pixels at 1.
 *
 * The quarter-res target has no depth buffer; occlusion comes from a
 * manual reject against the full-res prepass depth (an occluder in
 * front of the emitter keeps its glow from leaking through walls).
 *
 * u_matColor   : rgb = emission color * intensity
 * u_bloomTexel : zw = quarter-res target texel size (screen uv)
 */

#include <bgfx_shader.sh>

SAMPLER2D(s_texNormalZ, 0);

uniform vec4 u_matColor;
uniform vec4 u_bloomTexel;

void main()
{
	vec2 uv = gl_FragCoord.xy * u_bloomTexel.zw;
	vec4 nz = texture2D(s_texNormalZ, uv);
	float depth = -v_vpos.z;
	// Reject when opaque geometry sits clearly in front of the emitter
	// fragment (relative bias keeps the emitter's own prepass footprint
	// passing; the quarter-res uv rounding needs the slack anyway).
	if (nz.w > 0.5 && nz.z < depth - max(0.05 * depth, 0.1))
		discard;
	gl_FragColor = vec4(u_matColor.rgb, 1.0);
}
