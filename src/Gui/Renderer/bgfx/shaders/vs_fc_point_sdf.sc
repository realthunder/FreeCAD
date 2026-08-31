$input a_position, i_data0, i_data1
$output v_color0, v_line, v_dist, v_vpos

/*
 * Point sprite vertex shader for the distance-field pass.
 * The body lives in fc_point_vs.sh.
 */

#define POINT_SDF


#include <bgfx_shader.sh>
#include "fc_point_vs.sh"
