$input a_position, i_data0, i_data1, i_data2, i_data3
$output v_color0, v_line, v_vpos

/*
 * Line vertex shader for the distance-field pass: the same screen-space
 * quad expansion as vs_fc_line, widened to carry the field's support,
 * and with the view-space position passed along so the fragment stage
 * can reject anything in front of the glass.
 */

#define LINE_SDF

#include <bgfx_shader.sh>
#include "fc_line_vs.sh"
