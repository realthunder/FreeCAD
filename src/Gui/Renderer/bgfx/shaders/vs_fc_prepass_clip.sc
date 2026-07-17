$input a_position, a_normal
$output v_normal, v_vpos, v_wpos

/*
 * SSAO depth+normal prepass vertex shader, clip-plane variant; the body
 * lives in fc_prepass_vs.sh.
 */

#define CLIP_PLANES
#include <bgfx_shader.sh>
#include "fc_prepass_vs.sh"
