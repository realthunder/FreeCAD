$input a_position
$output v_texcoord0

/*
 * Fullscreen vertex stage, shared by every screen-space pass in the
 * engine (present, composite, depth encode, env, sun, bloom, AO,
 * cavity, shadow blur, volumetrics, particle steps, debug): the input
 * is a clip-space triangle covering the viewport, and the UV is
 * derived from it.
 *
 * The derivation is the one place the frame's handedness is decided --
 * a render target's texture origin is bottom-left only under OpenGL --
 * so it goes through fc_clipToUv rather than an open-coded 0.5 + 0.5.
 * Off GL the two conventions disagree, and this stage feeding the
 * mirrored UV is what turned the whole Metal frame upside down.
 */

#include <bgfx_shader.sh>
#include "fc_screen.sh"

void main()
{
	gl_Position = vec4(a_position.xy, 0.0, 1.0);
	v_texcoord0 = fc_clipToUv(a_position.xy);
}
