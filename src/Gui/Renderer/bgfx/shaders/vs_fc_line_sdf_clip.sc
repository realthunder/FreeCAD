$input a_position, i_data0, i_data1, i_data2, i_data3
$output v_color0, v_line, v_vpos, v_wpos

/*
 * Clip-plane (section) variant of the distance-field line vertex
 * shader.
 */

#define LINE_SDF
#define CLIP_PLANES

#include <bgfx_shader.sh>
#include "fc_line_vs.sh"
