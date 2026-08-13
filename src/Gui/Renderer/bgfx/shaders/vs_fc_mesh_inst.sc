$input a_position, a_normal, a_color0, a_color1, a_color2, i_data0, i_data1, i_data2, i_data3, i_data4
$output v_normal, v_color0, v_color1, v_color2, v_vpos

/*
 * Instanced variant of the CAD-mesh vertex shader: the model transform
 * and diffuse color come from the per-instance data stream (identical
 * placements of one shared geometry cache — Link arrays — collapse
 * into a single instanced submit). The body lives in fc_mesh_vs.sh.
 */

#define INSTANCED

#include <bgfx_shader.sh>
#include "fc_mesh_vs.sh"
