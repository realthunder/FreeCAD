$input v_normal, v_vpos, v_wpos

/*
 * Medium interval depth writer, clip-plane variant; the body lives in
 * fc_meddepth_fs.sh.
 */

#define CLIP_PLANES
#include <bgfx_shader.sh>
#include "fc_clip.sh"
#include "fc_meddepth_fs.sh"
