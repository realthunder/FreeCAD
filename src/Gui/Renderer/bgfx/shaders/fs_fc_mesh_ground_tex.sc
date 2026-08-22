$input v_normal, v_color0, v_color1, v_color2, v_texcoord0, v_vpos, v_opos, v_onrm, v_findex

/*
 * Textured ground: fs_fc_mesh_tex's texture environment and bump
 * mapping, faded out toward the rim like fs_fc_mesh_ground.
 */

#define TEXTURE
#define GROUND_FADE

#include <bgfx_shader.sh>
#include "fc_mesh_fs.sh"
