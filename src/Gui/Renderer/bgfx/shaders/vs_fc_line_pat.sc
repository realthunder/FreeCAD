$input a_position, i_data0, i_data1, i_data2, i_data3
$output v_color0, v_dist, v_line

/*
 * Patterned (stipple) variant of the thick-line vertex shader: adds the
 * screen-space distance along the segment consumed by fs_fc_line_pat.
 */

#define LINE_PATTERN

#include <bgfx_shader.sh>
#include "fc_line_vs.sh"
