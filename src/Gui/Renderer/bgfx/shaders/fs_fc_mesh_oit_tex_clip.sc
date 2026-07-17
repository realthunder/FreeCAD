$input v_normal, v_color0, v_texcoord0, v_vpos, v_wpos

/*
 * Textured weighted-blended OIT accumulation + clip-plane variant of
 * the CAD-mesh fragment shader; pairs with vs_fc_mesh_tex_clip.
 */

#define TEXTURE
#define OIT
#define CLIP_PLANES

#include <bgfx_shader.sh>
#include "fc_clip.sh"
#include "fc_mesh_fs.sh"
