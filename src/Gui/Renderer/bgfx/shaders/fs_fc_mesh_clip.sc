$input v_normal, v_color0, v_color1, v_color2, v_vpos, v_wpos

/*
 * Clip-plane (section) variant of the CAD-mesh fragment shader. Selected
 * only for draws with active clip planes, so the discard here does not
 * cost the unclipped scene its early-Z.
 */

#define CLIP_PLANES

#include <bgfx_shader.sh>
#include "fc_clip.sh"
#include "fc_mesh_fs.sh"
