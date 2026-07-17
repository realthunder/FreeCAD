$input v_texcoord0

/*
 * SSAO apply pass: multiplies the blurred ambient visibility onto the
 * opaque scene color (blend ZERO / SRC_COLOR, alpha preserved). The
 * background multiplies by 1 and is unchanged.
 */

#include <bgfx_shader.sh>

SAMPLER2D(s_texAO, 0);

void main()
{
	float ao = texture2D(s_texAO, v_texcoord0).x;
	gl_FragColor = vec4(ao, ao, ao, 1.0);
}
