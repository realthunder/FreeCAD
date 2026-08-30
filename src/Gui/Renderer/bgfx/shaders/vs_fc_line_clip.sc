$input a_position, i_data0, i_data1, i_data2, i_data3
$output v_color0, v_wpos, v_line

/*
 * Clip-plane (section) variant of the thick-line vertex shader: adds the
 * world-space position consumed by fs_fc_line_clip.
 */

#define CLIP_PLANES

#include <bgfx_shader.sh>
#include "fc_line_vs.sh"
