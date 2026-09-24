$input v_color0, v_texcoord0, v_vpos

/*
 * Point marker fragment shader, unclipped variant; the body lives in
 * fc_flat_fs.sh (MARKER).
 */

#define MARKER

#include <bgfx_shader.sh>
#include "fc_flat_fs.sh"
