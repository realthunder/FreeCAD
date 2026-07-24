$input v_texcoord0, v_wpos

/*
 * Debug scene re-render fragment shader, clip-plane variant; the body
 * lives in fc_debug_scene_fs.sh.
 */

#define CLIP_PLANES
#include <bgfx_shader.sh>
#include "fc_clip.sh"
#include "fc_debug_scene_fs.sh"
