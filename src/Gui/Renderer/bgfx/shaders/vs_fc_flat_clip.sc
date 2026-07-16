$input a_position, a_color0
$output v_color0, v_wpos

/*
 * Clip-plane (section) variant of the flat line/point vertex shader:
 * adds the world-space position consumed by fs_fc_flat_clip.
 */

#define CLIP_PLANES

#include <bgfx_shader.sh>
#include "fc_flat_vs.sh"
