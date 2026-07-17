$input a_position, a_normal
$output v_normal, v_vpos

/*
 * SSAO depth+normal prepass vertex shader, unclipped variant; the body
 * lives in fc_prepass_vs.sh.
 */

#include <bgfx_shader.sh>
#include "fc_prepass_vs.sh"
