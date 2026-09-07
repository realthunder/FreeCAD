$input v_texcoord0

/*
 * Capture depth encode (vs_fc_comp pair): samples the scene depth
 * attachment and writes it into a plain colour target.
 *
 * It exists because a capture must be readable on every backend. The
 * frame readback used to be glReadPixels(GL_DEPTH_COMPONENT) straight
 * out of the scene framebuffer, which is why the golden render tests
 * could only ever gate the OpenGL path -- on Metal or Vulkan there is
 * no GL framebuffer to read. bgfx blits and reads back COLOUR textures
 * portably and depth textures nowhere, so the one step that cannot be
 * made portable is moved into a pass: sample the depth here, write it
 * as colour, and the blit + readTexture that follows is the same on
 * every backend.
 *
 * R32F, not a packed RGBA8: geometryPixels counts against a 0.999
 * threshold, and eight bits of a non-linear depth buffer put far more
 * than a thousandth of the range in the last code. The alpha is 1.0 so
 * a target that ends up RGBA-backed still reads as opaque.
 */

#include <bgfx_shader.sh>

SAMPLER2D(s_texSceneDepth, 0);

void main()
{
	gl_FragColor = vec4(texture2D(s_texSceneDepth, v_texcoord0).x,
	                    0.0, 0.0, 1.0);
}
