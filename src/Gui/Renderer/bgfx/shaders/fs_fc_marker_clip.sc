$input v_color0, v_texcoord0, v_vpos, v_wpos

/*
 * Clip-plane (section) variant of the point marker fragment shader.
 */

#define MARKER
#define CLIP_PLANES

#include <bgfx_shader.sh>
#include "fc_clip.sh"
#include "fc_flat_fs.sh"
