$input a_position, a_texcoord0
$output v_texcoord0, v_wpos

/*
 * Debug scene re-render vertex shader, clip-plane variant; the body
 * lives in fc_debug_scene_vs.sh (CLIP_PLANES adds the world position
 * for the fragment clip discard).
 */

#define CLIP_PLANES
#include <bgfx_shader.sh>
#include "fc_debug_scene_vs.sh"
