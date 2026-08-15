$input a_position, a_normal, a_color0, a_color1, a_color2, a_color3, a_texcoord0, i_data0, i_data1, i_data2, i_data3, i_data4
$output v_normal, v_color0, v_color1, v_color2, v_texcoord0, v_vpos, v_opos, v_onrm, v_findex

/*
 * Textured instanced variant of the CAD-mesh vertex shader: the model
 * transform and diffuse color come from the per-instance data stream,
 * the (texture matrix transformed) texture coordinates from the third
 * vertex stream. Pairs with fs_fc_mesh_tex / fs_fc_mesh_oit_tex. The
 * body lives in fc_mesh_vs.sh.
 */

#define INSTANCED
#define TEXTURE

#include <bgfx_shader.sh>
#include "fc_mesh_vs.sh"
