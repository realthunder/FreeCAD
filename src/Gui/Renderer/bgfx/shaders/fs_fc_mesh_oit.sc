$input v_normal, v_color0, v_color1, v_color2, v_vpos

/*
 * Weighted-blended OIT accumulation variant of the CAD-mesh fragment
 * shader (transparent scene triangles); pairs with vs_fc_mesh. The
 * shading body lives in fc_mesh_fs.sh.
 */

#define OIT

#include <bgfx_shader.sh>
#include "fc_mesh_fs.sh"
