$input a_position, i_data0, i_data1
$output v_color0, v_texcoord0, v_vpos

/*
 * Point marker vertex shader (a SoMarkerSet point drawn as its bitmap),
 * unclipped variant; the body lives in fc_point_vs.sh. Pairs with
 * fs_fc_marker.
 */

#define MARKER

#include <bgfx_shader.sh>
#include "fc_point_vs.sh"
