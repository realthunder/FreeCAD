$input v_normal, v_color0, v_texcoord0, v_wpos

/*
 * Textured + clip-plane (section) variant of the CAD-mesh fragment
 * shader.
 */

#define TEXTURE
#define CLIP_PLANES

#include <bgfx_shader.sh>
#include "fc_clip.sh"
#include "fc_mesh_fs.sh"
