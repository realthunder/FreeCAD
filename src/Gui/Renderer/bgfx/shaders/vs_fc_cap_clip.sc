$input a_position, a_texcoord0
$output v_texcoord0, v_wpos

/*
 * Clip-plane variant of the section-cap vertex shader: adds the
 * world-space position consumed by fs_fc_cap_clip (the cap quad of one
 * section plane is clipped by the remaining planes).
 */

#define CLIP_PLANES

#include <bgfx_shader.sh>
#include "fc_cap_vs.sh"
