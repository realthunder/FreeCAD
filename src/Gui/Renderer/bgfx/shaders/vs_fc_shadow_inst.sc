$input a_position, i_data0, i_data1, i_data2, i_data3

/*
 * Instanced shadow map caster vertex shader: the model transform comes
 * from the per-instance data stream (i_data0-3 are the columns of the
 * instance's model matrix, the same GL-layout 16 floats DrawCall::model
 * holds; the diffuse in i_data4 rides along unused — the instance
 * buffer is shared with the color pass). Position-only transform under
 * the light's view/projection (set as the shadow view's transform).
 */

#include <bgfx_shader.sh>

void main()
{
	mat4 model = mtxFromCols(i_data0, i_data1, i_data2, i_data3);
	gl_Position = mul(u_viewProj, mul(model, vec4(a_position, 1.0)));
}
