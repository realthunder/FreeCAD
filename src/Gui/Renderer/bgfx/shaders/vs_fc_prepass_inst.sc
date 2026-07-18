$input a_position, a_normal, i_data0, i_data1, i_data2, i_data3
$output v_normal, v_vpos

/*
 * Instanced variant of the SSAO depth+normal prepass vertex shader:
 * the model transform comes from the per-instance data stream
 * (i_data0-3 are the columns of the instance's model matrix; the
 * diffuse in i_data4 rides along unused — the instance buffer is
 * shared with the color pass). Instancable draws are never
 * section-clipped, so there is no clip variant.
 */

#include <bgfx_shader.sh>

void main()
{
	mat4 model = mtxFromCols(i_data0, i_data1, i_data2, i_data3);
	vec4 wpos = mul(model, vec4(a_position, 1.0));
	gl_Position = mul(u_viewProj, wpos);
	v_normal = mul(u_view, vec4(mul(model, vec4(a_normal, 0.0)).xyz,
	                            0.0)).xyz;
	v_vpos = mul(u_view, wpos).xyz;
}
