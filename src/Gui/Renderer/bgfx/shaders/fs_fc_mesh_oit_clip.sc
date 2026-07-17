$input v_normal, v_color0, v_vpos, v_wpos

/*
 * Weighted-blended OIT accumulation + clip-plane variant of the
 * CAD-mesh fragment shader; pairs with vs_fc_mesh_clip.
 */

#define OIT
#define CLIP_PLANES

#include <bgfx_shader.sh>
#include "fc_clip.sh"
#include "fc_mesh_fs.sh"
