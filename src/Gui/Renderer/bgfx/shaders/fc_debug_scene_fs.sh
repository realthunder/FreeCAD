/*
 * Debug scene re-render fragment shader body (docs/RenderDebug.md):
 * mode 6 (overdraw) outputs a unit count into .x — the pass blends
 * additively with the depth test off, so the target accumulates the
 * number of rasterized fragments per pixel; mode 8 (UV) outputs the
 * interpolated texcoord depth-tested, .w = 1 marking coverage; mode 11
 * (instance id) outputs the identity of the draw itself.
 *
 * u_debugParams.x carries the mode (the same uniform the composite
 * visualization pass reads); in mode 11 .yzw carry the draw's id split
 * into three raw byte lanes (id = y + z*256 + w*65536, each 0..255).
 *
 * ⭐ Raw integers, not a hash or a palette: colliding ids are precisely
 * the failure the instrument exists to detect, and the target is
 * RGBA16F, which represents 0..255 exactly. Never make this mode
 * prettier.
 */

uniform vec4 u_debugParams;

void main()
{
#ifdef CLIP_PLANES
	clipDiscard(v_wpos);
#endif
	if (u_debugParams.x > 10.5)
		gl_FragColor = vec4(u_debugParams.y, u_debugParams.z,
		                    u_debugParams.w, 1.0);
	else if (u_debugParams.x > 7.5)
		gl_FragColor = vec4(v_texcoord0.x, v_texcoord0.y, 0.0, 1.0);
	else
		gl_FragColor = vec4(1.0, 0.0, 0.0, 1.0);
}
