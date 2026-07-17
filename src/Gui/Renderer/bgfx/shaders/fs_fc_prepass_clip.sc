$input v_normal, v_vpos, v_wpos

/*
 * SSAO depth+normal prepass fragment shader, clip-plane variant; the
 * body lives in fc_prepass_fs.sh.
 */

#define CLIP_PLANES
#include <bgfx_shader.sh>
#include "fc_clip.sh"
#include "fc_prepass_fs.sh"
