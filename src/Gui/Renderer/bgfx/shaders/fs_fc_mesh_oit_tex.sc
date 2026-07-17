$input v_normal, v_color0, v_texcoord0, v_vpos

/*
 * Textured weighted-blended OIT accumulation variant of the CAD-mesh
 * fragment shader; pairs with vs_fc_mesh_tex.
 */

#define TEXTURE
#define OIT

#include <bgfx_shader.sh>
#include "fc_mesh_fs.sh"
