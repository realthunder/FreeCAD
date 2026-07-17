$input v_texcoord0, v_wpos

/*
 * Clip-plane variant of the section-cap fragment shader.
 */

#define CLIP_PLANES

#include <bgfx_shader.sh>
#include "fc_clip.sh"
#include "fc_cap_fs.sh"
