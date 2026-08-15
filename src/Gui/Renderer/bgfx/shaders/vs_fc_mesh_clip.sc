$input a_position, a_normal, a_color0, a_color1, a_color2, a_color3
$output v_normal, v_color0, v_color1, v_color2, v_vpos, v_wpos, v_opos, v_onrm, v_findex

/*
 * Clip-plane (section) variant of the CAD-mesh vertex shader: adds the
 * world-space position consumed by fs_fc_mesh_clip.
 */

#define CLIP_PLANES

#include <bgfx_shader.sh>
#include "fc_mesh_vs.sh"
