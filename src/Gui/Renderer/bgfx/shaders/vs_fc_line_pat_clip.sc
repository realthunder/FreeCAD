$input a_position, i_data0, i_data1, i_data2, i_data3
$output v_color0, v_wpos, v_dist

/*
 * Patterned (stipple) + clip-plane variant of the thick-line vertex
 * shader; pairs with fs_fc_line_pat_clip.
 */

#define LINE_PATTERN
#define CLIP_PLANES

#include <bgfx_shader.sh>
#include "fc_line_vs.sh"
