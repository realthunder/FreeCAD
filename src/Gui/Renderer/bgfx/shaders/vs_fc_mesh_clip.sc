$input a_position, a_normal, a_color0
$output v_normal, v_color0, v_wpos

/*
 * Clip-plane (section) variant of the CAD-mesh vertex shader: adds the
 * world-space position consumed by fs_fc_mesh_clip.
 */

#define CLIP_PLANES

#include <bgfx_shader.sh>
#include "fc_mesh_vs.sh"
