$input a_position, i_data0, i_data1
$output v_color0, v_wpos

/*
 * Clip-plane (section) variant of the point sprite vertex shader: adds
 * the world-space position consumed by fs_fc_flat_clip.
 */

#define CLIP_PLANES

#include <bgfx_shader.sh>
#include "fc_point_vs.sh"
