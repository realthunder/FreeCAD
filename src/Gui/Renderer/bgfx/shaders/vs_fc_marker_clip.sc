$input a_position, i_data0, i_data1
$output v_color0, v_texcoord0, v_vpos, v_wpos

/*
 * Clip-plane (section) variant of the point marker vertex shader.
 */

#define MARKER
#define CLIP_PLANES

#include <bgfx_shader.sh>
#include "fc_point_vs.sh"
