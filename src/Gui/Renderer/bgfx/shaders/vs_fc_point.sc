$input a_position, i_data0, i_data1
$output v_color0

/*
 * FreeCAD point sprite vertex shader (instanced screen-space quad
 * expansion), unclipped variant; the body lives in fc_point_vs.sh.
 * Pairs with fs_fc_flat.
 */

#include <bgfx_shader.sh>
#include "fc_point_vs.sh"
