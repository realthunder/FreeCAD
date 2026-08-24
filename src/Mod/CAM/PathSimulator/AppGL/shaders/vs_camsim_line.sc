$input a_position, a_texcoord0
$output v_index

/*
 * VertShader3DLine: the tool-path polyline. a_texcoord0.x carries the
 * segment index (the GL path's integer SegmentId attribute, now a
 * float -- exact below 2^24 segments; the facade layout has no second
 * texcoord slot and supplies a single float here); the fragment shader colors
 * done-vs-remaining against u_simParams.z.
 */

#include <bgfx_shader.sh>

void main()
{
	gl_Position = mul(u_viewProj, vec4(a_position, 1.0));
	v_index = a_texcoord0.x;
}
