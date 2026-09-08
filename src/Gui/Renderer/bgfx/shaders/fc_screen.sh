/*
 * Screen-space UV and the clip space it came from.
 *
 * A render target's texture origin follows the API: bottom-left under
 * OpenGL, top-left under Metal, D3D and Vulkan -- bgfx reports it as
 * caps->originBottomLeft. Clip y = +1 is the top of the viewport on
 * every backend, so the UV that addresses the very pixel a fullscreen
 * pass is rasterizing is NOT the same function of clip space
 * everywhere: v = 1 at the top under GL, v = 0 at the top elsewhere.
 *
 * The fullscreen vertex stage (vs_fc_comp.sc) derives its UV from clip
 * space, so every pass built on it needs this. Shaders that build a UV
 * from gl_FragCoord instead need none of it: gl_FragCoord.y counts
 * from the same edge the texture v does, on both conventions -- which
 * is why the two kinds of pass agree once this one is applied.
 *
 * BGFX_SHADER_LANGUAGE_GLSL is defined for GLSL and ESSL alike, so the
 * WebGL viewer takes the OpenGL branch with the desktop.
 */

#ifndef FC_SCREEN_SH
#define FC_SCREEN_SH

// UV of the pixel at clip-space xy -- what a fullscreen triangle's
// vertex stage hands its fragment stage, and what a screen-space walk
// samples the frame with after its own perspective divide.
vec2 fc_clipToUv(vec2 clip)
{
	vec2 uv = clip * 0.5 + vec2_splat(0.5);
#if !BGFX_SHADER_LANGUAGE_GLSL
	uv.y = 1.0 - uv.y;
#endif
	return uv;
}

// The inverse: clip-space xy of the pixel a screen UV addresses. Used
// to unproject a texel back into view space, and by a splat that must
// land on one named texel of a target it writes.
vec2 fc_uvToNdc(vec2 uv)
{
	vec2 ndc = uv * 2.0 - vec2_splat(1.0);
#if !BGFX_SHADER_LANGUAGE_GLSL
	ndc.y = -ndc.y;
#endif
	return ndc;
}

#endif // FC_SCREEN_SH
